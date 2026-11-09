#pragma once
// ======= Réseau du kit =======
// Les identifiants Wi-Fi sont dans secrets.h (non versionné). À défaut, on utilise secrets.example.h.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Le serveur est trouvé automatiquement (annonce UDP du laptop).
// Si la découverte échoue, on essaie cette adresse (laisser "" pour désactiver).
#define SERVER_FALLBACK_HOST ""
#define SERVER_PORT          47800   // utilisé seulement avec SERVER_FALLBACK_HOST ; sinon le port vient de l'annonce du serveur
#define DISCOVERY_PORT       47808

// ======= Comportement =======
#define BRIGHT_FULL      255     // luminosité à la réception d'un message
#define BRIGHT_DIM       60      // luminosité après FULL_MS (texte toujours lisible)
#define BRIGHT_IDLE      25      // luminosité sans message
#define FULL_MS          30000   // durée en pleine luminosité après un message
#define SCREEN_OFF_MS    120000  // écran éteint après 2 min sans message ni appui (réveil : message ou couronne)
#define HEARTBEAT_MS     10000
// Vibration à la réception : BUZZ_COUNT impulsions de BUZZ_ON_MS à pleine puissance, séparées de BUZZ_OFF_MS
#define BUZZ_COUNT       3
#define BUZZ_ON_MS       400
#define BUZZ_OFF_MS      180
#define BUZZ_POWER       50      // intensité en % (100 = max ; plus bas = moins de bruit de résonance du boîtier)
#define FW_VERSION       "0.1.4"

// ======= Broches T-Watch S3 (source : LilyGO src/utilities.h) =======
#define PIN_I2C_SDA   10
#define PIN_I2C_SCL   11
#define PIN_PMU_INT   21
#define PIN_TFT_BL    45
