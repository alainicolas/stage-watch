# Firmware T-Watch S3

## Avant de flasher
Copie `src/secrets.example.h` en `src/secrets.h` et mets-y le nom et le mot de passe du Wi-Fi du kit
(`WIFI_SSID` et `WIFI_PASSWORD`). `secrets.h` n’est jamais envoyé sur git. Aucune IP à configurer : la montre trouve le laptop toute seule
(annonce réseau envoyée par StageWatch chaque seconde).

## Flasher (Mac)
1. Installer PlatformIO une fois : `python3 -m venv ~/.pio-venv && ~/.pio-venv/bin/pip install platformio`
2. Brancher la montre en USB-C (câble de **données**). Si rien n'apparaît, maintenir la couronne ~1 s pour l'allumer.
3. `cd firmware && ~/.pio-venv/bin/pio run -e twatch -t upload`
4. Choisir le numéro de la montre (voir ci-dessous) — il reste en mémoire, même après un nouveau flash.

Si la montre redémarre en boucle et que le port USB apparaît/disparaît : ouvrir le capot arrière,
maintenir le bouton BOOT interne, appuyer 1 s sur la couronne, relâcher, puis relancer l'upload.

Logs pour diagnostiquer (câble branché) : `pio run -e twatch-debug -t upload && pio device monitor`.
**Ne pas laisser le firmware debug** : il peut empêcher la montre de démarrer sur batterie.

## Utilisation de la couronne
| Geste | Effet |
|---|---|
| appui court | écran d'info 5 s (numéro, Wi-Fi, serveur, batterie, version) |
| appui long (~1,5 s) | menu « Numéro de montre » : appuis courts = 1→8, appui long = valider |
| appui 4 s | extinction |

## À l'écran
- Au repos : le numéro de la montre en gris. En haut : numéro, pastille verte/rouge (serveur), batterie.
- Message : **2 vibrations**, puis le texte dans la plus grande police qui tient (accents OK).
  Pleine luminosité 30 s, puis luminosité réduite ; le texte reste affiché jusqu'au suivant ou « Effacer ».

## À valider sur la vraie montre
Orientation de l'écran (`setRotation`), force/forme de la vibration (`buzzMessage`), lisibilité, autonomie.
