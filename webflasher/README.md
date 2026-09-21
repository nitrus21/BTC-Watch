# BTC Watch — Installation automatique

Cet installateur Web programme automatiquement **BTC Watch 0.9.9** sur un
**ESP32-2432S028R** depuis Chrome ou Microsoft Edge.

## Utilisation

1. Publier ce dossier sur une adresse HTTPS ou le servir sur `localhost`.
2. Brancher l'écran avec un câble USB de données.
3. Ouvrir `index.html` puis cliquer sur **Installer BTC Watch**.
4. Choisir le port série de l'ESP32 et attendre la fin complète de l'écriture.

L'image `firmware/btc-watch-0.9.9-esp32-2432s028r.factory.bin` contient tous les
éléments nécessaires et s'installe à l'adresse `0x0000`.

Après le premier démarrage, se connecter au Wi-Fi **BTC Watch** avec le mot de
passe **bitcoin21**, puis ouvrir **http://192.168.22.1**.

## Manuel utilisateur

Le manuel officiel de **20 pages** est disponible dans
[`downloads/manuel-btc-watch-v0.9.9.pdf`](downloads/manuel-btc-watch-v0.9.9.pdf).
Il utilise les véritables écrans de BTC Watch, décrit tous les boutons tactiles
et toutes les sections d'administration, puis explique la sauvegarde, le mode
panique et la remise à zéro d'usine avec le bouton BOOT.

Sur le Web Flasher, il est mis en avant dans un grand bandeau situé juste sous
l'en-tête, ainsi que dans les liens **Manuel PDF** de l'en-tête et de la page
d'accueil. La version publique est accessible sur
[nitrus21.github.io/BTC-Watch](https://nitrus21.github.io/BTC-Watch/).

Une installation avec effacement supprime les anciens réglages enregistrés.
