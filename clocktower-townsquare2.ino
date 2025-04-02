// ESP32-C6 Blood on the Clocktower Town Square Controller
// Using Adafruit libraries for the ST7789 display
// Features:
// - WS2812 LED control for up to 20 players (3 LEDs each)
// - ST7789 color LCD display (172x320) using Adafruit libraries
// - WiFi setup with fallback AP mode
// - Web server with AJAX for responsive control without page refreshes
// - Persistent player states and names using SPIFFS

#define APP_VERSION "v0.0.3"

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <WebServer.h>
#include <ElegantOTA.h>
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
bool updateGameStateLine = true;
bool updateWinLine = true;
bool updatePlayerCount = true;

// Initialize display
void setupDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init(172, 320);
  tft.setRotation(3);
  tft.fillScreen(BACKGROUND);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("BOTC Townsquare");
  tft.println(APP_VERSION);
  tft.println("Initializing...");
}

void redrawLine(int y, const String &label, const String &value, uint16_t color) {
  tft.fillRect(0, y, 320, 24, BACKGROUND); // was 12
  tft.setCursor(0, y);
  tft.setTextColor(color);
  tft.print(label + value);
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
  StaticJsonDocument<2048> doc;

  doc["gameStarted"] = gameStarted;
  doc["gameEnded"] = gameEnded;
  doc["evilWon"] = evilWon;

  JsonArray players = doc.createNestedArray("players");
  for (int i = 0; i < NUM_PLAYERS; i++) {
    JsonObject p = players.createNestedObject();
    p["active"] = playerActive[i];
    p["status"] = (int)playerStates[i];
    p["name"] = playerNames[i];
    p["traveller"] = isTraveller[i];
  }

  File f = SPIFFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("Failed to open config file for writing.");
    return;
  }

  if (serializeJson(doc, f) == 0) {
    Serial.println("Failed to write JSON to file.");
  } else {
    Serial.println("State saved successfully.");
  }

  f.close();
}



void loadState() {
  if (!SPIFFS.exists(CONFIG_FILE)) {
    Serial.println("No saved state file found.");
    return;
  }

  File f = SPIFFS.open(CONFIG_FILE, "r");
  if (!f) {
    Serial.println("Failed to open config file for reading.");
    return;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(err.c_str());
    return;
  }

  gameStarted = doc["gameStarted"] | false;
  gameEnded = doc["gameEnded"] | false;
  evilWon = doc["evilWon"] | false;

  JsonArray players = doc["players"];
  for (int i = 0; i < NUM_PLAYERS && i < players.size(); i++) {
    JsonObject p = players[i];
    playerActive[i] = p["active"] | false;
    playerStates[i] = (PlayerStatus)(p["status"] | 0);
    isTraveller[i] = p["traveller"] | false;

    if (p["name"].is<const char*>()) {
      playerNames[i] = p["name"].as<const char*>();
    } else {
      playerNames[i] = "Player " + String(i);
    }
  }

  Serial.println("State loaded successfully.");
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

String getHTMLStyles() {
  return R"(
    <style>
      * {
        box-sizing: border-box;
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      }
      body {
        background-color: #121212;
        color: #f0f0f0;
        max-width: 1200px;
        margin: 0 auto;
        padding: 20px;
      }
      h1 {
        text-align: center;
        color: #bb5555;
        margin-bottom: 30px;
        font-family: 'Times New Roman', serif;
        font-size: 2.5em;
        text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.5);
      }
      .controls {
        background-color: #1e1e1e;
        border-radius: 10px;
        padding: 20px;
        margin-bottom: 30px;
        box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
      }
      .game-stats {
        display: flex;
        flex-wrap: wrap;
        justify-content: space-between;
        margin: 20px 0;
      }
      .stat-box {
        background-color: #2a2a2a;
        border-radius: 8px;
        padding: 15px;
        margin: 5px;
        flex: 1 0 200px;
      }
      .stat-box h3 {
        margin-top: 0;
        color: #bb9b55;
        border-bottom: 1px solid #444;
        padding-bottom: 8px;
      }
      .player-grid {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
        gap: 15px;
      }
      .player {
        background-color: #2a2a2a;
        border-radius: 8px;
        padding: 15px;
        transition: all 0.3s ease;
        box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
        height: auto;
        max-width: 100%;
        display: flex;
        flex-direction: column;
      }
      .player:hover {
        transform: translateY(-5px);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.3);
      }
      .name-form {
        width: 100%;
      }
      .player-name {
        width: 100%;
        padding: 8px;
        margin-bottom: 10px;
        border: none;
        border-radius: 4px;
        background-color: #3a3a3a;
        color: #f0f0f0;
        box-sizing: border-box;
      }
      .btn {
        display: inline-block;
        padding: 10px 15px;
        border: none;
        border-radius: 5px;
        cursor: pointer;
        font-weight: bold;
        transition: background-color 0.3s, transform 0.2s;
        margin: 5px 0;
        width: 100%;
      }
      .btn:hover {
        transform: scale(1.05);
      }
      .btn-primary {
        background-color: #555fbb;
        color: white;
      }
      .btn-danger {
        background-color: #bb5555;
        color: white;
      }
      .btn-success {
        background-color: #55bb55;
        color: white;
      }
      .btn-warning {
        background-color: #bbbb55;
        color: black;
      }
      .btn-inactive {
        background-color: #444;
        color: #ddd;
      }
      .status-green {
        background-color: #1a472a;
        border: 2px solid #2a623d;
      }
      .status-blue {
        background-color: #1a3a5f;
        border: 2px solid #2a5a8f;
      }
      .status-purple {
        background-color: #3a1a5f;
        border: 2px solid #5a2a8f;
      }
      .status-yellow {
        background-color: #5a4a1a;
        border: 2px solid #8a7a2a;
      }
      .status-evil {
        background-color: #5a1a1a;
        border: 2px solid #8a2a2a;
      }
      .status-good {
        background-color: #1a3a5f;
        border: 2px solid #2a5a8f;
      }
      .status-inactive {
        background-color: #2a2a2a;
        border: 2px solid #3a3a3a;
        opacity: 0.7;
      }
      .status-btn {
        background-color: #3a3a3a;
        color: white;
        font-weight: bold;
      }

      .traveller-checkbox {
        margin: 10px 0;
        display: flex;
        align-items: center;
        width: 100%;
      }

      .traveller-checkbox label {
        flex-grow: 1;
      }
      .resurrect-icon::before {
       content: "✨";
      }

      .resurrect-icon {
        font-size: 1.5em;
        cursor: pointer;
        margin-left: 5px;
        padding: 2px;
        transition: transform 0.2s;
        flex-shrink: 0;
      }

      .resurrect-icon:hover {
        transform: scale(1.2);
      }
      #notification {
        position: fixed;
        top: 20px;
        right: 20px;
        padding: 15px;
        border-radius: 5px;
        background-color: #44bb44;
        color: white;
        z-index: 100;
        transform: translateX(200%);
        transition: transform 0.5s ease;
      }
      #notification.show {
        transform: translateX(0);
      }
      .hidden {
        display: none;
      }
      @media (max-width: 600px) {
        .player-grid {
          grid-template-columns: repeat(2, 1fr);
        }
        .controls button {
          margin: 5px 0;
        }
      }
    </style>
  )";
}

String getHTMLJavaScript() {
  return R"(
    <script>
      document.addEventListener('DOMContentLoaded', function() {
        // Functions to show notifications
        function showNotification(message, isSuccess = true) {
          const n = document.getElementById('notification');
          n.textContent = message;
          n.style.backgroundColor = isSuccess ? '#44bb44' : '#bb4444';
          n.classList.add('show');
          setTimeout(() => n.classList.remove('show'), 3000);
        }
        
        // Function to refresh game state
        function refreshGameState() {
          fetch('/api/state')
            .then(response => response.json())
            .then(data => {
              // Update game status
              document.getElementById('game-status').textContent = data.gameStatus;
              
              // Update counts
              document.getElementById('alive-count').textContent = data.aliveCount;
              document.getElementById('votes-needed').textContent = data.votesNeeded;
              document.getElementById('votes-available').textContent = data.votesAvailable;
              
              // Update role breakdown
              document.getElementById('townsfolk-count').textContent = data.townsfolk;
              document.getElementById('outsiders-count').textContent = data.outsiders;
              document.getElementById('minions-count').textContent = data.minions;
              
              // Update player display based on game state
              document.querySelectorAll('.player').forEach((player, index) => {
                // Only show active players during game
                if (data.gameStarted && data.playerStates[index] === 0) {
                  player.classList.add('hidden');
                } else {
                  player.classList.remove('hidden');
                  
                  // Update status class
                  player.className = player.className.replace(/status-\w+/g, '');
                  
                  if (data.gameEnded) {
                    player.classList.add(data.evilWon ? 'status-evil' : 'status-good');
                  } else {
                    if (data.playerStates[index] === 1) {
                      player.classList.add(data.isTraveller[index] ? 'status-yellow' : 'status-green');
                    } else if (data.playerStates[index] === 2) {
                      player.classList.add('status-blue');
                    } else if (data.playerStates[index] === 3) {
                      player.classList.add('status-purple');
                    } else {
                      player.classList.add('status-inactive');
                    }
                  }
                  
                  // Update player name displays
                  const statusBtn = player.querySelector('.toggle-status');
                  if (statusBtn) {
                    if (!data.playerActive[index] || data.playerStates[index] === 0) { // NOT_IN_PLAY
                      statusBtn.textContent = "Add Player";
                    } else if (data.playerStates[index] === 1) { // ALIVE
                      statusBtn.textContent = "Kill";
                    } else if (data.playerStates[index] === 2) { // DEAD_WITH_VOTE
                      statusBtn.textContent = "Remove Vote";
                    } else if (data.playerStates[index] === 3) { // DEAD_NO_VOTE
                      // If game is in progress, don't show "Remove" option
                      statusBtn.textContent = data.gameStarted ? "Dead" : "Remove";
                      // Optionally disable the button if game is in progress
                      statusBtn.disabled = data.gameStarted;
                      if (data.gameStarted) {
                        statusBtn.classList.add('btn-inactive');
                      } else {
                        statusBtn.classList.remove('btn-inactive');
                      }
                    }
                  }
                  // Update resurrection button visibility based on player status
                  const resurrectIcon = player.querySelector('.resurrect-icon');
                  if (resurrectIcon) {
                    if (data.playerStates[index] === 2 || data.playerStates[index] === 3) { // DEAD_WITH_VOTE or DEAD_NO_VOTE
                      resurrectIcon.style.display = 'inline-block';
                    } else {
                      resurrectIcon.style.display = 'none';
                    }
                  }
                  const nameInput = player.querySelector('.player-name');
                  if (nameInput) {
                    // Only update the name if the input is not currently focused
                    if (document.activeElement !== nameInput) {
                      nameInput.value = data.playerNames[index];
                    }
                    nameInput.disabled = data.playerActive[index];
                  }

                  // Update traveller checkbox
                  const travellerCheckbox = player.querySelector('input[type="checkbox"]');
                  if (travellerCheckbox) {
                    travellerCheckbox.checked = data.isTraveller[index];
                  }
                }
              });
              
              // Update visible action buttons based on game state
              const startGameBtn = document.getElementById('btn-start-game');
              const resetGameBtn = document.getElementById('btn-reset-game');
              const goodWinsBtn = document.getElementById('btn-good-wins');
              const evilWinsBtn = document.getElementById('btn-evil-wins');
              
              if (data.gameStarted && !data.gameEnded) {
                startGameBtn.classList.add('hidden');
                resetGameBtn.classList.remove('hidden');
                goodWinsBtn.classList.remove('hidden');
                evilWinsBtn.classList.remove('hidden');
              } else if (data.gameEnded) {
                startGameBtn.classList.add('hidden');
                resetGameBtn.classList.remove('hidden');
                goodWinsBtn.classList.add('hidden');
                evilWinsBtn.classList.add('hidden');
              } else {
                startGameBtn.classList.remove('hidden');
                resetGameBtn.classList.add('hidden');
                goodWinsBtn.classList.add('hidden');
                evilWinsBtn.classList.add('hidden');
              }
            })
            .catch(error => {
              console.error('Error fetching game state:', error);
            });
        }
        
        // Set up interval to refresh game state
        setInterval(refreshGameState, 3000);
        refreshGameState(); // Initial load
        
        // Add event listeners for action buttons
        document.querySelectorAll('.ajax-btn').forEach(button => {
          button.addEventListener('click', function(e) {
            e.preventDefault();
            const url = this.getAttribute('data-url');
            const confirmMsg = this.getAttribute('data-confirm');
            
            if (confirmMsg && !confirm(confirmMsg)) {
              return;
            }
            
            fetch(url)
              .then(response => {
                if (response.ok) {
                  showNotification('Action completed successfully');
                  refreshGameState();
                } else {
                  throw new Error('Server error');
                }
              })
              .catch(error => {
                showNotification('Error: ' + error.message, false);
              });
          });
        });
        
        // Handle toggle status clicks
        document.querySelectorAll('.toggle-status').forEach(button => {
        button.addEventListener('click', function(e) {
          e.preventDefault();
          const playerId = this.getAttribute('data-player');
          const player = this.closest('.player');
          const nameInput = player.querySelector('.player-name');
    
          // Only include name if the player is being added
          let url = `/api/toggle?i=${playerId}`;
          if (nameInput && !nameInput.disabled) {
            url += `&name=${encodeURIComponent(nameInput.value)}`;
          }
    
          fetch(url)
            .then(response => {
            if (response.ok) {
              refreshGameState();
            } else {
              throw new Error('Server error');
            }
          })
        .catch(error => {
          showNotification('Error: ' + error.message, false);
        });
      });
    });
        
        // Handle traveller checkboxes
        document.querySelectorAll('.traveller-checkbox input').forEach(checkbox => {
          checkbox.addEventListener('change', function() {
            const playerId = this.getAttribute('data-player');
            const isChecked = this.checked ? 1 : 0;
            
            fetch(`/api/traveller?i=${playerId}&t=${isChecked}`)
              .then(response => {
                if (response.ok) {
                  refreshGameState();
                } else {
                  throw new Error('Server error');
                }
              })
              .catch(error => {
                showNotification('Error: ' + error.message, false);
              });
          });
        });
        document.querySelectorAll('.player-name').forEach(input => {
          input.addEventListener('blur', function() {
            // Only save if the player is not active yet
            if (!this.disabled) {
              const playerId = this.getAttribute('data-player');
              const playerName = this.value.trim();
              
              // Skip empty names
              if (!playerName) return;
              
              fetch(`/api/name?i=${playerId}&name=${encodeURIComponent(playerName)}`)
                .then(response => {
                  if (!response.ok) {
                    throw new Error('Server error');
                  }
                })
                .catch(error => {
                  showNotification('Error saving name: ' + error.message, false);
                });
            }
          });
        });
        // Handle resurrection button clicks
        document.querySelectorAll('.resurrect-icon').forEach(button => {
          button.addEventListener('click', function(e) {
            e.preventDefault();
            const playerId = this.getAttribute('data-player');

            fetch(`/api/resurrect?i=${playerId}`)
              .then(response => response.json())
              .then(data => {
                if (data.needsEnable) {
                  // Immediately re-enable the status button for this player
                  const playerCard = this.closest('.player');
                  if (playerCard) {
                    const statusBtn = playerCard.querySelector('.toggle-status');
                    if (statusBtn) {
                      statusBtn.disabled = false;
                      statusBtn.classList.remove('btn-inactive');
                      statusBtn.textContent = "Kill"; // Since they're now alive
                    }
                  }
                }
                refreshGameState(); // Still refresh the full state
              })
              .catch(error => {
                console.error('Error:', error);
                showNotification('Error: ' + error.message, false);
                refreshGameState(); // Refresh anyway to keep UI in sync
              });
          });
        });
      });
    </script>
  )";
}

String generateHTMLPage() {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>BOTC Townsquare " + String(APP_VERSION) + "</title>";
  html += getHTMLStyles();
  html += "</head><body>";
  html += "<h1>BOTC Townsquare " + String(APP_VERSION) + "</h1>";
  
  // Game Controls Section
  html += "<div class='controls'>";
  html += "<button id='btn-start-game' class='btn btn-primary ajax-btn' data-url='/api/start'>Start Game</button>";
  html += "<button id='btn-reset-game' class='btn btn-danger ajax-btn hidden' data-url='/api/reset' data-confirm='Are you sure you want to reset the game?'>Reset Game</button>";
  html += "<button id='btn-good-wins' class='btn btn-success ajax-btn hidden' data-url='/api/end?result=good'>Good Wins</button>";
  html += "<button id='btn-evil-wins' class='btn btn-danger ajax-btn hidden' data-url='/api/end?result=evil'>Evil Wins</button>";
  html += "<button class='btn btn-danger ajax-btn' data-url='/api/clear' data-confirm='Delete all saved state and restart?'>Clear All</button>";
  
  // Game status display
  html += "<div class='game-stats'>";
  html += "<div class='stat-box'><h3>Game Status</h3><p id='game-status'>Not Started</p></div>";
  
  // Player count stats
  html += "<div class='stat-box'><h3>Player Counts</h3>";
  html += "<p>Alive: <span id='alive-count'>0</span></p>";
  html += "<p>Votes Needed: <span id='votes-needed'>0</span></p>";
  html += "<p>Votes Available: <span id='votes-available'>0</span></p></div>";
  
  // Role breakdown
  html += "<div class='stat-box'><h3>Role Breakdown</h3>";
  html += "<p>Townsfolk: <span id='townsfolk-count'>3</span></p>";
  html += "<p>Outsiders: <span id='outsiders-count'>0</span></p>";
  html += "<p>Minions: <span id='minions-count'>1</span></p>";
  html += "<p>Demon: 1</p></div>";
  
  html += "</div>"; // End game-stats
  html += "</div>"; // End controls
  
  // Player grid
  html += "<div class='player-grid'>";
  for (int i = 0; i < NUM_PLAYERS; i++) {
    String statusClass = "status-inactive";
    if (gameEnded) {
      statusClass = evilWon ? "status-evil" : "status-good";
    } else {
      if (playerStates[i] == ALIVE) statusClass = isTraveller[i] ? "status-yellow" : "status-green";
      else if (playerStates[i] == DEAD_WITH_VOTE) statusClass = "status-blue";
      else if (playerStates[i] == DEAD_NO_VOTE) statusClass = "status-purple";
    }
    
    html += "<div class='player " + statusClass + "'>";
    
    html += "<input type='text' class='player-name' data-player='" + String(i) + "' value='" + playerNames[i] + "' " + (playerActive[i] ? "disabled" : "") + ">";

    
    // Status toggle button
    String btnText;
    String btnClass = "btn status-btn toggle-status";

    if (!playerActive[i] || playerStates[i] == NOT_IN_PLAY) {
      btnText = "Add Player";
    } else if (playerStates[i] == ALIVE) {
      btnText = "Kill";
    } else if (playerStates[i] == DEAD_WITH_VOTE) {
      btnText = "Remove Vote";
    } else if (playerStates[i] == DEAD_NO_VOTE) {
      // If game is in progress, don't show "Remove" option
      btnText = gameStarted ? "Dead" : "Remove";
      // Optionally disable the button if game is in progress
      if (gameStarted) {
        btnClass += " btn-inactive";
      }
    }

    html += "<button class='" + btnClass + "' data-player='" + String(i) + "'" + 
            (gameStarted && playerStates[i] == DEAD_NO_VOTE ? " disabled" : "") + ">" + 
            btnText + "</button>";
    
    // Traveller checkbox
    html += "<div class='traveller-checkbox'>";
    html += "<label><input type='checkbox' data-player='" + String(i) + "' " + (isTraveller[i] ? "checked" : "") + "> Traveller</label>";
    // Add resurrection icon/button as a span with a fixed width
    html += "<span class='resurrect-icon' data-player='" + String(i) + "' style='display: " + 
       ((playerStates[i] == DEAD_WITH_VOTE || playerStates[i] == DEAD_NO_VOTE) ? "inline-block" : "none") + 
       "' title='Resurrect player'></span>";
    html += "</div>";

    html += "</div>"; // End player
  }
  html += "</div>"; // End player-grid
  
  // Notification element
  html += "<div id='notification'></div>";
  
  // Include JavaScript
  html += getHTMLJavaScript();
  html += "<p><a href=\"/update\">OTA Update</a></p></body></html>";
  return html;
}

// API endpoint to get game state in JSON format
void handleAPIState() {
  int aliveCount = 0;
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (playerStates[i] == ALIVE) aliveCount++;
  }
  int votesNeeded = (aliveCount + 1) / 2;
  int votesAvailable = aliveCount;
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (playerStates[i] == DEAD_WITH_VOTE) votesAvailable++;
  }
  
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
  
  String gameStatus;
  if (gameEnded) {
    gameStatus = evilWon ? "EVIL WON" : "GOOD WON";
  } else {
    gameStatus = gameStarted ? "IN PROGRESS" : "NOT STARTED";
  }
  
  JsonDocument doc;
  doc["gameStarted"] = gameStarted;
  doc["gameEnded"] = gameEnded;
  doc["evilWon"] = evilWon;
  doc["gameStatus"] = gameStatus;
  doc["aliveCount"] = aliveCount;
  doc["votesNeeded"] = votesNeeded;
  doc["votesAvailable"] = votesAvailable;
  doc["townsfolk"] = townsfolk;
  doc["outsiders"] = outsiders;
  doc["minions"] = minions;
  
  JsonArray playerStatesArray = doc.createNestedArray("playerStates");
  JsonArray playerNamesArray = doc.createNestedArray("playerNames");
  JsonArray isTravellerArray = doc.createNestedArray("isTraveller");
  
  for (int i = 0; i < NUM_PLAYERS; i++) {
    playerStatesArray.add((int)playerStates[i]);
    playerNamesArray.add(playerNames[i]);
    isTravellerArray.add(isTraveller[i]);
  }
  JsonArray playerActiveArray = doc.createNestedArray("playerActive");
  for (int i = 0; i < NUM_PLAYERS; i++) {
    playerActiveArray.add(playerActive[i]);
  }
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}
// API: Toggle player state
void handleToggle() {
  if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NUM_PLAYERS) {
      playerActive[i] = true;
      
      // Prevent removing players if game is in progress
      if (gameStarted && playerStates[i] == DEAD_NO_VOTE) {
        // Do nothing - keep them as DEAD_NO_VOTE
      } else {
        // Normal cycle through states
        playerStates[i] = (PlayerStatus)((playerStates[i] + 1) % 4);
      }
      
      updateLEDs();
      saveState();
    }
  }
  server.send(200, "text/plain", "OK");
}

// API: Update player name
void handleName() {
  if (server.hasArg("i") && server.hasArg("name")) {
    int i = server.arg("i").toInt();
    String name = server.arg("name");
    if (i >= 0 && i < NUM_PLAYERS) {
      playerNames[i] = name;
      updateLEDs();
      saveState();
    }
  }
  server.send(200, "text/plain", "OK");
}

// API: Start game
void handleStart() {
  gameStarted = true;
  gameEnded = false;
  saveState();
  server.send(200, "text/plain", "OK");
  displayNeedsRefresh = true;
  updateDisplay();
}

// API: Reset game
void handleReset() {
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
  server.send(200, "text/plain", "OK");
  displayNeedsRefresh = true;
  updateDisplay();
}


// API: Resurrect
void handleResurrect() {
    if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NUM_PLAYERS) {
      // Set player to alive state
      playerActive[i] = true;
      playerStates[i] = ALIVE;
      updateLEDs();
      saveState();
      
      // In the response, include information that this player needs their button re-enabled
      String response = "{\"status\":\"OK\",\"playerId\":" + String(i) + ",\"needsEnable\":true}";
      server.send(200, "application/json", response);
      return;
    }
  }
  server.send(200, "text/plain", "OK");
}

// API: End game with result
void handleEnd() {
  if (server.hasArg("result")) {
    gameEnded = true;
    gameStarted = false;
    evilWon = server.arg("result") == "evil";
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
  server.send(200, "text/plain", "OK");
  displayNeedsRefresh = true;
  updateDisplay();
}

// API: Clear saved state and reset
void handleClear() {
  SPIFFS.remove(CONFIG_FILE);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();

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
  server.send(200, "text/plain", "OK");
  displayNeedsRefresh = true;
  updateDisplay();
}

// API: Mark as traveller
void handleTraveller() {
  if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NUM_PLAYERS) {
      isTraveller[i] = server.hasArg("t") && server.arg("t") == "1";
      updateLEDs();
      saveState();
    }
  }
  server.send(200, "text/plain", "OK");
}
void setupWebServer() {
  // Enable OTA update interface
  ElegantOTA.begin(&server); // Now compatible!

  // Main HTML page
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", generateHTMLPage());
  });

  // API routes
  server.on("/api/state", HTTP_GET, handleAPIState);
  server.on("/api/toggle", HTTP_GET, handleToggle);
  server.on("/api/name", HTTP_GET, handleName);
  server.on("/api/start", HTTP_GET, handleStart);
  server.on("/api/reset", HTTP_GET, handleReset);
  server.on("/api/end", HTTP_GET, handleEnd);
  server.on("/api/clear", HTTP_GET, handleClear);
  server.on("/api/traveller", HTTP_GET, handleTraveller);
  server.on("/api/resurrect", HTTP_GET, handleResurrect);

  // Static files (optional)
  server.serveStatic("/static", SPIFFS, "/static/");

  // 404 handler
  server.onNotFound([](){
    server.send(404, "text/plain", "Not found");
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
  Serial.println();
  Serial.println("=================================");
  Serial.println("BOTC Townsquare Controller");
  Serial.println(APP_VERSION);
  Serial.println("=================================");

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