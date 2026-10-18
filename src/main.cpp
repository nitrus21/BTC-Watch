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
bool timeRefreshRequested = false;
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
uint8_t panicTapCount = 0;
unsigned long lastPanicTapAt = 0;
unsigned long lastSlideshowChange = 0;

enum class NetworkJobKind : uint8_t { None, Market, Wallet, TimeRecovery };

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
  char time[6] = {0};
  bool timeValid = false;
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

bool inClockSlideshow(uint8_t slot) {
  return slot < MAX_CLOCK_BACKGROUNDS &&
         (settings.clockSlideshowMask & (1U << slot)) &&
         Settings::clockBackgroundExists(slot);
}

// Image suivante du diaporama apres current, en boucle. Renvoie current s'il
// est la seule image selectionnee, ou CLOCK_BACKGROUND_NONE si aucune.
uint8_t nextSlideshowSlot(uint8_t current) {
  const uint8_t start = current < MAX_CLOCK_BACKGROUNDS ? current + 1 : 0;
  for (uint8_t i = 0; i < MAX_CLOCK_BACKGROUNDS; ++i) {
    const uint8_t slot = (start + i) % MAX_CLOCK_BACKGROUNDS;
    if (inClockSlideshow(slot)) return slot;
  }
  return CLOCK_BACKGROUND_NONE;
}

// Applique l'image par defaut, ou la premiere image du diaporama si l'image
// par defaut n'en fait pas partie.
void applyClockBackgroundSettings() {
  uint8_t slot = settings.clockBackgroundSlot;
  if (settings.clockSlideshowEnabled && !inClockSlideshow(slot)) {
    const uint8_t first = nextSlideshowSlot(CLOCK_BACKGROUND_NONE);
    if (first != CLOCK_BACKGROUND_NONE) slot = first;
  }
  display.setClockBackgroundSlot(slot);
  lastSlideshowChange = millis();
}

void serviceClockSlideshow() {
  if (!settings.clockSlideshowEnabled || currentScreen != UiScreen::Clock)
    return;
  const unsigned long now = millis();
  if (now - lastSlideshowChange <
      static_cast<unsigned long>(settings.clockSlideshowSeconds) * 1000UL)
    return;
  lastSlideshowChange = now;
  const uint8_t current = display.clockBackgroundSlot();
  const uint8_t next = nextSlideshowSlot(current);
  if (next == CLOCK_BACKGROUND_NONE || next == current) return;
  display.setClockBackgroundSlot(next);
  render();
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
    } else if (job.kind == NetworkJobKind::TimeRecovery) {
      result.timeValid = network.recoverTimeFromInternet();
      if (result.timeValid)
        strlcpy(result.time, network.getFormattedTime().c_str(),
                sizeof(result.time));
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

bool queueTimeRecovery() {
  if (!networkWorkerMutex || !networkWorkerHandle) return false;
  if (xSemaphoreTake(networkWorkerMutex, pdMS_TO_TICKS(5)) != pdTRUE)
    return false;
  if (networkWorkerBusy || networkResultPending) {
    xSemaphoreGive(networkWorkerMutex);
    return false;
  }
  pendingNetworkJob = NetworkJob();
  pendingNetworkJob.kind = NetworkJobKind::TimeRecovery;
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
  } else if (result.kind == NetworkJobKind::TimeRecovery) {
    // Une tentative suffit pour ce cycle. En cas d'echec, lastTimeUpdate
    // programme le prochain essai sans monopoliser la file reseau.
    timeRefreshRequested = false;
    lastTimeUpdate = result.timeValid
                         ? millis()
                         : millis() - (TIME_UPDATE_INTERVAL - 1000UL);
    if (result.timeValid) {
      const String updatedTime = result.time;
      if (updatedTime != "--:--" && updatedTime != currentTime) {
        currentTime = updatedTime;
        if (currentScreen == UiScreen::Clock)
          render();
        else
          display.updateHeader(true, currentTime);
      }
    }
  }
}

bool refreshDisplayedTime() {
  if (!network.isConnected()) return false;
  const bool valid = network.updateTime();
  lastTimeUpdate = valid ? millis()
                         : millis() - (TIME_UPDATE_INTERVAL - 1000UL);
  if (!valid) return false;

  const String updatedTime = network.getFormattedTime();
  if (updatedTime == "--:--") return false;
  if (updatedTime == currentTime) return true;
  currentTime = updatedTime;
  if (currentScreen == UiScreen::Clock)
    render();
  else
    display.updateHeader(true, currentTime);
  return true;
}

void handleTouch() {
  const TouchAction action = display.pollTouch();
  if (action == TouchAction::None) return;
  if (action == TouchAction::PanicClearWallets) {
    const unsigned long now = millis();
    if (lastPanicTapAt == 0 || now - lastPanicTapAt > 1000UL)
      panicTapCount = 0;
    lastPanicTapAt = now;
    ++panicTapCount;
    if (panicTapCount < 3) return;

    panicTapCount = 0;
    lastPanicTapAt = 0;
    for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
      settings.wallets[i].collection = "Manuel";
      settings.wallets[i].label = "Wallet " + String(i + 1);
      settings.wallets[i].address = "";
    }
    // L'ecriture de la configuration puis la synchronisation du moniteur
    // effacent a la fois la liste persistante et tous les soldes/transactions
    // conserves en RAM. Un resultat reseau deja en vol sera ignore car son
    // adresse ne correspondra plus a l'emplacement vide.
    settings.save();
    addressMonitor.sync(settings);
    walletRefreshRequested = false;
    nextWalletToRefresh = 0;
    lastWalletCycle = millis();
    display.moveAddressPage(-127, 0);
    render();
    return;
  }
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
    // L'image affichee en arrivant sur l'horloge reste visible toute la duree.
    if (currentScreen == UiScreen::Clock) lastSlideshowChange = millis();
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

void serviceFirstRunWizard() {
  network.update();
  webInterface->handleClient();
  if (webInterface->consumeSettingsChanged()) {
    display.setBrightness(settings.brightness);
    addressMonitor.sync(settings);
  }
  delay(2);
}

void waitForWifiResultButton() {
  while (true) {
    serviceFirstRunWizard();
    int16_t x = 0;
    int16_t y = 0;
    if (display.pollTouchPoint(x, y) && x >= 76 && x < 244 && y >= 184)
      return;
  }
}

bool enterWifiPassword(const String &ssid, bool secured, String &password) {
  uint8_t keyboardMode = 0;
  String message;
  bool redraw = true;
  while (true) {
    if (redraw) {
      display.drawWifiPassword(ssid, password, keyboardMode, message);
      redraw = false;
    }
    serviceFirstRunWizard();
    int16_t x = 0;
    int16_t y = 0;
    if (!display.pollTouchPoint(x, y)) continue;
    display.highlightWifiKeyboardTouch(x, y, keyboardMode);
    delay(90);

    message = "";
    if (y >= 63 && y < 179 && x >= 3 && x < 320) {
      const uint8_t row = (y - 63) / 29;
      const uint8_t rowOffset = (y - 63) % 29;
      const uint8_t column = (x - 3) / 32;
      const uint8_t columnOffset = (x - 3) % 32;
      if (row < 4 && column < 10 && rowOffset < 26 && columnOffset < 29 &&
          password.length() < 63) {
        password +=
            CYDDisplay::wifiKeyboardCharacter(keyboardMode, row, column);
        redraw = true;
      }
      continue;
    }

    if (y >= 181 && y < 206) {
      if (x >= 3 && x < 60) {
        keyboardMode = keyboardMode == 0 ? 1 : 0;
        redraw = true;
      } else if (x >= 63 && x < 174) {
        if (password.length() < 63) password += ' ';
        redraw = true;
      } else if (x >= 177 && x < 238) {
        if (!password.isEmpty()) password.remove(password.length() - 1);
        redraw = true;
      } else if (x >= 241) {
        keyboardMode = keyboardMode == 2 ? 0 : 2;
        redraw = true;
      }
      continue;
    }

    if (y >= 210) {
      if (x < 160) return false;
      if (secured && password.length() < 8) {
        message = "8 caracteres minimum";
        redraw = true;
        continue;
      }
      return true;
    }
  }
}

bool connectFromFirstRunWizard(const String &ssid, bool secured) {
  String password;
  while (true) {
    if (!enterWifiPassword(ssid, secured, password)) return false;

    display.drawWifiConnecting(ssid);
    network.reconnect(ssid, password, settings.timezone, settings.apPassword,
                      settings.staticIPEnabled, settings.staticIP,
                      settings.staticGateway, settings.staticSubnet,
                      settings.staticDNS);
    const unsigned long startedAt = millis();
    while (!network.isConnected() && millis() - startedAt < 20000UL)
      serviceFirstRunWizard();

    if (network.isConnected()) {
      settings.wifiSSID = ssid;
      settings.wifiPASS = password;
      settings.save();
      display.drawWifiConnectionResult(
          true, "http://" + WiFi.localIP().toString());
      waitForWifiResultButton();
      return true;
    }

    display.drawWifiConnectionResult(false, "Connexion impossible");
    waitForWifiResultButton();
  }
}

bool runFirstRunWifiWizard() {
  constexpr uint8_t MAX_SETUP_NETWORKS = 16;
  String ssids[MAX_SETUP_NETWORKS];
  int32_t rssis[MAX_SETUP_NETWORKS] = {};
  bool secured[MAX_SETUP_NETWORKS] = {};

  while (true) {
    display.drawWifiSearching();
    const int found = WiFi.scanNetworks(false, true);
    uint8_t count = 0;
    if (found > 0) {
      for (int i = 0; i < found && count < MAX_SETUP_NETWORKS; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        bool duplicate = false;
        for (uint8_t previous = 0; previous < count; ++previous) {
          if (ssids[previous] == ssid) {
            duplicate = true;
            break;
          }
        }
        if (duplicate) continue;
        ssids[count] = ssid;
        rssis[count] = WiFi.RSSI(i);
        secured[count] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        ++count;
      }
    }
    WiFi.scanDelete();

    uint8_t page = 0;
    bool redraw = true;
    while (true) {
      if (redraw) {
        display.drawWifiNetworkList(ssids, rssis, secured, count, page);
        redraw = false;
      }
      serviceFirstRunWizard();
      if (network.isConnected() && settings.hasStoredConfiguration())
        return true;

      int16_t x = 0;
      int16_t y = 0;
      if (!display.pollTouchPoint(x, y)) continue;

      if (y >= 40 && y < 192 && count > 0) {
        const uint8_t row = (y - 40) / 38;
        const uint8_t rowOffset = (y - 40) % 38;
        const uint8_t index = page * 4 + row;
        if (row < 4 && rowOffset < 33 && index < count &&
            connectFromFirstRunWizard(ssids[index], secured[index]))
          return true;
        redraw = true;
        continue;
      }

      if (y < 198) continue;
      const uint8_t pages = max<uint8_t>(1, (count + 3) / 4);
      if (x < 64) {
        if (page > 0) --page;
        redraw = true;
      } else if (x < 126) {
        if (page + 1 < pages) ++page;
        redraw = true;
      } else if (x < 213) {
        break;
      } else {
        return false;
      }
    }
  }
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

  // Le Wi-Fi et le panneau d'administration sont disponibles immédiatement,
  // même tant que l'écran d'accueil attend l'appui sur DEMARRER.
  addressMonitor.sync(settings);
  network.begin(settings.wifiSSID, settings.wifiPASS, settings.timezone,
                settings.apPassword, settings.staticIPEnabled,
                settings.staticIP, settings.staticGateway,
                settings.staticSubnet, settings.staticDNS);
  webInterface = new WebInterface(&settings, &network);
  webInterface->begin();

  // L'accueil reste affiche jusqu'a l'appui sur le bouton DEMARRER, sauf si
  // le Wi-Fi enregistre se connecte : BTC Watch s'ouvre alors sur l'horloge.
  bool bootOnClock = false;
  while (true) {
    if (handleFactoryResetButton()) {
      delay(5);
      continue;
    }
    network.update();
    webInterface->handleClient();
    if (webInterface->consumeSettingsChanged()) {
      display.setBrightness(settings.brightness);
      addressMonitor.sync(settings);
    }
    if (network.isConnected()) {
      bootOnClock = true;
      break;
    }
    if (display.pollTouch() == TouchAction::StartApplication) break;
    delay(5);
  }
  welcomeScreenActive = false;
  firstRunGuideActive = !settings.hasStoredConfiguration();

  if (firstRunGuideActive && runFirstRunWifiWizard())
    firstRunGuideActive = false;
  if (bootOnClock) currentScreen = UiScreen::Clock;
  applyClockBackgroundSettings();

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
  handleTouch();
  network.update();
  webInterface->handleClient();
  processNetworkResult();

  const bool connected = network.isConnected();
  if (connected && !wasConnected) {
    currentTime = "--:--";
    timeRefreshRequested = true;
    requestFullRefresh();
    render();
  } else if (!connected && wasConnected) {
    currentTime = "--:--";
    render();
  }
  wasConnected = connected;

  if (connected && network.consumeTimezonePending()) {
    timeRefreshRequested = true;
  }

  if (webInterface->consumeSettingsChanged()) {
    firstRunGuideActive = false;
    display.setBrightness(settings.brightness);
    display.invalidate();
    applyClockBackgroundSettings();
    addressMonitor.sync(settings);
    requestFullRefresh();
    render();
  }
  serviceClockSlideshow();

  const unsigned long now = millis();
  if (connected && now - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
    timeRefreshRequested = !refreshDisplayedTime();
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
    if (timeRefreshRequested) {
      if (refreshDisplayedTime())
        timeRefreshRequested = false;
      else
        queueTimeRecovery();
    } else if (walletRefreshRequested && marketRefreshRequested &&
        nextWalletToRefresh == 0) {
      queueNextWalletRefresh();
    } else if (marketRefreshRequested) {
      queueMarketRefresh();
    } else if (walletRefreshRequested) {
      queueNextWalletRefresh();
    }
  }

  delay(2);
}
