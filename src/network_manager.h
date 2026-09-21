/**************************************************************
 *  Gestionnaire Réseau - Clock-21M
 *
 *  Classe pour gérer WiFi (AP+STA), mDNS et NTP.
 **************************************************************/

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>


class NetworkManager {
private:
  bool connected;
  String current_timezone;
  bool timezone_pending;
  unsigned long time_sync_started_at;
  unsigned long last_http_time_attempt;
  int parseTimezone(const String &tz);
  void configureTime();
  bool syncTimeFromHttp();
  bool configureStation(bool staticIPEnabled, const String &staticIP,
                        const String &gateway, const String &subnet,
                        const String &dns);

public:
  NetworkManager();
  ~NetworkManager();

  // Initialisation du réseau (AP + STA)
  void begin(const String &ssid, const String &pass, const String &timezone,
             const String &apPassword, bool staticIPEnabled,
             const String &staticIP, const String &gateway,
             const String &subnet, const String &dns);

  // Vérification de la connexion
  bool isConnected();

  // Mise à jour de l'heure
  bool updateTime();

  // Secours HTTPS bloquant, a appeler uniquement depuis la tache reseau.
  bool recoverTimeFromInternet();

  // Récupération de l'heure formatée (HH:MM)
  String getFormattedTime();

  // Reconnexion WiFi
  void reconnect(const String &ssid, const String &pass,
                 const String &timezone, const String &apPassword,
                 bool staticIPEnabled, const String &staticIP,
                 const String &gateway, const String &subnet,
                 const String &dns);

  // Applique un nouveau fuseau sans couper la connexion Wi-Fi.
  void applyTimezone(const String &timezone);

  // Mise à jour non bloquante (détecte connexion STA et init NTP/mDNS)
  void update();

  // Indique si un fuseau horaire vient d'être appliqué
  bool consumeTimezonePending();
};

#endif // NETWORK_MANAGER_H
