#include "address_monitor.h"
#include "bitcoin_api.h"
#include "config.h"
#include "cyd_display.h"
#include "network_manager.h"
#include "settings.h"
#include "web_interface.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

Settings settings;
NetworkManager network;
BitcoinAPI bitcoinAPI;
AddressMonitor addressMonitor;
CYDDisplay display;
WebInterface *webInterface = nullptr;

UiScreen currentScreen = UiScreen::Portfolio;
String currentTime = "--:--";
float btcPrice = 0.0f;
float priceChange = 0.0f;
int feeLow = -1;
int feeMedium = -1;
int feeHigh = -1;

bool wasConnected = false;
bool walletRefreshRequested = false;
bool marketRefreshRequested = false;
uint8_t nextWalletToRefresh = 0;
unsigned long lastWalletCycle = 0;
unsigned long lastPriceUpdate = 0;
unsigned long lastFeeUpdate = 0;
unsigned long lastFeeAttempt = 0;
unsigned long lastTimeUpdate = 0;
bool firstRunGuideActive = false;
bool welcomeScreenActive = false;
unsigned long factoryButtonPressedAt = 0;
bool factoryResetOverlayVisible = false;
uint8_t lastFactoryCountdown = 255;

enum class NetworkJobKind : uint8_t { None, Market, Wallet };

struct NetworkJob {
  NetworkJobKind kind = NetworkJobKind::None;
  uint8_t walletIndex = 0;
  char address[91] = {0};
  char api[16] = {0};
  char currency[4] = {0};
};

struct NetworkResult {
  NetworkJobKind kind = NetworkJobKind::None;
  uint8_t walletIndex = 0;
  char address[91] = {0};
  char api[16] = {0};
  char currency[4] = {0};
  WalletBalance wallet;
  float price = 0.0f;
  float change = 0.0f;
  int feeLow = -1;
  int feeMedium = -1;
  int feeHigh = -1;
  bool priceValid = false;
  bool feesValid = false;
};

SemaphoreHandle_t networkWorkerMutex = nullptr;
TaskHandle_t networkWorkerHandle = nullptr;
NetworkJob pendingNetworkJob;
NetworkResult pendingNetworkResult;
bool networkJobPending = false;
bool networkResultPending = false;
bool networkWorkerBusy = false;

void render() {
  display.render(currentScreen, settings, addressMonitor, btcPrice, priceChange,
                 feeLow, feeMedium, feeHigh, network.isConnected(), currentTime,
                 marketRefreshRequested);
}

void requestFullRefresh() {
  walletRefreshRequested = true;
  marketRefreshRequested = true;
  nextWalletToRefresh = 0;
}

bool fetchFeesValues(int &low, int &medium, int &high) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.begin("https://mempool.space/api/v1/fees/recommended");
  const int status = http.GET();
  bool success = false;
  if (status == HTTP_CODE_OK) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
      low = doc["hourFee"] | -1;
      medium = doc["halfHourFee"] | -1;
      high = doc["fastestFee"] | -1;
      success = low >= 0 && medium >= 0 && high >= 0;
    }
  }
  http.end();
  return success;
}

void networkWorkerTask(void *) {
  for (;;) {
    NetworkJob job;
    bool hasJob = false;
    if (xSemaphoreTake(networkWorkerMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      if (networkJobPending) {
        job = pendingNetworkJob;
        networkJobPending = false;
        hasJob = true;
      }
      xSemaphoreGive(networkWorkerMutex);
    }

    if (!hasJob) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
      continue;
    }

    NetworkResult result;
    result.kind = job.kind;
    result.walletIndex = job.walletIndex;
    strlcpy(result.address, job.address, sizeof(result.address));
    strlcpy(result.api, job.api, sizeof(result.api));
    strlcpy(result.currency, job.currency, sizeof(result.currency));

    if (job.kind == NetworkJobKind::Market) {
      bitcoinAPI.updatePrice(String(job.api), String(job.currency));
      result.price = bitcoinAPI.getPrice();
      result.change = bitcoinAPI.getChange();
      result.priceValid = result.price > 0.0f;
      result.feesValid = fetchFeesValues(
          result.feeLow, result.feeMedium, result.feeHigh);
    } else if (job.kind == NetworkJobKind::Wallet) {
      addressMonitor.fetchAddress(String(job.address), result.wallet);
    }

    if (xSemaphoreTake(networkWorkerMutex, portMAX_DELAY) == pdTRUE) {
      pendingNetworkResult = result;
      networkResultPending = true;
      networkWorkerBusy = false;
      xSemaphoreGive(networkWorkerMutex);
    }
  }
}

bool queueMarketRefresh() {
  if (!networkWorkerMutex || !networkWorkerHandle) return false;
  if (xSemaphoreTake(networkWorkerMutex, pdMS_TO_TICKS(5)) != pdTRUE)
    return false;
  if (networkWorkerBusy || networkResultPending) {
    xSemaphoreGive(networkWorkerMutex);
    return false;
  }
  pendingNetworkJob = NetworkJob();
  pendingNetworkJob.kind = NetworkJobKind::Market;
  strlcpy(pendingNetworkJob.api, settings.priceAPI.c_str(),
          sizeof(pendingNetworkJob.api));
  strlcpy(pendingNetworkJob.currency, settings.devise.c_str(),
          sizeof(pendingNetworkJob.currency));
  networkJobPending = true;
  networkWorkerBusy = true;
  lastFeeAttempt = millis();
  xSemaphoreGive(networkWorkerMutex);
  xTaskNotifyGive(networkWorkerHandle);
  return true;
}

bool queueNextWalletRefresh() {
  while (nextWalletToRefresh < MAX_WALLETS &&
         settings.wallets[nextWalletToRefresh].address.isEmpty()) {
    ++nextWalletToRefresh;
  }
  if (nextWalletToRefresh >= MAX_WALLETS) {
    walletRefreshRequested = false;
    lastWalletCycle = millis();
    return false;
  }

  if (!networkWorkerMutex || !networkWorkerHandle) return false;
  if (xSemaphoreTake(networkWorkerMutex, pdMS_TO_TICKS(5)) != pdTRUE)
    return false;
  if (networkWorkerBusy || networkResultPending) {
    xSemaphoreGive(networkWorkerMutex);
    return false;
  }
  pendingNetworkJob = NetworkJob();
  pendingNetworkJob.kind = NetworkJobKind::Wallet;
  pendingNetworkJob.walletIndex = nextWalletToRefresh;
  strlcpy(pendingNetworkJob.address,
          settings.wallets[nextWalletToRefresh].address.c_str(),
          sizeof(pendingNetworkJob.address));
  ++nextWalletToRefresh;
  networkJobPending = true;
  networkWorkerBusy = true;
  xSemaphoreGive(networkWorkerMutex);
  xTaskNotifyGive(networkWorkerHandle);
  return true;
}

void processNetworkResult() {
  if (!networkWorkerMutex) return;
  NetworkResult result;
  bool hasResult = false;
  if (xSemaphoreTake(networkWorkerMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (networkResultPending) {
      result = pendingNetworkResult;
      networkResultPending = false;
      hasResult = true;
    }
    xSemaphoreGive(networkWorkerMutex);
  }
  if (!hasResult) return;

  if (result.kind == NetworkJobKind::Market) {
    const bool stillCurrent = settings.priceAPI == result.api &&
                              settings.devise == result.currency;
    if (stillCurrent) {
      if (result.priceValid) {
        btcPrice = result.price;
        priceChange = result.change;
      }
      if (result.feesValid) {
        feeLow = result.feeLow;
        feeMedium = result.feeMedium;
        feeHigh = result.feeHigh;
        lastFeeUpdate = millis();
      }
      lastPriceUpdate = millis();
      marketRefreshRequested = false;
      if (currentScreen == UiScreen::Portfolio ||
          currentScreen == UiScreen::Market ||
          currentScreen == UiScreen::Network)
        render();
    }
  } else if (result.kind == NetworkJobKind::Wallet) {
    addressMonitor.applyResult(result.walletIndex, String(result.address),
                               result.wallet);
    if (currentScreen == UiScreen::Portfolio ||
        currentScreen == UiScreen::Addresses)
      render();
  }
}

void handleTouch() {
  const TouchAction action = display.pollTouch();
  if (action == TouchAction::None) return;
  if (firstRunGuideActive && action == TouchAction::ShowPortfolio) {
    firstRunGuideActive = false;
    currentScreen = UiScreen::Portfolio;
    render();
    return;
  }
  if (action == TouchAction::SelectEUR || action == TouchAction::SelectUSD) {
    const String selectedCurrency =
        action == TouchAction::SelectEUR ? "EUR" : "USD";
    if (settings.devise == selectedCurrency) return;
    settings.devise = selectedCurrency;
    settings.save();
    marketRefreshRequested = true;
    render();
    return;
  }
  if (action == TouchAction::PreviousPage || action == TouchAction::NextPage) {
    display.moveAddressPage(action == TouchAction::PreviousPage ? -1 : 1,
                            settings.walletCount());
    render();
    return;
  }

  const uint8_t index = static_cast<uint8_t>(action) -
                        static_cast<uint8_t>(TouchAction::ShowPortfolio);
  if (index <= 4) {
    currentScreen = static_cast<UiScreen>(index);
    render();
  }
}

bool handleFactoryResetButton() {
  const bool pressed = digitalRead(FACTORY_RESET_BUTTON_PIN) == LOW;
  if (!pressed) {
    factoryButtonPressedAt = 0;
    lastFactoryCountdown = 255;
    if (factoryResetOverlayVisible) {
      factoryResetOverlayVisible = false;
      if (welcomeScreenActive)
        display.drawSplash();
      else if (firstRunGuideActive)
        display.drawFirstRunGuide(settings.apPassword);
      else
        render();
    }
    return false;
  }

  if (factoryButtonPressedAt == 0) factoryButtonPressedAt = millis();
  const unsigned long heldFor = millis() - factoryButtonPressedAt;

  if (heldFor >= FACTORY_RESET_HOLD_MS) {
    display.drawFactoryResetComplete();
    if (!settings.factoryReset()) SPIFFS.format();
    WiFi.disconnect(true, true);
    delay(1400);
    ESP.restart();
    return true;
  }

  if (heldFor >= 800) {
    uint8_t remaining = static_cast<uint8_t>(
        (FACTORY_RESET_HOLD_MS - heldFor + 999) / 1000);
    if (remaining < 1) remaining = 1;
    if (remaining != lastFactoryCountdown) {
      lastFactoryCountdown = remaining;
      factoryResetOverlayVisible = true;
      display.drawFactoryResetCountdown(remaining);
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(FACTORY_RESET_BUTTON_PIN, INPUT_PULLUP);
  SPIFFS.begin(true);
  firstRunGuideActive = !settings.hasStoredConfiguration();
  settings.load();

  display.begin(settings.brightness);
  welcomeScreenActive = true;
  display.drawSplash();

  // L'accueil reste affiche jusqu'a l'appui sur le bouton DEMARRER.
  while (true) {
    if (handleFactoryResetButton()) {
      delay(5);
      continue;
    }
    if (display.pollTouch() == TouchAction::StartApplication) break;
    delay(5);
  }
  welcomeScreenActive = false;

  addressMonitor.sync(settings);
  network.begin(settings.wifiSSID, settings.wifiPASS, settings.timezone,
                settings.apPassword);
  webInterface = new WebInterface(&settings, &network);
  webInterface->begin();
  networkWorkerMutex = xSemaphoreCreateMutex();
  if (networkWorkerMutex) {
    if (xTaskCreatePinnedToCore(networkWorkerTask, "btc-network", 12288,
                                nullptr, 1, &networkWorkerHandle, 0) != pdPASS)
      networkWorkerHandle = nullptr;
  }
  if (firstRunGuideActive)
    display.drawFirstRunGuide(settings.apPassword);
  else
    render();
}

void loop() {
  if (handleFactoryResetButton()) {
    delay(5);
    return;
  }
  network.update();
  webInterface->handleClient();
  handleTouch();
  processNetworkResult();

  const bool connected = network.isConnected();
  if (connected && !wasConnected) {
    network.updateTime();
    currentTime = network.getFormattedTime();
    requestFullRefresh();
    render();
  } else if (!connected && wasConnected) {
    currentTime = "--:--";
    render();
  }
  wasConnected = connected;

  if (connected && network.consumeTimezonePending()) {
    network.updateTime();
    const String updatedTime = network.getFormattedTime();
    if (updatedTime != currentTime) {
      currentTime = updatedTime;
      render();
    }
    lastTimeUpdate = millis();
  }

  if (webInterface->consumeSettingsChanged()) {
    firstRunGuideActive = false;
    display.setBrightness(settings.brightness);
    display.invalidate();
    addressMonitor.sync(settings);
    requestFullRefresh();
    render();
  }

  const unsigned long now = millis();
  if (connected && now - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
    network.updateTime();
    const String updatedTime = network.getFormattedTime();
    lastTimeUpdate = now;
    if (updatedTime != currentTime) {
      currentTime = updatedTime;
      if (currentScreen == UiScreen::Clock)
        render();
      else
        display.updateHeader(connected, currentTime);
    }
  }

  if (connected && !walletRefreshRequested &&
      now - lastWalletCycle >= WALLET_UPDATE_INTERVAL) {
    walletRefreshRequested = true;
    nextWalletToRefresh = 0;
  }
  if (connected && !marketRefreshRequested &&
      (now - lastPriceUpdate >= PRICE_UPDATE_INTERVAL ||
       now - lastFeeAttempt >= FEE_UPDATE_INTERVAL)) {
    marketRefreshRequested = true;
  }

  if (connected) {
    // Au démarrage, afficher d'abord le premier solde avant les cours et frais.
    if (walletRefreshRequested && marketRefreshRequested &&
        nextWalletToRefresh == 0) {
      queueNextWalletRefresh();
    } else if (marketRefreshRequested) {
      queueMarketRefresh();
    } else if (walletRefreshRequested) {
      queueNextWalletRefresh();
    }
  }

  delay(5);
}
