/**************************************************************
 *  Gestionnaire Réseau - Clock-21M
 *
 *  Implémentation de la gestion WiFi, mDNS et NTP.
 **************************************************************/

#include "network_manager.h"
#include "config.h"

NetworkManager::NetworkManager() {
  timeClient = new NTPClient(ntpUDP, "pool.ntp.org", 0, 60000);
  connected = false;
  ntp_initialized = false;
  current_timezone = "UTC+0";
  timezone_pending = false;
}

NetworkManager::~NetworkManager() { delete timeClient; }

void NetworkManager::begin(const String &ssid, const String &pass,
                           const String &timezone, const String &apPassword) {
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
    WiFi.begin(ssid.c_str(), pass.c_str());
    current_timezone = timezone;  // Sauvegarder pour init NTP ultérieure
    timeClient->setTimeOffset(parseTimezone(current_timezone));
    timezone_pending = true;
  }
}

bool NetworkManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void NetworkManager::updateTime() {
  if (connected) {
    timeClient->update();
  }
}

String NetworkManager::getFormattedTime() {
  if (connected) {
    return timeClient->getFormattedTime().substring(0, 5);
  }
  return "--:--";
}

void NetworkManager::reconnect(const String &ssid, const String &pass,
                               const String &timezone,
                               const String &apPassword) {
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
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Mise à jour du fuseau horaire
    // Conserver le fuseau pour une future ré-initialisation NTP après perte de connexion
    current_timezone = timezone;
    timeClient->setTimeOffset(parseTimezone(current_timezone));
    timezone_pending = true;

    // Application immédiate si déjà connecté (ou si la connexion est encore active)
    if (WiFi.status() == WL_CONNECTED) {
      if (!ntp_initialized) {
        timeClient->begin();
        ntp_initialized = true;
      }
      timeClient->forceUpdate();
    }
  }
}

void NetworkManager::applyTimezone(const String &timezone) {
  current_timezone = timezone;
  timeClient->setTimeOffset(parseTimezone(current_timezone));
  timezone_pending = true;
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntp_initialized) {
      timeClient->begin();
      ntp_initialized = true;
    }
    connected = true;
    timeClient->forceUpdate();
  }
}

void NetworkManager::update() {
  // Détecte la transition disconnected -> connected et init NTP/mDNS
  bool sta_now_connected = (WiFi.status() == WL_CONNECTED);

  if (sta_now_connected && !connected) {
    // Première connexion STA détectée
    connected = true;
    // Configuration NTP
    int tzOffset = parseTimezone(current_timezone);
    if (!ntp_initialized) {
      timeClient->begin();
      ntp_initialized = true;
    }
    timeClient->setTimeOffset(tzOffset);
    timeClient->forceUpdate();
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
