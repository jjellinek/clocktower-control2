# Blood on the Clocktower Town Square Controller - LED Strip Version

## Overview
This project is an ESP32-C6 based controller for managing a physical town square for the social deduction game "Blood on the Clocktower". It uses a single 40-LED WS2812 strip (using every other LED for players) to visually represent player status in the game, along with a color LCD display and web interface for game management.

## Hardware Components
- **ESP32-C6** microcontroller with ST7789 LCD (172x320)
- **40-LED WS2812 RGB Strip** (5V) - only uses every other LED for players (20 total player positions)
- **Logic Level Shifter** (3.3V to 5V) - TXS0108E recommended
- **Power supply** (5V, 2A minimum recommended)
- **Capacitor** (1000μF, 10V+) for power filtering
- **Resistor** (300-500Ω) for data line protection

## Features
- **LED Player Indicators**: Each player is represented by one LED with colors indicating their game status:
  - **Green**: Alive player
  - **Yellow**: Alive traveller  
  - **Blue**: Dead with voting token
  - **Purple**: Dead without voting token
  - **Red/Blue**: Game ended (Evil/Good win with full strip animation)
  
- **LED Mapping**: 
  - Player 1 → LED 0
  - Player 2 → LED 2  
  - Player 3 → LED 4
  - Player 4 → LED 6
  - ... and so on
  - LEDs 1, 3, 5, 7, etc. remain off (available for future effects)

- **Enhanced Animations**: Win conditions trigger full strip light shows using all 40 LEDs
  
- **ST7789 Display**: Shows game status information including:
  - Game state (Not started, In progress, Game over)
  - Win condition (if game ended)
  - Role distribution (Townsfolk, Outsiders, Minions, Demon)
  - LED strip configuration info
  
- **Web Interface**: Access via any browser connected to the same network
  - Responsive design that works on mobile devices
  - Real-time game state updates using AJAX
  - Player management (add/remove players, rename players)
  - Shows LED position for each player (e.g., "LED 0", "LED 2")
  - Traveller status toggling
  - Game state control (start/end/reset game)
  
- **WiFi Setup**:
  - Automatic WiFi manager with fallback access point mode
  - Creates "Clocktower-Setup" network if unable to connect to saved WiFi
  
- **Persistent Storage**:
  - Game state saved to SPIFFS even after power loss
  - Stores player names, statuses, and traveller flags

## Setup Instructions

### First Boot
1. Power on the device
2. Connect to the "Clocktower-Setup" WiFi network from your phone or computer
3. Follow the portal instructions to connect the device to your local WiFi
4. Note the IP address displayed on the ST7789 screen

## Hardware Setup

### Components Needed
- ESP32-C6 development board with integrated ST7789 display
- 1× WS2812 RGB LED Strip (40 LEDs, 5V, 60 LEDs/meter works well)
- TXS0108E Logic Level Shifter Module
- 5V 2A power supply (3A recommended for safety margin)
- 1000μF capacitor (10V or higher)
- 300-500Ω resistor
- 10kΩ pull-up resistor (optional)
- Breadboard or PCB for connections
- Wires and connectors

### Circuit Diagram

```
[5V Power Supply] ----+---- [1000μF Capacitor] ---- [GND]
                      |
                      +---- [LED Strip 5V] ---- [All 40 LEDs in series]
                      |                    |
                      +---- [TXS0108E VB]   +---- [LED Strip GND]
                      |                           |
[ESP32-C6 3.3V] ---- [TXS0108E VA] ---- [TXS0108E OE]
                      |                           |
[ESP32-C6 GPIO3] ---- [TXS0108E A1]               |
                      |                           |
[TXS0108E B1] ---- [300Ω Resistor] ---- [LED Strip Data In]
                      |                           |
[ESP32-C6 GND] ---- [TXS0108E GND] ---- [Common GND] ----+
```

### Wiring Steps

1. **Power Connections**:
   - Connect 5V power supply positive to LED strip 5V and TXS0108E VB pin
   - Connect all grounds together (power supply, ESP32, TXS0108E, LED strip)
   - Connect 1000μF capacitor across 5V and GND near the LED strip

2. **Level Shifter Setup**:
   - TXS0108E VA pin → ESP32-C6 3.3V
   - TXS0108E VB pin → 5V power supply
   - TXS0108E OE pin → 3.3V (enable the chip)
   - TXS0108E GND → Common ground

3. **Data Signal**:
   - ESP32-C6 GPIO3 → TXS0108E A1 pin
   - TXS0108E B1 pin → 300Ω resistor → LED strip data input

4. **ESP32-C6 Display**: Already integrated on the board with correct pin connections

### LED Strip Positioning

For a circular town square setup:
- Position LED 0 at "12 o'clock" (Player 1)
- LED 2 at "1 o'clock" position (Player 2)  
- LED 4 at "2 o'clock" position (Player 3)
- Continue clockwise around the circle
- The unused LEDs (1, 3, 5, etc.) can be positioned between players or used for decorative spacing

### Power Considerations

- **LED Power Draw**: Each WS2812 LED can draw up to 60mA at full brightness
- **Theoretical Maximum**: 40 LEDs × 60mA = 2.4A
- **Practical Usage**: With the color mixing used (~1A typical)
- **Recommended Supply**: 5V 2A minimum, 5V 3A for reliability and future expansion
- **Brightness Limiting**: Code sets brightness to 50% (128/255) by default to reduce power consumption

### Safety Notes

- **Power Supply Sizing**: Ensure your 5V supply can handle the current requirements
- **Voltage Drop**: For strips longer than 1 meter, consider injecting power at both ends
- **Capacitor Placement**: Keep the large capacitor close to the LED strip power input
- **Hot Plugging**: Never connect/disconnect while powered on

## Web Interface

The web interface now shows:
- LED position indicator for each player (e.g., "LED 0", "LED 2")
- LED strip configuration information
- Enhanced visual feedback during win animations

### Web Interface Access
1. Connect to the same WiFi network as the device
2. Open a web browser and enter the IP address shown on the display
3. The Blood on the Clocktower town square interface will load

## Game Management

### Player Status Control
- Each player card shows their LED position (LED 0, LED 2, LED 4, etc.)
- Click the status button to cycle through player states:
  - Not in play → Alive → Dead with vote → Dead without vote → Not in play
- Check the "Traveller" box to mark a player as a traveller (yellow LED instead of green)
- Enter a name to assign a player name

### Game Controls
- **Start Game**: Begins the game and updates the role distribution
- **Good/Evil Wins**: Ends the game and triggers the winning team's light show across all 40 LEDs
- **Reset Game**: Resets the game state without clearing player names
- **Clear All**: Completely resets all data including player names

### Enhanced Win Animations
When a game ends, the system now plays dramatic animations using all 40 LEDs:
- **Sequential Fill**: LEDs light up in sequence with the winning team's color
- **Flash Effect**: Full strip flashes 3 times
- **Return to Normal**: Returns to showing only active player states

## Technical Notes

### LED Configuration
- **Total LEDs**: 40 in a single WS2812 strip
- **Player LEDs**: Every other LED (0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38)
- **Unused LEDs**: 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39
- **Data Pin**: GPIO3
- **Operating Voltage**: 5V
- **Brightness**: Set to 50% (128/255) by default

### Display Configuration
- ST7789 172x320 display (integrated on ESP32-C6 board)
- Shows LED strip configuration and active LED count

### Customization Options

You can modify these constants in the code:
- `NUM_PLAYERS`: Maximum number of players (default 20)
- `NUM_LEDS`: Total LEDs in strip (default 40)
- `FastLED.setBrightness()`: Adjust brightness (default 128 = 50%)
- `getPlayerLEDIndex()`: Modify LED mapping if needed

### Advantages of LED Strip Design

1. **Simplified Wiring**: Single data connection instead of chaining multiple rings
2. **Better Power Distribution**: Single power injection point
3. **Enhanced Effects**: Can use unused LEDs for animations and effects
4. **Easier Mounting**: Single strip is easier to position in a circle
5. **Lower Cost**: Single strip typically costs less than 20 individual rings
6. **Future Expansion**: Unused LEDs available for special effects or indicators

## Troubleshooting

### Common Issues
- **No LEDs lighting**: Check 5V power supply and data signal connections
- **Wrong colors**: Verify GRB color order in FastLED configuration
- **Flickering**: Add or upgrade the 1000μF capacitor near LED strip power input
- **Partial strip working**: Check for voltage drop along the strip
- **WiFi issues**: Check display for error messages, use "Clear All" to reset

### LED Strip Specific Issues
- **Every other LED not working as expected**: Verify the `getPlayerLEDIndex()` function
- **Animations not smooth**: Check power supply capacity and brightness settings
- **Heat issues**: Reduce brightness or improve ventilation around power supply

### Power Supply Troubleshooting
- **LEDs dim**: Measure voltage at strip (should be 4.8V-5.2V under load)
- **ESP32 resets**: Use separate power supply or add more capacitance
- **Strip cutting out**: Upgrade power supply capacity

## Future Enhancements

With 20 unused LEDs available, potential additions include:
- **Vote countdown timer**: Use unused LEDs as a visual timer
- **Player spotlight**: Highlight current speaker
- **Phase indicators**: Show day/night phases
- **Custom role indicators**: Special colors for specific roles
- **Sound-reactive effects**: Respond to ambient noise levels

## Credits
Designed for use with "Blood on the Clocktower" by The Pandemonium Institute

## License
This project is open source and available under the MIT license.
