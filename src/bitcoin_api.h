/**************************************************************
 *  API Bitcoin - Clock-21M
 *
 *  Classe pour récupérer le prix du Bitcoin depuis différentes
 *  sources (Binance, CoinGecko, Bitstamp).
 **************************************************************/

#ifndef BITCOIN_API_H
#define BITCOIN_API_H

#include <Arduino.h>

class BitcoinAPI {
private:
  float lastPrice;
  float priceChange;

  String stripSeparators(const String &s);

public:
  BitcoinAPI();

  // Mise à jour du prix depuis l'API sélectionnée
  void updatePrice(const String &api, const String &currency);

  // Récupération des valeurs
  float getPrice() const { return lastPrice; }
  float getChange() const { return priceChange; }
};

#endif // BITCOIN_API_H
