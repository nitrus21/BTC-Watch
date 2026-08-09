#include "web_interface.h"
#include "config.h"
#include "web_logo_data.h"
#include <ArduinoJson.h>
#include <ESP.h>
#include <SPIFFS.h>

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
}

WebInterface::WebInterface(Settings *s, NetworkManager *n)
    : server(new WebServer(80)), settings(s), network(n), settingsChanged(false),
      clockBackgroundUploadBytes(0), clockBackgroundUploadOK(false) {}

WebInterface::~WebInterface() { delete server; }

void WebInterface::begin() {
  server->on("/", [this]() { handleRoot(); });
  server->on("/save", HTTP_POST, [this]() { handleSave(); });
  server->on("/reboot", HTTP_POST, [this]() { handleReboot(); });
  server->on("/wifi-scan", HTTP_GET, [this]() { handleWifiScan(); });
  server->on("/clock-background", HTTP_GET,
             [this]() { handleClockBackgroundGet(); });
  server->on("/clock-background", HTTP_POST,
             [this]() { handleClockBackgroundUploadComplete(); },
             [this]() { handleClockBackgroundUpload(); });
  server->on("/clock-background/delete", HTTP_POST,
             [this]() { handleClockBackgroundDelete(); });
  server->on("/configuration", HTTP_GET,
             [this]() { handleConfigurationExport(); });
  server->on("/configuration/import", HTTP_POST,
             [this]() { handleConfigurationImport(); });
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

void WebInterface::handleRoot() { server->send(200, "text/html", generateHTML()); }

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

  if (server->hasArg("ap_pass") && !server->arg("ap_pass").isEmpty()) {
    const size_t length = server->arg("ap_pass").length();
    if (length < 8 || length > 63) {
      server->send(400, "application/json",
                   "{\"ok\":false,\"message\":\"Le mot de passe BTC Watch doit contenir entre 8 et 63 caracteres\"}");
      return;
    }
    settings->apPassword = server->arg("ap_pass");
  }

  if (server->hasArg("ssid")) settings->wifiSSID = server->arg("ssid");
  if (server->hasArg("pass") && !server->arg("pass").isEmpty())
    settings->wifiPASS = server->arg("pass");
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
                               oldAPPassword != settings->apPassword;
  const bool timezoneChanged = oldTimezone != settings->timezone;
  server->send(200, "application/json",
               "{\"ok\":true,\"saved\":" + String(incomingCount) + "}");
  if (reconnectNeeded) {
    delay(250);
    network->reconnect(settings->wifiSSID, settings->wifiPASS,
                       settings->timezone, settings->apPassword);
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

void WebInterface::handleClockBackgroundGet() {
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
    SPIFFS.remove(CLOCK_BACKGROUND_TEMP_PATH);
    clockBackgroundUploadBytes = 0;
    clockBackgroundUploadOK = true;
    clockBackgroundUpload = SPIFFS.open(CLOCK_BACKGROUND_TEMP_PATH, "w");
    if (!clockBackgroundUpload) clockBackgroundUploadOK = false;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!clockBackgroundUploadOK || !clockBackgroundUpload) return;
    if (clockBackgroundUploadBytes + upload.currentSize >
        CLOCK_BACKGROUND_BYTES) {
      clockBackgroundUploadOK = false;
      return;
    }
    const size_t written =
        clockBackgroundUpload.write(upload.buf, upload.currentSize);
    clockBackgroundUploadBytes += written;
    if (written != upload.currentSize) clockBackgroundUploadOK = false;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (clockBackgroundUpload) clockBackgroundUpload.close();
    clockBackgroundUploadOK =
        clockBackgroundUploadOK &&
        clockBackgroundUploadBytes == CLOCK_BACKGROUND_BYTES;
    if (clockBackgroundUploadOK) {
      SPIFFS.remove(CLOCK_BACKGROUND_PATH);
      clockBackgroundUploadOK = SPIFFS.rename(CLOCK_BACKGROUND_TEMP_PATH,
                                               CLOCK_BACKGROUND_PATH);
    }
    if (!clockBackgroundUploadOK) SPIFFS.remove(CLOCK_BACKGROUND_TEMP_PATH);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (clockBackgroundUpload) clockBackgroundUpload.close();
    clockBackgroundUploadOK = false;
    SPIFFS.remove(CLOCK_BACKGROUND_TEMP_PATH);
  }
}

void WebInterface::handleClockBackgroundUploadComplete() {
  if (!clockBackgroundUploadOK ||
      !SPIFFS.exists(CLOCK_BACKGROUND_PATH)) {
    server->send(400, "application/json",
                 "{\"ok\":false,\"message\":\"Image incomplete ou invalide\"}");
    return;
  }
  settingsChanged = true;
  server->send(200, "application/json",
               "{\"ok\":true,\"bytes\":" +
                   String(clockBackgroundUploadBytes) + "}");
}

void WebInterface::handleClockBackgroundDelete() {
  if (clockBackgroundUpload) clockBackgroundUpload.close();
  SPIFFS.remove(CLOCK_BACKGROUND_TEMP_PATH);
  const bool existed = SPIFFS.exists(CLOCK_BACKGROUND_PATH);
  const bool removed = !existed || SPIFFS.remove(CLOCK_BACKGROUND_PATH);
  if (!removed) {
    server->send(500, "application/json",
                 "{\"ok\":false,\"message\":\"Suppression impossible\"}");
    return;
  }
  settingsChanged = true;
  server->send(200, "application/json", "{\"ok\":true}");
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

  String errorMessage;
  if (!settings->restoreFromJson(server->arg("config_json"), errorMessage)) {
    JsonDocument response;
    response["ok"] = false;
    response["message"] = errorMessage;
    String json;
    serializeJson(response, json);
    server->send(400, "application/json", json);
    return;
  }

  settingsChanged = true;
  server->send(200, "application/json",
               "{\"ok\":true,\"message\":\"Sauvegarde restauree\",\"reboot\":true}");
  delay(750);
  ESP.restart();
}

String WebInterface::generateHTML() {
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
  bool hasClockBackground = false;
  if (SPIFFS.exists(CLOCK_BACKGROUND_PATH)) {
    fs::File background = SPIFFS.open(CLOCK_BACKGROUND_PATH, "r");
    hasClockBackground =
        background && background.size() == CLOCK_BACKGROUND_BYTES;
    if (background) background.close();
  }

  String page;
  page.reserve(20000);
  page += F(R"HTML(<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>BTC Watch</title><style>
:root{--bg:#000;--card:#090909;--card2:#141414;--line:#303030;--text:#f0f3f8;--muted:#969696;--btc:#f7931a;--green:#45d483;--red:#ff6577}*{box-sizing:border-box}body{margin:0;background:#000;color:var(--text);font-family:Inter,system-ui,sans-serif}.wrap{width:min(820px,100%);margin:auto;padding:24px 16px 48px}.hero{text-align:center;margin-bottom:20px}.coin{display:grid;place-items:center;width:62px;height:82px;margin:auto}.coin img{display:block;width:56px;height:auto}.hero h1{margin:12px 0 4px;font-size:26px}.hero p{margin:0;color:var(--muted)}#settings{display:flex;flex-direction:column}.wifi-card{order:10}.market-card{order:20}.clock-card{order:30}.addresses-card{order:40}.backup-card{order:50}.maintenance-card{order:60}.actions{order:70}#saveMsg{order:80}.card{background:linear-gradient(145deg,var(--card2),var(--card));border:1px solid var(--line);border-radius:18px;padding:18px;margin:14px 0;box-shadow:0 12px 34px #0005}.titlebar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}.title{display:flex;align-items:center;gap:9px;margin-bottom:14px;font-weight:800}.titlebar .title{margin-bottom:0}.dot{width:9px;height:9px;border-radius:50%;background:var(--btc);box-shadow:0 0 10px var(--btc)}.badge{background:#050505;border:1px solid var(--line);border-radius:999px;padding:5px 10px;color:var(--btc);font-size:12px}.tools{display:flex;flex-wrap:wrap;gap:9px;margin-bottom:14px}.file{position:relative;overflow:hidden}.file input{position:absolute;inset:0;opacity:0;cursor:pointer}.btn{border:0;border-radius:10px;padding:11px 14px;font-weight:800;cursor:pointer}.primary{background:var(--btc);color:#171008}.secondary{background:#242424;color:var(--text)}.danger{background:#3a1e28;color:#ff9baa}.wallets{display:grid;gap:9px}.wallet{display:grid;grid-template-columns:130px 1fr 1.8fr 38px;gap:8px;align-items:end;padding:11px;border:1px solid var(--line);border-radius:12px;background:#050505}.remove{height:40px;background:#3a1e28;color:#ff9baa;border:0;border-radius:9px;font-size:18px;cursor:pointer}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.subsection{grid-column:1/-1;margin-top:6px;padding-top:12px;border-top:1px solid var(--line)}.subsection:first-child{margin-top:0;padding-top:0;border-top:0}.subsection strong{display:block;color:var(--text);font-size:14px}.subsection span{display:block;margin-top:3px;color:var(--muted);font-size:12px}.checkline{display:flex;align-items:center;gap:8px;margin-top:7px;color:var(--text)}.checkline input{width:auto;accent-color:var(--btc)}.image-panel{grid-column:1/-1;display:grid;grid-template-columns:minmax(220px,320px) 1fr;gap:14px;align-items:center;padding:12px;border:1px solid var(--line);border-radius:12px;background:#050505}.image-preview{width:100%;aspect-ratio:4/3;border:1px solid var(--line);border-radius:9px;background:#000;image-rendering:auto}.image-actions{display:flex;flex-direction:column;gap:9px}.image-actions .btn{width:100%;text-align:center}.image-status{color:var(--muted);font-size:12px;line-height:1.45}label{display:block;color:var(--muted);font-size:12px;margin:0 0 5px}input,select{width:100%;border:1px solid var(--line);border-radius:9px;background:#050505;color:var(--text);padding:10px;outline:none}input:focus,select:focus{border-color:var(--btc)}.scan{width:100%;margin-top:7px}.info-box{display:flex;min-height:64px;flex-direction:column;justify-content:center;padding:11px 13px;border:1px solid var(--line);border-radius:10px;background:#050505}.info-box strong{font-size:13px}.info-box span{margin-top:3px;color:var(--muted);font-size:12px}.backup-box{padding:14px;border:1px solid var(--line);border-radius:12px;background:#050505}.backup-box .tools{margin:0 0 10px}.maintenance{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:14px;border:1px solid #673421;border-radius:12px;background:#251207}.maintenance p{margin:0;color:#d9b08d;font-size:12px;line-height:1.5}.key{display:inline-block;margin:0 3px;padding:2px 7px;border:1px solid #9f5730;border-radius:6px;background:#080808;color:var(--btc);font-weight:800}.actions{display:flex;gap:10px;margin-top:18px}.actions .btn{flex:1}.msg{display:none;border-radius:10px;padding:11px;margin:12px 0 0;text-align:center;font-weight:700}.ok{display:block;background:#173525;color:var(--green)}.error{display:block;background:#3a1e28;color:#ff9baa}.hint{font-size:12px;color:var(--muted);line-height:1.5}@media(max-width:700px){.wallet{grid-template-columns:1fr 1fr}.wallet .address{grid-column:1/-1}.grid{grid-template-columns:1fr}.remove{grid-column:2;grid-row:1}.maintenance{align-items:stretch;flex-direction:column}.image-panel{grid-template-columns:1fr}}
</style></head><body><main class="wrap"><header class="hero"><div class="coin"><img alt="Bitcoin" src="data:image/png;base64,)HTML");
  page += FPSTR(WEB_LOGO_BASE64);
  page += F(R"HTML("></div><h1>BTC Watch</h1><p>Import JSON et ajout manuel d'adresses publiques</p></header><form id="settings"><section class="card addresses-card"><div class="titlebar"><div class="title"><span class="dot"></span>Adresses Bitcoin</div><span class="badge"><b id="count">0</b> / )HTML");
  page += String(MAX_WALLETS);
  page += F(R"HTML(</span></div><div class="tools"><label class="btn primary file">Importer un fichier d'adresses<input id="file" type="file" accept=".json,application/json"></label><button class="btn secondary" id="exportAddresses" type="button">Exporter toutes les adresses</button><button class="btn secondary" id="add" type="button">+ Ajouter une adresse</button><button class="btn danger" id="clear" type="button">Vider la liste</button></div><div id="importMsg" class="msg"></div><div id="wallets" class="wallets"></div><p class="hint">L'import conserve les adresses déjà présentes, ignore les doublons et ajoute le nom de leur collection. L'export crée un fichier JSON compatible avec le format bitwatch-collections. Seules les adresses publiques sont acceptées. Ne saisissez jamais une clé privée ou une phrase de récupération.</p></section><section class="card wifi-card"><div class="title"><span class="dot"></span>Wi-Fi et accès</div><div class="grid"><div><label>Réseau Wi-Fi</label><input id="ssid" name="ssid" value=)HTML");
  page += "'" + htmlEscape(settings->wifiSSID) + "'";
  page += F(R"HTML(><select id="wifiList" class="scan" style="display:none"><option value="">Choisir un réseau détecté</option></select><button id="scan" class="btn secondary scan" type="button">Rechercher les réseaux Wi-Fi</button></div><div><label>Mot de passe du Wi-Fi sélectionné</label><input type="password" name="pass" placeholder="Laisser vide pour conserver"></div><div><label>Nouveau mot de passe du réseau BTC Watch</label><input type="password" name="ap_pass" minlength="8" maxlength="63" placeholder="Laisser vide pour conserver"></div><div class="info-box"><strong>Réseau de secours BTC Watch</strong><span>Accès direct : 192.168.22.1</span><span>Depuis le même réseau : http://btc-watch.local</span></div></div></section><section class="card market-card"><div class="title"><span class="dot"></span>Cours et données Bitcoin</div><div class="grid"><div><label>Source du cours</label><select name="price_api"><option value="COINGECKO")HTML");
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
  page += F(R"HTML('></div><div class='subsection'><strong>Image d'arrière-plan</strong><span>Le navigateur recadre et convertit automatiquement l'image en RGB565 320 × 240.</span></div><div class='image-panel'><canvas id='clockBgPreview' class='image-preview' width='320' height='240'></canvas><div class='image-actions'><label class='btn primary file'>Choisir ou remplacer l'image<input id='clockBgFile' type='file' accept='image/png,image/jpeg,image/webp,image/gif'></label><button class='btn danger' id='clockBgDelete' type='button')HTML");
  if (!hasClockBackground) page += F(" disabled");
  page += F(R"HTML(>Supprimer l'image</button><div id='clockBgStatus' class='image-status'>)HTML");
  page += hasClockBackground
              ? F("Une image personnalisée est enregistrée sur BTC Watch.")
              : F("Aucune image enregistrée. La couleur de fond est utilisée.");
  page += F(R"HTML(</div><div id='clockBgMsg' class='msg'></div></div></div><label class='checkline'><input type='checkbox' name='clock_outline_visible' value='1')HTML");
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
  page += F(R"HTML(>Afficher la ligne décorative</label></div></div></section><section class="card backup-card"><div class="title"><span class="dot"></span>Sauvegarde personnalisée complète</div><div class="backup-box"><div class="tools"><button class="btn primary" id="backupExport" type="button">Exporter toute la configuration</button><label class="btn secondary file">Importer une sauvegarde complète<input id="backupFile" type="file" accept=".json,application/json"></label></div><div id="backupMsg" class="msg"></div><p class="hint">Un seul fichier JSON conserve les adresses, le Wi-Fi, les mots de passe, les textes, les couleurs, la devise, le fuseau horaire, la luminosité, les options du cadran et l'image de fond. Enregistrez vos modifications avant l'export et gardez ce fichier dans un endroit privé.</p></div></section><section class="card maintenance-card"><div class="title"><span class="dot"></span>Maintenance de l'appareil</div><div class="maintenance"><p><strong>Remise à zéro complète :</strong><br>Appareil allumé, maintenez le bouton arrière <span class="key">BOOT</span> pendant 5 secondes. Le Wi-Fi, les adresses, les couleurs, l'image de fond et le mot de passe BTC Watch seront effacés.</p><button class="btn secondary" id="reboot" type="button">Redémarrer</button></div></section><div class="actions"><button class="btn primary" type="submit">Enregistrer et actualiser</button></div><div id="saveMsg" class="msg"></div></form></main><script>
const APP_VERSION=")HTML");
  page += APP_VERSION;
  page += F(R"HTML(",MAX=)HTML");
  page += String(MAX_WALLETS);
  page += F(", wallets=");
  page += initialWallets;
  page += F(";let hasClockBackground=");
  page += hasClockBackground ? F("true") : F("false");
  page += F(R"HTML(;const box=document.querySelector('#wallets'),count=document.querySelector('#count'),im=document.querySelector('#importMsg'),sm=document.querySelector('#saveMsg');
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const key=a=>/^bc1/i.test(a)?a.toLowerCase():a;const valid=a=>/^(bc1|BC1|1|3)[A-Za-z0-9]{13,89}$/.test(a);
function message(el,text,ok=true){el.className='msg '+(ok?'ok':'error');el.textContent=text;el.style.display='block'}
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
const bgCanvas=document.querySelector('#clockBgPreview'),bgCtx=bgCanvas.getContext('2d',{willReadFrequently:true}),bgFile=document.querySelector('#clockBgFile'),bgDelete=document.querySelector('#clockBgDelete'),bgStatus=document.querySelector('#clockBgStatus'),bgMsg=document.querySelector('#clockBgMsg');
function emptyClockBackground(){bgCtx.fillStyle='#000';bgCtx.fillRect(0,0,320,240);bgCtx.fillStyle='#777';bgCtx.font='14px system-ui';bgCtx.textAlign='center';bgCtx.fillText('AUCUNE IMAGE',160,120)}
function showRGB565(bytes){if(bytes.length!==153600)throw Error('Format RGB565 incorrect');const image=bgCtx.createImageData(320,240),p=image.data;for(let i=0,j=0;i<bytes.length;i+=2,j+=4){const v=bytes[i]|(bytes[i+1]<<8);p[j]=Math.round(((v>>11)&31)*255/31);p[j+1]=Math.round(((v>>5)&63)*255/63);p[j+2]=Math.round((v&31)*255/31);p[j+3]=255}bgCtx.putImageData(image,0,0)}
async function loadClockBackground(){if(!hasClockBackground){emptyClockBackground();return}try{const r=await fetch('/clock-background?ts='+Date.now(),{cache:'no-store'});if(!r.ok)throw Error('Fond indisponible');showRGB565(new Uint8Array(await r.arrayBuffer()))}catch(e){emptyClockBackground();message(bgMsg,e.message,false)}}
function canvasToRGB565(){const rgba=bgCtx.getImageData(0,0,320,240).data,out=new Uint8Array(153600);for(let i=0,j=0;i<rgba.length;i+=4,j+=2){const v=((rgba[i]>>3)<<11)|((rgba[i+1]>>2)<<5)|(rgba[i+2]>>3);out[j]=v&255;out[j+1]=v>>8}return out}
bgFile.onchange=async e=>{const file=e.target.files[0];if(!file)return;bgMsg.style.display='none';try{const image=await createImageBitmap(file);const scale=Math.max(320/image.width,240/image.height),w=image.width*scale,h=image.height*scale;bgCtx.fillStyle='#000';bgCtx.fillRect(0,0,320,240);bgCtx.drawImage(image,(320-w)/2,(240-h)/2,w,h);image.close?.();const converted=canvasToRGB565(),body=new FormData();body.append('background',new Blob([converted],{type:'application/octet-stream'}),'clock_bg.rgb565');bgFile.disabled=true;bgDelete.disabled=true;bgStatus.textContent='Conversion et envoi en cours...';const r=await fetch('/clock-background',{method:'POST',body});const j=await r.json().catch(()=>({message:'Réponse illisible'}));if(!r.ok)throw Error(j.message||'Envoi impossible');hasClockBackground=true;bgDelete.disabled=false;bgStatus.textContent='Image convertie en RGB565 320 × 240 et enregistrée.';message(bgMsg,'Nouveau fond appliqué sur le cadran Heure.')}catch(err){message(bgMsg,'Image impossible : '+err.message,false);await loadClockBackground()}finally{bgFile.disabled=false;e.target.value=''}};
bgDelete.onclick=async()=>{if(!confirm("Supprimer l'image de fond personnalisée ?"))return;bgDelete.disabled=true;try{const r=await fetch('/clock-background/delete',{method:'POST'});const j=await r.json().catch(()=>({message:'Réponse illisible'}));if(!r.ok)throw Error(j.message||'Suppression impossible');hasClockBackground=false;emptyClockBackground();bgStatus.textContent='Aucune image enregistrée. La couleur de fond est utilisée.';message(bgMsg,'Image de fond supprimée.')}catch(err){message(bgMsg,err.message,false);bgDelete.disabled=false}};loadClockBackground();
const f=document.querySelector('#settings'),b=document.querySelector('#brightness'),bv=document.querySelector('#bv'),backupExport=document.querySelector('#backupExport'),backupFile=document.querySelector('#backupFile'),backupMsg=document.querySelector('#backupMsg');
function mergeVisibleConfiguration(config){const form=new FormData(f);config.ssid=String(form.get('ssid')||'');if(form.get('pass'))config.pass=String(form.get('pass'));if(form.get('ap_pass'))config.ap_password=String(form.get('ap_pass'));config.tz=String(form.get('tz')||config.tz);config.price_api=String(form.get('price_api')||config.price_api);config.clock_color=String(form.get('clock_color')||config.clock_color);config.clock_background=String(form.get('clock_background')||config.clock_background);config.clock_title=String(form.get('clock_title')??config.clock_title);config.clock_subtitle=String(form.get('clock_subtitle')??config.clock_subtitle);config.clock_title_color=String(form.get('clock_title_color')||config.clock_title_color);config.clock_subtitle_color=String(form.get('clock_subtitle_color')||config.clock_subtitle_color);config.clock_bar_color=String(form.get('clock_bar_color')||config.clock_bar_color);config.clock_button_color=String(form.get('clock_button_color')||config.clock_button_color);config.clock_bar_same_as_time=form.has('clock_bar_same');config.clock_button_same_as_time=form.has('clock_button_same');config.clock_bar_visible=form.has('clock_bar_visible');config.clock_outline_visible=form.has('clock_outline_visible');config.brightness=Number(form.get('brightness')||config.brightness);config.wallets=wallets.filter(w=>String(w.address||'').trim()).map(w=>({collection:String(w.collection||'Manuel').trim().slice(0,18)||'Manuel',label:String(w.label||'Adresse').trim().slice(0,18)||'Adresse',address:String(w.address).trim()}));return config}
backupExport.onclick=async()=>{backupExport.disabled=true;backupFile.disabled=true;backupMsg.style.display='none';try{const configResponse=await fetch('/configuration?ts='+Date.now(),{cache:'no-store'});if(!configResponse.ok)throw Error('Configuration indisponible');const configuration=mergeVisibleConfiguration(await configResponse.json());let clock_background={present:false};if(hasClockBackground){const backgroundResponse=await fetch('/clock-background?ts='+Date.now(),{cache:'no-store'});if(!backgroundResponse.ok)throw Error("L'image de fond est indisponible");const bytes=new Uint8Array(await backgroundResponse.arrayBuffer());if(bytes.length!==153600)throw Error("L'image de fond est invalide");clock_background={present:true,encoding:'base64',format:'rgb565-le',width:320,height:240,data:bytesToBase64(bytes)}}const backup={format:'btc-watch-complete-backup',schema_version:1,app_version:APP_VERSION,exported_at:new Date().toISOString(),configuration,clock_background};const date=new Date().toISOString().slice(0,10);downloadJSON('btc-watch-sauvegarde-'+date+'.json',backup);message(backupMsg,'Sauvegarde complète téléchargée. Conservez-la dans un endroit privé.')}catch(err){message(backupMsg,'Sauvegarde impossible : '+err.message,false)}finally{backupExport.disabled=false;backupFile.disabled=false}};
backupFile.onchange=async e=>{const file=e.target.files[0];if(!file)return;backupExport.disabled=true;backupFile.disabled=true;backupMsg.style.display='none';try{const backup=JSON.parse(await file.text()),required=['ssid','pass','ap_password','tz','currency','price_api','clock_color','clock_background','clock_title','clock_subtitle','clock_title_color','clock_subtitle_color','clock_bar_color','clock_button_color','clock_bar_same_as_time','clock_button_same_as_time','clock_bar_visible','clock_outline_visible','brightness','wallets'];if(backup.format!=='btc-watch-complete-backup'||backup.schema_version!==1||!backup.configuration||!backup.clock_background)throw Error('Ce fichier n’est pas une sauvegarde BTC Watch compatible');const missing=required.filter(key=>!Object.prototype.hasOwnProperty.call(backup.configuration,key));if(missing.length)throw Error('Réglages manquants : '+missing.join(', '));if(!confirm('Restaurer toute cette configuration ? Les réglages, adresses, mots de passe et image actuels seront remplacés.'))return;if(backup.clock_background.present){if(backup.clock_background.encoding!=='base64'||backup.clock_background.format!=='rgb565-le'||backup.clock_background.width!==320||backup.clock_background.height!==240)throw Error("Format d'image non compatible");const bytes=base64ToBytes(String(backup.clock_background.data||''));if(bytes.length!==153600)throw Error("Données de l'image incomplètes");const imageBody=new FormData();imageBody.append('background',new Blob([bytes],{type:'application/octet-stream'}),'clock_bg.rgb565');const imageResponse=await fetch('/clock-background',{method:'POST',body:imageBody});if(!imageResponse.ok){const problem=await imageResponse.json().catch(()=>({}));throw Error(problem.message||"Restauration de l'image impossible")}hasClockBackground=true}else{const deleteResponse=await fetch('/clock-background/delete',{method:'POST'});if(!deleteResponse.ok)throw Error("Suppression de l'ancien fond impossible");hasClockBackground=false}const body=new FormData();body.set('config_json',JSON.stringify(backup.configuration));const response=await fetch('/configuration/import',{method:'POST',body});const result=await response.json().catch(()=>({message:'Réponse illisible'}));if(!response.ok)throw Error(result.message||'Restauration impossible');message(backupMsg,'Configuration complète restaurée. BTC Watch redémarre pour tout réappliquer.');setTimeout(()=>location.reload(),7000)}catch(err){message(backupMsg,'Import impossible : '+err.message,false)}finally{backupExport.disabled=false;backupFile.disabled=false;e.target.value=''}};
b.oninput=()=>bv.textContent=b.value;f.onsubmit=async e=>{e.preventDefault();sm.style.display='none';const cleaned=[];const seen=new Set();for(const w of wallets){w.collection=String(w.collection||'Manuel').trim().slice(0,18)||'Manuel';w.label=String(w.label||'Adresse').trim().slice(0,18)||'Adresse';w.address=String(w.address||'').trim();if(!w.address)continue;if(!valid(w.address))return message(sm,'Adresse invalide : '+w.address,false);const k=key(w.address);if(seen.has(k))continue;seen.add(k);cleaned.push(w)}wallets.splice(0,wallets.length,...cleaned);const body=new FormData(f);body.set('wallets_json',JSON.stringify(wallets));const r=await fetch('/save',{method:'POST',body});const j=await r.json().catch(()=>({message:'Reponse illisible'}));if(r.ok){render();message(sm,j.saved+' adresse(s) enregistree(s).')}else message(sm,j.message||'Enregistrement impossible.',false)};document.querySelector('#reboot').onclick=()=>fetch('/reboot',{method:'POST'});render();
</script></body></html>)HTML");
  return page;
}
