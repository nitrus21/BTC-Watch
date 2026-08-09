#include "settings.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>

namespace {
bool validColor(const String &value) {
  if (value.length() != 7 || value.charAt(0) != '#') return false;
  for (uint8_t i = 1; i < 7; ++i)
    if (!isHexadecimalDigit(value.charAt(i))) return false;
  return true;
}

bool validWalletAddress(const String &address) {
  if (address.length() < 14 || address.length() > 90) return false;
  if (!(address.startsWith("1") || address.startsWith("3") ||
        address.startsWith("bc1") || address.startsWith("BC1"))) return false;
  for (size_t i = 0; i < address.length(); ++i)
    if (!isAlphaNumeric(address.charAt(i))) return false;
  return true;
}

bool validTimezone(const String &timezone) {
  if (!timezone.startsWith("UTC") || timezone.length() < 5) return false;
  const char sign = timezone.charAt(3);
  if (sign != '+' && sign != '-') return false;
  for (size_t i = 4; i < timezone.length(); ++i)
    if (!isDigit(timezone.charAt(i))) return false;
  const int offset = timezone.substring(3).toInt();
  return offset >= -12 && offset <= 14;
}
}

Settings::Settings()
    : wifiSSID(""), wifiPASS(""), apPassword(getDefaultAPPassword()),
      devise(getDefaultDevise()),
      timezone(getDefaultTimezone()), priceAPI(getDefaultPriceAPI()),
      clockColor(getDefaultClockColor()),
      clockBackground(getDefaultClockBackground()),
      clockTitle(getDefaultClockTitle()),
      clockSubtitle(getDefaultClockSubtitle()),
      clockTitleColor(getDefaultClockTitleColor()),
      clockSubtitleColor(getDefaultClockSubtitleColor()),
      clockBarColor(getDefaultClockBarColor()),
      clockButtonColor(getDefaultClockButtonColor()),
      clockBarUseTimeColor(true), clockButtonUseTimeColor(true),
      clockBarVisible(getDefaultClockBarVisible()),
      clockOutlineVisible(getDefaultClockOutlineVisible()), brightness(220) {
  for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
    wallets[i].collection = "Manuel";
    wallets[i].label = "Wallet " + String(i + 1);
    wallets[i].address = "";
  }
}

uint8_t Settings::walletCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
    if (!wallets[i].address.isEmpty()) ++count;
  }
  return count;
}

bool Settings::hasStoredConfiguration() const {
  return SPIFFS.exists("/config.json");
}

bool Settings::factoryReset() {
  bool success = true;
  if (SPIFFS.exists("/config.json"))
    success = SPIFFS.remove("/config.json") && success;
  if (SPIFFS.exists(CLOCK_BACKGROUND_PATH))
    success = SPIFFS.remove(CLOCK_BACKGROUND_PATH) && success;
  if (SPIFFS.exists(CLOCK_BACKGROUND_TEMP_PATH))
    success = SPIFFS.remove(CLOCK_BACKGROUND_TEMP_PATH) && success;
  return success;
}

void Settings::load() {
  if (!SPIFFS.exists("/config.json")) return;
  File file = SPIFFS.open("/config.json", "r");
  if (!file) return;

  JsonDocument doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok) {
    wifiSSID = doc["ssid"] | wifiSSID;
    wifiPASS = doc["pass"] | wifiPASS;
    apPassword = doc["ap_password"] | apPassword;
    if (apPassword.length() < 8 || apPassword.length() > 63)
      apPassword = getDefaultAPPassword();
    timezone = doc["tz"] | timezone;
    devise = doc["currency"] | devise;
    priceAPI = doc["price_api"] | priceAPI;
    clockColor = doc["clock_color"] | clockColor;
    clockBackground = doc["clock_background"] | clockBackground;
    clockTitle = doc["clock_title"] | clockTitle;
    if (clockTitle == "21 CLOCK") clockTitle = getDefaultClockTitle();
    clockSubtitle = doc["clock_subtitle"] | clockSubtitle;
    clockTitleColor = doc["clock_title_color"] | clockTitleColor;
    clockSubtitleColor = doc["clock_subtitle_color"] | clockSubtitleColor;
    clockBarColor = doc["clock_bar_color"] | clockBarColor;
    clockButtonColor = doc["clock_button_color"] | clockButtonColor;
    clockBarUseTimeColor = doc["clock_bar_same_as_time"] | clockBarUseTimeColor;
    clockButtonUseTimeColor =
        doc["clock_button_same_as_time"] | clockButtonUseTimeColor;
    clockBarVisible = doc["clock_bar_visible"] | clockBarVisible;
    clockOutlineVisible =
        doc["clock_outline_visible"] | clockOutlineVisible;
    brightness = constrain(doc["brightness"] | brightness, 30, 255);

    JsonArray list = doc["wallets"].as<JsonArray>();
    if (!list.isNull()) {
      uint8_t index = 0;
      for (JsonObject item : list) {
        if (index >= MAX_WALLETS) break;
        wallets[index].collection = item["collection"] | "Manuel";
        wallets[index].label = item["label"] | wallets[index].label;
        wallets[index].address = item["address"] | "";
        wallets[index].address.trim();
        ++index;
      }
    }
  }
  file.close();
}

void Settings::save() {
  JsonDocument doc;
  doc["ssid"] = wifiSSID;
  doc["pass"] = wifiPASS;
  doc["ap_password"] = apPassword;
  doc["tz"] = timezone;
  doc["currency"] = devise;
  doc["price_api"] = priceAPI;
  doc["clock_color"] = clockColor;
  doc["clock_background"] = clockBackground;
  doc["clock_title"] = clockTitle;
  doc["clock_subtitle"] = clockSubtitle;
  doc["clock_title_color"] = clockTitleColor;
  doc["clock_subtitle_color"] = clockSubtitleColor;
  doc["clock_bar_color"] = clockBarColor;
  doc["clock_button_color"] = clockButtonColor;
  doc["clock_bar_same_as_time"] = clockBarUseTimeColor;
  doc["clock_button_same_as_time"] = clockButtonUseTimeColor;
  doc["clock_bar_visible"] = clockBarVisible;
  doc["clock_outline_visible"] = clockOutlineVisible;
  doc["brightness"] = brightness;

  JsonArray list = doc["wallets"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
    if (wallets[i].address.isEmpty()) continue;
    JsonObject item = list.add<JsonObject>();
    item["collection"] = wallets[i].collection;
    item["label"] = wallets[i].label;
    item["address"] = wallets[i].address;
  }

  File file = SPIFFS.open("/config.json", "w");
  if (!file) return;
  serializeJson(doc, file);
  file.close();
}

bool Settings::restoreFromJson(const String &json, String &errorMessage) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, json);
  if (jsonError || !doc.is<JsonObject>()) {
    errorMessage = "Configuration JSON invalide";
    return false;
  }

  Settings restored;
  restored.wifiSSID = doc["ssid"] | "";
  restored.wifiPASS = doc["pass"] | "";
  restored.apPassword = doc["ap_password"] | restored.apPassword;
  restored.timezone = doc["tz"] | restored.timezone;
  restored.devise = doc["currency"] | restored.devise;
  restored.priceAPI = doc["price_api"] | restored.priceAPI;
  restored.clockColor = doc["clock_color"] | restored.clockColor;
  restored.clockBackground =
      doc["clock_background"] | restored.clockBackground;
  restored.clockTitle = doc["clock_title"] | restored.clockTitle;
  restored.clockSubtitle = doc["clock_subtitle"] | restored.clockSubtitle;
  restored.clockTitleColor =
      doc["clock_title_color"] | restored.clockTitleColor;
  restored.clockSubtitleColor =
      doc["clock_subtitle_color"] | restored.clockSubtitleColor;
  restored.clockBarColor =
      doc["clock_bar_color"] | restored.clockBarColor;
  restored.clockButtonColor =
      doc["clock_button_color"] | restored.clockButtonColor;
  restored.clockBarUseTimeColor =
      doc["clock_bar_same_as_time"] | restored.clockBarUseTimeColor;
  restored.clockButtonUseTimeColor =
      doc["clock_button_same_as_time"] | restored.clockButtonUseTimeColor;
  restored.clockBarVisible =
      doc["clock_bar_visible"] | restored.clockBarVisible;
  restored.clockOutlineVisible =
      doc["clock_outline_visible"] | restored.clockOutlineVisible;
  restored.brightness = constrain(doc["brightness"] | restored.brightness,
                                  30, 255);

  if (restored.wifiSSID.length() > 32 || restored.wifiPASS.length() > 64) {
    errorMessage = "Identifiants Wi-Fi invalides";
    return false;
  }
  if (restored.apPassword.length() < 8 || restored.apPassword.length() > 63) {
    errorMessage = "Mot de passe BTC Watch invalide";
    return false;
  }
  if (!validTimezone(restored.timezone)) {
    errorMessage = "Fuseau horaire invalide";
    return false;
  }
  if (restored.devise != "EUR" && restored.devise != "USD") {
    errorMessage = "Devise invalide";
    return false;
  }
  if (restored.priceAPI != "COINGECKO" && restored.priceAPI != "BINANCE" &&
      restored.priceAPI != "BITSTAMP") {
    errorMessage = "Source du cours invalide";
    return false;
  }
  if (!validColor(restored.clockColor) ||
      !validColor(restored.clockBackground) ||
      !validColor(restored.clockTitleColor) ||
      !validColor(restored.clockSubtitleColor) ||
      !validColor(restored.clockBarColor) ||
      !validColor(restored.clockButtonColor)) {
    errorMessage = "Une couleur est invalide";
    return false;
  }
  restored.clockTitle.trim();
  restored.clockSubtitle.trim();
  if (restored.clockTitle.length() > 28 ||
      restored.clockSubtitle.length() > 32) {
    errorMessage = "Un texte du cadran est trop long";
    return false;
  }

  JsonArray walletsJson = doc["wallets"].as<JsonArray>();
  if (!walletsJson.isNull()) {
    uint8_t index = 0;
    for (JsonObject item : walletsJson) {
      if (index >= MAX_WALLETS) {
        errorMessage = "La sauvegarde contient trop d'adresses";
        return false;
      }
      String address = item["address"] | "";
      address.trim();
      if (address.isEmpty()) continue;
      if (!validWalletAddress(address)) {
        errorMessage = "Une adresse Bitcoin est invalide";
        return false;
      }
      for (uint8_t previous = 0; previous < index; ++previous) {
        if (restored.wallets[previous].address == address) {
          errorMessage = "La sauvegarde contient une adresse en double";
          return false;
        }
      }
      restored.wallets[index].collection = item["collection"] | "Manuel";
      restored.wallets[index].label = item["label"] | "Adresse";
      restored.wallets[index].collection.trim();
      restored.wallets[index].label.trim();
      if (restored.wallets[index].collection.isEmpty())
        restored.wallets[index].collection = "Manuel";
      if (restored.wallets[index].label.isEmpty())
        restored.wallets[index].label = "Adresse " + String(index + 1);
      if (restored.wallets[index].collection.length() > 18)
        restored.wallets[index].collection.remove(18);
      if (restored.wallets[index].label.length() > 18)
        restored.wallets[index].label.remove(18);
      restored.wallets[index].address = address;
      ++index;
    }
  }

  *this = restored;
  save();
  errorMessage = "";
  return true;
}
