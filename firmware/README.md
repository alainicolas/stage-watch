# Firmware T-Watch S3

## Avant de flasher
Copie `src/secrets.example.h` en `src/secrets.h` et mets-y le nom et le mot de passe du Wi-Fi du kit
(`WIFI_SSID` et `WIFI_PASSWORD`). `secrets.h` n’est jamais envoyé sur git. Aucune IP à configurer : la montre trouve le laptop toute seule
(annonce réseau envoyée par StageWatch chaque seconde).

## Flasher (Mac)
1. Installer PlatformIO une fois : `python3 -m venv ~/.pio-venv && ~/.pio-venv/bin/pip install platformio`
2. Brancher la montre (câble de **données**). Elle doit être **allumée et réveillée** (couronne 2 s, toucher l'écran) : en veille, le flash échoue avec « No serial data received ».
3. `cd firmware && ~/.pio-venv/bin/pio run -e twatch -t upload`
4. Choisir le numéro de la montre (voir ci-dessous) — il reste en mémoire, même après un nouveau flash.

Normalement, **pas besoin d'ouvrir la montre** (testé sur la 1re montre, deux fois de suite).
En dernier recours seulement (le port USB apparaît/disparaît en boucle, ou le flash échoue toujours), suivre la
[procédure officielle LilyGO](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/lilygo-t-watch-s3.md#entering-download-mode-on-the-t-watch-s3) :
ouvrir le capot, **soulever la batterie sans tirer sur ses fils**, maintenir BOOT ([photo](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/images/twatchs3-bootbutton.jpg)),
appuyer 1 s sur la couronne, relâcher BOOT, flasher, puis **éteindre et rallumer** la montre (il n'y a pas de bouton reset).

Firmware d'usine LilyGO (pour tester le matériel en cas de doute) : `twatchs3-sx1262-factory-*.bin` dans
[LilyGoLib/firmware](https://github.com/Xinyuan-LilyGO/LilyGoLib/tree/master/firmware), à écrire à l'adresse **0x0**.

Logs pour diagnostiquer (câble branché) : `pio run -e twatch-debug -t upload && pio device monitor`.
**Ne pas laisser le firmware debug** : il peut empêcher la montre de démarrer sur batterie.

## Utilisation de la couronne
| Geste | Effet |
|---|---|
| appui court | écran d'info 5 s (numéro, Wi-Fi, serveur, batterie, version) |
| appui long (~1,5 s) | menu « Numéro de montre » : appuis courts = 1→8, appui long = valider |
| appui 6 s | extinction (rallumer : 2 s) |

## À l'écran
- Au repos : le numéro de la montre en gris. En haut : numéro, pastille verte/rouge (serveur), batterie.
- Message : **2 vibrations**, puis le texte dans la plus grande police qui tient (accents OK).
  Pleine luminosité 30 s, puis luminosité réduite ; le texte reste affiché jusqu'au suivant ou « Effacer ».

## À valider sur la vraie montre
Orientation de l'écran (`setRotation`), force/forme de la vibration (`buzzMessage`), lisibilité, autonomie.
