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
  fs::File clockBackgroundUpload;
  size_t clockBackgroundUploadBytes;
  bool clockBackgroundUploadOK;

  String generateHTML();
  String htmlEscape(const String &value);
  void handleRoot();
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
