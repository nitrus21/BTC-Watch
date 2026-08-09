# BTC Watch — Installation automatique

Cet installateur Web programme automatiquement **BTC Watch 0.8.9** sur un
**ESP32-2432S028R** depuis Chrome ou Microsoft Edge.

## Utilisation

1. Publier ce dossier sur une adresse HTTPS ou le servir sur `localhost`.
2. Brancher l'écran avec un câble USB de données.
3. Ouvrir `index.html` puis cliquer sur **Installer BTC Watch**.
4. Choisir le port série de l'ESP32 et attendre la fin complète de l'écriture.

L'image `firmware/btc-watch-0.8.9-esp32-2432s028r.factory.bin` contient tous les
éléments nécessaires et s'installe à l'adresse `0x0000`.

Après le premier démarrage, se connecter au Wi-Fi **BTC Watch** avec le mot de
passe **bitcoin21**, puis ouvrir **http://192.168.22.1**.

Une installation avec effacement supprime les anciens réglages enregistrés.
