#include "web_interface.h"
#include "config.h"
#include "web_logo_data.h"
#include <ArduinoJson.h>
#include <ESP.h>
#include <SPIFFS.h>
#include <TJpg_Decoder.h>
#include <esp_system.h>

namespace {
bool validAddress(const String &address) {
  if (address.length() < 14 || address.length() > 90) return false;
  if (!(address.startsWith("1") || address.startsWith("3") ||
        address.startsWith("bc1") || address.startsWith("BC1"))) return false;
  for (size_t i = 0; i < address.length(); ++i)
    if (!isAlphaNumeric(address.charAt(i))) return false;
  return true;
}

bool validHtmlColor(const String &color) {
  if (color.length() != 7 || color.charAt(0) != '#') return false;
  for (uint8_t i = 1; i < 7; ++i)
    if (!isHexadecimalDigit(color.charAt(i))) return false;
  return true;
}

bool validIPv4(const String &value) {
  IPAddress address;
  return address.fromString(value);
}

// Marge laissee libre sur SPIFFS pour la configuration et la reorganisation
// interne du systeme de fichiers.
constexpr size_t SPIFFS_RESERVED_BYTES = 24576;

// Envoie la page d'administration par morceaux (Transfer-Encoding chunked).
// Les grands textes sont lus directement en flash et les valeurs dynamiques
// passent par un petit tampon : la page complete (45 a 55 Ko) n'est jamais
// assemblee en RAM, ce qui evite une page tronquee faute de memoire.
class PageWriter {
public:
  explicit PageWriter(WebServer &server) : server(server) {
    buffer.reserve(FLUSH_BYTES + 256);
  }
  PageWriter &operator+=(const __FlashStringHelper *text) {
    PGM_P content = reinterpret_cast<PGM_P>(text);
    const size_t length = strlen_P(content);
    // Un morceau vide terminerait la reponse chunked.
    if (length == 0) return *this;
    flush();
    server.sendContent_P(content, length);
    return *this;
  }
  PageWriter &operator+=(const String &text) {
    buffer += text;
    if (buffer.length() >= FLUSH_BYTES) flush();
    return *this;
  }
  PageWriter &operator+=(const char *text) { return *this += String(text); }
  void flush() {
    if (buffer.isEmpty()) return;
    server.sendContent(buffer);
    buffer = "";
  }

private:
  static constexpr size_t FLUSH_BYTES = 1024;
  WebServer &server;
  String buffer;
};

size_t fileSize(const String &path) {
  if (!SPIFFS.exists(path)) return 0;
  fs::File file = SPIFFS.open(path, "r");
  const size_t size = file ? file.size() : 0;
  if (file) file.close();
  return size;
}
}

WebInterface::WebInterface(Settings *s, NetworkManager *n)
    : server(new WebServer(80)), settings(s), network(n), settingsChanged(false),
      clockBackgroundUploadBytes(0), clockBackgroundUploadLimit(0),
      clockBackgroundUploadOK(false), clockBackgroundUploadRemovedFile(false),
      clockBackgroundUploadSlot(CLOCK_BACKGROUND_NONE) {}

WebInterface::~WebInterface() { delete server; }

void WebInterface::begin() {
  sessionToken = String(esp_random(), HEX) + String(esp_random(), HEX) +
                 String(esp_random(), HEX);
  const char *headerKeys[] = {"Cookie"};
  server->collectHeaders(headerKeys, 1);
  server->on("/", [this]() { handleRoot(); });
  server->on("/login", HTTP_POST, [this]() { handleLogin(); });
  server->on("/logout", HTTP_POST, [this]() { handleLogout(); });
  server->on("/session", HTTP_GET, [this]() {
    if (requireAuthentication())
      server->send(200, "application/json", "{\"ok\":true}");
  });
  server->on("/save", HTTP_POST, [this]() {
    if (requireAuthentication()) handleSave();
  });
  server->on("/reboot", HTTP_POST, [this]() {
    if (requireAuthentication()) handleReboot();
  });
  server->on("/wifi-scan", HTTP_GET, [this]() {
    if (requireAuthentication()) handleWifiScan();
  });
  server->on("/clock-background", HTTP_GET,
             [this]() {
               if (requireAuthentication()) handleClockBackgroundGet();
             });
  server->on("/clock-background", HTTP_POST,
             [this]() {
               if (requireAuthentication()) handleClockBackgroundUploadComplete();
             },
             [this]() {
               if (isAuthenticated()) handleClockBackgroundUpload();
             });
  server->on("/clock-background/delete", HTTP_POST,
             [this]() {
               if (requireAuthentication()) handleClockBackgroundDelete();
             });
  server->on("/clock-backgrounds", HTTP_GET,
             [this]() {
               if (requireAuthentication()) handleClockBackgroundStatus();
             });
  server->on("/configuration", HTTP_GET,
             [this]() {
               if (requireAuthentication()) handleConfigurationExport();
             });
  server->on("/configuration/import", HTTP_POST,
             [this]() {
               if (requireAuthentication()) handleConfigurationImport();
             });
  server->onNotFound([this]() {
    server->sendHeader("Location", "/", true);
    server->send(302);
  });
  server->begin();
}

void WebInterface::handleClient() { server->handleClient(); }

bool WebInterface::consumeSettingsChanged() {
  const bool changed = settingsChanged;
  settingsChanged = false;
  return changed;
}

String WebInterface::htmlEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value.charAt(i)) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '\"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += value.charAt(i); break;
    }
  }
  return out;
}

bool WebInterface::isAuthenticated() {
  const String expected = "BTCWATCH_AUTH=" + sessionToken;
  return server->hasHeader("Cookie") &&
         server->header("Cookie").indexOf(expected) >= 0;
}

void WebInterface::refreshSessionCookie() {
  server->sendHeader("Set-Cookie", "BTCWATCH_AUTH=" + sessionToken +
                                       "; Path=/; Max-Age=600; HttpOnly; SameSite=Strict");
}

bool WebInterface::requireAuthentication() {
  if (isAuthenticated()) {
    refreshSessionCookie();
    return true;
  }
  server->send(401, "application/json",
               "{\"ok\":false,\"message\":\"Authentification requise\"}");
  return false;
}

String WebInterface::generateLoginHTML(bool invalidPassword) {
  String page;
  page.reserve(3500);
  page += F(R"HTML(<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Connexion — BTC Watch</title><style>
:root{--btc:#f7931a;--text:#f2f2f2;--muted:#969696;--line:#303030}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:20px;background:#000;color:var(--text);font-family:Inter,system-ui,sans-serif}.login{width:min(420px,100%);padding:28px;border:1px solid var(--line);border-radius:22px;background:linear-gradient(145deg,#151515,#080808);box-shadow:0 24px 70px #000}.coin{display:grid;place-items:center;width:68px;height:82px;margin:0 auto 14px}.coin img{display:block;width:54px;height:auto;filter:drop-shadow(0 0 12px #f7931a55)}.eyebrow{text-align:center;color:var(--btc);font-size:12px;font-weight:800;letter-spacing:.14em}.login h1{margin:7px 0;text-align:center;font-size:28px}.intro{margin:0 0 24px;text-align:center;color:var(--muted);line-height:1.5}label{display:block;margin-bottom:7px;color:var(--muted);font-size:13px}input{width:100%;padding:14px;border:1px solid var(--line);border-radius:11px;background:#000;color:#fff;font-size:17px;outline:none}input:focus{border-color:var(--btc);box-shadow:0 0 0 3px #f7931a22}button{width:100%;margin-top:14px;padding:14px;border:0;border-radius:11px;background:var(--btc);color:#1a1005;font-size:16px;font-weight:900;cursor:pointer}.error{margin:0 0 15px;padding:11px;border-radius:9px;background:#3a1e28;color:#ff9baa;text-align:center}.hint{margin:18px 0 0;color:var(--muted);font-size:12px;text-align:center}</style></head><body><main class="login"><div class="coin"><img alt="Bitcoin" src="data:image/png;base64,)HTML");
  page += FPSTR(WEB_LOGO_BASE64);
  page += F(R"HTML("></div><div class="eyebrow">PANNEAU D'ADMINISTRATION</div><h1>BTC Watch</h1><p class="intro">Connectez-vous pour gérer le Wi-Fi, les adresses et l'affichage.</p>)HTML");
  if (invalidPassword)
    page += F("<div class='error'>Mot de passe incorrect.</div>");
  page += F(R"HTML(<form method="post" action="/login"><label for="password">Mot de passe d'administration</label><input id="password" name="password" type="password" minlength="8" maxlength="63" autocomplete="current-password" autofocus required><button type="submit">ENTRER DANS L'ADMINISTRATION</button></form><p class="hint">Mot de passe initial : bitcoin21</p></main></body></html>)HTML");
  return page;
}

void WebInterface::handleRoot() {
  if (!isAuthenticated()) {
    server->send(200, "text/html", generateLoginHTML());
    return;
  }
  refreshSessionCookie();
  sendHTML();
}

void WebInterface::handleLogin() {
  if (!server->hasArg("password") ||
      server->arg("password") != settings->apPassword) {
    server->send(401, "text/html", generateLoginHTML(true));
    return;
  }
  refreshSessionCookie();
  server->sendHeader("Location", "/", true);
  server->send(303, "text/plain", "");
}

void WebInterface::handleLogout() {
  server->sendHeader("Set-Cookie",
                     "BTCWATCH_AUTH=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  server->sendHeader("Location", "/", true);
  server->send(303, "text/plain", "");
}

void WebInterface::handleSave() {
  WalletSetting incoming[MAX_WALLETS];
  uint8_t incomingCount = 0;

  if (server->hasArg("wallets_json")) {
    JsonDocument walletDoc;
    const DeserializationError error =
        deserializeJson(walletDoc, server->arg("wallets_json"));
    if (error || !walletDoc.is<JsonArray>()) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Fichier ou liste JSON invalide\"}");
      return;
    }

    for (JsonObject item : walletDoc.as<JsonArray>()) {
      if (incomingCount >= MAX_WALLETS) break;
      String address = item["address"] | "";
      address.trim();
      if (address.isEmpty()) continue;
      if (!validAddress(address)) {
        server->send(400, "application/json",
                     "{\"ok\":false,\"message\":\"Une adresse Bitcoin est invalide\"}");
        return;
      }

      bool duplicate = false;
      for (uint8_t i = 0; i < incomingCount; ++i) {
        if (incoming[i].address == address) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;

      incoming[incomingCount].collection = item["collection"] | "Manuel";
      incoming[incomingCount].label = item["label"] | "Adresse";
      incoming[incomingCount].address = address;
      incoming[incomingCount].collection.trim();
      incoming[incomingCount].label.trim();
      if (incoming[incomingCount].collection.isEmpty())
        incoming[incomingCount].collection = "Manuel";
      if (incoming[incomingCount].label.isEmpty())
        incoming[incomingCount].label = "Adresse " + String(incomingCount + 1);
      if (incoming[incomingCount].collection.length() > 18)
        incoming[incomingCount].collection.remove(18);
      if (incoming[incomingCount].label.length() > 18)
        incoming[incomingCount].label.remove(18);
      ++incomingCount;
    }
  }

  const String oldSSID = settings->wifiSSID;
  const String oldPass = settings->wifiPASS;
  const String oldAPPassword = settings->apPassword;
  const String oldTimezone = settings->timezone;
  const bool oldStaticIPEnabled = settings->staticIPEnabled;
  const String oldStaticIP = settings->staticIP;
  const String oldStaticGateway = settings->staticGateway;
  const String oldStaticSubnet = settings->staticSubnet;
  const String oldStaticDNS = settings->staticDNS;

  const String currentPassword = server->arg("current_password");
  const String newPassword = server->arg("new_password");
  const String confirmPassword = server->arg("confirm_password");
  const bool passwordChangeRequested = !currentPassword.isEmpty() ||
                                       !newPassword.isEmpty() ||
                                       !confirmPassword.isEmpty();
  if (passwordChangeRequested) {
    if (currentPassword.isEmpty() || newPassword.isEmpty() ||
        confirmPassword.isEmpty()) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Remplissez les trois champs du mot de passe\"}");
      return;
    }
    if (currentPassword != settings->apPassword) {
      server->send(403, "application/json",
                   "{\"ok\":false,\"message\":\"L'ancien mot de passe est incorrect\"}");
      return;
    }
    if (newPassword != confirmPassword) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Les deux nouveaux mots de passe ne correspondent pas\"}");
      return;
    }
    const size_t length = newPassword.length();
    if (length < 8 || length > 63) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Le mot de passe BTC Watch doit contenir entre 8 et 63 caracteres\"}");
      return;
    }
    settings->apPassword = newPassword;
  }

  if (server->hasArg("ssid")) settings->wifiSSID = server->arg("ssid");
  if (server->hasArg("pass") && !server->arg("pass").isEmpty())
    settings->wifiPASS = server->arg("pass");
  const bool newStaticIPEnabled = server->hasArg("static_ip_enabled");
  const String newStaticIP = server->arg("static_ip");
  const String newStaticGateway = server->arg("static_gateway");
  const String newStaticSubnet = server->arg("static_subnet");
  const String newStaticDNS = server->arg("static_dns");
  if (!validIPv4(newStaticIP) || !validIPv4(newStaticGateway) ||
      !validIPv4(newStaticSubnet) || !validIPv4(newStaticDNS)) {
    server->send(400, "application/json",
                 "{\"ok\":false,\"message\":\"Les parametres de l'adresse IP fixe sont invalides\"}");
    return;
  }
  settings->staticIPEnabled = newStaticIPEnabled;
  settings->staticIP = newStaticIP;
  settings->staticGateway = newStaticGateway;
  settings->staticSubnet = newStaticSubnet;
  settings->staticDNS = newStaticDNS;
  if (server->hasArg("currency")) settings->devise = server->arg("currency");
  if (server->hasArg("price_api")) settings->priceAPI = server->arg("price_api");
  if (server->hasArg("clock_color") && validHtmlColor(server->arg("clock_color")))
    settings->clockColor = server->arg("clock_color");
  if (server->hasArg("clock_background") &&
      validHtmlColor(server->arg("clock_background")))
    settings->clockBackground = server->arg("clock_background");
  if (server->hasArg("clock_title")) {
    settings->clockTitle = server->arg("clock_title");
    settings->clockTitle.trim();
    if (settings->clockTitle.length() > 28) settings->clockTitle.remove(28);
  }
  if (server->hasArg("clock_subtitle")) {
    settings->clockSubtitle = server->arg("clock_subtitle");
    settings->clockSubtitle.trim();
    if (settings->clockSubtitle.length() > 32) settings->clockSubtitle.remove(32);
  }
  if (server->hasArg("clock_title_color") &&
      validHtmlColor(server->arg("clock_title_color")))
    settings->clockTitleColor = server->arg("clock_title_color");
  if (server->hasArg("clock_subtitle_color") &&
      validHtmlColor(server->arg("clock_subtitle_color")))
    settings->clockSubtitleColor = server->arg("clock_subtitle_color");
  if (server->hasArg("clock_bar_color") &&
      validHtmlColor(server->arg("clock_bar_color")))
    settings->clockBarColor = server->arg("clock_bar_color");
  if (server->hasArg("clock_button_color") &&
      validHtmlColor(server->arg("clock_button_color")))
    settings->clockButtonColor = server->arg("clock_button_color");
  settings->clockBarUseTimeColor = server->hasArg("clock_bar_same");
  settings->clockButtonUseTimeColor = server->hasArg("clock_button_same");
  settings->clockBarVisible = server->hasArg("clock_bar_visible");
  settings->clockOutlineVisible = server->hasArg("clock_outline_visible");
  if (server->hasArg("clock_background_slot")) {
    const long slot = server->arg("clock_background_slot").toInt();
    if ((slot >= 0 && slot < MAX_CLOCK_BACKGROUNDS) ||
        slot == CLOCK_BACKGROUND_NONE)
      settings->clockBackgroundSlot = static_cast<uint8_t>(slot);
  }
  settings->clockSlideshowEnabled = server->hasArg("clock_slideshow_enabled");
  if (server->hasArg("clock_slideshow_mask"))
    settings->clockSlideshowMask = static_cast<uint16_t>(
        server->arg("clock_slideshow_mask").toInt() &
        ((1L << MAX_CLOCK_BACKGROUNDS) - 1L));
  if (server->hasArg("clock_slideshow_seconds"))
    settings->clockSlideshowSeconds = constrain(
        server->arg("clock_slideshow_seconds").toInt(),
        CLOCK_SLIDESHOW_MIN_SECONDS, CLOCK_SLIDESHOW_MAX_SECONDS);
  if (server->hasArg("tz")) settings->timezone = server->arg("tz");
  if (server->hasArg("brightness"))
    settings->brightness = constrain(server->arg("brightness").toInt(), 30, 255);

  if (server->hasArg("wallets_json")) {
    for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
      settings->wallets[i].collection = "Manuel";
      settings->wallets[i].label = "Wallet " + String(i + 1);
      settings->wallets[i].address = "";
    }
    for (uint8_t i = 0; i < incomingCount; ++i)
      settings->wallets[i] = incoming[i];
  }

  settings->save();
  settingsChanged = true;
  const bool reconnectNeeded = oldSSID != settings->wifiSSID ||
                               oldPass != settings->wifiPASS ||
                               oldAPPassword != settings->apPassword ||
                               oldStaticIPEnabled != settings->staticIPEnabled ||
                               oldStaticIP != settings->staticIP ||
                               oldStaticGateway != settings->staticGateway ||
                               oldStaticSubnet != settings->staticSubnet ||
                               oldStaticDNS != settings->staticDNS;
  const bool timezoneChanged = oldTimezone != settings->timezone;
  server->send(200, "application/json",
               "{\"ok\":true,\"saved\":" + String(incomingCount) + "}");
  if (reconnectNeeded) {
    delay(250);
    network->reconnect(settings->wifiSSID, settings->wifiPASS,
                       settings->timezone, settings->apPassword,
                       settings->staticIPEnabled, settings->staticIP,
                       settings->staticGateway, settings->staticSubnet,
                       settings->staticDNS);
  } else if (timezoneChanged) {
    network->applyTimezone(settings->timezone);
  }
}

void WebInterface::handleReboot() {
  server->send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void WebInterface::handleWifiScan() {
  int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_FAILED) {
    found = WiFi.scanNetworks(true, true);
  }
  if (found == WIFI_SCAN_RUNNING) {
    server->send(202, "application/json", "{\"running\":true}");
    return;
  }
  if (found < 0) {
    WiFi.scanDelete();
    server->send(503, "application/json",
                 "{\"running\":false,\"message\":\"Recherche Wi-Fi impossible\"}");
    return;
  }
  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();
  for (int i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool duplicate = false;
    for (JsonObject saved : networks) {
      if (saved["ssid"].as<String>() == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = ssid;
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String json;
  serializeJson(doc, json);
  WiFi.scanDelete();
  server->send(200, "application/json", json);
}

// Renvoie false sans argument slot (ancien fond RGB565). Avec un argument
// invalide, slot vaut CLOCK_BACKGROUND_NONE.
bool WebInterface::requestedClockBackgroundSlot(uint8_t &slot) {
  if (!server->hasArg("slot")) return false;
  const String value = server->arg("slot");
  slot = CLOCK_BACKGROUND_NONE;
  if (value.isEmpty() || value.length() > 2) return true;
  for (size_t i = 0; i < value.length(); ++i)
    if (!isDigit(value.charAt(i))) return true;
  const long parsed = value.toInt();
  if (parsed >= 0 && parsed < MAX_CLOCK_BACKGROUNDS)
    slot = static_cast<uint8_t>(parsed);
  return true;
}

String WebInterface::clockBackgroundStatusJson() {
  JsonDocument doc;
  doc["ok"] = true;
  JsonArray slots = doc["slots"].to<JsonArray>();
  for (uint8_t slot = 0; slot < MAX_CLOCK_BACKGROUNDS; ++slot) {
    const size_t size = fileSize(Settings::clockBackgroundPath(slot));
    JsonObject item = slots.add<JsonObject>();
    item["slot"] = slot;
    item["present"] = size > 0;
    item["bytes"] = size;
  }
  doc["legacy"] = fileSize(CLOCK_BACKGROUND_PATH) == CLOCK_BACKGROUND_BYTES;
  doc["total"] = SPIFFS.totalBytes();
  doc["used"] = SPIFFS.usedBytes();
  doc["max_bytes"] = CLOCK_BACKGROUND_MAX_JPEG_BYTES;
  String json;
  serializeJson(doc, json);
  return json;
}

void WebInterface::handleClockBackgroundStatus() {
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", clockBackgroundStatusJson());
}

void WebInterface::handleClockBackgroundGet() {
  uint8_t slot = CLOCK_BACKGROUND_NONE;
  if (requestedClockBackgroundSlot(slot)) {
    if (slot >= MAX_CLOCK_BACKGROUNDS) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Emplacement invalide\"}");
      return;
    }
    const String path = Settings::clockBackgroundPath(slot);
    fs::File file = SPIFFS.exists(path) ? SPIFFS.open(path, "r") : fs::File();
    if (!file) {
      server->send(404, "application/json", "{\"ok\":false}");
      return;
    }
    server->sendHeader("Cache-Control", "no-store");
    server->streamFile(file, "image/jpeg");
    file.close();
    return;
  }

  // Ancien fond RGB565 unique, lu par le panneau pour le convertir en JPEG.
  if (!SPIFFS.exists(CLOCK_BACKGROUND_PATH)) {
    server->send(404, "application/json", "{\"ok\":false}");
    return;
  }
  fs::File file = SPIFFS.open(CLOCK_BACKGROUND_PATH, "r");
  if (!file || file.size() != CLOCK_BACKGROUND_BYTES) {
    if (file) file.close();
    server->send(500, "application/json",
                 "{\"ok\":false,\"message\":\"Fond invalide\"}");
    return;
  }
  server->sendHeader("Cache-Control", "no-store");
  server->streamFile(file, "application/octet-stream");
  file.close();
}

void WebInterface::handleClockBackgroundUpload() {
  HTTPUpload &upload = server->upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (clockBackgroundUpload) clockBackgroundUpload.close();
    SPIFFS.remove(CLOCK_BACKGROUND_JPEG_TEMP_PATH);
    clockBackgroundUploadBytes = 0;
    clockBackgroundUploadLimit = 0;
    clockBackgroundUploadOK = false;
    clockBackgroundUploadRemovedFile = false;
    clockBackgroundUploadError = "";
    uint8_t slot = CLOCK_BACKGROUND_NONE;
    if (!requestedClockBackgroundSlot(slot) || slot >= MAX_CLOCK_BACKGROUNDS) {
      clockBackgroundUploadSlot = CLOCK_BACKGROUND_NONE;
      clockBackgroundUploadError = "Emplacement invalide";
      return;
    }
    clockBackgroundUploadSlot = slot;

    const String path = Settings::clockBackgroundPath(slot);
    auto availableBytes = []() -> size_t {
      const size_t total = SPIFFS.totalBytes();
      const size_t used = SPIFFS.usedBytes() + SPIFFS_RESERVED_BYTES;
      return total > used ? total - used : 0;
    };
    size_t available = availableBytes();
    // Manque de place : l'ancien fond RGB565 (150 Ko) est libere en premier ;
    // le panneau web en a deja une copie s'il est en cours de conversion.
    if (available < CLOCK_BACKGROUND_MAX_JPEG_BYTES &&
        SPIFFS.exists(CLOCK_BACKGROUND_PATH)) {
      SPIFFS.remove(CLOCK_BACKGROUND_PATH);
      clockBackgroundUploadRemovedFile = true;
      available = availableBytes();
    }
    // En dernier recours, l'image remplacee est effacee avant la reception.
    if (available < CLOCK_BACKGROUND_MAX_JPEG_BYTES && SPIFFS.exists(path)) {
      SPIFFS.remove(path);
      clockBackgroundUploadRemovedFile = true;
      available = availableBytes();
    }
    clockBackgroundUploadLimit =
        min<size_t>(available, CLOCK_BACKGROUND_MAX_JPEG_BYTES);
    if (clockBackgroundUploadLimit < 4096) {
      clockBackgroundUploadError = "Espace de stockage insuffisant";
      return;
    }
    clockBackgroundUpload = SPIFFS.open(CLOCK_BACKGROUND_JPEG_TEMP_PATH, "w");
    if (!clockBackgroundUpload) {
      clockBackgroundUploadError = "Ecriture impossible";
      return;
    }
    clockBackgroundUploadOK = true;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!clockBackgroundUploadOK || !clockBackgroundUpload) return;
    if (clockBackgroundUploadBytes == 0 &&
        (upload.currentSize < 2 || upload.buf[0] != 0xFF ||
         upload.buf[1] != 0xD8)) {
      clockBackgroundUploadOK = false;
      clockBackgroundUploadError = "Le fichier n'est pas une image JPEG";
      return;
    }
    if (clockBackgroundUploadBytes + upload.currentSize >
        clockBackgroundUploadLimit) {
      clockBackgroundUploadOK = false;
      clockBackgroundUploadError =
          clockBackgroundUploadLimit < CLOCK_BACKGROUND_MAX_JPEG_BYTES
              ? "Espace de stockage insuffisant"
              : "Image trop volumineuse";
      return;
    }
    const size_t written =
        clockBackgroundUpload.write(upload.buf, upload.currentSize);
    clockBackgroundUploadBytes += written;
    if (written != upload.currentSize) {
      clockBackgroundUploadOK = false;
      clockBackgroundUploadError = "Ecriture impossible";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (clockBackgroundUpload) clockBackgroundUpload.close();
    if (clockBackgroundUploadOK) {
      uint16_t width = 0;
      uint16_t height = 0;
      if (TJpgDec.getFsJpgSize(&width, &height,
                               CLOCK_BACKGROUND_JPEG_TEMP_PATH,
                               SPIFFS) != JDR_OK ||
          width != DISPLAY_WIDTH || height != DISPLAY_HEIGHT) {
        clockBackgroundUploadOK = false;
        clockBackgroundUploadError = "L'image doit etre un JPEG de 320 x 240";
      }
    }
    if (clockBackgroundUploadOK) {
      const String path = Settings::clockBackgroundPath(clockBackgroundUploadSlot);
      SPIFFS.remove(path);
      clockBackgroundUploadOK =
          SPIFFS.rename(CLOCK_BACKGROUND_JPEG_TEMP_PATH, path);
      if (!clockBackgroundUploadOK)
        clockBackgroundUploadError = "Enregistrement impossible";
    }
    if (!clockBackgroundUploadOK) SPIFFS.remove(CLOCK_BACKGROUND_JPEG_TEMP_PATH);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (clockBackgroundUpload) clockBackgroundUpload.close();
    clockBackgroundUploadOK = false;
    clockBackgroundUploadError = "Envoi interrompu";
    SPIFFS.remove(CLOCK_BACKGROUND_JPEG_TEMP_PATH);
  }
}

void WebInterface::handleClockBackgroundUploadComplete() {
  const bool success =
      clockBackgroundUploadOK &&
      clockBackgroundUploadSlot < MAX_CLOCK_BACKGROUNDS &&
      Settings::clockBackgroundExists(clockBackgroundUploadSlot);
  const String error = clockBackgroundUploadError;
  const bool removedFile = clockBackgroundUploadRemovedFile;
  // Un envoi suivant sans fichier ne doit pas reutiliser cet etat.
  clockBackgroundUploadOK = false;
  clockBackgroundUploadRemovedFile = false;
  clockBackgroundUploadSlot = CLOCK_BACKGROUND_NONE;
  clockBackgroundUploadError = "";
  if (!success) {
    // Une image a pu etre effacee pour faire de la place : le cadran doit
    // etre redessine sans elle.
    if (removedFile) settingsChanged = true;
    JsonDocument response;
    response["ok"] = false;
    response["message"] =
        error.isEmpty() ? "Image incomplete ou invalide" : error;
    String json;
    serializeJson(response, json);
    server->send(400, "application/json", json);
    return;
  }
  settingsChanged = true;
  server->send(200, "application/json", clockBackgroundStatusJson());
}

void WebInterface::handleClockBackgroundDelete() {
  if (clockBackgroundUpload) clockBackgroundUpload.close();
  SPIFFS.remove(CLOCK_BACKGROUND_TEMP_PATH);
  SPIFFS.remove(CLOCK_BACKGROUND_JPEG_TEMP_PATH);
  uint8_t slot = CLOCK_BACKGROUND_NONE;
  String path = CLOCK_BACKGROUND_PATH;
  if (requestedClockBackgroundSlot(slot)) {
    if (slot >= MAX_CLOCK_BACKGROUNDS) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Emplacement invalide\"}");
      return;
    }
    path = Settings::clockBackgroundPath(slot);
  }
  const bool existed = SPIFFS.exists(path);
  const bool removed = !existed || SPIFFS.remove(path);
  if (!removed) {
    server->send(500, "application/json",
                 "{\"ok\":false,\"message\":\"Suppression impossible\"}");
    return;
  }
  settingsChanged = true;
  server->send(200, "application/json", clockBackgroundStatusJson());
}

void WebInterface::handleConfigurationExport() {
  settings->save();
  fs::File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    server->send(500, "application/json",
                 "{\"ok\":false,\"message\":\"Configuration indisponible\"}");
    return;
  }
  server->sendHeader("Cache-Control", "no-store");
  server->streamFile(file, "application/json");
  file.close();
}

void WebInterface::handleConfigurationImport() {
  if (!server->hasArg("config_json")) {
    server->send(400, "application/json",
                 "{\"ok\":false,\"message\":\"Configuration absente\"}");
    return;
  }

  // dry_run : le panneau verifie la configuration avant de remplacer les
  // images, pour ne jamais laisser l'appareil restaure a moitie.
  const bool dryRun = server->hasArg("dry_run");
  String errorMessage;
  if (!settings->restoreFromJson(server->arg("config_json"), errorMessage,
                                 !dryRun)) {
    JsonDocument response;
    response["ok"] = false;
    response["message"] = errorMessage;
    String json;
    serializeJson(response, json);
    server->send(400, "application/json", json);
    return;
  }
  if (dryRun) {
    server->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  settingsChanged = true;
  server->send(200, "application/json",
               "{\"ok\":true,\"message\":\"Sauvegarde restauree\",\"reboot\":true}");
  delay(750);
  ESP.restart();
}

void WebInterface::sendHTML() {
  auto selected = [](const String &current, const char *value) {
    return current == value ? " selected" : "";
  };
  auto checked = [](bool value) { return value ? " checked" : ""; };

  JsonDocument walletDoc;
  JsonArray wallets = walletDoc.to<JsonArray>();
  for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
    if (settings->wallets[i].address.isEmpty()) continue;
    JsonObject item = wallets.add<JsonObject>();
    item["collection"] = settings->wallets[i].collection;
    item["label"] = settings->wallets[i].label;
    item["address"] = settings->wallets[i].address;
  }
  String initialWallets;
  serializeJson(walletDoc, initialWallets);
  initialWallets.replace("<", "\\u003c");
  initialWallets.replace(">", "\\u003e");
  initialWallets.replace("&", "\\u0026");

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "text/html", "");
  PageWriter page(*server);
  page += F(R"HTML(<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>BTC Watch</title><style>
:root{--bg:#000;--card:#090909;--card2:#141414;--line:#303030;--text:#f0f3f8;--muted:#969696;--btc:#f7931a;--green:#45d483;--red:#ff6577}*{box-sizing:border-box}body{margin:0;background:#000;color:var(--text);font-family:Inter,system-ui,sans-serif}.wrap{width:min(820px,100%);margin:auto;padding:24px 16px 48px}.hero{text-align:center;margin-bottom:20px}.coin{display:grid;place-items:center;width:62px;height:82px;margin:auto}.coin img{display:block;width:56px;height:auto}.hero h1{margin:12px 0 4px;font-size:26px}.hero p{margin:0;color:var(--btc);font-size:13px;font-weight:800;letter-spacing:.08em}#settings{display:flex;flex-direction:column}.wifi-card{order:10}.security-card{order:15}.market-card{order:20}.clock-card{order:30}.addresses-card{order:40}.backup-card{order:50}.maintenance-card{order:60}.actions{order:70}#saveMsg{order:80}.card{background:linear-gradient(145deg,var(--card2),var(--card));border:1px solid var(--line);border-radius:18px;padding:18px;margin:14px 0;box-shadow:0 12px 34px #0005}.titlebar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}.title{display:flex;align-items:center;gap:9px;margin-bottom:14px;font-weight:800}.titlebar .title{margin-bottom:0}.dot{width:9px;height:9px;border-radius:50%;background:var(--btc);box-shadow:0 0 10px var(--btc)}.badge{background:#050505;border:1px solid var(--line);border-radius:999px;padding:5px 10px;color:var(--btc);font-size:12px}.tools{display:flex;flex-wrap:wrap;gap:9px;margin-bottom:14px}.file{position:relative;overflow:hidden}.file input{position:absolute;inset:0;opacity:0;cursor:pointer}.btn{border:0;border-radius:10px;padding:11px 14px;font-weight:800;cursor:pointer}.primary{background:var(--btc);color:#171008}.secondary{background:#242424;color:var(--text)}.danger{background:#3a1e28;color:#ff9baa}.wallets{display:grid;gap:9px}.wallet{display:grid;grid-template-columns:130px 1fr 1.8fr 38px;gap:8px;align-items:end;padding:11px;border:1px solid var(--line);border-radius:12px;background:#050505}.remove{height:40px;background:#3a1e28;color:#ff9baa;border:0;border-radius:9px;font-size:18px;cursor:pointer}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.subsection{grid-column:1/-1;margin-top:6px;padding-top:12px;border-top:1px solid var(--line)}.subsection:first-child{margin-top:0;padding-top:0;border-top:0}.subsection strong{display:block;color:var(--text);font-size:14px}.subsection span{display:block;margin-top:3px;color:var(--muted);font-size:12px}.checkline{display:flex;align-items:center;gap:8px;margin-top:7px;color:var(--text)}.checkline input{width:auto;accent-color:var(--btc)}.gallery-head{grid-column:1/-1;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px}.gallery-head .checkline{margin:0}.gallery{grid-column:1/-1;display:grid;grid-template-columns:repeat(auto-fill,minmax(148px,1fr));gap:10px}.gallery-msg{grid-column:1/-1}.slot{display:flex;flex-direction:column;gap:7px;padding:9px;border:1px solid var(--line);border-radius:12px;background:#050505}.slot.default{border-color:var(--btc);box-shadow:inset 0 0 0 1px var(--btc)}.slot-top{display:flex;justify-content:space-between;align-items:baseline;gap:6px;font-size:13px}.slot-top span{color:var(--muted);font-size:11px}.slot img,.slot .ph{display:block;width:100%;aspect-ratio:4/3;border-radius:8px;background:#000;object-fit:cover}.slot .ph{display:grid;place-items:center;border:1px dashed var(--line);color:#666;font-size:11px;letter-spacing:.08em}.slot .checkline{margin:0;font-size:13px}.slot-actions{display:flex;gap:6px}.slot-actions .btn{flex:1;min-width:0;padding:8px 4px;font-size:12px;text-align:center}.btn:disabled{opacity:.4;cursor:not-allowed}.image-status{color:var(--muted);font-size:12px;line-height:1.45}label{display:block;color:var(--muted);font-size:12px;margin:0 0 5px}input,select{width:100%;border:1px solid var(--line);border-radius:9px;background:#050505;color:var(--text);padding:10px;outline:none}input:focus,select:focus{border-color:var(--btc)}.scan{width:100%;margin-top:7px}.info-box{display:flex;min-height:64px;flex-direction:column;justify-content:center;padding:11px 13px;border:1px solid var(--line);border-radius:10px;background:#050505}.info-box strong{font-size:13px}.info-box span{margin-top:3px;color:var(--muted);font-size:12px}.backup-box{padding:14px;border:1px solid var(--line);border-radius:12px;background:#050505}.backup-box .tools{margin:0 0 10px}.maintenance{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:14px;border:1px solid #673421;border-radius:12px;background:#251207}.maintenance p{margin:0;color:#d9b08d;font-size:12px;line-height:1.5}.key{display:inline-block;margin:0 3px;padding:2px 7px;border:1px solid #9f5730;border-radius:6px;background:#080808;color:var(--btc);font-weight:800}.actions{display:flex;gap:10px;margin-top:18px}.actions .btn{flex:1}.msg{display:none;border-radius:10px;padding:11px;margin:12px 0 0;text-align:center;font-weight:700}.ok{display:block;background:#173525;color:var(--green)}.error{display:block;background:#3a1e28;color:#ff9baa}.hint{font-size:12px;color:var(--muted);line-height:1.5}@media(max-width:700px){.wallet{grid-template-columns:1fr 1fr}.wallet .address{grid-column:1/-1}.grid{grid-template-columns:1fr}.remove{grid-column:2;grid-row:1}.maintenance{align-items:stretch;flex-direction:column}.gallery{grid-template-columns:repeat(2,minmax(0,1fr))}.slot-actions{flex-direction:column}}
</style></head><body><main class="wrap"><header class="hero"><div class="coin"><img alt="Bitcoin" src="data:image/png;base64,)HTML");
  page += FPSTR(WEB_LOGO_BASE64);
  page += F(R"HTML("></div><h1>BTC Watch</h1><p>VERSION )HTML");
  page += APP_VERSION;
  page += F(R"HTML(</p></header><form id="settings"><section class="card addresses-card"><div class="titlebar"><div class="title"><span class="dot"></span>Adresses Bitcoin</div><span class="badge"><b id="count">0</b> / )HTML");
  page += String(MAX_WALLETS);
  page += F(R"HTML(</span></div><div class="tools"><label class="btn primary file">Importer un fichier d'adresses<input id="file" type="file" accept=".json,application/json"></label><button class="btn secondary" id="exportAddresses" type="button">Exporter toutes les adresses</button><button class="btn secondary" id="add" type="button">+ Ajouter une adresse</button><button class="btn danger" id="clear" type="button">Vider la liste</button></div><div id="importMsg" class="msg"></div><div id="wallets" class="wallets"></div><p class="hint">L'import conserve les adresses déjà présentes, ignore les doublons et ajoute le nom de leur collection. L'export crée un fichier JSON compatible avec le format bitwatch-collections. Seules les adresses publiques sont acceptées. Ne saisissez jamais une clé privée ou une phrase de récupération.</p></section><section class="card wifi-card"><div class="title"><span class="dot"></span>Wi-Fi et accès</div><div class="grid"><div><label>Réseau Wi-Fi</label><input id="ssid" name="ssid" value=)HTML");
  page += "'" + htmlEscape(settings->wifiSSID) + "'";
  page += F(R"HTML(><select id="wifiList" class="scan" style="display:none"><option value="">Choisir un réseau détecté</option></select><button id="scan" class="btn secondary scan" type="button">Rechercher les réseaux Wi-Fi</button></div><div><label>Mot de passe du Wi-Fi sélectionné</label><input type="password" name="pass" placeholder="Laisser vide pour conserver"></div><div class="subsection"><strong>Accès permanent sur le réseau domestique</strong><span>L'adresse fixe permet de retrouver le panneau d'administration sans rejoindre le réseau de secours.</span><label class="checkline"><input id="staticIpEnabled" type="checkbox" name="static_ip_enabled" value="1")HTML");
  page += checked(settings->staticIPEnabled);
  page += F(R"HTML(>Utiliser une adresse IP fixe</label></div><div><label>Adresse de BTC Watch</label><input class="static-ip-field" type="text" name="static_ip" inputmode="decimal" value=)HTML");
  page += "'" + htmlEscape(settings->staticIP) + "'";
  page += F(R"HTML(></div><div><label>Passerelle / adresse de la box</label><input class="static-ip-field" type="text" name="static_gateway" inputmode="decimal" value=)HTML");
  page += "'" + htmlEscape(settings->staticGateway) + "'";
  page += F(R"HTML(></div><div><label>Masque de sous-réseau</label><input class="static-ip-field" type="text" name="static_subnet" inputmode="decimal" value=)HTML");
  page += "'" + htmlEscape(settings->staticSubnet) + "'";
  page += F(R"HTML(></div><div><label>Serveur DNS</label><input class="static-ip-field" type="text" name="static_dns" inputmode="decimal" value=)HTML");
  page += "'" + htmlEscape(settings->staticDNS) + "'";
  page += F(R"HTML(></div><div class="info-box"><strong>Accès au panneau d'administration</strong><span>Réseau domestique : http://)HTML");
  page += htmlEscape(settings->staticIP);
  page += F(R"HTML( ou http://btc-watch.local</span><span>Réseau de secours BTC Watch : http://192.168.22.1</span></div></div></section><section class="card security-card"><div class="title"><span class="dot"></span>Sécurité et mot de passe</div><div class="grid"><div><label>Ancien mot de passe</label><input type="password" name="current_password" minlength="8" maxlength="63" autocomplete="current-password" placeholder="Mot de passe actuel"></div><div><label>Nouveau mot de passe</label><input type="password" name="new_password" minlength="8" maxlength="63" autocomplete="new-password" placeholder="8 à 63 caractères"></div><div><label>Confirmer le nouveau mot de passe</label><input type="password" name="confirm_password" minlength="8" maxlength="63" autocomplete="new-password" placeholder="Saisissez-le une seconde fois"></div><div class="info-box"><strong>Protection BTC Watch</strong><span>Ce mot de passe protège le Wi-Fi de secours et le panneau d'administration. Les deux nouveaux mots de passe doivent être identiques.</span></div></div></section><section class="card market-card"><div class="title"><span class="dot"></span>Cours et données Bitcoin</div><div class="grid"><div><label>Source du cours</label><select name="price_api"><option value="COINGECKO")HTML");
  page += selected(settings->priceAPI, "COINGECKO");
  page += F(">CoinGecko</option><option value='BINANCE'");
  page += selected(settings->priceAPI, "BINANCE");
  page += F(">Binance</option><option value='BITSTAMP'");
  page += selected(settings->priceAPI, "BITSTAMP");
  page += F(R"HTML(>Bitstamp</option></select></div><div class="info-box"><strong>Devise EUR / USD</strong><span>La devise se choisit directement avec les boutons tactiles USD et EUR de l'écran Marché.</span></div></div></section><section class="card clock-card"><div class="title"><span class="dot"></span>Cadran Heure et affichage</div><div class="grid"><div class="subsection"><strong>Réglages généraux</strong><span>Fuseau horaire et luminosité globale de l'écran.</span></div><div><label>Fuseau horaire</label><select name="tz">)HTML");
  for (int tz = -12; tz <= 14; ++tz) {
    const String value = String("UTC") + (tz >= 0 ? "+" : "") + String(tz);
    page += "<option" + String(settings->timezone == value ? " selected" : "") + ">" + value + "</option>";
  }
  page += F("</select></div><div><label>Luminosité : <span id='bv'>");
  page += String(settings->brightness);
  page += F("</span></label><input id='brightness' type='range' min='30' max='255' name='brightness' value='");
  page += String(settings->brightness);
  page += F("'></div><div class='subsection'><strong>Couleurs principales</strong><span>Personnalisez l'heure et le fond du cadran.</span></div><div><label>Couleur de l'heure</label><input type='color' name='clock_color' value='");
  page += htmlEscape(settings->clockColor);
  page += F("'></div><div><label>Fond de l'horloge</label><input type='color' name='clock_background' value='");
  page += htmlEscape(settings->clockBackground);
  page += F(R"HTML('></div><div class='subsection'><strong>Images d'arrière-plan</strong><span>Jusqu'à 10 images. Le navigateur recadre chaque photo en 320 × 240 et la compresse en JPEG. L'image par défaut et le diaporama s'appliquent après « Enregistrer et actualiser ».</span></div><div class='gallery-head'><label class='checkline'><input type='radio' name='clock_background_slot' value='255')HTML");
  page += checked(settings->clockBackgroundSlot == CLOCK_BACKGROUND_NONE);
  page += F(R"HTML(>Aucune image : couleur de fond seule</label><span id='bgSpace' class='image-status'></span></div><div id='gallery' class='gallery'></div><div id='clockBgMsg' class='msg gallery-msg'></div><div class='subsection'><strong>Diaporama</strong><span>Fait défiler sur l'écran Heure les images cochées « Diaporama ».</span></div><div><label class='checkline'><input type='checkbox' name='clock_slideshow_enabled' value='1')HTML");
  page += checked(settings->clockSlideshowEnabled);
  page += F(R"HTML(>Activer le diaporama</label><div id='slideCount' class='image-status'></div></div><div><label>Durée d'affichage de chaque image</label><select name='clock_slideshow_seconds'>)HTML");
  {
    static const uint16_t durations[] = {5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
    static const char *const durationLabels[] = {
        "5 secondes", "10 secondes", "15 secondes", "30 secondes",
        "1 minute", "2 minutes", "5 minutes", "10 minutes",
        "15 minutes", "30 minutes", "1 heure"};
    bool durationListed = false;
    for (uint8_t i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i) {
      const bool current = settings->clockSlideshowSeconds == durations[i];
      durationListed = durationListed || current;
      page += "<option value='" + String(durations[i]) + "'" +
              String(current ? " selected" : "") + ">" + durationLabels[i] +
              "</option>";
    }
    // Duree restauree depuis une sauvegarde mais absente de la liste.
    if (!durationListed)
      page += "<option value='" + String(settings->clockSlideshowSeconds) +
              "' selected>" + String(settings->clockSlideshowSeconds) +
              " secondes</option>";
  }
  page += F("</select></div><input type='hidden' id='slideshowMask' name='clock_slideshow_mask' value='");
  page += String(settings->clockSlideshowMask);
  page += F(R"HTML('><label class='checkline'><input type='checkbox' name='clock_outline_visible' value='1')HTML");
  page += checked(settings->clockOutlineVisible);
  page += F(R"HTML(>Afficher le contour de l'heure sur l'image</label><div class='subsection'><strong>Textes du cadran</strong><span>Choisissez les deux textes et leur couleur.</span></div><div><label>Texte au-dessus de l'heure</label><input type='text' name='clock_title' maxlength='28' value=')HTML");
  page += htmlEscape(settings->clockTitle);
  page += F("'></div><div><label>Couleur du texte supérieur</label><input type='color' name='clock_title_color' value='");
  page += htmlEscape(settings->clockTitleColor);
  page += F("'></div><div><label>Texte sous l'heure</label><input type='text' name='clock_subtitle' maxlength='32' value='");
  page += htmlEscape(settings->clockSubtitle);
  page += F("'></div><div><label>Couleur du texte inférieur</label><input type='color' name='clock_subtitle_color' value='");
  page += htmlEscape(settings->clockSubtitleColor);
  page += F("'></div><div class='subsection'><strong>Barre et bouton de navigation</strong><span>Par défaut, ces éléments reprennent la couleur de l'heure.</span></div><div><label>Couleur de la barre</label><input type='color' name='clock_bar_color' value='");
  page += htmlEscape(settings->clockBarColor);
  page += F("'><label class='checkline'><input type='checkbox' name='clock_bar_same' value='1'");
  page += checked(settings->clockBarUseTimeColor);
  page += F(">Utiliser la couleur de l'heure</label></div><div><label>Couleur du bouton de navigation</label><input type='color' name='clock_button_color' value='");
  page += htmlEscape(settings->clockButtonColor);
  page += F("'><label class='checkline'><input type='checkbox' name='clock_button_same' value='1'");
  page += checked(settings->clockButtonUseTimeColor);
  page += F(">Utiliser la couleur de l'heure</label><label class='checkline'><input type='checkbox' name='clock_bar_visible' value='1'");
  page += checked(settings->clockBarVisible);
  page += F(R"HTML(>Afficher la ligne décorative</label></div></div></section><section class="card backup-card"><div class="title"><span class="dot"></span>Sauvegarde personnalisée complète</div><div class="backup-box"><div class="tools"><button class="btn primary" id="backupExport" type="button">Exporter toute la configuration</button><label class="btn secondary file">Importer une sauvegarde complète<input id="backupFile" type="file" accept=".json,application/json"></label></div><div id="backupMsg" class="msg"></div><p class="hint">Un seul fichier JSON conserve les adresses, le Wi-Fi, les mots de passe, les textes, les couleurs, la devise, le fuseau horaire, la luminosité, les options du cadran, le diaporama et les images de fond. Enregistrez vos modifications avant l'export et gardez ce fichier dans un endroit privé.</p></div></section><section class="card maintenance-card"><div class="title"><span class="dot"></span>Maintenance de l'appareil</div><div class="maintenance"><p><strong>Remise à zéro complète :</strong><br>Appareil allumé, maintenez le bouton arrière <span class="key">BOOT</span> pendant 5 secondes. Le Wi-Fi, les adresses, les couleurs, les images de fond et le mot de passe BTC Watch seront effacés.</p><button class="btn secondary" id="reboot" type="button">Redémarrer</button></div></section><div class="actions"><button class="btn primary" type="submit">Enregistrer et actualiser</button></div><div id="saveMsg" class="msg"></div></form></main><script>
const APP_VERSION=")HTML");
  page += APP_VERSION;
  page += F(R"HTML(",MAX=)HTML");
  page += String(MAX_WALLETS);
  page += F(", wallets=");
  page += initialWallets;
  page += F(";let bgDefault=");
  page += String(settings->clockBackgroundSlot);
  page += F(",bgMask=");
  page += String(settings->clockSlideshowMask);
  page += F(";const MAXBG=");
  page += String(MAX_CLOCK_BACKGROUNDS);
  page += F(",MAXJPEG=");
  page += String(CLOCK_BACKGROUND_MAX_JPEG_BYTES);
  page += F(R"HTML(;const box=document.querySelector('#wallets'),count=document.querySelector('#count'),im=document.querySelector('#importMsg'),sm=document.querySelector('#saveMsg');
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const key=a=>/^bc1/i.test(a)?a.toLowerCase():a;const valid=a=>/^(bc1|BC1|1|3)[A-Za-z0-9]{13,89}$/.test(a);
function message(el,text,ok=true){el.className='msg '+(ok?'ok':'error');el.textContent=text;el.style.display='block'}
setInterval(()=>fetch('/session',{cache:'no-store'}).catch(()=>{}),60000);
function downloadJSON(name,data){const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1500)}
function bytesToBase64(bytes){let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,Math.min(i+32768,bytes.length)));return btoa(binary)}
function base64ToBytes(value){const binary=atob(value),bytes=new Uint8Array(binary.length);for(let i=0;i<binary.length;i++)bytes[i]=binary.charCodeAt(i);return bytes}
function render(){count.textContent=wallets.length;box.innerHTML=wallets.map((w,i)=>`<div class="wallet"><div><label>Collection</label><input data-i="${i}" data-k="collection" maxlength="18" value="${esc(w.collection||'Manuel')}"></div><div><label>Nom</label><input data-i="${i}" data-k="label" maxlength="18" value="${esc(w.label||'')}"></div><div class="address"><label>Adresse publique</label><input data-i="${i}" data-k="address" maxlength="90" spellcheck="false" value="${esc(w.address||'')}"></div><button class="remove" type="button" data-remove="${i}" title="Supprimer">x</button></div>`).join('')||'<p class="hint">Aucune adresse. Importez le fichier JSON ou utilisez le bouton Ajouter une adresse.</p>';}
box.oninput=e=>{const i=Number(e.target.dataset.i),k=e.target.dataset.k;if(k&&wallets[i])wallets[i][k]=e.target.value};box.onclick=e=>{if(e.target.dataset.remove!==undefined){wallets.splice(Number(e.target.dataset.remove),1);render()}};
document.querySelector('#add').onclick=()=>{if(wallets.length>=MAX)return message(im,'Limite de '+MAX+' adresses atteinte.',false);wallets.push({collection:'Manuel',label:'Adresse '+(wallets.length+1),address:''});render();box.lastElementChild?.scrollIntoView({behavior:'smooth',block:'center'})};
document.querySelector('#clear').onclick=()=>{if(confirm('Supprimer toutes les adresses de la liste ?')){wallets.splice(0);render();message(im,'Liste vide. Cliquez sur Enregistrer pour confirmer.')}};
document.querySelector('#file').onchange=async e=>{const f=e.target.files[0];if(!f)return;try{const data=JSON.parse(await f.text());if(!data.collections||typeof data.collections!=='object')throw Error('Le champ collections est absent.');let added=0,dupes=0,invalid=0,full=0;const known=new Set(wallets.filter(w=>w.address).map(w=>key(w.address.trim())));for(const [collection,value] of Object.entries(data.collections)){for(const item of (value.addresses||[])){const address=String(item.address||'').trim();if(!valid(address)){invalid++;continue}const k=key(address);if(known.has(k)){dupes++;continue}if(wallets.length>=MAX){full++;continue}wallets.push({collection:String(collection).slice(0,18),label:String(item.name||('Adresse '+(wallets.length+1))).slice(0,18),address});known.add(k);added++}}render();message(im,`${added} adresse(s) importee(s), ${dupes} doublon(s), ${invalid} invalide(s)${full?', '+full+' hors limite':''}.`,invalid===0&&full===0)}catch(err){message(im,'Import impossible : '+err.message,false)}e.target.value=''};
document.querySelector('#exportAddresses').onclick=()=>{const collections={};let exported=0;for(const wallet of wallets){const address=String(wallet.address||'').trim();if(!address)continue;if(!valid(address))return message(im,'Export impossible, adresse invalide : '+address,false);const collection=String(wallet.collection||'Manuel').trim()||'Manuel',name=String(wallet.label||'Adresse').trim()||'Adresse';if(!collections[collection])collections[collection]={addresses:[],extendedKeys:[],descriptors:[]};collections[collection].addresses.push({address,name,expect:{chain_in:0,chain_out:0,mempool_in:0,mempool_out:0},alerted:{chain_in:false,chain_out:false,mempool_in:false,mempool_out:false},trackWebsocket:false,monitor:{chain_in:'auto-accept',chain_out:'alert',mempool_in:'auto-accept',mempool_out:'alert'},queued:false,actual:{chain_in:0,chain_out:0,mempool_in:0,mempool_out:0},error:false,errorMessage:null});exported++}if(!exported)return message(im,'Aucune adresse à exporter.',false);const date=new Date().toISOString().slice(0,10);downloadJSON('btc-watch-adresses-'+date+'.json',{collections});message(im,exported+' adresse(s) exportée(s) au format Bitwatch.')};
const scan=document.querySelector('#scan'),wifiList=document.querySelector('#wifiList'),ssid=document.querySelector('#ssid');let wifiScanning=false;async function scanWifi(){if(!wifiScanning){wifiScanning=true;scan.disabled=true;scan.textContent='Recherche en cours...'}try{const r=await fetch('/wifi-scan');if(r.status===202){setTimeout(scanWifi,350);return}if(!r.ok)throw Error('Recherche impossible');const networks=await r.json();wifiList.innerHTML='<option value="">Choisir un réseau détecté</option>'+networks.map(n=>`<option value="${esc(n.ssid)}">${esc(n.ssid)} (${n.rssi} dBm)${n.secure?' - sécurisé':''}</option>`).join('');wifiList.style.display='block';scan.textContent=networks.length+' réseau(x) détecté(s)'}catch(e){scan.textContent='Relancer la recherche'}wifiScanning=false;scan.disabled=false}scan.onclick=()=>{if(!wifiScanning)scanWifi()};wifiList.onchange=()=>{if(wifiList.value)ssid.value=wifiList.value};setTimeout(scanWifi,600);
const staticIpEnabled=document.querySelector('#staticIpEnabled'),staticIpFields=[...document.querySelectorAll('.static-ip-field')];function updateStaticIpFields(){for(const field of staticIpFields){field.readOnly=!staticIpEnabled.checked;field.style.opacity=staticIpEnabled.checked?'1':'.55'}}staticIpEnabled.onchange=updateStaticIpFields;updateStaticIpFields();
const gallery=document.querySelector('#gallery'),bgMsg=document.querySelector('#clockBgMsg'),bgSpace=document.querySelector('#bgSpace'),slideCount=document.querySelector('#slideCount'),slideMaskInput=document.querySelector('#slideshowMask'),noBackground=document.querySelector('input[name=clock_background_slot][value="255"]');let bgSlots=[],bgStamp=Date.now();
const kb=n=>Math.round(Number(n||0)/1024)+' Ko';
function updateBgSummary(){slideMaskInput.value=bgMask;const n=bgSlots.filter((s,i)=>s&&s.present&&(bgMask&(1<<i))).length;slideCount.textContent=n>1?n+' images sélectionnées pour le défilement.':n?'1 image sélectionnée : cochez-en au moins deux pour le défilement.':'Aucune image sélectionnée pour le défilement.'}
function renderGallery(){noBackground.checked=bgDefault===255;gallery.innerHTML=Array.from({length:MAXBG},(_,i)=>{const s=bgSlots[i]||{},present=!!s.present,isDefault=bgDefault===i,inShow=!!(bgMask&(1<<i));return `<div class="slot${isDefault?' default':''}"><div class="slot-top"><b>Image ${i+1}</b><span>${present?kb(s.bytes):'Vide'}</span></div>${present?`<img src="/clock-background?slot=${i}&ts=${bgStamp}" alt="Image ${i+1}" loading="lazy">`:'<div class="ph">AUCUNE IMAGE</div>'}<label class="checkline"><input type="radio" name="clock_background_slot" value="${i}"${isDefault?' checked':''}${present||isDefault?'':' disabled'}>Par défaut</label><label class="checkline"><input type="checkbox" data-show="${i}"${inShow?' checked':''}${present?'':' disabled'}>Diaporama</label><div class="slot-actions"><label class="btn primary file">${present?'Remplacer':'Ajouter'}<input type="file" data-upload="${i}" accept="image/png,image/jpeg,image/webp,image/gif"></label><button class="btn danger" type="button" data-delete="${i}"${present?'':' disabled'}>Supprimer</button></div></div>`}).join('');updateBgSummary()}
function applyBgStatus(j){if(!j||!Array.isArray(j.slots))throw Error('Réponse de la galerie illisible');bgSlots=j.slots;bgStamp=Date.now();bgSpace.textContent='Stockage utilisé : '+kb(j.used)+' / '+kb(j.total);renderGallery();return j}
async function loadBgStatus(){const r=await fetch('/clock-backgrounds?ts='+Date.now(),{cache:'no-store'});if(!r.ok)throw Error('Galerie indisponible');return applyBgStatus(await r.json())}
function newCanvas(){const c=document.createElement('canvas');c.width=320;c.height=240;return c}
async function canvasToJpeg(c){for(const q of [.88,.8,.72,.64,.55,.45,.35]){const blob=await new Promise(r=>c.toBlob(r,'image/jpeg',q));if(blob&&blob.size<=MAXJPEG)return blob}throw Error('image trop détaillée pour être compressée')}
async function fileToJpeg(file){const image=await createImageBitmap(file),c=newCanvas(),ctx=c.getContext('2d'),scale=Math.max(320/image.width,240/image.height),w=image.width*scale,h=image.height*scale;ctx.fillStyle='#000';ctx.fillRect(0,0,320,240);ctx.drawImage(image,(320-w)/2,(240-h)/2,w,h);image.close?.();return canvasToJpeg(c)}
function rgb565ToCanvas(bytes){if(bytes.length!==153600)throw Error('Format RGB565 incorrect');const c=newCanvas(),ctx=c.getContext('2d'),image=ctx.createImageData(320,240),p=image.data;for(let i=0,j=0;i<bytes.length;i+=2,j+=4){const v=bytes[i]|(bytes[i+1]<<8);p[j]=Math.round(((v>>11)&31)*255/31);p[j+1]=Math.round(((v>>5)&63)*255/63);p[j+2]=Math.round((v&31)*255/31);p[j+3]=255}ctx.putImageData(image,0,0);return c}
async function uploadBackground(slot,blob){const body=new FormData();body.append('background',blob,'clock_bg.jpg');const r=await fetch('/clock-background?slot='+slot,{method:'POST',body});const j=await r.json().catch(()=>({message:'Réponse illisible'}));if(!r.ok)throw Error(j.message||'Envoi impossible');return applyBgStatus(j)}
async function deleteBackground(slot){const r=await fetch('/clock-background/delete'+(slot===undefined?'':'?slot='+slot),{method:'POST'});const j=await r.json().catch(()=>({message:'Réponse illisible'}));if(!r.ok)throw Error(j.message||'Suppression impossible');return applyBgStatus(j)}
function setGalleryBusy(busy){gallery.style.pointerEvents=busy?'none':'';gallery.style.opacity=busy?'.55':''}
noBackground.onchange=()=>{if(noBackground.checked){bgDefault=255;renderGallery()}};
gallery.onchange=async e=>{const t=e.target;if(t.name==='clock_background_slot'){bgDefault=Number(t.value);renderGallery();return}if(t.dataset.show!==undefined){const bit=1<<Number(t.dataset.show);bgMask=t.checked?bgMask|bit:bgMask&~bit;updateBgSummary();return}if(t.dataset.upload===undefined)return;const slot=Number(t.dataset.upload),file=t.files[0];if(!file)return;bgMsg.style.display='none';setGalleryBusy(true);try{const blob=await fileToJpeg(file);const previousDefault=bgDefault;await uploadBackground(slot,blob);let text='Image '+(slot+1)+' enregistrée ('+kb(blob.size)+').';if(previousDefault!==255&&!bgSlots[previousDefault]?.present){bgDefault=slot;renderGallery();text+=' Elle est choisie par défaut : cliquez sur Enregistrer pour confirmer.'}message(bgMsg,text)}catch(err){message(bgMsg,'Image impossible : '+err.message,false);loadBgStatus().catch(()=>{})}finally{setGalleryBusy(false)}};
gallery.onclick=async e=>{const slot=e.target.dataset.delete;if(slot===undefined)return;if(!confirm("Supprimer l'image "+(Number(slot)+1)+' ?'))return;bgMsg.style.display='none';setGalleryBusy(true);try{await deleteBackground(Number(slot));message(bgMsg,'Image '+(Number(slot)+1)+' supprimée.')}catch(err){message(bgMsg,err.message,false)}finally{setGalleryBusy(false)}};
async function migrateLegacyBackground(status){if(!status||!status.legacy)return;const free=bgSlots.findIndex(s=>!s.present);if(free<0)return;try{const r=await fetch('/clock-background?ts='+Date.now(),{cache:'no-store'});if(!r.ok)throw Error('ancienne image illisible');const blob=await canvasToJpeg(rgb565ToCanvas(new Uint8Array(await r.arrayBuffer())));await uploadBackground(free,blob);await deleteBackground();message(bgMsg,"Votre ancienne image de fond a été ajoutée à la galerie (image "+(free+1)+').')}catch(err){message(bgMsg,"Conversion de l'ancienne image impossible : "+err.message,false)}}
renderGallery();loadBgStatus().then(migrateLegacyBackground).catch(err=>message(bgMsg,err.message,false));
const f=document.querySelector('#settings'),b=document.querySelector('#brightness'),bv=document.querySelector('#bv'),backupExport=document.querySelector('#backupExport'),backupFile=document.querySelector('#backupFile'),backupMsg=document.querySelector('#backupMsg');
const ipv4=v=>/^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$/.test(v);
function jpegInfo(b){if(b[0]!==255||b[1]!==216)return null;let i=2;while(i+9<b.length){if(b[i]!==255)return null;const m=b[i+1],len=b[i+2]<<8|b[i+3];if(m>=192&&m<=194)return{progressive:m===194,height:b[i+5]<<8|b[i+6],width:b[i+7]<<8|b[i+8]};i+=2+len}return null}
// Le Wi-Fi et les mots de passe restent ceux enregistres : une saisie non
// validee ne doit jamais devenir le mot de passe d'une restauration.
function mergeVisibleConfiguration(config){const form=new FormData(f);config.static_ip_enabled=form.has('static_ip_enabled');config.static_ip=String(form.get('static_ip')||config.static_ip);config.static_gateway=String(form.get('static_gateway')||config.static_gateway);config.static_subnet=String(form.get('static_subnet')||config.static_subnet);config.static_dns=String(form.get('static_dns')||config.static_dns);for(const k of ['static_ip','static_gateway','static_subnet','static_dns'])if(!ipv4(config[k]))throw Error('adresse IP invalide : '+config[k]);config.tz=String(form.get('tz')||config.tz);config.price_api=String(form.get('price_api')||config.price_api);config.clock_color=String(form.get('clock_color')||config.clock_color);config.clock_background=String(form.get('clock_background')||config.clock_background);config.clock_title=String(form.get('clock_title')??config.clock_title);config.clock_subtitle=String(form.get('clock_subtitle')??config.clock_subtitle);config.clock_title_color=String(form.get('clock_title_color')||config.clock_title_color);config.clock_subtitle_color=String(form.get('clock_subtitle_color')||config.clock_subtitle_color);config.clock_bar_color=String(form.get('clock_bar_color')||config.clock_bar_color);config.clock_button_color=String(form.get('clock_button_color')||config.clock_button_color);config.clock_bar_same_as_time=form.has('clock_bar_same');config.clock_button_same_as_time=form.has('clock_button_same');config.clock_bar_visible=form.has('clock_bar_visible');config.clock_outline_visible=form.has('clock_outline_visible');config.clock_background_slot=bgDefault;config.clock_slideshow_enabled=form.has('clock_slideshow_enabled');config.clock_slideshow_mask=bgMask;config.clock_slideshow_seconds=Number(form.get('clock_slideshow_seconds')||config.clock_slideshow_seconds);config.brightness=Number(form.get('brightness')||config.brightness);const seen=new Set();config.wallets=[];for(const w of wallets){const address=String(w.address||'').trim();if(!address)continue;if(!valid(address))throw Error('adresse invalide : '+address+'. Corrigez-la avant d’exporter');const k=key(address);if(seen.has(k))continue;seen.add(k);config.wallets.push({collection:String(w.collection||'Manuel').trim().slice(0,18)||'Manuel',label:String(w.label||'Adresse').trim().slice(0,18)||'Adresse',address})}config.wallets=config.wallets.slice(0,MAX);return config}
backupExport.onclick=async()=>{backupExport.disabled=true;backupFile.disabled=true;backupMsg.style.display='none';try{const configResponse=await fetch('/configuration?ts='+Date.now(),{cache:'no-store'});if(!configResponse.ok)throw Error('Configuration indisponible');const configuration=mergeVisibleConfiguration(await configResponse.json());await loadBgStatus();const clock_backgrounds=[];for(const s of bgSlots){if(!s.present)continue;const r=await fetch('/clock-background?slot='+s.slot+'&ts='+Date.now(),{cache:'no-store'});if(!r.ok)throw Error("L'image "+(s.slot+1)+' est indisponible');clock_backgrounds.push({slot:s.slot,format:'jpeg',encoding:'base64',data:bytesToBase64(new Uint8Array(await r.arrayBuffer()))})}const backup={format:'btc-watch-complete-backup',schema_version:2,app_version:APP_VERSION,exported_at:new Date().toISOString(),configuration,clock_backgrounds};const date=new Date().toISOString().slice(0,10);downloadJSON('btc-watch-sauvegarde-'+date+'.json',backup);message(backupMsg,'Sauvegarde complète téléchargée ('+clock_backgrounds.length+' image(s) de fond). Conservez-la dans un endroit privé.')}catch(err){message(backupMsg,'Sauvegarde impossible : '+err.message,false)}finally{backupExport.disabled=false;backupFile.disabled=false}};
backupFile.onchange=async e=>{const file=e.target.files[0];if(!file)return;backupExport.disabled=true;backupFile.disabled=true;backupMsg.style.display='none';try{const backup=JSON.parse(await file.text()),required=['ssid','pass','ap_password','tz','currency','price_api','clock_color','clock_background','clock_title','clock_subtitle','clock_title_color','clock_subtitle_color','clock_bar_color','clock_button_color','clock_bar_same_as_time','clock_button_same_as_time','clock_bar_visible','clock_outline_visible','brightness','wallets'],incompatible='Ce fichier n’est pas une sauvegarde BTC Watch compatible';if(backup.format!=='btc-watch-complete-backup'||![1,2].includes(backup.schema_version)||!backup.configuration)throw Error(incompatible);const missing=required.filter(key=>!Object.prototype.hasOwnProperty.call(backup.configuration,key));if(missing.length)throw Error('Réglages manquants : '+missing.join(', '));const images=[];if(backup.schema_version===1){const old=backup.clock_background;if(!old)throw Error(incompatible);if(old.present){if(old.encoding!=='base64'||old.format!=='rgb565-le'||old.width!==320||old.height!==240)throw Error("Format d'image non compatible");images.push({slot:0,blob:await canvasToJpeg(rgb565ToCanvas(base64ToBytes(String(old.data||''))))})}}else{if(!Array.isArray(backup.clock_backgrounds))throw Error(incompatible);for(const item of backup.clock_backgrounds){const slot=Number(item.slot);if(!Number.isInteger(slot)||slot<0||slot>=MAXBG||item.format!=='jpeg'||item.encoding!=='base64'||images.some(i=>i.slot===slot))throw Error("Image de fond non compatible");const bytes=base64ToBytes(String(item.data||'')),info=jpegInfo(bytes);if(!info||info.progressive||info.width!==320||info.height!==240||bytes.length>MAXJPEG)throw Error("L'image "+(slot+1)+" de la sauvegarde n'est pas compatible");images.push({slot,blob:new Blob([bytes],{type:'image/jpeg'})})}}const check=new FormData();check.set('config_json',JSON.stringify(backup.configuration));check.set('dry_run','1');const checkResponse=await fetch('/configuration/import',{method:'POST',body:check});const checkResult=await checkResponse.json().catch(()=>({message:'Réponse illisible'}));if(!checkResponse.ok)throw Error((checkResult.message||'Configuration refusée')+'. Rien n’a été modifié');if(!confirm('Restaurer toute cette configuration ? Les réglages, adresses, mots de passe et images actuels seront remplacés.'))return;await loadBgStatus();for(let slot=0;slot<MAXBG;slot++)if(bgSlots[slot]?.present&&!images.some(i=>i.slot===slot))await deleteBackground(slot);for(const image of images){try{await uploadBackground(image.slot,image.blob)}catch(err){throw Error("Restauration de l'image "+(image.slot+1)+' impossible : '+err.message)}}await deleteBackground();const body=new FormData();body.set('config_json',JSON.stringify(backup.configuration));const response=await fetch('/configuration/import',{method:'POST',body});const result=await response.json().catch(()=>({message:'Réponse illisible'}));if(!response.ok)throw Error(result.message||'Restauration impossible');message(backupMsg,'Configuration complète restaurée. BTC Watch redémarre pour tout réappliquer.');setTimeout(()=>location.reload(),7000)}catch(err){message(backupMsg,'Import impossible : '+err.message,false);loadBgStatus().catch(()=>{})}finally{backupExport.disabled=false;backupFile.disabled=false;e.target.value=''}};
b.oninput=()=>bv.textContent=b.value;f.onsubmit=async e=>{e.preventDefault();sm.style.display='none';const currentPassword=f.elements.current_password.value,newPassword=f.elements.new_password.value,confirmPassword=f.elements.confirm_password.value,passwordChange=currentPassword||newPassword||confirmPassword;if(passwordChange){if(!currentPassword||!newPassword||!confirmPassword)return message(sm,'Remplissez les trois champs du mot de passe.',false);if(newPassword.length<8||newPassword.length>63)return message(sm,'Le nouveau mot de passe doit contenir entre 8 et 63 caractères.',false);if(newPassword!==confirmPassword)return message(sm,'Les deux nouveaux mots de passe ne correspondent pas.',false)}const cleaned=[];const seen=new Set();for(const w of wallets){w.collection=String(w.collection||'Manuel').trim().slice(0,18)||'Manuel';w.label=String(w.label||'Adresse').trim().slice(0,18)||'Adresse';w.address=String(w.address||'').trim();if(!w.address)continue;if(!valid(w.address))return message(sm,'Adresse invalide : '+w.address,false);const k=key(w.address);if(seen.has(k))continue;seen.add(k);cleaned.push(w)}wallets.splice(0,wallets.length,...cleaned);const body=new FormData(f);body.set('wallets_json',JSON.stringify(wallets));const r=await fetch('/save',{method:'POST',body});const j=await r.json().catch(()=>({message:'Reponse illisible'}));if(r.ok){render();if(passwordChange){f.elements.current_password.value='';f.elements.new_password.value='';f.elements.confirm_password.value=''}message(sm,j.saved+' adresse(s) enregistree(s).')}else message(sm,j.message||'Enregistrement impossible.',false)};document.querySelector('#reboot').onclick=()=>fetch('/reboot',{method:'POST'});render();
</script></body></html>)HTML");
  page.flush();
  // Morceau vide final : fin de la reponse chunked.
  server->sendContent("");
}
