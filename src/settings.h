#ifndef SETTINGS_H
#define SETTINGS_H

#include "config.h"
#include <Arduino.h>

struct WalletSetting {
  String collection;
  String label;
  String address;
};

class Settings {
public:
  String wifiSSID;
  String wifiPASS;
  String apPassword;
  String devise;
  String timezone;
  String priceAPI;
  String clockColor;
  String clockBackground;
  String clockTitle;
  String clockSubtitle;
  String clockTitleColor;
  String clockSubtitleColor;
  String clockBarColor;
  String clockButtonColor;
  bool clockBarUseTimeColor;
  bool clockButtonUseTimeColor;
  bool clockBarVisible;
  bool clockOutlineVisible;
  uint8_t brightness;
  WalletSetting wallets[MAX_WALLETS];

  Settings();
  void load();
  void save();
  bool restoreFromJson(const String &json, String &errorMessage);
  bool hasStoredConfiguration() const;
  bool factoryReset();
  uint8_t walletCount() const;

  static String getDefaultDevise() { return "EUR"; }
  static String getDefaultTimezone() { return "UTC+1"; }
  static String getDefaultPriceAPI() { return "COINGECKO"; }
  static String getDefaultClockColor() { return "#F7931A"; }
  static String getDefaultClockBackground() { return "#000000"; }
  static String getDefaultClockTitle() { return "BTC WATCH"; }
  static String getDefaultClockSubtitle() { return "BITCOIN NEVER SLEEPS"; }
  static String getDefaultClockTitleColor() { return "#FFFFFF"; }
  static String getDefaultClockSubtitleColor() { return "#A0A0A0"; }
  static String getDefaultClockBarColor() { return "#F7931A"; }
  static String getDefaultClockButtonColor() { return "#F7931A"; }
  static bool getDefaultClockBarVisible() { return true; }
  static bool getDefaultClockOutlineVisible() { return true; }
  static String getDefaultAPPassword() { return DEFAULT_AP_PASS; }
};

#endif
