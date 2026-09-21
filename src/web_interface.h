#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include "network_manager.h"
#include "settings.h"
#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

class WebInterface {
public:
  WebInterface(Settings *settings, NetworkManager *network);
  ~WebInterface();
  void begin();
  void handleClient();
  bool consumeSettingsChanged();

private:
  WebServer *server;
  Settings *settings;
  NetworkManager *network;
  bool settingsChanged;
  String sessionToken;
  fs::File clockBackgroundUpload;
  size_t clockBackgroundUploadBytes;
  bool clockBackgroundUploadOK;

  String generateHTML();
  String generateLoginHTML(bool invalidPassword = false);
  String htmlEscape(const String &value);
  bool isAuthenticated();
  void refreshSessionCookie();
  bool requireAuthentication();
  void handleRoot();
  void handleLogin();
  void handleLogout();
  void handleSave();
  void handleReboot();
  void handleWifiScan();
  void handleClockBackgroundGet();
  void handleClockBackgroundUpload();
  void handleClockBackgroundUploadComplete();
  void handleClockBackgroundDelete();
  void handleConfigurationExport();
  void handleConfigurationImport();
};

#endif
