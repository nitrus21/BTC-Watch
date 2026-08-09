#ifndef ADDRESS_MONITOR_H
#define ADDRESS_MONITOR_H

#include "config.h"
#include "settings.h"
#include <Arduino.h>

struct WalletBalance {
  bool configured = false;
  bool valid = false;
  bool loading = false;
  String error;
  int64_t confirmedSats = 0;
  int64_t pendingSats = 0;
  uint32_t txCount = 0;
  unsigned long updatedAt = 0;
};

class AddressMonitor {
public:
  AddressMonitor();
  void sync(const Settings &settings);
  bool refreshOne(const Settings &settings, uint8_t index);
  bool fetchAddress(const String &address, WalletBalance &result) const;
  void applyResult(uint8_t index, const String &address,
                   const WalletBalance &result);
  void refreshAll(const Settings &settings);
  const WalletBalance &get(uint8_t index) const;
  int64_t totalSats() const;
  int64_t totalPendingSats() const;
  uint32_t totalTransactions() const;
  bool hasData() const;

private:
  WalletBalance balances[MAX_WALLETS];
  String knownAddresses[MAX_WALLETS];
  bool looksLikeBitcoinAddress(const String &address) const;
};

#endif
