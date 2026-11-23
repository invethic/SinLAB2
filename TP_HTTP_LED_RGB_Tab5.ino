/*
 * ============================================================================
 *  TP STI2D SIN — Lycée Baggio
 *  « Commander une LED RGB par requêtes HTTP »
 * ----------------------------------------------------------------------------
 *  Matériel :
 *    - M5Stack Tab5 (ESP32-P4 + ESP32-C6 pour le WiFi)
 *    - 1 LED RGB à CATHODE COMMUNE + 3 résistances 220 Ω
 *    - Port 3 du Tab5 :  GPIO49 -> Rouge | GPIO50 -> Vert | GPIO1 -> Bleu
 *                        GND    -> Cathode commune (patte la plus longue)
 *
 *  Cathode commune => une broche à HIGH (3,3 V) ALLUME la couleur.
 *
 *  3 façons de commander la LED :
 *    1) Boutons tactiles sur l'écran du Tab5            (commande LOCALE)
 *    2) Site web embarqué  http://<IP_DU_TAB5>/         (navigateur)
 *    3) Requêtes HTTP envoyées par Node-RED             (API REST)
 *
 *  API HTTP (méthode GET) :
 *    GET /                                -> page web (HTML)
 *    GET /api/state                       -> état JSON {"red":0,"green":1,"blue":0}
 *    GET /api/led?color=red&state=on      -> color = red|green|blue
 *                                            state = on|off|toggle
 *    GET /api/rgb?r=1&g=0&b=1             -> règle les 3 couleurs d'un coup
 *    GET /api/off                         -> éteint tout
 *
 *  Bibliothèques (Gestionnaire de bibliothèques Arduino) :
 *    - M5Unified (+ M5GFX)
 *  Carte : "M5Tab5" (paquet M5Stack ≥ 3.2) — ou ESP32P4 Dev Module
 *  WiFi.h et WebServer.h sont fournis avec le paquet de cartes ESP32 (3.x).
 * ============================================================================
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>

// ---------------------------------------------------------------------------
//  1) CONFIGURATION WiFi  — À MODIFIER par l'élève / le professeur
// ---------------------------------------------------------------------------
const char* WIFI_SSID     = "SSID_DE_LA_SALLE";
const char* WIFI_PASSWORD = "MOT_DE_PASSE";


// ---------------------------------------------------------------------------
//  2) Broches SDIO entre l'ESP32-P4 et le coprocesseur WiFi ESP32-C6 du Tab5
//     (ne pas modifier)
// ---------------------------------------------------------------------------
#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

// ---------------------------------------------------------------------------
//  3) Broches de la LED RGB (Port 3)
// ---------------------------------------------------------------------------
const int PIN_RED   = 49;
const int PIN_GREEN = 50;
const int PIN_BLUE  = 1;

// État courant des 3 couleurs (true = allumée)
bool ledRed = false, ledGreen = false, ledBlue = false;
bool needRedraw = true;           // l'écran doit-il être redessiné ?
unsigned long httpCount = 0;      // compteur de requêtes reçues
String lastRequest = "-";         // dernière requête affichée à l'écran

WebServer server(80);             // serveur HTTP sur le port 80

// ---------------------------------------------------------------------------
//  Interface tactile : 4 boutons
// ---------------------------------------------------------------------------
struct Button {
  int x, y, w, h;
  const char* label;
  uint16_t colorOn;
};
Button btnRed, btnGreen, btnBlue, btnOff;

// ===========================================================================
//  PARTIE MATÉRIELLE : appliquer l'état aux broches
// ===========================================================================
void applyLeds() {
  // Cathode commune : HIGH = allumé
  digitalWrite(PIN_RED,   ledRed   ? HIGH : LOW);
  digitalWrite(PIN_GREEN, ledGreen ? HIGH : LOW);
  digitalWrite(PIN_BLUE,  ledBlue  ? HIGH : LOW);
  needRedraw = true;
}

// Renvoie l'état sous forme JSON :  {"red":1,"green":0,"blue":0}
String stateJson() {
  String j = "{\"red\":";
  j += ledRed ? 1 : 0;
  j += ",\"green\":";
  j += ledGreen ? 1 : 0;
  j += ",\"blue\":";
  j += ledBlue ? 1 : 0;
  j += ",\"requests\":";
  j += httpCount;
  j += "}";
  return j;
}

// Modifie une couleur : action = "on" | "off" | "toggle"
bool setColor(const String& color, const String& action) {
  bool* target = nullptr;
  if (color == "red")   target = &ledRed;
  if (color == "green") target = &ledGreen;
  if (color == "blue")  target = &ledBlue;
  if (target == nullptr) return false;

  if (action == "on")          *target = true;
  else if (action == "off")    *target = false;
  else if (action == "toggle") *target = !*target;
  else return false;

  applyLeds();
  return true;
}

// ===========================================================================
//  PARTIE WEB : page HTML embarquée (stockée en mémoire Flash)
// ===========================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tab5 - LED RGB</title>
<style>
  body{font-family:Arial,sans-serif;background:#14213d;color:#fff;text-align:center;margin:0;padding:20px}
  h1{margin:10px 0 4px}
  p.sub{color:#a8b3cf;margin-top:0}
  .led{width:120px;height:120px;border-radius:50%;margin:20px auto;border:4px solid #fff;background:#000;transition:.2s}
  .row{display:flex;justify-content:center;gap:12px;flex-wrap:wrap}
  button{font-size:20px;padding:16px 22px;border:none;border-radius:12px;cursor:pointer;color:#fff;min-width:120px}
  .r{background:#c0392b}.g{background:#1e8449}.b{background:#1f4e9e}.o{background:#555}
  button.on{outline:4px solid #fca311}
  pre{background:#0b1426;color:#7CFC9A;text-align:left;padding:12px;border-radius:8px;max-width:520px;margin:20px auto;white-space:pre-wrap}
</style>
</head>
<body>
  <h1>LED RGB — Tab5</h1>
  <p class="sub">Site web embarqué dans le Tab5 (serveur HTTP port 80)</p>
  <div class="led" id="led"></div>
  <div class="row">
    <button class="r" id="bred"   onclick="send('/api/led?color=red&state=toggle')">Rouge</button>
    <button class="g" id="bgreen" onclick="send('/api/led?color=green&state=toggle')">Vert</button>
    <button class="b" id="bblue"  onclick="send('/api/led?color=blue&state=toggle')">Bleu</button>
    <button class="o"             onclick="send('/api/off')">Tout éteindre</button>
  </div>
  <pre id="log">Requête envoyée / réponse reçue…</pre>
<script>
// Envoie une requête HTTP GET au Tab5 et affiche la réponse JSON
async function send(url){
  const t0 = performance.now();
  const rep = await fetch(url);
  const txt = await rep.text();
  const dt = Math.round(performance.now() - t0);
  document.getElementById('log').textContent =
    'GET ' + url + '\nCode : ' + rep.status + ' ' + rep.statusText +
    '  (' + dt + ' ms)\nRéponse : ' + txt;
  show(JSON.parse(txt));
}
// Met à jour l'affichage à partir de l'état JSON
function show(s){
  const r = s.red ? 255 : 0, g = s.green ? 255 : 0, b = s.blue ? 255 : 0;
  document.getElementById('led').style.background = 'rgb('+r+','+g+','+b+')';
  document.getElementById('bred').classList.toggle('on', !!s.red);
  document.getElementById('bgreen').classList.toggle('on', !!s.green);
  document.getElementById('bblue').classList.toggle('on', !!s.blue);
}
// Interroge l'état toutes les secondes (synchronisation avec l'écran tactile et Node-RED)
async function poll(){
  try{ const rep = await fetch('/api/state'); show(await rep.json()); }catch(e){}
}
setInterval(poll, 1000); poll();
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
//  Gestionnaires des routes HTTP
// ---------------------------------------------------------------------------
void logRequest() {
  httpCount++;
  lastRequest = server.uri();
  for (int i = 0; i < server.args(); i++) {
    lastRequest += (i == 0 ? "?" : "&");
    lastRequest += server.argName(i) + "=" + server.arg(i);
  }
  Serial.printf("[HTTP] %s depuis %s\n", lastRequest.c_str(),
                server.client().remoteIP().toString().c_str());
  needRedraw = true;
}

void sendJson(int code, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");   // autorise Node-RED / autres pages
  server.send(code, "application/json", body);
}

// GET /
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// GET /api/state   (non comptée : interrogée en boucle par la page web)
void handleState() {
  sendJson(200, stateJson());
}

// GET /api/led?color=red&state=on
void handleLed() {
  logRequest();
  if (!server.hasArg("color") || !server.hasArg("state")) {
    sendJson(400, "{\"error\":\"parametres color et state obligatoires\"}");
    return;
  }
  String color = server.arg("color");  color.toLowerCase();
  String state = server.arg("state");  state.toLowerCase();
  if (!setColor(color, state)) {
    sendJson(400, "{\"error\":\"color=red|green|blue  state=on|off|toggle\"}");
    return;
  }
  sendJson(200, stateJson());
}

// GET /api/rgb?r=1&g=0&b=1
void handleRgb() {
  logRequest();
  if (server.hasArg("r")) ledRed   = server.arg("r").toInt() != 0;
  if (server.hasArg("g")) ledGreen = server.arg("g").toInt() != 0;
  if (server.hasArg("b")) ledBlue  = server.arg("b").toInt() != 0;
  applyLeds();
  sendJson(200, stateJson());
}

// GET /api/off
void handleOff() {
  logRequest();
  ledRed = ledGreen = ledBlue = false;
  applyLeds();
  sendJson(200, stateJson());
}

// Route inconnue -> 404
void handleNotFound() {
  logRequest();
  sendJson(404, "{\"error\":\"route inconnue\"}");
}

// ===========================================================================
//  PARTIE ÉCRAN : dessin de l'interface tactile
// ===========================================================================
void layoutButtons() {
  int W = M5.Display.width();
  int H = M5.Display.height();
  int margin = 30;
  int bw = (W - 3 * margin) / 2;      // 2 colonnes
  int bh = 150;
  int y0 = H - 2 * bh - 3 * margin;   // boutons en bas de l'écran

  btnRed   = { margin,            y0,                 bw, bh, "ROUGE",   TFT_RED };
  btnGreen = { 2 * margin + bw,   y0,                 bw, bh, "VERT",    TFT_GREEN };
  btnBlue  = { margin,            y0 + bh + margin,   bw, bh, "BLEU",    TFT_BLUE };
  btnOff   = { 2 * margin + bw,   y0 + bh + margin,   bw, bh, "ETEINDRE", TFT_DARKGREY };
}

void drawButton(const Button& b, bool on) {
  uint16_t fill = on ? b.colorOn : (uint16_t)0x2104;   // gris très foncé si éteint
  M5.Display.fillRoundRect(b.x, b.y, b.w, b.h, 20, fill);
  M5.Display.drawRoundRect(b.x, b.y, b.w, b.h, 20, b.colorOn);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
}

bool inside(const Button& b, int x, int y) {
  return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

void drawScreen() {
  int W = M5.Display.width();
  M5.Display.startWrite();
  M5.Display.fillScreen(0x10A2);                       // fond bleu nuit

  // Titre
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(4);
  M5.Display.drawString("TP HTTP - LED RGB", W / 2, 30);

  // Infos réseau
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_YELLOW);
  if (WiFi.status() == WL_CONNECTED) {
    String url = "http://" + WiFi.localIP().toString() + "/";
    M5.Display.drawString(url, W / 2, 100);
  } else {
    M5.Display.drawString("WiFi non connecte...", W / 2, 100);
  }

  // LED virtuelle (couleur résultante)
  uint8_t r = ledRed ? 255 : 0, g = ledGreen ? 255 : 0, b = ledBlue ? 255 : 0;
  int cy = 300;
  M5.Display.fillCircle(W / 2, cy, 110, M5.Display.color565(r, g, b));
  M5.Display.drawCircle(W / 2, cy, 112, TFT_WHITE);
  M5.Display.drawCircle(W / 2, cy, 113, TFT_WHITE);

  // Dernière requête HTTP reçue
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.setTextSize(2);
  M5.Display.drawString("Derniere requete HTTP (" + String(httpCount) + ") :", W / 2, 440);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(lastRequest, W / 2, 475);

  // Boutons
  drawButton(btnRed,   ledRed);
  drawButton(btnGreen, ledGreen);
  drawButton(btnBlue,  ledBlue);
  drawButton(btnOff,   false);
  M5.Display.endWrite();
  needRedraw = false;
}

// Gestion du tactile (commande LOCALE)
void handleTouch() {
  auto t = M5.Touch.getDetail();
  if (!t.wasPressed()) return;

  if      (inside(btnRed,   t.x, t.y)) setColor("red",   "toggle");
  else if (inside(btnGreen, t.x, t.y)) setColor("green", "toggle");
  else if (inside(btnBlue,  t.x, t.y)) setColor("blue",  "toggle");
  else if (inside(btnOff,   t.x, t.y)) { ledRed = ledGreen = ledBlue = false; applyLeds(); }
  else return;

  lastRequest = "(bouton tactile local)";
  Serial.println("[TOUCH] commande locale");
}

// ===========================================================================
//  WiFi
// ===========================================================================
void connectWiFi() {
  WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connexion a %s", WIFI_SSID);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connecte ! Adresse IP : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Echec de connexion WiFi (verifier SSID / mot de passe)");
  }
}

// ===========================================================================
//  SETUP / LOOP
// ===========================================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(0);   // portrait (essayer 1 ou 3 pour le paysage)
  layoutButtons();

  // Broches de la LED en sortie, LED éteinte au démarrage
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);
  applyLeds();

  drawScreen();
  connectWiFi();

  // Déclaration des routes du serveur HTTP
  server.on("/",          HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/led",   HTTP_GET, handleLed);
  server.on("/api/rgb",   HTTP_GET, handleRgb);
  server.on("/api/off",   HTTP_GET, handleOff);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Serveur HTTP demarre sur le port 80");

  needRedraw = true;
}

void loop() {
  M5.update();             // lecture du tactile
  server.handleClient();   // traitement des requêtes HTTP
  handleTouch();

  // Reconnexion automatique si le WiFi tombe
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      needRedraw = true;
    }
  }

  if (needRedraw) drawScreen();
  delay(5);
}
