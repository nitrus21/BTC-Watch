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

// Galerie de fonds JPEG 320 x 240 pour l'horloge. L'ancien fichier RGB565
// unique reste lu tant qu'il n'a pas ete converti par le panneau web.
#define MAX_CLOCK_BACKGROUNDS 10
#define CLOCK_BACKGROUND_NONE 255
#define CLOCK_BACKGROUND_MAX_JPEG_BYTES 61440UL
#define CLOCK_BACKGROUND_JPEG_TEMP_PATH "/bg_upload.tmp"
#define CLOCK_SLIDESHOW_MIN_SECONDS 5
#define CLOCK_SLIDESHOW_MAX_SECONDS 3600
#define CLOCK_SLIDESHOW_DEFAULT_SECONDS 30

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

// Adresse permanente par defaut sur le reseau domestique. Ces valeurs restent
// modifiables depuis le tableau de bord et n'affectent pas le reseau de secours.
#define DEFAULT_STATIC_IP "192.168.1.50"
#define DEFAULT_STATIC_GATEWAY "192.168.1.254"
#define DEFAULT_STATIC_SUBNET "255.255.255.0"
#define DEFAULT_STATIC_DNS "192.168.1.253"

#define TIME_UPDATE_INTERVAL 10000UL
#define PRICE_UPDATE_INTERVAL 60000UL
#define WALLET_UPDATE_INTERVAL 120000UL
#define FEE_UPDATE_INTERVAL 60000UL

#define MAX_WALLETS 64
#define WALLETS_PER_PAGE 4
#define APP_VERSION "1.0.0"

#endif
