#pragma once
// ======= À ADAPTER : réseau du kit =======
#define WIFI_SSID      "StageWatch"
#define WIFI_PASSWORD  "changeme123"

// Le serveur est trouvé automatiquement (annonce UDP du laptop).
// Si la découverte échoue, on essaie cette adresse (laisser "" pour désactiver).
#define SERVER_FALLBACK_HOST ""
#define SERVER_PORT          8080
#define DISCOVERY_PORT       47808

// ======= Comportement =======
#define BRIGHT_FULL      255     // luminosité à la réception d'un message
#define BRIGHT_DIM       60      // luminosité après FULL_MS (texte toujours lisible)
#define BRIGHT_IDLE      25      // luminosité sans message
#define FULL_MS          30000   // durée en pleine luminosité après un message
#define HEARTBEAT_MS     10000
#define FW_VERSION       "0.1.0"

// ======= Broches T-Watch S3 (source : LilyGO src/utilities.h) =======
#define PIN_I2C_SDA   10
#define PIN_I2C_SCL   11
#define PIN_PMU_INT   21
#define PIN_TFT_BL    45
