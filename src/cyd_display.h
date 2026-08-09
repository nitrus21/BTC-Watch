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
  StartApplication
};

class CYDDisplay {
public:
  CYDDisplay();
  void begin(uint8_t brightness);
  void setBrightness(uint8_t brightness);
  void invalidate();
  void drawSplash();
  void drawFirstRunGuide(const String &apPassword);
  void drawFactoryResetCountdown(uint8_t secondsRemaining);
  void drawFactoryResetComplete();
  void render(UiScreen screen, const Settings &settings,
              const AddressMonitor &monitor, float btcPrice,
              float priceChange, int feeLow, int feeMedium, int feeHigh,
              bool wifiConnected, const String &time, bool marketLoading);
  TouchAction pollTouch();
  void moveAddressPage(int8_t delta, uint8_t walletCount);
  void updateHeader(bool wifiConnected, const String &time);

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
