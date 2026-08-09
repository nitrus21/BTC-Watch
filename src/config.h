#ifndef CONFIG_H
#define CONFIG_H

// ESP32-2432S028R / Cheap Yellow Display
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_ROTATION 1
#define TFT_BL 21
#define CLOCK_BACKGROUND_PATH "/clock_bg.rgb565"
#define CLOCK_BACKGROUND_TEMP_PATH "/clock_bg.tmp"
#define CLOCK_BACKGROUND_BYTES (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2UL)

// XPT2046 touch controller (dedicated SPI bus)
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
#define TOUCH_CS_PIN 33

// Raw calibration values for the common ESP32-2432S028R panel.
// They can be adjusted later if a particular unit is slightly offset.
#define TOUCH_MIN_X 250
#define TOUCH_MAX_X 3850
#define TOUCH_MIN_Y 250
#define TOUCH_MAX_Y 3850
#define TOUCH_MIN_PRESSURE 300
#define TOUCH_DEBOUNCE_MS 25UL

// Bouton BOOT situe a l'arriere du CYD. Un appui long remet BTC Watch
// completement en configuration d'usine.
#define FACTORY_RESET_BUTTON_PIN 0
#define FACTORY_RESET_HOLD_MS 5000UL

// Network identity and local configuration portal
#define AP_SSID "BTC Watch"
#define DEFAULT_AP_PASS "bitcoin21"
#define MDNS_NAME "btc-watch"
#define HOST_NAME "BTC-Watch"
#define AP_IP_ADDR 192, 168, 22, 1
#define AP_GATEWAY 192, 168, 22, 1
#define AP_SUBNET 255, 255, 255, 0

#define TIME_UPDATE_INTERVAL 10000UL
#define PRICE_UPDATE_INTERVAL 60000UL
#define WALLET_UPDATE_INTERVAL 120000UL
#define FEE_UPDATE_INTERVAL 60000UL

#define MAX_WALLETS 64
#define WALLETS_PER_PAGE 4
#define APP_VERSION "0.8.9"

#endif
