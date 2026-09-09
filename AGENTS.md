# AGENTS.md

## Projet

BTC Watch est un tableau de bord Bitcoin tactile pour l’ESP32-2432S028R
(Cheap Yellow Display 2,8 pouces, écran ILI9341 et contrôleur tactile XPT2046).

## Règles de travail

- Utiliser `src/main.cpp` comme point d’entrée du firmware PlatformIO.
- Ne pas recréer ni renommer `BTC-Watch.ino` sans vérifier son rôle de compatibilité.
- Préserver les fonctions existantes : suivi des adresses publiques, cours EUR/USD,
  frais réseau, horloge, tactile, Wi-Fi, dashboard, imports/exports, sauvegarde
  complète et remise à zéro d’usine.
- Ne jamais supprimer un fichier jugé inutile sans vérifier ses références dans le
  code, `platformio.ini`, les manifestes, le Web Flasher et les scripts.
- Ne pas modifier les broches de l’écran ou du tactile sans demande explicite.
- Ne jamais intégrer de clé privée, phrase de récupération, mot de passe Wi-Fi
  personnel, jeton ou autre secret dans le dépôt.
- Les fichiers générés dans `.pio/` ne doivent pas être versionnés.

## Vérification obligatoire

Après toute modification du firmware, exécuter depuis la racine du projet :

```powershell
C:\Users\nitru\.platformio\penv\Scripts\platformio.exe run
```

La compilation doit réussir pour l’environnement `cyd`. En cas d’échec, rapporter
l’erreur exacte et ne pas remplacer ou supprimer des composants au hasard.

## Web Flasher et firmware

- Le site statique se trouve dans `webflasher/`.
- Le manifeste `webflasher/manifest.json` doit toujours pointer vers un fichier
  réellement présent dans `webflasher/firmware/`.
- Après reconstruction d’un firmware publié, synchroniser la version, les noms de
  fichiers et `SHA256SUMS.txt`.
- Vérifier le rendu du Web Flasher sur ordinateur et mobile après une modification
  de `index.html`, `assets/styles.css` ou `assets/app.js`.
- Ne pas remplacer une image factory ou les fichiers de flash manuel sans vérifier
  leurs adresses dans `flash-layout.json`.

## Git

- Préserver les changements de l’utilisateur et éviter toute commande destructive.
- Examiner `git status` et le diff avant chaque commit.
- Ne versionner que les fichiers liés à la demande en cours.
- Ne créer une release ou ne pousser sur GitHub que lorsque l’utilisateur le demande.
