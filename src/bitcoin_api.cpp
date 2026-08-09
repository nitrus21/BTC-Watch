/**************************************************************
 *  API Bitcoin - Clock-21M
 *
 *  Implémentation de la récupération du prix Bitcoin depuis
 *  Binance, CoinGecko et Bitstamp.
 **************************************************************/

#include "bitcoin_api.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>


BitcoinAPI::BitcoinAPI() {
  lastPrice = 0.0;
  priceChange = 0.0;
}

String BitcoinAPI::stripSeparators(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == ',' || c == ' ')
      continue;
    out += c;
  }
  return out;
}

void BitcoinAPI::updatePrice(const String &api, const String &currency) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  int httpCode = -1;

  // ==================== BINANCE ====================
  if (api == "BINANCE") {
    String pair = (currency == "EUR") ? "BTCEUR" : "BTCUSDT";
    http.begin("https://api.binance.com/api/v3/ticker/24hr?symbol=" + pair);
    httpCode = http.GET();

    if (httpCode == 200) {
      JsonDocument doc;
      DeserializationError e = deserializeJson(doc, http.getString());
      if (!e) {
        String lastStr = doc["lastPrice"].as<String>();
        lastStr = stripSeparators(lastStr);
        lastPrice = lastStr.toFloat();

        String chgStr = doc["priceChangePercent"].as<String>();
        chgStr = stripSeparators(chgStr);
        priceChange = chgStr.toFloat();
      }
    }
    http.end();
    return;
  }

  // ==================== COINGECKO ====================
  if (api == "COINGECKO") {
    http.begin(
        "https://api.coingecko.com/api/v3/simple/"
        "price?ids=bitcoin&vs_currencies=eur,usd&include_24hr_change=true");
    httpCode = http.GET();

    if (httpCode == 200) {
      JsonDocument doc;
      DeserializationError e = deserializeJson(doc, http.getString());
      if (!e && doc["bitcoin"].is<JsonObject>()) {
        JsonObject btc = doc["bitcoin"];
        if (currency == "EUR") {
          lastPrice = btc["eur"] | lastPrice;
          priceChange = btc["eur_24h_change"] | priceChange;
        } else {
          lastPrice = btc["usd"] | lastPrice;
          priceChange = btc["usd_24h_change"] | priceChange;
        }
      }
    }
    http.end();
    return;
  }

  // ==================== BITSTAMP ====================
  if (api == "BITSTAMP") {
    String ep = (currency == "EUR") ? "btceur" : "btcusd";
    http.begin("https://www.bitstamp.net/api/v2/ticker/" + ep + "/");
    httpCode = http.GET();

    if (httpCode == 200) {
      JsonDocument doc;
      DeserializationError e = deserializeJson(doc, http.getString());
      if (!e) {
        String lastStr = String((const char *)doc["last"]);
        String openStr = String((const char *)doc["open"]);
        lastStr = stripSeparators(lastStr);
        openStr = stripSeparators(openStr);

        float lastF = lastStr.toFloat();
        float openF = openStr.toFloat();

        if (lastF > 0)
          lastPrice = lastF;
        if (openF > 0)
          priceChange = ((lastF - openF) / openF) * 100.0f;
      }
    }
    http.end();
    return;
  }

  // ==================== FALLBACK → BINANCE ====================
  String pair = (currency == "EUR") ? "BTCEUR" : "BTCUSDT";
  http.begin("https://api.binance.com/api/v3/ticker/24hr?symbol=" + pair);
  httpCode = http.GET();

  if (httpCode == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      String lastStr = doc["lastPrice"].as<String>();
      lastStr = stripSeparators(lastStr);
      lastPrice = lastStr.toFloat();

      String chgStr = doc["priceChangePercent"].as<String>();
      chgStr = stripSeparators(chgStr);
      priceChange = chgStr.toFloat();
    }
  }
  http.end();
}
