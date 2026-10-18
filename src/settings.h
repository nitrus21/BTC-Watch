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
  bool staticIPEnabled;
  String staticIP;
  String staticGateway;
  String staticSubnet;
  String staticDNS;
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
  // Image affichee par defaut (0 a MAX_CLOCK_BACKGROUNDS - 1) ou
  // CLOCK_BACKGROUND_NONE pour la seule couleur de fond.
  uint8_t clockBackgroundSlot;
  bool clockSlideshowEnabled;
  // Un bit par emplacement : bit n = image n incluse dans le diaporama.
  uint16_t clockSlideshowMask;
  uint16_t clockSlideshowSeconds;
  uint8_t brightness;
  WalletSetting wallets[MAX_WALLETS];

  Settings();
  void load();
  void save();
  // Avec apply = false, verifie seulement la sauvegarde sans rien modifier.
  bool restoreFromJson(const String &json, String &errorMessage,
                       bool apply = true);
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
  static String clockBackgroundPath(uint8_t slot) {
    return "/bg" + String(slot) + ".jpg";
  }
  static bool clockBackgroundExists(uint8_t slot);
  static String getDefaultAPPassword() { return DEFAULT_AP_PASS; }
  static bool getDefaultStaticIPEnabled() { return true; }
  static String getDefaultStaticIP() { return DEFAULT_STATIC_IP; }
  static String getDefaultStaticGateway() { return DEFAULT_STATIC_GATEWAY; }
  static String getDefaultStaticSubnet() { return DEFAULT_STATIC_SUBNET; }
  static String getDefaultStaticDNS() { return DEFAULT_STATIC_DNS; }
};

#endif
