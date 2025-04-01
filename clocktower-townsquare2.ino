// ESP32-C6 Blood on the Clocktower Town Square Controller
// Using Adafruit libraries for the ST7789 display
// Features:
// - WS2812 LED control for up to 20 players (3 LEDs each)
// - ST7789 color LCD display (172x320) using Adafruit libraries
// - WiFi setup with fallback AP mode
// - Web server to control game state and player statuses
// - Persistent player states and names using SPIFFS

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// TFT Display pins
#define TFT_CS   14  // GPIO14
#define TFT_DC   15  // GPIO15
#define TFT_RST  21  // GPIO21
#define TFT_MOSI  6  // GPIO6
#define TFT_SCLK  7  // GPIO7
#define TFT_BL   22  // GPIO22 backlight
// SD Card (Not used)
// #define SD_CS    4   // GPIO4 for SD Card

// Initialize ST7789 display (using hardware SPI)
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// LEDs
#define DATA_PIN 8  // GPIO8 for RGB light beads (WS2812)
#define NUM_PLAYERS 20
#define LEDS_PER_PLAYER 3
#define NUM_LEDS (NUM_PLAYERS * LEDS_PER_PLAYER)
CRGB leds[NUM_LEDS];

// Player state types
enum PlayerStatus { NOT_IN_PLAY, ALIVE, DEAD_WITH_VOTE, DEAD_NO_VOTE };

// Player states
PlayerStatus playerStates[NUM_PLAYERS];
bool playerActive[NUM_PLAYERS];
String playerNames[NUM_PLAYERS];
bool isTraveller[NUM_PLAYERS];
bool gameEnded = false;
bool evilWon = false;
bool gameStarted = false;

int minions = 1;
int outsiders = 0;
int townsfolk = 3;

// Web server
WebServer server(80);

// Persistent state
#define CONFIG_FILE "/state.json"

// Defined colors for the ST7789 display
#define BACKGROUND ST77XX_BLACK
#define TEXT_COLOR ST77XX_WHITE
#define ALIVE_COLOR ST77XX_GREEN
#define DEAD_WITH_VOTE_COLOR ST77XX_BLUE
#define DEAD_NO_VOTE_COLOR 0x780F // Purple
#define MINION_COLOR 0xFDA0 // Orange
#define TRAVELLER_COLOR ST77XX_YELLOW
#define GOOD_WIN_COLOR ST77XX_BLUE
#define EVIL_WIN_COLOR ST77XX_RED
#define INFO_COLOR ST77XX_CYAN
#define WARNING_COLOR ST77XX_YELLOW
#define ERROR_COLOR ST77XX_RED
#define SUCCESS_COLOR ST77XX_GREEN

// State cache for display
int last_activeCount = -1;
int last_aliveCount = -1;
bool last_gameStarted = false;
bool last_gameEnded = false;
bool last_evilWon = false;
bool displayNeedsRefresh = true;  // Trigger first draw

// Non-blocking display update state
unsigned long lastDisplayUpdate = 0;
int displayStep = 0;

// Display line redraw flags
bool updateActiveLine = true;
bool updateAliveLine = true;
bool updateGameStateLine = true;
bool updateWinLine = true;
bool updatePlayerCount = true;

// Initialize display
// Initialize the ST7789 display
void setupDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init(172, 320);
  tft.setRotation(3);
  tft.fillScreen(BACKGROUND);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Initializing...");
}

void redrawLine(int y, const String &label, const String &value, uint16_t color) {
  tft.fillRect(0, y, 320, 24, BACKGROUND); // was 12
  tft.setCursor(0, y);
  tft.setTextColor(color);
  tft.print(label);
  tft.print(value);
}

void updateDisplay() {
  uint16_t col;
  String msg;
  int lineY = 0;

  if (updateGameStateLine || displayNeedsRefresh) {
    lineY=0;
    if (gameStarted != last_gameStarted || displayNeedsRefresh) {
      redrawLine(lineY, "Game state: ", gameStarted ? "Started" : "Not started", INFO_COLOR);
      last_gameStarted = gameStarted;
    }
    updateGameStateLine = false;
  }

  if (updateWinLine || displayNeedsRefresh) {
    lineY=24;
    if (gameEnded != last_gameEnded || evilWon != last_evilWon || displayNeedsRefresh) {
      msg = gameEnded ? (evilWon ? "EVIL Wins!" : "GOOD Wins!") : "";
      col = evilWon ? EVIL_WIN_COLOR : GOOD_WIN_COLOR;
      redrawLine(lineY, "", msg, col);
      last_gameEnded = gameEnded;
      last_evilWon = evilWon;
    }
    updateWinLine = false;
  }

  if (updatePlayerCount || displayNeedsRefresh) {
    lineY=48;

    redrawLine(lineY, "Demons: ", "1", EVIL_WIN_COLOR);

    lineY+=24;
    redrawLine(lineY, "Minions: ", String(minions), MINION_COLOR);
  
    lineY+=24;
    redrawLine(lineY, "Outsiders: ", String(outsiders), INFO_COLOR);
  
    lineY+=24;
    redrawLine(lineY, "Townsfolk: ", String(townsfolk), ALIVE_COLOR);

    lineY+=24;
    redrawLine(lineY, " ", " ", BACKGROUND);

    lineY+=24;
    redrawLine(lineY, " ", " ", BACKGROUND);
  }
  tft.setTextColor(TEXT_COLOR);
  displayNeedsRefresh = false;
  updatePlayerCount = false;
}

void saveState() {
  JsonDocument doc;
  doc["gameStarted"] = gameStarted;
  doc["gameEnded"] = gameEnded;
  doc["evilWon"] = evilWon;
  
  for (int i = 0; i < NUM_PLAYERS; i++) {
    doc["players"][i]["active"] = playerActive[i];
    doc["players"][i]["status"] = (int)playerStates[i];
    doc["players"][i]["name"] = playerNames[i];
    doc["players"][i]["traveller"] = isTraveller[i];
  }
  
  File f = SPIFFS.open(CONFIG_FILE, "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

void loadState() {
  if (!SPIFFS.exists(CONFIG_FILE)) return;
  File f = SPIFFS.open(CONFIG_FILE, "r");
  if (!f) return;
  
  // Using ArduinoJson 7.x
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  if (err) return;
  
  gameStarted = doc["gameStarted"].as<bool>();
  gameEnded = doc["gameEnded"].as<bool>();
  evilWon = doc["evilWon"].as<bool>();
  
  for (int i = 0; i < NUM_PLAYERS; i++) {
    playerActive[i] = doc["players"][i]["active"].as<bool>();
    playerStates[i] = (PlayerStatus)(doc["players"][i]["status"].as<int>());
    playerNames[i] = doc["players"][i]["name"].as<String>();
    isTraveller[i] = doc["players"][i]["traveller"].as<bool>();
  }
  f.close();
}

void updateLEDs() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    CRGB color = CRGB::Black;
    if (playerStates[i] == ALIVE) color = isTraveller[i] ? CRGB::Yellow : CRGB::Green;
    else if (playerStates[i] == DEAD_WITH_VOTE) color = CRGB::Blue;
    else if (playerStates[i] == DEAD_NO_VOTE) color = CRGB(80, 0, 80);
    for (int j = 0; j < LEDS_PER_PLAYER; j++) {
      leds[(i * LEDS_PER_PLAYER) + j] = color;
    }
  }
  FastLED.show();
}

String generateControlPage() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
  html += ".player{display:inline-block;margin:10px;padding:10px;border:1px solid #ccc;width:120px; border-radius:5px;}";
  html += "button{width:100%;height:40px; border-radius:5px;} .green{background:#0f0;} .blue{background:#00f;color:#fff;} .purple{background:#808;color:#fff;} .yellow{background:#ff0;} .inactive{background:#ddd;} .ended-red{background:#f00;color:#fff;} .ended-blue{background:#00f;color:#fff;}";
  html += "body{font-family:Arial,sans-serif; max-width:1200px; margin:0 auto; padding:10px;}";
  html += ".controls{margin-bottom:20px; padding:10px; background:#f5f5f5; border-radius:5px;}";
  html += "</style></head><body><h1>Blood on the Clocktower</h1><div class='controls'>";
  
  if (!gameStarted && !gameEnded) {
    html += "<form action='/start'><button type='submit'>Start Game</button></form>";
  }
  if (gameStarted && !gameEnded) {
    html += "<form action='/reset' onsubmit=\"return confirm('Are you sure you want to reset the game?')\"><button type='submit'>Reset Game</button></form>";
    html += "<form action='/end' method='get'><input type='hidden' name='result' value='good'><button type='submit'>Good Wins</button></form>";
    html += "<form action='/end' method='get'><input type='hidden' name='result' value='evil'><button type='submit'>Evil Wins</button></form>";
  }
  if (gameEnded) {
    html += "<form action='/reset' onsubmit=\"return confirm('Are you sure you want to reset the game?')\"><button type='submit'>Reset Game</button></form>";
  }
  html += "<p style='font-weight:bold;'>Game status: ";
  if (gameEnded) html += evilWon ? "EVIL WON" : "GOOD WON";
  else html += gameStarted ? "IN PROGRESS" : "NOT STARTED";
  html += "</p>";

  int aliveCount = 0;
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (playerStates[i] == ALIVE) aliveCount++;
  }
  int votesNeeded = (aliveCount + 1) / 2;
  int votesAvailable = aliveCount;
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (playerStates[i] == DEAD_WITH_VOTE) votesAvailable++;
  }
  html += "<p><strong>Alive:</strong> " + String(aliveCount) + "<br>";
  html += "<strong>Votes needed:</strong> " + String(votesNeeded) + "<br>";
  html += "<strong>Votes available:</strong> " + String(votesAvailable) + "</p>";

  int activeCount = 0;
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (playerActive[i] && !isTraveller[i]) activeCount++;
  }
  int base = (activeCount + 2) / 3;

  if (activeCount > 6) {
    townsfolk = 2 * base - 1;
    outsiders = (activeCount + 2) % 3;
    minions = min(base - 2, 3);
  }
  html += "<p><strong>Role Breakdown:</strong><br>";
  html += "Townsfolk: " + String(townsfolk) + "<br>";
  html += "Outsiders: " + String(outsiders) + "<br>";
  html += "Minions: " + String(minions) + "<br>";
  html += "Demon: 1</p>";
  html += "<form action='/clear' onsubmit=\"return confirm('Delete all saved state and restart?')\"><button type='submit'>Clear All</button></form></div>";

  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (gameStarted and playerStates[i] != NOT_IN_PLAY) {
      html += "<div class='player'>";
      html += "<form method='get' action='/name'><input type='hidden' name='i' value='" + String(i) + "'>";
      html += "<input name='name' value='" + playerNames[i] + "' style='width:100%; margin-bottom:5px;'><input type='submit' value='Rename' style='width:100%; margin-bottom:5px;'></form>";
      html += "<form method='get' action='/toggle'><input type='hidden' name='i' value='" + String(i) + "'>";
    } else {
      if (!gameStarted) {
        html += "<div class='player'>";
        html += "<form method='get' action='/name'><input type='hidden' name='i' value='" + String(i) + "'>";
        html += "<input name='name' value='" + playerNames[i] + "' style='width:100%; margin-bottom:5px;'><input type='submit' value='Rename' style='width:100%; margin-bottom:5px;'></form>";
        html += "<form method='get' action='/toggle'><input type='hidden' name='i' value='" + String(i) + "'>";
      }
    }

    String css = "inactive";
    if (gameEnded) {
      css = evilWon ? "ended-red" : "ended-blue";
    } else {
      if (playerStates[i] == ALIVE) css = isTraveller[i] ? "yellow" : "green";
      else if (playerStates[i] == DEAD_WITH_VOTE) css = "blue";
      else if (playerStates[i] == DEAD_NO_VOTE) css = "purple";
    }
    
    if (gameStarted and playerStates[i] != NOT_IN_PLAY) {
      html += "<button class='" + css + "' type='submit'>" + playerNames[i] + "</button></form>";
      html += "<form method='get' action='/traveller'><input type='hidden' name='i' value='" + String(i) + "'>";
      html += "<label><input type='checkbox' name='t' value='1' onchange='this.form.submit();' ";
      html += String(isTraveller[i] ? "checked" : "");
      html += "> Traveller</label></form></div>";
    } else {
      if (!gameStarted) {
        html += "<button class='" + css + "' type='submit'>" + playerNames[i] + "</button></form>";
        html += "<form method='get' action='/traveller'><input type='hidden' name='i' value='" + String(i) + "'>";
        html += "<label><input type='checkbox' name='t' value='1' onchange='this.form.submit();' ";
        html += String(isTraveller[i] ? "checked" : "");
        html += "> Traveller</label></form></div>";
      }
    }
  }
  html += "</body></html>";
  return html;
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", generateControlPage());
  });

  server.on("/toggle", HTTP_GET, [](){
    if (server.hasArg("i")) {
      int i = server.arg("i").toInt();
      if (i >= 0 && i < NUM_PLAYERS) {
        playerActive[i] = true;
        playerStates[i] = (PlayerStatus)((playerStates[i] + 1) % 4);
        updateLEDs();
        saveState();
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/name", HTTP_GET, [](){
    if (server.hasArg("i") && server.hasArg("name")) {
      int i = server.arg("i").toInt();
      String name = server.arg("name");
      if (i >= 0 && i < NUM_PLAYERS) {
        playerNames[i] = name;
        playerActive[i] = true;
        playerStates[i] = ALIVE;
        updateLEDs();
        saveState();
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/start", HTTP_GET, [](){
    gameStarted = true;
    gameEnded = false;
    saveState();
    server.sendHeader("Location", "/");
    server.send(303);
    displayNeedsRefresh = true;
    updateDisplay();
  });

  server.on("/reset", HTTP_GET, [](){
    gameStarted = false;
    gameEnded = false;
    evilWon = false;
    minions = 1;
    outsiders = 0;
    townsfolk = 3;
    for (int i = 0; i < NUM_PLAYERS; i++) {
      playerStates[i] = NOT_IN_PLAY;
      playerActive[i] = false;
    }
    updateLEDs();
    saveState();
    server.sendHeader("Location", "/");
    server.send(303);
    displayNeedsRefresh = true;
    updateDisplay();
  });

  server.on("/end", HTTP_GET, [](){
    if (server.hasArg("result")) {
      gameEnded = true;
      gameStarted = false;
      evilWon = (server.arg("result") == "evil");
      saveState();

      for (int i = 0; i < NUM_PLAYERS; i++) {
        CRGB color = evilWon ? CRGB::Red : CRGB::Blue;
        for (int j = 0; j < LEDS_PER_PLAYER; j++) {
          leds[(i * LEDS_PER_PLAYER) + j] = color;
        }
        FastLED.show();
        delay(100);
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
    displayNeedsRefresh = true;
    updateDisplay();
  });

  server.on("/clear", HTTP_GET, [](){
    SPIFFS.remove(CONFIG_FILE);
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    FastLED.show();

    // Also reset runtime state
    gameStarted = false;
    gameEnded = false;
    evilWon = false;
    for (int i = 0; i < NUM_PLAYERS; i++) {
      playerStates[i] = NOT_IN_PLAY;
      playerActive[i] = false;
      playerNames[i] = "P" + String(i + 1);
      isTraveller[i] = false;
    }
    saveState();
    server.sendHeader("Location", "/");
    server.send(303);
    displayNeedsRefresh = true;
    updateDisplay();
  });

  server.on("/traveller", HTTP_GET, [](){
    if (server.hasArg("i")) {
      int i = server.arg("i").toInt();
      if (i >= 0 && i < NUM_PLAYERS) {
        isTraveller[i] = server.hasArg("t");
        updateLEDs();
        saveState();
        
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/favicon.ico", HTTP_GET, [](){
    server.send(204);
  });
  
  server.begin();
}

void setupWiFi() {
  WiFiManager wifiManager;
  wifiManager.setTimeout(60);
  
  tft.println("WiFi setup...");
  tft.println("Connect to:");
  tft.println("Clocktower-Setup");
  tft.println("to configure WiFi");
  
  if(!wifiManager.autoConnect("Clocktower-Setup")) {
    tft.setTextColor(ERROR_COLOR);
    tft.println("WiFi Failed!");
    tft.println("Restarting...");
    delay(3000);
    ESP.restart();
  }
  
  tft.setTextColor(SUCCESS_COLOR);
  tft.println("WiFi connected!");
  tft.println("IP: " + WiFi.localIP().toString());
  tft.setTextColor(TEXT_COLOR);
}

void setupFileSystem() {
  if (!SPIFFS.begin(true)) {
    tft.setTextColor(ERROR_COLOR);
    tft.println("SPIFFS failed");
    while (true) { delay(1000); }
  }
}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);

  // Initialize display
  setupDisplay();
  
  // Setup file system
  tft.println("Initializing file system...");
  setupFileSystem();
  
  // Setup LEDs
  tft.println("Initializing LEDs...");
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();

  // Initialize player states
  for (int i = 0; i < NUM_PLAYERS; i++) {
    playerStates[i] = NOT_IN_PLAY;
    playerNames[i] = "P" + String(i + 1);
    playerActive[i] = false;
    isTraveller[i] = false;
  }
  
  // Load saved state
  loadState();
  updateLEDs();
  
  // Setup WiFi
  tft.println("Setting up WiFi...");
  setupWiFi();
  
  // Setup web server
  tft.println("Starting web server...");
  setupWebServer();
  
  // Update display with initial game state
  displayNeedsRefresh = true;
  updateDisplay();
}

void loop() {
  server.handleClient();
}