#include "cyd_display.h"
#include "bitcoin_logo_bitmaps.h"
#include "config.h"
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <math.h>

namespace {
constexpr uint16_t C_BG = TFT_BLACK;
constexpr uint16_t C_PANEL = 0x0841;
constexpr uint16_t C_PANEL_2 = 0x1082;
constexpr uint16_t C_LINE = 0x2945;
constexpr uint16_t C_TEXT = 0xEF7D;
constexpr uint16_t C_MUTED = 0x8410;
constexpr uint16_t C_ORANGE = 0xFBA3;
constexpr uint16_t C_ORANGE_DARK = 0xA301;
constexpr uint16_t C_GREEN = 0x3E8E;
constexpr uint16_t C_RED = 0xF9E7;
const char *NAV_LABELS[] = {"TOTAL", "ADRESSES", "HEURE", "MARCHE", "RESEAU"};
}

CYDDisplay::CYDDisplay()
    : clockSprite(&tft), touchSPI(HSPI), touch(TOUCH_CS_PIN, TOUCH_IRQ),
      activeScreen(UiScreen::Portfolio), touchWasDown(false), lastTouchAt(0),
      addressPage(0), hasRenderedScreen(false), clockSpriteReady(false),
      firstRunGuideVisible(false), welcomeScreenVisible(false),
      renderedClockBarUseTimeColor(false),
      renderedClockButtonUseTimeColor(false), renderedClockBarVisible(true),
      renderedClockOutlineVisible(true) {}

void CYDDisplay::begin(uint8_t brightness) {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.setTextWrap(false);
  tft.fillScreen(C_BG);
  clockSprite.setColorDepth(16);
  clockSpriteReady = clockSprite.createSprite(300, 88) != nullptr;

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  touch.setRotation(DISPLAY_ROTATION);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  setBrightness(brightness);
}

void CYDDisplay::setBrightness(uint8_t brightness) {
  ledcWrite(0, constrain(brightness, 30, 255));
}

void CYDDisplay::invalidate() { hasRenderedScreen = false; }

void CYDDisplay::drawSplash() {
  welcomeScreenVisible = true;
  firstRunGuideVisible = false;
  tft.startWrite();
  tft.fillScreen(C_BG);

  // Trame sombre et details decoratifs sur toute la surface.
  for (int16_t x = 0; x < DISPLAY_WIDTH; x += 32)
    tft.drawFastVLine(x, 0, DISPLAY_HEIGHT, C_PANEL);
  for (int16_t y = 0; y < DISPLAY_HEIGHT; y += 30)
    tft.drawFastHLine(0, y, DISPLAY_WIDTH, C_PANEL);
  tft.fillCircle(160, 76, 62, C_PANEL);
  tft.drawCircle(160, 76, 58, C_ORANGE_DARK);
  tft.drawCircle(160, 76, 49, C_LINE);
  tft.drawArc(160, 76, 64, 61, 205, 326, C_ORANGE, C_BG);
  tft.drawArc(160, 76, 69, 67, 24, 126, C_ORANGE_DARK, C_BG);
  tft.fillCircle(93, 47, 2, C_ORANGE);
  tft.fillCircle(226, 108, 2, C_ORANGE);
  tft.fillCircle(245, 45, 1, C_MUTED);
  tft.fillCircle(72, 111, 1, C_MUTED);
  drawBitcoinIcon(160, 76, 32);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextFont(4);
  tft.drawString("BTC WATCH", 160, 139);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("TABLEAU DE BORD BITCOIN TACTILE", 160, 159);

  drawCard(110, 177, 100, 32, C_PANEL_2, C_ORANGE_DARK);
  tft.setTextFont(2);
  tft.setTextColor(C_ORANGE, C_PANEL_2);
  tft.drawString("DEMARRER", 160, 193);

  tft.setTextFont(1);
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("v" APP_VERSION, 312, 233);
  tft.endWrite();
}

void CYDDisplay::drawFirstRunGuide(const String &apPassword) {
  welcomeScreenVisible = false;
  firstRunGuideVisible = true;
  tft.startWrite();
  tft.fillScreen(C_BG);
  drawBitcoinIcon(19, 19, 11);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.drawString("PREMIERE CONFIGURATION", 37, 18);
  tft.drawFastHLine(8, 36, 304, C_LINE);

  const char *labels[] = {"1  RESEAU WI-FI", "2  MOT DE PASSE", "3  OUVRIR LE NAVIGATEUR"};
  const String values[] = {AP_SSID, apPassword, "192.168.22.1"};
  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t y = 44 + i * 47;
    drawCard(10, y, 300, 40, C_PANEL, C_LINE);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(C_MUTED, C_PANEL);
    tft.drawString(labels[i], 22, y + 5);
    tft.setTextFont(2);
    tft.setTextColor(i == 2 ? C_ORANGE : C_TEXT, C_PANEL);
    tft.drawString(values[i], 22, y + 19);
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Connectez un telephone ou un ordinateur", 160, 188);
  drawCard(102, 203, 116, 29, C_PANEL_2, C_ORANGE_DARK);
  tft.setTextFont(2);
  tft.setTextColor(C_ORANGE, C_PANEL_2);
  tft.drawString("CONTINUER", 160, 217);
  tft.endWrite();
}

void CYDDisplay::drawFactoryResetCountdown(uint8_t secondsRemaining) {
  tft.startWrite();
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(C_RED, C_BG);
  tft.drawString("RESET USINE", 160, 66);
  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Maintenez le bouton BOOT", 160, 103);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Relachez pour annuler", 160, 126);
  tft.drawCircle(160, 169, 25, C_RED);
  tft.setTextFont(4);
  tft.setTextColor(C_RED, C_BG);
  tft.drawString(String(secondsRemaining), 160, 169);
  tft.endWrite();
}

void CYDDisplay::drawFactoryResetComplete() {
  welcomeScreenVisible = false;
  firstRunGuideVisible = false;
  tft.startWrite();
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.drawString("MODE USINE", 160, 90);
  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Reglages et adresses effaces", 160, 126);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Redemarrage...", 160, 153);
  tft.endWrite();
}

void CYDDisplay::drawBackground() {
  tft.fillScreen(C_BG);
}

void CYDDisplay::drawCard(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color, uint16_t border) {
  tft.fillRoundRect(x, y, w, h, 9, color);
  if (border) tft.drawRoundRect(x, y, w, h, 9, border);
}

void CYDDisplay::drawBitcoinIcon(int16_t x, int16_t y, int16_t radius) {
  const uint8_t *bitmap = BTC_LOGO_44X64;
  int16_t width = 44;
  int16_t height = 64;
  if (radius <= 11) {
    bitmap = BTC_LOGO_16X24;
    width = 16;
    height = 24;
  } else if (radius <= 16) {
    bitmap = BTC_LOGO_24X36;
    width = 24;
    height = 36;
  } else if (radius <= 24) {
    bitmap = BTC_LOGO_34X50;
    width = 34;
    height = 50;
  }
  tft.drawXBitmap(x - width / 2, y - height / 2, bitmap, width, height,
                  C_ORANGE);
}

void CYDDisplay::drawWifiIcon(int16_t x, int16_t y, bool connected,
                             uint16_t background) {
  const uint16_t color = connected ? C_GREEN : C_RED;
  tft.fillCircle(x, y + 7, 2, color);
  const bool transparent = background == TFT_TRANSPARENT;
  tft.drawArc(x, y + 7, 7, 5, 220, 320, color,
              transparent ? C_BG : background, !transparent);
  tft.drawArc(x, y + 7, 12, 10, 220, 320, color,
              transparent ? C_BG : background, !transparent);
}

void CYDDisplay::drawHeader(bool wifiConnected, const String &time) {
  tft.fillRect(0, 0, DISPLAY_WIDTH, 34, C_BG);
  drawBitcoinIcon(18, 17, 11);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("BTC WATCH", 36, 17);

  drawWifiIcon(257, 10, wifiConnected);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(time.length() ? time : "--:--", 312, 17);
  tft.drawFastHLine(8, 33, 304, C_LINE);
}

void CYDDisplay::drawNavIcon(uint8_t index, int16_t cx, int16_t cy,
                             uint16_t color) {
  if (index == 0) {
    tft.drawRoundRect(cx - 9, cy - 6, 18, 12, 3, color);
    tft.drawLine(cx - 5, cy - 9, cx + 5, cy - 9, color);
  } else if (index == 1) {
    for (int i = -6; i <= 6; i += 6) {
      tft.fillCircle(cx - 8, cy + i, 1, color);
      tft.drawFastHLine(cx - 4, cy + i, 13, color);
    }
  } else if (index == 2) {
    tft.drawCircle(cx, cy, 8, color);
    tft.drawLine(cx, cy, cx, cy - 5, color);
    tft.drawLine(cx, cy, cx + 4, cy + 3, color);
    tft.fillCircle(cx, cy, 1, color);
  } else if (index == 3) {
    tft.drawLine(cx - 9, cy + 6, cx - 3, cy, color);
    tft.drawLine(cx - 3, cy, cx + 2, cy + 3, color);
    tft.drawLine(cx + 2, cy + 3, cx + 9, cy - 7, color);
    tft.fillTriangle(cx + 9, cy - 7, cx + 5, cy - 5, cx + 8, cy - 2, color);
  } else {
    tft.drawCircle(cx - 7, cy + 4, 4, color);
    tft.drawCircle(cx + 7, cy + 4, 4, color);
    tft.drawCircle(cx, cy - 7, 4, color);
    tft.drawLine(cx - 4, cy - 4, cx - 6, cy, color);
    tft.drawLine(cx + 4, cy - 4, cx + 6, cy, color);
    tft.drawFastHLine(cx - 3, cy + 4, 6, color);
  }
}

void CYDDisplay::drawNavigation(UiScreen screen) {
  tft.fillRect(0, 198, DISPLAY_WIDTH, 42, C_PANEL);
  tft.drawFastHLine(0, 198, DISPLAY_WIDTH, C_LINE);
  for (uint8_t i = 0; i < 5; ++i) {
    const bool active = static_cast<uint8_t>(screen) == i;
    const int16_t x = i * 64;
    if (active) {
      tft.fillRoundRect(x + 3, 201, 58, 36, 7, C_PANEL_2);
      tft.fillRoundRect(x + 20, 201, 24, 3, 2, C_ORANGE);
    }
    const uint16_t color = active ? C_ORANGE : C_MUTED;
    drawNavIcon(i, x + 32, 213, color);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(color, active ? C_PANEL_2 : C_PANEL);
    tft.drawString(NAV_LABELS[i], x + 32, 231);
    // The built-in bitmap font has no accented capital E. Add the acute
    // accent as pixels so the visible label reads "MARCHÉ" on the device.
    if (i == 3) tft.drawLine(x + 47, 226, x + 49, 224, color);
  }
}

String CYDDisplay::formatBTC(int64_t satoshis) const {
  char buffer[24];
  const double btc = static_cast<double>(satoshis) / 100000000.0;
  if (fabs(btc) >= 1.0)
    snprintf(buffer, sizeof(buffer), "%.4f BTC", btc);
  else if (fabs(btc) >= 0.001)
    snprintf(buffer, sizeof(buffer), "%.6f BTC", btc);
  else
    snprintf(buffer, sizeof(buffer), "%.8f BTC", btc);
  return String(buffer);
}

String CYDDisplay::formatFiat(double value, const String &currency) const {
  char buffer[28];
  if (fabs(value) >= 1000000.0)
    snprintf(buffer, sizeof(buffer), "%.2f M %s", value / 1000000.0,
             currency.c_str());
  else
    snprintf(buffer, sizeof(buffer), "%.2f %s", value, currency.c_str());
  return String(buffer);
}

String CYDDisplay::shortenAddress(const String &address) const {
  if (address.length() <= 16) return address;
  return address.substring(0, 8) + "..." + address.substring(address.length() - 6);
}

uint16_t CYDDisplay::htmlColorTo565(const String &color) {
  if (color.length() != 7 || color.charAt(0) != '#') return TFT_WHITE;
  const uint32_t rgb = strtoul(color.substring(1).c_str(), nullptr, 16);
  return tft.color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void CYDDisplay::drawPortfolio(const Settings &settings,
                               const AddressMonitor &monitor, float btcPrice) {
  drawCard(9, 41, 302, 91, C_PANEL, C_ORANGE_DARK);
  tft.fillRoundRect(9, 41, 5, 91, 3, C_ORANGE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString("SOLDE TOTAL", 26, 51);

  if (settings.walletCount() == 0) {
    tft.setTextFont(4);
    tft.setTextColor(C_TEXT, C_PANEL);
    tft.drawString("Aucune adresse", 26, 75);
    tft.setTextFont(2);
    tft.setTextColor(C_ORANGE, C_PANEL);
    tft.drawString("Ajoutez-la sur 192.168.22.1", 26, 108);
  } else if (!monitor.hasData()) {
    tft.setTextFont(4);
    tft.setTextColor(C_TEXT, C_PANEL);
    tft.drawString("Actualisation...", 26, 76);
  } else {
    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, C_PANEL);
    tft.drawString(formatBTC(monitor.totalSats()), 26, 73);
    const double fiat = static_cast<double>(monitor.totalSats()) / 100000000.0 * btcPrice;
    tft.setTextFont(2);
    tft.setTextColor(C_ORANGE, C_PANEL);
    tft.drawString(formatFiat(fiat, settings.devise), 26, 108);
  }
  drawBitcoinIcon(273, 87, 23);

  drawCard(9, 141, 94, 47, C_PANEL_2, C_LINE);
  drawCard(113, 141, 94, 47, C_PANEL_2, C_LINE);
  drawCard(217, 141, 94, 47, C_PANEL_2, C_LINE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_PANEL_2);
  tft.drawString("ADRESSES", 56, 151);
  tft.drawString("TRANSACTIONS", 160, 151);
  tft.drawString("EN ATTENTE", 264, 151);
  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_PANEL_2);
  tft.drawString(String(settings.walletCount()), 56, 173);
  tft.drawString(String(monitor.totalTransactions()), 160, 173);
  tft.setTextColor(monitor.totalPendingSats() == 0 ? C_TEXT : C_ORANGE, C_PANEL_2);
  tft.drawString(formatBTC(monitor.totalPendingSats()).substring(0, 10), 264, 173);
}

void CYDDisplay::drawAddresses(const Settings &settings,
                               const AddressMonitor &monitor) {
  const uint8_t count = settings.walletCount();
  const uint8_t pageCount = max<uint8_t>(1, (count + WALLETS_PER_PAGE - 1) / WALLETS_PER_PAGE);
  if (addressPage >= pageCount) addressPage = pageCount - 1;

  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("MES ADRESSES  " + String(count), 10, 49);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(String(addressPage + 1) + "/" + String(pageCount), 225, 49);
  drawCard(242, 36, 32, 27, C_PANEL, C_LINE);
  drawCard(280, 36, 32, 27, C_PANEL, C_LINE);
  tft.setTextFont(2);
  tft.setTextColor(addressPage > 0 ? C_ORANGE : C_MUTED, C_PANEL);
  tft.drawString("<", 258, 49);
  tft.setTextColor(addressPage + 1 < pageCount ? C_ORANGE : C_MUTED, C_PANEL);
  tft.drawString(">", 296, 49);

  if (count == 0) {
    drawCard(8, 70, 304, 104, C_PANEL, C_LINE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(C_TEXT, C_PANEL);
    tft.drawString("Liste vide", 160, 105);
    tft.setTextFont(2);
    tft.setTextColor(C_ORANGE, C_PANEL);
    tft.drawString("Importez un fichier via le Wi-Fi", 160, 143);
    return;
  }

  const uint8_t first = addressPage * WALLETS_PER_PAGE;
  for (uint8_t row = 0; row < WALLETS_PER_PAGE; ++row) {
    const uint8_t i = first + row;
    const int16_t y = 66 + row * 31;
    drawCard(8, y, 304, 28, C_PANEL, C_LINE);
    const bool configured = i < count && !settings.wallets[i].address.isEmpty();
    const WalletBalance &balance = monitor.get(i);
    tft.fillCircle(23, y + 17, 7, configured ? C_ORANGE : C_LINE);
    if (configured) tft.fillCircle(23, y + 17, 3, C_BG);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(configured ? C_TEXT : C_MUTED, C_PANEL);
    String title = configured ? settings.wallets[i].label : "Emplacement libre";
    if (configured && !settings.wallets[i].collection.isEmpty())
      title = settings.wallets[i].collection + " / " + title;
    if (title.length() > 24) title = title.substring(0, 23) + ".";
    tft.drawString(title, 37, y + 2);
    tft.setTextFont(1);
    tft.setTextColor(C_MUTED, C_PANEL);
    tft.drawString(configured ? shortenAddress(settings.wallets[i].address)
                              : "Configurer via le portail web",
                   37, y + 16);

    tft.setTextDatum(MR_DATUM);
    tft.setTextFont(2);
    if (!configured) {
      tft.setTextColor(C_MUTED, C_PANEL);
      tft.drawString("--", 302, y + 14);
    } else if (balance.valid) {
      tft.setTextColor(C_ORANGE, C_PANEL);
      tft.drawString(formatBTC(balance.confirmedSats), 302, y + 14);
    } else {
      tft.setTextColor(C_RED, C_PANEL);
      tft.drawString(balance.error.length() ? "ERREUR" : "...", 302, y + 14);
    }
  }
}

bool CYDDisplay::drawClockBackgroundRegion(int16_t x, int16_t y, int16_t w,
                                           int16_t h) {
  fs::File file = SPIFFS.open(CLOCK_BACKGROUND_PATH, "r");
  if (!file || file.size() != CLOCK_BACKGROUND_BYTES || w <= 0 || h <= 0 ||
      x < 0 || y < 0 || x + w > DISPLAY_WIDTH || y + h > DISPLAY_HEIGHT) {
    if (file) file.close();
    return false;
  }
  uint16_t line[DISPLAY_WIDTH];
  const bool oldSwapBytes = tft.getSwapBytes();
  tft.setSwapBytes(true);
  for (int16_t row = 0; row < h; ++row) {
    const size_t offset =
        (static_cast<size_t>(y + row) * DISPLAY_WIDTH + x) * 2U;
    if (!file.seek(offset) ||
        file.read(reinterpret_cast<uint8_t *>(line), w * 2U) != w * 2U) {
      tft.setSwapBytes(oldSwapBytes);
      file.close();
      return false;
    }
    tft.pushImage(x, y + row, w, 1, line);
  }
  tft.setSwapBytes(oldSwapBytes);
  file.close();
  return true;
}

bool CYDDisplay::loadClockBackgroundIntoSprite(int16_t x, int16_t y,
                                               int16_t w, int16_t h) {
  if (!clockSpriteReady || w > clockSprite.width() || h > clockSprite.height())
    return false;
  fs::File file = SPIFFS.open(CLOCK_BACKGROUND_PATH, "r");
  if (!file || file.size() != CLOCK_BACKGROUND_BYTES) {
    if (file) file.close();
    return false;
  }
  uint16_t line[DISPLAY_WIDTH];
  clockSprite.setSwapBytes(true);
  for (int16_t row = 0; row < h; ++row) {
    const size_t offset =
        (static_cast<size_t>(y + row) * DISPLAY_WIDTH + x) * 2U;
    if (!file.seek(offset) ||
        file.read(reinterpret_cast<uint8_t *>(line), w * 2U) != w * 2U) {
      clockSprite.setSwapBytes(false);
      file.close();
      return false;
    }
    clockSprite.pushImage(0, row, w, 1, line);
  }
  clockSprite.setSwapBytes(false);
  file.close();
  return true;
}

void CYDDisplay::drawClock(const String &time, const Settings &settings,
                           bool wifiConnected) {
  const uint16_t bg = htmlColorTo565(settings.clockBackground);
  const uint16_t titleColor = htmlColorTo565(settings.clockTitleColor);
  const uint16_t subtitleColor = htmlColorTo565(settings.clockSubtitleColor);
  const uint16_t barColor = htmlColorTo565(
      settings.clockBarUseTimeColor ? settings.clockColor : settings.clockBarColor);
  const uint16_t buttonColor = htmlColorTo565(
      settings.clockButtonUseTimeColor ? settings.clockColor
                                       : settings.clockButtonColor);
  const bool imageBackground = drawClockBackgroundRegion(
      0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  if (!imageBackground) tft.fillScreen(bg);

  // Sur le cadran, seul l'etat Wi-Fi reste visible dans l'angle superieur.
  drawWifiIcon(299, 7, wifiConnected,
               imageBackground ? TFT_TRANSPARENT : bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(settings.clockTitle.length() <= 16 ? 4 : 2);
  tft.setTextColor(titleColor);
  tft.drawString(settings.clockTitle, 160, 35);
  if (settings.clockBarVisible)
    tft.fillRoundRect(126, 54, 68, 3, 2, barColor);
  drawClockValue(time, settings.clockColor, settings.clockBackground,
                 settings.clockOutlineVisible);
  tft.setTextFont(2);
  tft.setTextColor(subtitleColor);
  tft.drawString(settings.clockSubtitle, 160, 178);

  // Bouton discret vers l'ecran Total.
  tft.drawRoundRect(282, 207, 30, 25, 6, buttonColor);
  tft.drawRoundRect(290, 215, 14, 9, 2, buttonColor);
  tft.drawFastHLine(293, 212, 8, buttonColor);
}

void CYDDisplay::drawClockValue(const String &time, const String &color,
                                const String &background,
                                bool outlineVisible) {
  const uint16_t bg = htmlColorTo565(background);
  const uint16_t fg = htmlColorTo565(color);
  const String value = time.length() ? time : "--:--";
  if (clockSpriteReady) {
    const bool imageBackground =
        loadClockBackgroundIntoSprite(10, 68, 300, 88);
    if (!imageBackground) clockSprite.fillSprite(bg);
    clockSprite.setTextDatum(MC_DATUM);
    clockSprite.setTextFont(8);
    if (imageBackground && outlineVisible) {
      clockSprite.setTextColor(bg);
      clockSprite.drawString(value, 148, 42);
      clockSprite.drawString(value, 150, 42);
      clockSprite.drawString(value, 152, 42);
      clockSprite.drawString(value, 148, 44);
      clockSprite.drawString(value, 152, 44);
      clockSprite.drawString(value, 148, 46);
      clockSprite.drawString(value, 150, 46);
      clockSprite.drawString(value, 152, 46);
    }
    clockSprite.setTextColor(fg);
    clockSprite.drawString(value, 150, 44);
    clockSprite.pushSprite(10, 68);
  } else {
    const bool imageBackground =
        drawClockBackgroundRegion(10, 68, 300, 88);
    if (!imageBackground) tft.fillRect(10, 68, 300, 88, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(8);
    if (imageBackground && outlineVisible) {
      tft.setTextColor(bg);
      tft.drawString(value, 158, 110);
      tft.drawString(value, 160, 110);
      tft.drawString(value, 162, 110);
      tft.drawString(value, 158, 112);
      tft.drawString(value, 162, 112);
      tft.drawString(value, 158, 114);
      tft.drawString(value, 160, 114);
      tft.drawString(value, 162, 114);
    }
    tft.setTextColor(fg);
    tft.drawString(value, 160, 112);
  }
}

void CYDDisplay::drawMarket(const AddressMonitor &monitor, float btcPrice,
                            float priceChange, const String &currency,
                            bool loading) {
  drawCard(9, 41, 190, 75, C_PANEL, C_LINE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString("BITCOIN", 21, 51);
  const bool usdActive = currency == "USD";
  const bool eurActive = !usdActive;
  drawCard(101, 46, 43, 24, usdActive ? C_ORANGE : C_PANEL_2,
           usdActive ? C_ORANGE : C_ORANGE_DARK);
  drawCard(148, 46, 43, 24, eurActive ? C_ORANGE : C_PANEL_2,
           eurActive ? C_ORANGE : C_ORANGE_DARK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(usdActive ? TFT_BLACK : C_ORANGE,
                   usdActive ? C_ORANGE : C_PANEL_2);
  tft.drawString("USD", 122, 58);
  tft.setTextColor(eurActive ? TFT_BLACK : C_ORANGE,
                   eurActive ? C_ORANGE : C_PANEL_2);
  tft.drawString("EUR", 169, 58);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(loading ? 2 : 4);
  tft.setTextColor(loading ? C_ORANGE : C_TEXT, C_PANEL);
  tft.drawString(loading ? "ACTUALISATION..." : formatFiat(btcPrice, currency),
                 21, loading ? 80 : 75);
  tft.setTextFont(2);
  const uint16_t changeColor = priceChange >= 0 ? C_GREEN : C_RED;
  tft.setTextColor(loading ? C_MUTED : changeColor, C_PANEL);
  tft.drawString(loading ? "COURS EN ATTENTE"
                         : String(priceChange >= 0 ? "+" : "") +
                               String(priceChange, 2) + "% / 24h",
                 21, 101);

  // Efface les éventuels pixels de texte ayant débordé dans l'espace entre
  // les deux cartes avant de redessiner la carte de droite.
  tft.fillRect(199, 41, 9, 75, C_BG);
  drawCard(208, 41, 103, 75, C_PANEL_2, C_LINE);
  drawBitcoinIcon(259, 65, 15);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_PANEL_2);
  tft.drawString("VALEUR DETENUE", 259, 91);
  tft.setTextFont(2);
  tft.setTextColor(C_ORANGE, C_PANEL_2);
  const double held = static_cast<double>(monitor.totalSats()) / 100000000.0 * btcPrice;
  tft.drawString(loading ? "--" : formatFiat(held, currency), 259, 105);

  drawCard(9, 126, 302, 62, C_PANEL, C_LINE);
  const int baseY = 172;
  const int points[] = {8, 18, 14, 28, 24, 36, 30, 43, 38, 52, 48, 58};
  for (int i = 0; i < 11; ++i) {
    int y1 = baseY - points[i] / 2;
    int y2 = baseY - points[i + 1] / 2;
    if (priceChange < 0) { y1 = 137 + points[i] / 2; y2 = 137 + points[i + 1] / 2; }
    tft.drawLine(21 + i * 25, y1, 21 + (i + 1) * 25, y2, changeColor);
    tft.drawLine(21 + i * 25, y1 + 1, 21 + (i + 1) * 25, y2 + 1, changeColor);
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString("TENDANCE 24H", 20, 133);
}

void CYDDisplay::drawNetwork(int low, int medium, int high,
                             bool wifiConnected) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("FRAIS MEMPOOL", 10, 40);
  const int values[] = {low, medium, high};
  const char *labels[] = {"ECO", "NORMAL", "RAPIDE"};
  const uint16_t colors[] = {C_GREEN, C_ORANGE, C_RED};
  for (uint8_t i = 0; i < 3; ++i) {
    const int x = 9 + i * 104;
    drawCard(x, 61, 94, 61, C_PANEL, C_LINE);
    tft.fillRoundRect(x, 61, 94, 4, 2, colors[i]);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(C_MUTED, C_PANEL);
    tft.drawString(labels[i], x + 47, 76);
    tft.setTextFont(4);
    tft.setTextColor(colors[i], C_PANEL);
    tft.drawString(values[i] >= 0 ? String(values[i]) : "--", x + 47, 98);
    tft.setTextFont(1);
    tft.setTextColor(C_MUTED, C_PANEL);
    tft.drawString("sat/vB", x + 47, 114);
  }

  drawCard(9, 132, 302, 55, C_PANEL_2, C_LINE);
  drawWifiIcon(28, 149, wifiConnected, C_PANEL_2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(wifiConnected ? C_GREEN : C_RED, C_PANEL_2);
  tft.drawString(wifiConnected ? "Wi-Fi connecte" : "Mode configuration", 49, 140);
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_PANEL_2);
  String detail = wifiConnected ? (WiFi.localIP().toString() + "  |  btc-watch.local")
                                : "SSID: BTC Watch  |  192.168.22.1";
  tft.drawString(detail, 49, 165);
}

void CYDDisplay::render(UiScreen screen, const Settings &settings,
                        const AddressMonitor &monitor, float btcPrice,
                        float priceChange, int feeLow, int feeMedium,
                        int feeHigh, bool wifiConnected, const String &time,
                        bool marketLoading) {
  const bool screenChanged = !hasRenderedScreen || screen != activeScreen;
  welcomeScreenVisible = false;
  firstRunGuideVisible = false;
  activeScreen = screen;
  tft.startWrite();
  if (screen == UiScreen::Clock) {
    const bool clockSettingsChanged =
        settings.clockColor != renderedClockColor ||
        settings.clockBackground != renderedClockBackground ||
        settings.clockTitle != renderedClockTitle ||
        settings.clockSubtitle != renderedClockSubtitle ||
        settings.clockTitleColor != renderedClockTitleColor ||
        settings.clockSubtitleColor != renderedClockSubtitleColor ||
        settings.clockBarColor != renderedClockBarColor ||
        settings.clockButtonColor != renderedClockButtonColor ||
        settings.clockBarUseTimeColor != renderedClockBarUseTimeColor ||
        settings.clockButtonUseTimeColor != renderedClockButtonUseTimeColor ||
        settings.clockBarVisible != renderedClockBarVisible ||
        settings.clockOutlineVisible != renderedClockOutlineVisible;
    if (screenChanged || clockSettingsChanged) {
      drawClock(time, settings, wifiConnected);
    } else {
      drawClockValue(time, settings.clockColor, settings.clockBackground,
                     settings.clockOutlineVisible);
      const uint16_t bg = htmlColorTo565(settings.clockBackground);
      const bool imageBackground =
          drawClockBackgroundRegion(278, 0, 42, 29);
      if (!imageBackground) tft.fillRect(278, 0, 42, 29, bg);
      drawWifiIcon(299, 7, wifiConnected,
                   imageBackground ? TFT_TRANSPARENT : bg);
    }
    renderedClockColor = settings.clockColor;
    renderedClockBackground = settings.clockBackground;
    renderedClockTitle = settings.clockTitle;
    renderedClockSubtitle = settings.clockSubtitle;
    renderedClockTitleColor = settings.clockTitleColor;
    renderedClockSubtitleColor = settings.clockSubtitleColor;
    renderedClockBarColor = settings.clockBarColor;
    renderedClockButtonColor = settings.clockButtonColor;
    renderedClockBarUseTimeColor = settings.clockBarUseTimeColor;
    renderedClockButtonUseTimeColor = settings.clockButtonUseTimeColor;
    renderedClockBarVisible = settings.clockBarVisible;
    renderedClockOutlineVisible = settings.clockOutlineVisible;
  } else {
    if (screenChanged) drawBackground();
    drawHeader(wifiConnected, time);
    if (screen == UiScreen::Portfolio)
    drawPortfolio(settings, monitor, btcPrice);
    else if (screen == UiScreen::Addresses)
      drawAddresses(settings, monitor);
    else if (screen == UiScreen::Market)
      drawMarket(monitor, btcPrice, priceChange, settings.devise,
                 marketLoading);
    else
      drawNetwork(feeLow, feeMedium, feeHigh, wifiConnected);
    if (screenChanged) drawNavigation(screen);
  }
  tft.endWrite();
  hasRenderedScreen = true;
}

void CYDDisplay::updateHeader(bool wifiConnected, const String &time) {
  tft.startWrite();
  drawHeader(wifiConnected, time);
  tft.endWrite();
}

TouchAction CYDDisplay::pollTouch() {
  const bool down = touch.touched();
  if (!down) {
    touchWasDown = false;
    return TouchAction::None;
  }
  TS_Point point = touch.getPoint();
  if (touchWasDown || point.z < TOUCH_MIN_PRESSURE ||
      millis() - lastTouchAt < TOUCH_DEBOUNCE_MS)
    return TouchAction::None;
  touchWasDown = true;
  lastTouchAt = millis();

  int16_t x = map(point.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, DISPLAY_WIDTH - 1);
  int16_t y = map(point.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, DISPLAY_HEIGHT - 1);
  x = constrain(x, 0, DISPLAY_WIDTH - 1);
  y = constrain(y, 0, DISPLAY_HEIGHT - 1);
  Serial.printf("[Touch] raw=%d,%d z=%d -> %d,%d\n", point.x, point.y,
                point.z, x, y);

  if (welcomeScreenVisible) {
    if (x >= 102 && x < 218 && y >= 169 && y < 217)
      return TouchAction::StartApplication;
    return TouchAction::None;
  }
  if (firstRunGuideVisible) {
    if (x >= 94 && x < 226 && y >= 195)
      return TouchAction::ShowPortfolio;
    return TouchAction::None;
  }
  if (activeScreen == UiScreen::Clock) {
    if (x >= 274 && y >= 199)
      return TouchAction::ShowPortfolio;
    return TouchAction::None;
  }
  if (activeScreen == UiScreen::Addresses && y >= 30 && y < 69) {
    if (x >= 236 && x < 277) return TouchAction::PreviousPage;
    if (x >= 277 && x < 318) return TouchAction::NextPage;
  }
  if (activeScreen == UiScreen::Market && y >= 40 && y < 76) {
    if (x >= 95 && x < 146) return TouchAction::SelectUSD;
    if (x >= 146 && x < 197) return TouchAction::SelectEUR;
  }
  if (y < 194) return TouchAction::None;
  const uint8_t index = min<uint8_t>(4, x / 64);
  return static_cast<TouchAction>(static_cast<uint8_t>(TouchAction::ShowPortfolio) + index);
}

void CYDDisplay::moveAddressPage(int8_t delta, uint8_t walletCount) {
  const uint8_t pageCount = max<uint8_t>(1, (walletCount + WALLETS_PER_PAGE - 1) / WALLETS_PER_PAGE);
  int next = static_cast<int>(addressPage) + delta;
  if (next < 0) next = 0;
  if (next >= pageCount) next = pageCount - 1;
  addressPage = static_cast<uint8_t>(next);
}
