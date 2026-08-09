/**************************************************************
 *  Gestionnaire Réseau - Clock-21M
 *
 *  Classe pour gérer WiFi (AP+STA), mDNS et NTP.
 **************************************************************/

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <ESPmDNS.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>


class NetworkManager {
private:
  WiFiUDP ntpUDP;
  NTPClient *timeClient;
  bool connected;
  bool ntp_initialized;
  String current_timezone;
  bool timezone_pending;
  int parseTimezone(const String &tz);

public:
  NetworkManager();
  ~NetworkManager();

  // Initialisation du réseau (AP + STA)
  void begin(const String &ssid, const String &pass, const String &timezone,
             const String &apPassword);

  // Vérification de la connexion
  bool isConnected();

  // Mise à jour de l'heure
  void updateTime();

  // Récupération de l'heure formatée (HH:MM)
  String getFormattedTime();

  // Reconnexion WiFi
  void reconnect(const String &ssid, const String &pass,
                 const String &timezone, const String &apPassword);

  // Applique un nouveau fuseau sans couper la connexion Wi-Fi.
  void applyTimezone(const String &timezone);

  // Mise à jour non bloquante (détecte connexion STA et init NTP/mDNS)
  void update();

  // Indique si un fuseau horaire vient d'être appliqué
  bool consumeTimezonePending();
};

#endif // NETWORK_MANAGER_H
