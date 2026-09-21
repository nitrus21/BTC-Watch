/**************************************************************
 *  Gestionnaire Réseau - Clock-21M
 *
 *  Implémentation de la gestion WiFi, mDNS et NTP.
 **************************************************************/

#include "network_manager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <sys/time.h>

NetworkManager::NetworkManager() {
  connected = false;
  current_timezone = "UTC+0";
  timezone_pending = false;
  time_sync_started_at = 0;
  last_http_time_attempt = 0;
}

NetworkManager::~NetworkManager() {}

void NetworkManager::begin(const String &ssid, const String &pass,
                           const String &timezone, const String &apPassword,
                           bool staticIPEnabled, const String &staticIP,
                           const String &gateway, const String &subnet,
                           const String &dns) {
  // Configuration mDNS
  MDNS.begin(MDNS_NAME);

  // Mode AP+STA simultanés
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Configuration de l'Access Point
  IPAddress apIP(AP_IP_ADDR);
  IPAddress apGW(AP_GATEWAY);
  IPAddress apMASK(AP_SUBNET);

  WiFi.softAPConfig(apIP, apGW, apMASK);
  WiFi.softAP(AP_SSID, apPassword.c_str(), 6, 0, 4);

  // Connexion au WiFi si SSID fourni
  if (!ssid.isEmpty()) {
    WiFi.setHostname(HOST_NAME);
    configureStation(staticIPEnabled, staticIP, gateway, subnet, dns);
    WiFi.begin(ssid.c_str(), pass.c_str());
    current_timezone = timezone;  // Sauvegarder pour init NTP ultérieure
    timezone_pending = true;
  }
}

bool NetworkManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool NetworkManager::updateTime() {
  if (!connected) return false;
  struct tm timeInfo;
  return getLocalTime(&timeInfo, 10);
}

bool NetworkManager::recoverTimeFromInternet() {
  if (!connected || updateTime()) return connected;

  // Certains routeurs ou fournisseurs bloquent le NTP (UDP/123). Laisser
  // d'abord quelques secondes aux serveurs NTP, puis utiliser ponctuellement
  // une API HTTPS qui renvoie l'heure Unix selon la connexion Internet.
  const unsigned long now = millis();
  if (now - time_sync_started_at < 8000UL ||
      (last_http_time_attempt != 0 &&
       now - last_http_time_attempt < 60000UL))
    return false;
  last_http_time_attempt = now;
  return syncTimeFromHttp();
}

String NetworkManager::getFormattedTime() {
  if (!connected) return "--:--";
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10)) return "--:--";
  char formatted[6];
  strftime(formatted, sizeof(formatted), "%H:%M", &timeInfo);
  return String(formatted);
}

void NetworkManager::reconnect(const String &ssid, const String &pass,
                               const String &timezone,
                               const String &apPassword, bool staticIPEnabled,
                               const String &staticIP, const String &gateway,
                               const String &subnet, const String &dns) {
  // Maintenir l'AP et relancer le STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  IPAddress apIP(AP_IP_ADDR);
  IPAddress apGW(AP_GATEWAY);
  IPAddress apMASK(AP_SUBNET);

  WiFi.softAPdisconnect(false);
  delay(100);
  WiFi.softAPConfig(apIP, apGW, apMASK);
  WiFi.softAP(AP_SSID, apPassword.c_str(), 6, 0, 4);

  if (!ssid.isEmpty()) {
    WiFi.setHostname(HOST_NAME);
    WiFi.disconnect(false, false);
    configureStation(staticIPEnabled, staticIP, gateway, subnet, dns);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Mise à jour du fuseau horaire
    // Conserver le fuseau pour une future ré-initialisation NTP après perte de connexion
    current_timezone = timezone;
    timezone_pending = true;
    connected = false;
  }
}

void NetworkManager::applyTimezone(const String &timezone) {
  current_timezone = timezone;
  timezone_pending = true;
  if (WiFi.status() == WL_CONNECTED) {
    connected = true;
    configureTime();
  }
}

bool NetworkManager::configureStation(bool staticIPEnabled,
                                      const String &staticIP,
                                      const String &gateway,
                                      const String &subnet,
                                      const String &dns) {
  if (!staticIPEnabled) {
    const IPAddress automatic(0, 0, 0, 0);
    return WiFi.config(automatic, automatic, automatic);
  }

  IPAddress localIP;
  IPAddress gatewayIP;
  IPAddress subnetMask;
  IPAddress dnsIP;
  if (!localIP.fromString(staticIP) || !gatewayIP.fromString(gateway) ||
      !subnetMask.fromString(subnet) || !dnsIP.fromString(dns)) {
    Serial.println("Configuration IP fixe invalide, retour au DHCP");
    const IPAddress automatic(0, 0, 0, 0);
    WiFi.config(automatic, automatic, automatic);
    return false;
  }

  // Le DNS secondaire permet aux serveurs horaires et aux API de rester
  // joignables si le routeur local ne relaie pas correctement les requetes DNS.
  const IPAddress fallbackDNS(1, 1, 1, 1);
  const bool configured =
      WiFi.config(localIP, gatewayIP, subnetMask, dnsIP, fallbackDNS);
  if (configured)
    Serial.println("Adresse locale fixe configuree : " + localIP.toString());
  else
    Serial.println("Impossible d'appliquer l'adresse IP fixe");
  return configured;
}

void NetworkManager::update() {
  // Détecte la transition disconnected -> connected et init NTP/mDNS
  bool sta_now_connected = (WiFi.status() == WL_CONNECTED);

  if (sta_now_connected && !connected) {
    // Première connexion STA détectée
    connected = true;
    // Le service SNTP natif travaille en arriere-plan et ne bloque jamais
    // la boucle principale ni la lecture du tactile.
    configureTime();
    timezone_pending = true;

    // Démarrage mDNS
    if (MDNS.begin(MDNS_NAME)) {
      Serial.println("mDNS démarré : http://" + String(MDNS_NAME) + ".local");
    }
  } else if (!sta_now_connected) {
    // Connexion perdue
    connected = false;
  }
}

void NetworkManager::configureTime() {
  // Trois fournisseurs independants : la pile SNTP choisit le premier
  // disponible et poursuit ensuite la mise a jour en arriere-plan.
  configTime(parseTimezone(current_timezone), 0, "time.cloudflare.com",
             "time.google.com", "pool.ntp.org");
  time_sync_started_at = millis();
  last_http_time_attempt = 0;
  Serial.println("Synchronisation horaire NTP demandee");
}

bool NetworkManager::syncTimeFromHttp() {
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  if (!http.begin("https://gateway.timeapi.world/ip")) return false;

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.println("Secours horaire HTTPS indisponible : " + String(status));
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  const int64_t unixTime = doc["unixtime"] | 0LL;
  http.end();
  if (error || unixTime < 1609459200LL) {
    Serial.println("Reponse horaire HTTPS invalide");
    return false;
  }

  timeval value = {static_cast<time_t>(unixTime), 0};
  settimeofday(&value, nullptr);
  Serial.println("Heure synchronisee par le secours HTTPS");
  return true;
}

// ==================== PARSE TIMEZONE ====================
// Format attendu: "UTC+X" ou "UTC-YY". Gère les signes '+' qui
// provoquent toInt()=0 avec la méthode précédente.
int NetworkManager::parseTimezone(const String &tz) {
  if (!tz.startsWith("UTC") || tz.length() < 5) return 0;
  char sign = tz.charAt(3);
  String hoursStr = tz.substring(4); // après le signe
  int hours = hoursStr.toInt();
  if (hours < 0) hours = -hours; // sécurité si toInt traite '-'
  if (sign == '-') hours = -hours;
  return hours * 3600;
}

bool NetworkManager::consumeTimezonePending() {
  if (!timezone_pending)
    return false;
  timezone_pending = false;
  return true;
}
