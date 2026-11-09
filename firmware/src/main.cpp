// StageWatch — firmware LilyGO T-Watch S3
// Reçoit des messages du serveur (WebSocket), vibre, affiche le texte en très gros, confirme la réception.
//
// Écran : s'éteint après 2 min sans message ni appui ; se rallume à l'arrivée d'un message ou avec la couronne.
// Couronne (bouton latéral) :
//   - appui court  : écran éteint → le rallume ; sinon écran d'info (ID, Wi-Fi, serveur, batterie) 5 s
//   - appui long   : (1,5 s environ, géré par l'AXP2101) entre dans le menu ID ; chaque appui court
//                    fait défiler 1..8, un nouvel appui long valide et enregistre.
//   - appui 6 s     : extinction (matériel, valeur LilyGO)
#include <Arduino.h>
#include <vector>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include <XPowersLib.h>
#include <SensorDRV2605.hpp>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "battery_cal.h"

#ifdef SW_DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

TFT_eSPI tft;
TFT_eSprite frame(&tft);  // rendu hors écran (pas de scintillement)
U8g2_for_TFT_eSPI u8f;
XPowersAXP2101 pmu;
SensorDRV2605 drv;
WebSocketsClient ws;
WiFiUDP udp;
Preferences prefs;

// ---------- État ----------
int watchId = 1;
String serverHost = "";
uint16_t serverPort = SERVER_PORT;
bool wsStarted = false, wsConnected = false;
String curText = "";
uint32_t msgAt = 0, lastBeat = 0, lastDraw = 0, infoUntil = 0;
uint32_t lastActivity = 0;  // dernier message ou appui couronne
bool screenOff = false;
bool batCalOk = false;
bool menuMode = false;
int menuId = 1;
uint32_t menuAt = 0;        // dernier appui dans le menu (sortie auto sans enregistrer)
#define MENU_TIMEOUT_MS 8000
volatile bool pmuIrq = false;
bool hasDrv = false;
uint8_t curBright = 0;
int lumPct = 100;            // luminosité réglée depuis l'admin (10..100 %), appliquée à tous les niveaux

// Wi-Fi : 3 réseaux mémorisés (préférences). Le n°1 vaut par défaut celui de secrets.h.
String wifiSsid[3], wifiPass[3];
int wifiSlot = -1;           // réseau en cours d'essai / utilisé
uint32_t wifiTryAt = 0;
#define WIFI_TRY_MS 15000    // temps laissé à un réseau avant de passer au suivant

// Grille de texte FIXE : 12 colonnes x 8 lignes = 96 caractères (identique au simulateur et à la page public).
// Police à chasse fixe (accents OK). Coupure entre les mots ; un mot de plus de 12 lettres est coupé.
#define MSG_FONT  u8g2_font_inb21_mf
const int COLS = 12, ROWS = 8, CELL_W = 19, CELL_H = 27, GRID_X = 6, GRID_Y = 22;

const uint16_t C_BG = TFT_BLACK, C_TXT = TFT_WHITE, C_MUT = 0x7BEF, C_OK = 0x3E6F, C_BAD = 0xF2AA, C_ACC = 0xFE87;
const int W = 240, H = 240, MARGIN = 6, TOPBAR = 18;

// ---------- Matériel ----------
void IRAM_ATTR onPmu() { pmuIrq = true; }

void setBrightness(uint8_t v) {
  if (v == curBright) return;
  curBright = v;
  ledcWrite(0, (uint32_t)v * lumPct / 100);
}
void applyLum() { ledcWrite(0, (uint32_t)curBright * lumPct / 100); }

// Écran : extinction complète (rétroéclairage coupé + écran en veille) et réveil
void screenSleep() {
  if (screenOff) return;
  screenOff = true;
  setBrightness(0);
  tft.writecommand(0x10);  // ST7789 : sleep in
  pmu.disableALDO2();      // alimentation du rétroéclairage
}
void screenWake() {
  lastActivity = millis();
  if (!screenOff) return;
  screenOff = false;
  pmu.enableALDO2();
  tft.writecommand(0x11);  // sleep out
  delay(120);              // délai requis par le ST7789 après sleep out
}

void initPower() {
  // Séquence reprise de LilyGoLib::beginPower() (dépôt officiel)
  if (!pmu.init(Wire, PIN_I2C_SDA, PIN_I2C_SCL)) { LOG("PMU absent\n"); return; }
  pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
  pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);
  pmu.setSysPowerDownVoltage(2600);
  pmu.setALDO1Voltage(3300);  // RTC
  pmu.setALDO2Voltage(3300);  // rétroéclairage
  pmu.setALDO3Voltage(3300);  // écran + tactile
  pmu.setALDO4Voltage(3300);  // radio LoRa (inutilisée)
  pmu.setBLDO2Voltage(3300);  // DRV2605 (vibration)
  pmu.disableDC2(); pmu.disableDC3(); pmu.disableDC4(); pmu.disableDC5();
  pmu.disableBLDO1(); pmu.disableCPUSLDO(); pmu.disableDLDO1(); pmu.disableDLDO2();
  pmu.enableALDO1(); pmu.enableALDO2(); pmu.enableALDO3();
  pmu.disableALDO4();         // LoRa coupée : économie de batterie
  pmu.enableBLDO2();
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
  pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
  pmu.disableTSPinMeasure();
  pmu.enableBattDetection(); pmu.enableVbusVoltageMeasure(); pmu.enableBattVoltageMeasure(); pmu.enableSystemVoltageMeasure();
  pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
  pmu.clearIrqStatus();
  pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_125MA);  // ne pas augmenter (recommandation LilyGO)
  pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
  pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V35);
  pinMode(PIN_PMU_INT, INPUT_PULLUP);
  attachInterrupt(PIN_PMU_INT, onPmu, FALLING);
}

void initHaptic() {
  delay(10);
  hasDrv = drv.init(Wire, PIN_I2C_SDA, PIN_I2C_SCL);
  if (!hasDrv) { LOG("DRV2605 absent\n"); return; }
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);
  drv.useERM();
}

// Vibration "message" : impulsions longues à pleine puissance (mode temps réel du DRV2605),
// pilotées sans bloquer par hapticTick() dans loop().
// Garde-fous : réglages bornés à la compilation + coupure forcée du moteur après MOTOR_MAX_MS
static_assert(BUZZ_ON_MS <= 1000, "BUZZ_ON_MS trop long (max 1000 ms) : risque d'échauffement du moteur");
static_assert(BUZZ_POWER >= 10 && BUZZ_POWER <= 100, "BUZZ_POWER doit être entre 10 et 100");
static_assert(BUZZ_COUNT >= 1 && BUZZ_COUNT <= 5, "BUZZ_COUNT doit être entre 1 et 5");
#define MOTOR_MAX_MS 3000
bool motorOn = false;
uint32_t motorSince = 0;
int buzzStep = -1;          // -1 = inactif ; pair = moteur ON, impair = pause
uint32_t buzzStepAt = 0;
void motor(bool on) {
  motorOn = on; motorSince = millis();
  if (on) { drv.setMode(DRV2605_MODE_REALTIME); drv.setRealtimeValue((uint8_t)(0x7F * BUZZ_POWER / 100)); }  // 0x7F = pleine échelle
  else    { drv.setRealtimeValue(0x00); drv.setMode(DRV2605_MODE_INTTRIG); }
}
void buzzMessage() {
  if (!hasDrv) return;
  buzzStep = 0; buzzStepAt = millis(); motor(true);
}
void hapticTick() {
  // Sécurité : quoi qu'il arrive, le moteur ne reste jamais allumé plus de MOTOR_MAX_MS
  if (motorOn && millis() - motorSince > MOTOR_MAX_MS) { motor(false); buzzStep = -1; return; }
  if (buzzStep < 0) return;
  uint32_t dur = (buzzStep % 2 == 0) ? BUZZ_ON_MS : BUZZ_OFF_MS;
  if (millis() - buzzStepAt < dur) return;
  buzzStep++; buzzStepAt = millis();
  if (buzzStep >= BUZZ_COUNT * 2 - 1) { motor(false); buzzStep = -1; return; }
  motor(buzzStep % 2 == 0);
}
void buzzShort() {
  if (!hasDrv || buzzStep >= 0) return;
  drv.setWaveform(0, 1); drv.setWaveform(1, 0); drv.run();
}

int batteryPercent() {
  if (!pmu.isBatteryConnect()) return -1;
  int p = pmu.getBatteryPercent();
  return (p < 0 || p > 100) ? -1 : p;
}

// ---------- Affichage ----------
// Découpe en lignes de COLS caractères max (UTF-8 : on compte les caractères, pas les octets).
std::vector<std::vector<String>> wrapGrid(const String& txt) {
  std::vector<std::vector<String>> lines;  // chaque ligne = liste de caractères UTF-8
  std::vector<String> line, word;
  auto flushWord = [&]() {
    if (word.empty()) return;
    if (!line.empty() && line.size() + 1 + word.size() <= (size_t)COLS) { line.push_back(" "); line.insert(line.end(), word.begin(), word.end()); }
    else {
      if (!line.empty()) { lines.push_back(line); line.clear(); }
      while (word.size() > (size_t)COLS) { lines.emplace_back(word.begin(), word.begin() + COLS); word.erase(word.begin(), word.begin() + COLS); }
      line = word;
    }
    word.clear();
  };
  for (size_t i = 0; i < txt.length();) {
    uint8_t c = txt[i];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    String ch = txt.substring(i, i + n); i += n;
    if (ch == " " || ch == "\t") flushWord();
    else if (ch == "\n") { flushWord(); lines.push_back(line); line.clear(); }
    else word.push_back(ch);
  }
  flushWord();
  lines.push_back(line);
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

void drawTopBar() {
  u8f.setFont(u8g2_font_helvB10_tf);
  u8f.setForegroundColor(C_ACC);
  u8f.setCursor(MARGIN, 14);
  u8f.print(String(watchId));
  frame.fillCircle(30, 9, 3, wsConnected ? C_OK : C_BAD);
  int b = batteryPercent();
  if (b >= 0) {
    String s = String(b) + "%";
    u8f.setForegroundColor(b < 20 ? C_BAD : C_MUT);
    u8f.setCursor(W - MARGIN - u8f.getUTF8Width(s.c_str()), 14);
    u8f.print(s);
  }
}

void drawMessage() {
  if (curText.isEmpty()) {
    u8f.setFont(u8g2_font_fub42_tf);
    u8f.setForegroundColor(0x2104);
    String s = String(watchId);
    u8f.setCursor((W - u8f.getUTF8Width(s.c_str())) / 2, H / 2 + 20);
    u8f.print(s);
    return;
  }
  auto lines = wrapGrid(curText);
  if ((int)lines.size() > ROWS) lines.resize(ROWS);
  int r0 = (ROWS - (int)lines.size()) / 2;
  u8f.setFont(MSG_FONT);
  u8f.setForegroundColor(C_TXT);
  int asc = u8f.getFontAscent(), desc = -u8f.getFontDescent();
  int baseOff = (CELL_H - (asc + desc)) / 2 + asc;  // texte centré verticalement dans la case
  for (int r = 0; r < (int)lines.size(); r++) {
    auto& l = lines[r];
    int c0 = (COLS - (int)l.size()) / 2;
    for (int c = 0; c < (int)l.size(); c++) {
      int cw = u8f.getUTF8Width(l[c].c_str());
      u8f.setCursor(GRID_X + (c0 + c) * CELL_W + (CELL_W - cw) / 2, GRID_Y + (r0 + r) * CELL_H + baseOff);
      u8f.print(l[c]);
    }
  }
}

void drawInfo() {
  u8f.setFont(u8g2_font_helvB14_tf);
  int y = 46;
  auto row = [&](const String& k, const String& v, uint16_t col) {
    u8f.setForegroundColor(C_MUT); u8f.setCursor(MARGIN + 4, y); u8f.print(k);
    u8f.setForegroundColor(col); u8f.setCursor(100, y); u8f.print(v); y += 30;
  };
  row("Montre", String(watchId), C_ACC);
  row("Wi-Fi", WiFi.isConnected() ? WiFi.localIP().toString() : String("..."), WiFi.isConnected() ? C_OK : C_BAD);
  row("Serveur", wsConnected ? serverHost + ":" + String(serverPort) : (serverHost.isEmpty() ? String("recherche") : serverHost + " ..."), wsConnected ? C_OK : C_BAD);
  int b = batteryPercent();
  // pourcentage (jauge) + tension mesurée directement (fiable même si la jauge se trompe)
  String bat = b >= 0 ? String(b) + "% " : String("? ");
  bat += String(pmu.getBattVoltage() / 1000.0f, 2) + "V";
  if (pmu.isCharging()) bat += " +";
  if (!batCalOk) bat += " !";
  row("Batterie", bat, C_TXT);
  row("Version", FW_VERSION, C_MUT);
}

void drawMenu() {
  u8f.setFont(u8g2_font_helvB14_tf);
  u8f.setForegroundColor(C_MUT);
  const char* t = "Numero de montre";
  u8f.setCursor((W - u8f.getUTF8Width(t)) / 2, 40); u8f.print(t);
  u8f.setFont(u8g2_font_fub42_tf);
  u8f.setForegroundColor(C_ACC);
  String s = String(menuId);
  u8f.setCursor((W - u8f.getUTF8Width(s.c_str())) / 2, 150); u8f.print(s);
  u8f.setFont(u8g2_font_helvR10_tf);
  u8f.setForegroundColor(C_MUT);
  const char* a = "appui court: suivant";
  const char* b = "appui long: valider";
  u8f.setCursor((W - u8f.getUTF8Width(a)) / 2, 200); u8f.print(a);
  u8f.setCursor((W - u8f.getUTF8Width(b)) / 2, 220); u8f.print(b);
}

void render() {
  frame.fillSprite(C_BG);
  u8f.begin(frame);
  u8f.setFontMode(1);
  if (menuMode) drawMenu();
  else if (millis() < infoUntil) drawInfo();
  else { drawTopBar(); drawMessage(); }
  frame.pushSprite(0, 0);
  lastDraw = millis();
}

// ---------- Réseau ----------
void wsSend(JsonDocument& d) {
  String s; serializeJson(d, s); ws.sendTXT(s);
}
void sendHello() {
  JsonDocument d; d["type"] = "hello"; d["id"] = watchId; d["battery"] = batteryPercent(); d["fw"] = FW_VERSION;
  d["mv"] = pmu.getBattVoltage(); d["chg"] = pmu.isCharging(); d["usb"] = pmu.isVbusIn();
  JsonArray nets = d["wifi"].to<JsonArray>();
  for (int i = 0; i < 3; i++) nets.add(wifiSsid[i]);   // noms seulement, jamais les mots de passe
  d["slot"] = WiFi.isConnected() ? wifiSlot : -1;
  d["rssi"] = WiFi.RSSI();
  d["lum"] = lumPct;
  wsSend(d);
}
void sendOta(const char* state, int pct = -1, const String& err = "") {
  JsonDocument d; d["type"] = "ota"; d["state"] = state;
  if (pct >= 0) d["pct"] = pct;
  if (err.length()) d["err"] = err;
  wsSend(d);
}

// ---------- Wi-Fi ----------
void loadWifi() {
  for (int i = 0; i < 3; i++) {
    wifiSsid[i] = prefs.getString(("s" + String(i)).c_str(), i == 0 ? WIFI_SSID : "");
    wifiPass[i] = prefs.getString(("p" + String(i)).c_str(), i == 0 ? WIFI_PASSWORD : "");
  }
}
void wifiTryNext() {
  for (int k = 1; k <= 3; k++) {
    int i = (wifiSlot + k + 3) % 3;
    if (wifiSsid[i].isEmpty()) continue;
    wifiSlot = i;
    WiFi.disconnect();
    WiFi.begin(wifiSsid[i].c_str(), wifiPass[i].c_str());
    wifiTryAt = millis();
    LOG("Wi-Fi : essai réseau %d (%s)\n", i + 1, wifiSsid[i].c_str());
    return;
  }
}
void wifiTick() {
  if (WiFi.isConnected()) { wifiTryAt = millis(); return; }
  if (millis() - wifiTryAt > WIFI_TRY_MS) wifiTryNext();
}

// ---------- Mise à jour sans fil ----------
void drawCentered(const char* l1, const String& l2, uint16_t col) {
  frame.fillSprite(C_BG);
  u8f.begin(frame); u8f.setFontMode(1);
  u8f.setFont(u8g2_font_helvB14_tf); u8f.setForegroundColor(C_MUT);
  u8f.setCursor((W - u8f.getUTF8Width(l1)) / 2, 100); u8f.print(l1);
  u8f.setFont(u8g2_font_fub25_tf); u8f.setForegroundColor(col);
  u8f.setCursor((W - u8f.getUTF8Width(l2.c_str())) / 2, 150); u8f.print(l2);
  frame.pushSprite(0, 0);
}
void doOta() {
  int b = batteryPercent();
  if (!pmu.isVbusIn() && b < 50) { sendOta("refused", -1, "batterie " + String(b) + "% (50% minimum ou câble USB)"); return; }
  sendOta("start");
  screenWake(); setBrightness(BRIGHT_FULL);
  drawCentered("Mise a jour", "0%", C_ACC);
  static int lastPct = -1; lastPct = -1;
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int cur, int total) {
    int pct = total > 0 ? (int)((int64_t)cur * 100 / total) : 0;
    if (pct == lastPct) return;
    lastPct = pct;
    drawCentered("Mise a jour", String(pct) + "%", C_ACC);
    if (pct % 10 == 0) { sendOta("progress", pct); ws.loop(); }
  });
  WiFiClient client;
  HTTPUpdateResult res = httpUpdate.update(client, serverHost, serverPort, "/firmware.bin");
  if (res == HTTP_UPDATE_OK) {
    sendOta("ok"); ws.loop();
    drawCentered("Mise a jour", "OK", C_OK);
    delay(800);
    ESP.restart();
  }
  String err = httpUpdate.getLastErrorString();
  sendOta("error", -1, err);
  drawCentered("Mise a jour", "ECHEC", C_BAD);
  delay(2000);
  render();
}

void onWs(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true; LOG("WS connecté\n"); sendHello(); render(); break;
    case WStype_DISCONNECTED:
      if (wsConnected) { wsConnected = false; LOG("WS déconnecté\n"); render(); } break;
    case WStype_TEXT: {
      JsonDocument d;
      if (deserializeJson(d, payload, len)) return;
      const char* t = d["type"] | "";
      if (!strcmp(t, "msg")) {
        buzzMessage();
        curText = String(d["text"] | "");
        curText.trim();
        msgAt = millis(); infoUntil = 0;
        screenWake();
        setBrightness(BRIGHT_FULL);
        render();
        JsonDocument a; a["type"] = "ack"; a["msgId"] = d["msgId"]; wsSend(a);
      } else if (!strcmp(t, "wifi")) {
        // {slot 0..2, ssid, pass} ; ssid vide = effacer. Jamais le réseau utilisé en ce moment.
        int i = d["slot"] | -1;
        if (i < 0 || i > 2) return;
        if (WiFi.isConnected() && i == wifiSlot) { sendOta("wifi-refused"); return; }
        wifiSsid[i] = String(d["ssid"] | ""); wifiPass[i] = String(d["pass"] | "");
        prefs.putString(("s" + String(i)).c_str(), wifiSsid[i]);
        prefs.putString(("p" + String(i)).c_str(), wifiPass[i]);
        buzzShort();
        sendHello();
      } else if (!strcmp(t, "lum")) {
        lumPct = constrain((int)(d["value"] | 100), 10, 100);
        prefs.putInt("lum", lumPct);
        if (!screenOff) applyLum();
        sendHello();
      } else if (!strcmp(t, "ota")) {
        doOta();
      } else if (!strcmp(t, "clear")) {
        curText = ""; if (!screenOff) setBrightness(BRIGHT_IDLE); render();
      }
      break;
    }
    default: break;
  }
}

void startWs() {
  ws.begin(serverHost.c_str(), serverPort, "/ws?role=watch");
  ws.onEvent(onWs);
  ws.setReconnectInterval(1500);
  ws.enableHeartbeat(10000, 4000, 2);  // détecte un lien mort en ~20 s
  wsStarted = true;
}

// Le serveur annonce "STAGEWATCH <port>" en UDP broadcast toutes les secondes.
void discover() {
  int n = udp.parsePacket();
  if (n <= 0) return;
  char buf[64]; int r = udp.read(buf, sizeof(buf) - 1); buf[r < 0 ? 0 : r] = 0;
  if (strncmp(buf, "STAGEWATCH", 10)) return;
  String ip = udp.remoteIP().toString();
  int p = atoi(buf + 10);  // "STAGEWATCH <port>"
  uint16_t port = (p > 0 && p < 65536) ? p : SERVER_PORT;
  if (ip != serverHost || port != serverPort) {
    LOG("Serveur trouvé : %s:%u\n", ip.c_str(), port);
    serverHost = ip;
    serverPort = port;
    if (wsStarted) ws.disconnect();
    startWs();
    render();
  }
}

// ---------- Couronne ----------
void handleButton() {
  if (!pmuIrq) return;
  pmuIrq = false;
  pmu.getIrqStatus();
  bool shortP = pmu.isPekeyShortPressIrq(), longP = pmu.isPekeyLongPressIrq();
  pmu.clearIrqStatus();
  if (!shortP && !longP) return;
  if (screenOff) {
    // écran éteint : l'appui court sert seulement à le rallumer (dernier message affiché)
    screenWake(); render(); setBrightness(BRIGHT_FULL); msgAt = millis();
    if (shortP) return;
  }
  screenWake();
  setBrightness(BRIGHT_FULL); msgAt = millis();
  if (menuMode) {
    menuAt = millis();
    if (shortP) { menuId = menuId % 8 + 1; buzzShort(); }
    if (longP) {
      menuMode = false;
      if (menuId != watchId) {
        watchId = menuId;
        prefs.putInt("id", watchId);
        if (wsConnected) sendHello();  // le serveur rattache la montre au nouvel ID
      }
      buzzMessage();
    }
  } else {
    if (longP) { menuMode = true; menuId = watchId; menuAt = millis(); infoUntil = 0; buzzShort(); }
    else if (shortP) { infoUntil = (infoUntil && millis() < infoUntil) ? 0 : millis() + 5000; }  // 2e appui = retour accueil
  }
  render();
}

// Marqueur lu par le serveur pour afficher la version d'un firmware.bin déposé
extern "C" { __attribute__((used)) const char SWFW_TAG[] = "SWFW:" FW_VERSION; }

// ---------- Setup / loop ----------
void setup() {
#ifdef SW_DEBUG
  Serial.begin(115200);
#endif
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  initPower();
  initHaptic();

  ledcSetup(0, 1000, 8);
  ledcAttachPin(PIN_TFT_BL, 0);
  ledcWrite(0, 0);
  tft.init();
  tft.setRotation(2);  // orientation utilisée par LilyGO
  tft.fillScreen(C_BG);
  frame.setColorDepth(16);
  frame.createSprite(W, H);
  setBrightness(BRIGHT_IDLE);
  lastActivity = millis();

  (void)*(volatile const char*)SWFW_TAG;
  prefs.begin("stagewatch", false);
  batCalOk = calibrateBatteryOnce(pmu, prefs);
  watchId = constrain(prefs.getInt("id", 1), 1, 8);
  lumPct = constrain(prefs.getInt("lum", 100), 10, 100);
  loadWifi();
  render();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // latence minimale (au prix de la batterie)
  WiFi.setAutoReconnect(true);
  wifiTryNext();
  udp.begin(DISCOVERY_PORT);
  if (strlen(SERVER_FALLBACK_HOST)) { serverHost = SERVER_FALLBACK_HOST; startWs(); }
}

void loop() {
  hapticTick();
  handleButton();
  wifiTick();
  discover();
  if (wsStarted) ws.loop();

  uint32_t now = millis();
  if (wsConnected && now - lastBeat > HEARTBEAT_MS) {
    lastBeat = now;
    JsonDocument d; d["type"] = "hb"; d["battery"] = batteryPercent();
    d["mv"] = pmu.getBattVoltage(); d["chg"] = pmu.isCharging(); d["usb"] = pmu.isVbusIn();
    d["rssi"] = WiFi.RSSI();
    wsSend(d);
  }
  // Luminosité : pleine après un message, puis réduite (le texte reste visible), puis écran éteint
  if (!screenOff && !menuMode && now - lastActivity > SCREEN_OFF_MS) screenSleep();
  if (!screenOff && !menuMode && msgAt && now - msgAt > FULL_MS) setBrightness(curText.isEmpty() ? BRIGHT_IDLE : BRIGHT_DIM);
  // Rafraîchit la barre (batterie, état) et la sortie de l'écran info
  if (!screenOff && now - lastDraw > 15000) render();
  // Menu numéro : sans appui pendant MENU_TIMEOUT_MS, on quitte sans rien changer
  if (menuMode && now - menuAt > MENU_TIMEOUT_MS) { menuMode = false; render(); }
  if (infoUntil && now > infoUntil) { infoUntil = 0; if (!screenOff) render(); }
  delay(5);
}
