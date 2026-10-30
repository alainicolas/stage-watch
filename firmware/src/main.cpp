// StageWatch — firmware LilyGO T-Watch S3
// Reçoit des messages du serveur (WebSocket), vibre, affiche le texte en très gros, confirme la réception.
//
// Couronne (bouton latéral) :
//   - appui court  : affiche l'écran d'info (ID, Wi-Fi, serveur, batterie) quelques secondes
//   - appui long   : (1,5 s environ, géré par l'AXP2101) entre dans le menu ID ; chaque appui court
//                    fait défiler 1..8, un nouvel appui long valide et enregistre.
//   - appui 4 s+   : extinction (matériel)
#include <Arduino.h>
#include <vector>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include <XPowersLib.h>
#include <SensorDRV2605.hpp>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"

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
bool wsStarted = false, wsConnected = false;
String curText = "";
uint32_t msgAt = 0, lastBeat = 0, lastDraw = 0, infoUntil = 0;
bool menuMode = false;
int menuId = 1;
volatile bool pmuIrq = false;
bool hasDrv = false;
uint8_t curBright = 0;

// Polices (U8g2, avec accents français) de la plus grande à la plus petite
struct FontDef { const uint8_t* f; };
const FontDef FONTS[] = {
  {u8g2_font_fub42_tf}, {u8g2_font_fub35_tf}, {u8g2_font_fub30_tf}, {u8g2_font_fub25_tf},
  {u8g2_font_fub20_tf}, {u8g2_font_fub17_tf}, {u8g2_font_fub14_tf}, {u8g2_font_fub11_tf},
};
const int NFONTS = sizeof(FONTS) / sizeof(FONTS[0]);

const uint16_t C_BG = TFT_BLACK, C_TXT = TFT_WHITE, C_MUT = 0x7BEF, C_OK = 0x3E6F, C_BAD = 0xF2AA, C_ACC = 0xFE87;
const int W = 240, H = 240, MARGIN = 6, TOPBAR = 18;

// ---------- Matériel ----------
void IRAM_ATTR onPmu() { pmuIrq = true; }

void setBrightness(uint8_t v) {
  if (v == curBright) return;
  curBright = v;
  ledcWrite(0, v);
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
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
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

// Vibration "message" : 2 impulsions fortes bien senties
void buzzMessage() {
  if (!hasDrv) return;
  drv.setWaveform(0, 47);  // Buzz 1 – 100%
  drv.setWaveform(1, 0x80 | 15);  // pause 150 ms
  drv.setWaveform(2, 47);
  drv.setWaveform(3, 0);
  drv.run();
}
void buzzShort() {
  if (!hasDrv) return;
  drv.setWaveform(0, 1); drv.setWaveform(1, 0); drv.run();
}

int batteryPercent() {
  if (!pmu.isBatteryConnect()) return -1;
  int p = pmu.getBatteryPercent();
  return (p < 0 || p > 100) ? -1 : p;
}

// ---------- Affichage ----------
// Découpe en lignes avec une police donnée ; renvoie false si ça ne tient pas.
bool layout(const String& txt, int fi, std::vector<String>& lines, int& lineH) {
  u8f.setFont(FONTS[fi].f);
  int asc = u8f.getFontAscent(), desc = -u8f.getFontDescent();
  lineH = asc + desc + 4;
  const int maxW = W - 2 * MARGIN, maxH = H - TOPBAR - MARGIN;
  lines.clear();
  String line = "", word = "";
  auto width = [&](const String& s) { return (int)u8f.getUTF8Width(s.c_str()); };
  auto flushWord = [&]() -> bool {
    if (word.isEmpty()) return true;
    String cand = line.isEmpty() ? word : line + " " + word;
    if (width(cand) <= maxW) { line = cand; }
    else {
      if (!line.isEmpty()) lines.push_back(line);
      if (width(word) > maxW) return false;  // mot trop long pour cette taille
      line = word;
    }
    word = "";
    return true;
  };
  for (size_t i = 0; i <= txt.length(); i++) {
    char c = i < txt.length() ? txt[i] : ' ';
    if (c == ' ' || c == '\n') {
      if (!flushWord()) return false;
      if (c == '\n') { lines.push_back(line); line = ""; }
    } else word += c;
  }
  if (!line.isEmpty()) lines.push_back(line);
  return (int)lines.size() * lineH <= maxH;
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
  std::vector<String> lines; int lineH = 0, fi = 0;
  for (; fi < NFONTS; fi++) if (layout(curText, fi, lines, lineH)) break;
  if (fi == NFONTS) { fi = NFONTS - 1; layout(curText, fi, lines, lineH); }  // au pire : coupé
  u8f.setFont(FONTS[fi].f);
  u8f.setForegroundColor(C_TXT);
  int asc = u8f.getFontAscent();
  int total = lines.size() * lineH;
  int y = TOPBAR + ((H - TOPBAR) - total) / 2 + asc;
  for (auto& l : lines) {
    u8f.setCursor((W - u8f.getUTF8Width(l.c_str())) / 2, y);
    u8f.print(l);
    y += lineH;
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
  row("Serveur", wsConnected ? serverHost : (serverHost.isEmpty() ? String("recherche") : serverHost + " ..."), wsConnected ? C_OK : C_BAD);
  int b = batteryPercent();
  row("Batterie", b >= 0 ? String(b) + "%" + (pmu.isCharging() ? " +" : "") : String("?"), C_TXT);
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
  wsSend(d);
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
        setBrightness(BRIGHT_FULL);
        render();
        JsonDocument a; a["type"] = "ack"; a["msgId"] = d["msgId"]; wsSend(a);
      } else if (!strcmp(t, "clear")) {
        curText = ""; setBrightness(BRIGHT_IDLE); render();
      }
      break;
    }
    default: break;
  }
}

void startWs() {
  ws.begin(serverHost.c_str(), SERVER_PORT, "/ws?role=watch");
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
  if (ip != serverHost) {
    LOG("Serveur trouvé : %s\n", ip.c_str());
    serverHost = ip;
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
  setBrightness(BRIGHT_FULL); msgAt = millis();
  if (menuMode) {
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
    if (longP) { menuMode = true; menuId = watchId; buzzShort(); }
    else if (shortP) { infoUntil = millis() + 5000; }
  }
  render();
}

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

  prefs.begin("stagewatch", false);
  watchId = constrain(prefs.getInt("id", 1), 1, 8);
  render();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // latence minimale (au prix de la batterie)
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  udp.begin(DISCOVERY_PORT);
  if (strlen(SERVER_FALLBACK_HOST)) { serverHost = SERVER_FALLBACK_HOST; startWs(); }
}

void loop() {
  handleButton();
  discover();
  if (wsStarted) ws.loop();

  uint32_t now = millis();
  if (wsConnected && now - lastBeat > HEARTBEAT_MS) {
    lastBeat = now;
    JsonDocument d; d["type"] = "hb"; d["battery"] = batteryPercent(); wsSend(d);
  }
  // Luminosité : pleine après un message, puis réduite (le texte reste visible)
  if (!menuMode && msgAt && now - msgAt > FULL_MS) setBrightness(curText.isEmpty() ? BRIGHT_IDLE : BRIGHT_DIM);
  // Rafraîchit la barre (batterie, état) et la sortie de l'écran info
  if (now - lastDraw > 15000 || (infoUntil && now > infoUntil && now - infoUntil < 50)) render();
  if (infoUntil && now > infoUntil) { infoUntil = 0; render(); }
  delay(5);
}
