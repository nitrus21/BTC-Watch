#ifndef CYD_DISPLAY_H
#define CYD_DISPLAY_H

#include "address_monitor.h"
#include "settings.h"
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

enum class UiScreen : uint8_t { Portfolio = 0, Addresses, Clock, Market, Network };
enum class TouchAction : uint8_t {
  None = 0,
  ShowPortfolio,
  ShowAddresses,
  ShowClock,
  ShowMarket,
  ShowNetwork,
  PreviousPage,
  NextPage,
  SelectEUR,
  SelectUSD,
  StartApplication,
  PanicClearWallets
};

class CYDDisplay {
public:
  CYDDisplay();
  void begin(uint8_t brightness);
  void setBrightness(uint8_t brightness);
  void invalidate();
  void drawSplash();
  void drawFirstRunGuide(const String &apPassword);
  void drawWifiSearching();
  void drawWifiNetworkList(const String ssids[], const int32_t rssis[],
                           const bool secured[], uint8_t count,
                           uint8_t page);
  void drawWifiPassword(const String &ssid, const String &password,
                        uint8_t keyboardMode, const String &message = "");
  void highlightWifiKeyboardTouch(int16_t x, int16_t y,
                                  uint8_t keyboardMode);
  void drawWifiConnecting(const String &ssid);
  void drawWifiConnectionResult(bool success, const String &detail);
  void drawFactoryResetCountdown(uint8_t secondsRemaining);
  void drawFactoryResetComplete();
  void render(UiScreen screen, const Settings &settings,
              const AddressMonitor &monitor, float btcPrice,
              float priceChange, int feeLow, int feeMedium, int feeHigh,
              bool wifiConnected, const String &time, bool marketLoading);
  TouchAction pollTouch();
  bool pollTouchPoint(int16_t &x, int16_t &y);
  static char wifiKeyboardCharacter(uint8_t mode, uint8_t row,
                                    uint8_t column);
  void moveAddressPage(int8_t delta, uint8_t walletCount);
  void updateHeader(bool wifiConnected, const String &time);
  // Emplacement JPEG dessine derriere l'horloge (CLOCK_BACKGROUND_NONE pour
  // la couleur seule). Un changement force le redessin complet du cadran.
  void setClockBackgroundSlot(uint8_t slot);
  uint8_t clockBackgroundSlot() const { return activeClockBackgroundSlot; }

private:
  TFT_eSPI tft;
  TFT_eSprite clockSprite;
  SPIClass touchSPI;
  XPT2046_Touchscreen touch;
  UiScreen activeScreen;
  bool touchWasDown;
  unsigned long lastTouchAt;
  uint8_t addressPage;
  bool hasRenderedScreen;
  bool clockSpriteReady;
  bool firstRunGuideVisible;
  bool welcomeScreenVisible;
  String renderedClockColor;
  String renderedClockBackground;
  String renderedClockTitle;
  String renderedClockSubtitle;
  String renderedClockTitleColor;
  String renderedClockSubtitleColor;
  String renderedClockBarColor;
  String renderedClockButtonColor;
  bool renderedClockBarUseTimeColor;
  bool renderedClockButtonUseTimeColor;
  bool renderedClockBarVisible;
  bool renderedClockOutlineVisible;
  uint8_t activeClockBackgroundSlot;
  uint8_t renderedClockBackgroundSlot;

  void drawBackground();
  void drawHeader(bool wifiConnected, const String &time);
  void drawNavigation(UiScreen screen);
  void drawPortfolio(const Settings &settings, const AddressMonitor &monitor,
                     float btcPrice);
  void drawAddresses(const Settings &settings, const AddressMonitor &monitor);
  void drawClock(const String &time, const Settings &settings,
                 bool wifiConnected);
  void drawClockValue(const String &time, const String &color,
                      const String &background, bool outlineVisible);
  bool drawClockBackgroundRegion(int16_t x, int16_t y, int16_t w, int16_t h);
  bool loadClockBackgroundIntoSprite(int16_t x, int16_t y, int16_t w,
                                     int16_t h);
  bool drawJpegBackgroundRegion(TFT_eSprite *sprite, int16_t x, int16_t y,
                                int16_t w, int16_t h);
  void drawMarket(const AddressMonitor &monitor, float btcPrice,
                  float priceChange, const String &currency, bool loading);
  void drawNetwork(int low, int medium, int high, bool wifiConnected);

  void drawCard(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color, uint16_t border = 0);
  void drawBitcoinIcon(int16_t x, int16_t y, int16_t radius);
  void drawWifiIcon(int16_t x, int16_t y, bool connected,
                    uint16_t background = TFT_BLACK);
  void drawNavIcon(uint8_t index, int16_t cx, int16_t cy, uint16_t color);
  String formatBTC(int64_t satoshis) const;
  String formatFiat(double value, const String &currency) const;
  String shortenAddress(const String &address) const;
  uint16_t htmlColorTo565(const String &color);
};

#endif
