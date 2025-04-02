# Blood on the Clocktower Town Square Controller

## Overview

This project provides a digital controller for managing Blood on the Clocktower games. It uses an ESP32-C6 microcontroller with a color LCD display and WS2812 LED strips to visualize player status in the game.

![Controller Overview](images/controller.jpg)

## Features

- **LED Player Status Indicators**: Controls WS2812 RGB LEDs for up to 20 players (3 LEDs per player)
- **Color-coded Status**: Visually represents player states (alive, dead with vote, dead without vote)
- **ST7789 Display**: Shows game status, counts, and role distribution
- **Web Interface**: Allows control via a responsive web interface accessible from any device
- **Adaptive Role Distribution**: Automatically calculates appropriate role numbers based on player count
- **Traveller Support**: Marks players as travellers with distinct coloring
- **Resurrection Feature**: Allows killed players to be brought back to life with a simple click on the ✨ icon
- **Game Ending Events**: Supports good/evil win declarations with visual feedback
- **Persistent State**: Saves game state between power cycles using SPIFFS

![Web Interface](images/web-interface.png)

## Hardware Requirements

- ESP32-C6 board
- ST7789 1.9" color display (172x320)
- WS2812 LED strip (60 LEDs for a standard 20-player game)
- 5V power supply capable of powering all LEDs at full brightness

## Pin Configuration

| Purpose | Pin |
|---------|-----|
| TFT CS   | GPIO14 |
| TFT DC   | GPIO15 |
| TFT RST  | GPIO21 |
| TFT MOSI | GPIO6  |
| TFT SCLK | GPIO7  |
| TFT BL   | GPIO22 |
| LED Data | GPIO8  |

## Installation

1. Clone this repository
2. Open the project in the Arduino IDE or PlatformIO
3. Install the required libraries:
   - WiFiManager
   - FastLED
   - ArduinoJson
   - Adafruit_GFX
   - Adafruit_ST7789
4. Configure board partitioning to "Minimal SPIFFS with OTA" to ensure enough space for web files and OTA updates
5. Upload the code to your ESP32-C6

## First-time Setup

1. Power on the device
2. Connect to the "Clocktower-Setup" WiFi network that appears
3. Follow the captive portal instructions to connect the device to your local WiFi

## Using the Controller

### Web Interface

1. Access the controller by entering its IP address in a web browser
2. Use the interface to:
   - Add/remove players
   - Set player names
   - Mark players as travellers
   - Toggle player status (alive/dead with vote/dead without vote)
   - Resurrect dead players with the ✨ icon
   - Start/end games
   - Declare winners

### LED Indicators

- **Green**: Player is alive
- **Blue**: Player is dead but has a vote
- **Purple**: Player is dead and has no vote
- **Yellow**: Player is a traveller
- **Red/Blue**: Game over animation (shows winning team color)

## Game Status Display

The ST7789 display shows:
- Current game state (Not Started/In Progress/Good Won/Evil Won)
- Player counts and voting information
- Role distribution based on player count

## Customization

The code includes extensive options for customizing:
- Number of players (default: 20)
- LEDs per player (default: 3)
- UI colors and styling
- Game logic parameters

## Troubleshooting

- If the device doesn't appear on your network, check the display for the IP address
- If LEDs don't light up correctly, verify your power supply can handle the current draw
- If the web interface is unresponsive, try restarting the device or checking if the correct IP is being used

## Acknowledgments

This project is designed for use with Blood on the Clocktower, a social deduction game by The Pandemonium Institute.
