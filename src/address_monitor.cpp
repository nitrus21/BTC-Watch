#include "address_monitor.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

AddressMonitor::AddressMonitor() {}

bool AddressMonitor::looksLikeBitcoinAddress(const String &address) const {
  const size_t length = address.length();
  if (length < 14 || length > 90) return false;
  if (!(address.startsWith("1") || address.startsWith("3") ||
        address.startsWith("bc1") || address.startsWith("BC1"))) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = address.charAt(i);
    if (!isAlphaNumeric(c)) return false;
  }
  return true;
}

void AddressMonitor::sync(const Settings &settings) {
  for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
    const String &address = settings.wallets[i].address;
    if (knownAddresses[i] != address) {
      knownAddresses[i] = address;
      balances[i] = WalletBalance();
    }
    balances[i].configured = !address.isEmpty();
  }
}

bool AddressMonitor::refreshOne(const Settings &settings, uint8_t index) {
  if (index >= MAX_WALLETS) return false;
  sync(settings);
  const String &address = settings.wallets[index].address;
  WalletBalance result;
  const bool success = fetchAddress(address, result);
  applyResult(index, address, result);
  return success;
}

bool AddressMonitor::fetchAddress(const String &address,
                                  WalletBalance &result) const {
  result = WalletBalance();
  result.configured = !address.isEmpty();
  if (address.isEmpty()) return false;
  if (!looksLikeBitcoinAddress(address)) {
    result.error = "Adresse invalide";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "Wi-Fi indisponible";
    return false;
  }

  result.loading = true;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.begin("https://mempool.space/api/address/" + address);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    result.loading = false;
    result.error = "Erreur API " + String(status);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, http.getStream());
  http.end();
  if (jsonError) {
    result.loading = false;
    result.error = "Reponse illisible";
    return false;
  }

  const int64_t chainFunded = doc["chain_stats"]["funded_txo_sum"] | 0LL;
  const int64_t chainSpent = doc["chain_stats"]["spent_txo_sum"] | 0LL;
  const int64_t mempoolFunded = doc["mempool_stats"]["funded_txo_sum"] | 0LL;
  const int64_t mempoolSpent = doc["mempool_stats"]["spent_txo_sum"] | 0LL;

  result.confirmedSats = chainFunded - chainSpent;
  result.pendingSats = mempoolFunded - mempoolSpent;
  result.txCount = (doc["chain_stats"]["tx_count"] | 0UL) +
                   (doc["mempool_stats"]["tx_count"] | 0UL);
  result.valid = true;
  result.loading = false;
  result.updatedAt = millis();
  return true;
}

void AddressMonitor::applyResult(uint8_t index, const String &address,
                                 const WalletBalance &result) {
  if (index >= MAX_WALLETS || knownAddresses[index] != address) return;
  balances[index] = result;
}

void AddressMonitor::refreshAll(const Settings &settings) {
  sync(settings);
  for (uint8_t i = 0; i < MAX_WALLETS; ++i) {
    if (settings.wallets[i].address.isEmpty()) continue;
    refreshOne(settings, i);
    delay(40);
  }
}

const WalletBalance &AddressMonitor::get(uint8_t index) const {
  static WalletBalance empty;
  return index < MAX_WALLETS ? balances[index] : empty;
}

int64_t AddressMonitor::totalSats() const {
  int64_t total = 0;
  for (uint8_t i = 0; i < MAX_WALLETS; ++i)
    if (balances[i].valid) total += balances[i].confirmedSats;
  return total;
}

int64_t AddressMonitor::totalPendingSats() const {
  int64_t total = 0;
  for (uint8_t i = 0; i < MAX_WALLETS; ++i)
    if (balances[i].valid) total += balances[i].pendingSats;
  return total;
}

uint32_t AddressMonitor::totalTransactions() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < MAX_WALLETS; ++i)
    if (balances[i].valid) total += balances[i].txCount;
  return total;
}

bool AddressMonitor::hasData() const {
  for (uint8_t i = 0; i < MAX_WALLETS; ++i)
    if (balances[i].valid) return true;
  return false;
}
