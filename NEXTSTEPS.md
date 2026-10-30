# StageWatch : prochaines étapes

État au 30/10/2026 : le serveur, l'admin, la page public et le simulateur sont **terminés**.
Le firmware **compile** mais n'a **jamais tourné sur une vraie montre**.
Commande en cours : 9–10 × LilyGO T-Watch S3 (+ routeur de voyage GL.iNet, câbles USB-C de données).

---

## 0. Pour se rafraîchir la mémoire (5 min)
- Lancer `dist/StageWatch-mac-arm64` (clic droit › Ouvrir la 1re fois), puis ouvrir `/sim` et `/public`.
- Vue d'ensemble : `README.md`. Firmware : `firmware/README.md`.
- Outils déjà installés sur le Mac : Go (Homebrew) et PlatformIO (`~/.pio-venv/bin/pio`).
  Sur un autre ordi : `brew install go` et `python3 -m venv ~/.pio-venv && ~/.pio-venv/bin/pip install platformio`.

## 1. Routeur Wi-Fi
- [ ] Le configurer avec un SSID et un mot de passe dédiés (ex. `StageWatch`), **en 2,4 GHz** (l'ESP32 ne fait pas le 5 GHz).
- [ ] Pas besoin d'Internet, ni d'IP fixe : les montres trouvent le laptop toutes seules (annonce UDP sur le port 47808).
- [ ] Mettre le SSID et le mot de passe dans `firmware/src/config.h`.
- Si l'isolation des clients (« AP isolation ») est active, la désactiver : sinon les montres ne voient pas le laptop.

## 2. Première montre (une soirée)
- [ ] Brancher la montre, puis `cd firmware && ~/.pio-venv/bin/pio run -e twatch -t upload`.
      Si ça ne marche pas : maintenir la couronne ~1 s pour l'allumer. Si elle redémarre en boucle, utiliser le bouton BOOT interne (voir `firmware/README.md`).
- [ ] Si l'écran reste noir ou affiche n'importe quoi : passer en `twatch-debug` et lire les logs avec `pio device monitor`.
- [ ] **Vérifications et réglages** (tout est dans `firmware/src/main.cpp` / `config.h`) :
  - [ ] Orientation du texte : `tft.setRotation(2)` → essayer 0 à 3.
  - [ ] Couleurs inversées : retirer `-DTFT_INVERSION_ON=1` dans `platformio.ini`.
  - [ ] Vibration : force et rythme dans `buzzMessage()` (effets DRV2605 de 1 à 123, le 47 est le plus fort).
  - [ ] Couronne : appui court = écran d'info, appui long = menu du numéro.
  - [ ] Batterie : le pourcentage s'affiche dans l'admin.
  - [ ] Lisibilité à bout de bras, sous un éclairage fort. Luminosités : `BRIGHT_*` dans `config.h`.
- [ ] Laptop **sur le routeur** : la montre passe au vert dans l'admin. Envoyer un message et vérifier vibration → affichage → ✓ reçu.
- [ ] Tester la coupure : éteindre le routeur 30 s puis le rallumer → la montre doit se reconnecter seule.

## 3. Toutes les montres
- [ ] Flasher les autres et leur donner un numéro de 1 à 8 avec la couronne. Les 1–2 restantes servent de rechange.
- [ ] Coller une étiquette avec le numéro au dos de chaque montre.
- [ ] Signaler rapidement au vendeur une montre défectueuse.

## 4. Répétition générale
- [ ] Les 8 montres en même temps pendant 2–3 h, avec des messages réguliers.
- [ ] Mesurer l'**autonomie** et noter le % de batterie dans l'admin toutes les 30 min.
      Si c'est trop juste : activer `WiFi.setSleep(true)` (un peu plus de latence) et baisser `BRIGHT_*`.
- [ ] Tester la **portée** dans une vraie salle (routeur caché en coulisse ?).
- [ ] Tester le **Windows** (`dist/StageWatch.exe`), s'il est utilisé un jour : autoriser le pare-feu au premier lancement.

## 5. Idées pour plus tard (non commencées)
- Messages préenregistrés / boutons rapides dans l'admin.
- Messages programmés (envoi à un moment précis).
- Thème et habillage de la page public (logo, couleurs du spectacle).
- Export de l'historique du spectacle.
- Mise à jour des montres par Wi-Fi (OTA), sans câble.
- Signer l'app Mac pour éviter l'avertissement au premier lancement.

## Points techniques à retenir
- Le protocole est décrit dans `README.md`. Le simulateur (`/sim`) suit exactement le même protocole que le firmware.
- Versions figées pour la stabilité : `espressif32@6.3.0` (Arduino core 2.0.9, version recommandée par LilyGO) et les bibliothèques dans `platformio.ini`. **Ne pas les mettre à jour** sans tester.
- Courant de charge : 125 mA. **Ne pas l'augmenter**, ça abîme la batterie (recommandation LilyGO).
- Configuration de l'écran et des broches reprise du dépôt officiel `Xinyuan-LilyGO/TTGO_TWatch_Library`, branche `t-watch-s3`.
- Noms et photos des participants : `stagewatch-config.json` et `photos/`, à côté de l'exécutable. Journaux des spectacles : `logs/`.
- Pour recompiler le serveur : `./build.sh` (produit les versions Mac et Windows dans `dist/`).
