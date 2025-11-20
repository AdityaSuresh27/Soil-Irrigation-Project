# 🌱 Smart Farm Monitoring System/Soil Irrigation Project

<div align="center">

**An intelligent agricultural monitoring system built with STM32 microcontroller**

[Features](#-features) • [Hardware](#-hardware-requirements) • [Setup](#-getting-started) • [Demo](#-system-demo) • [Documentation](#-documentation)

</div>

---

## 📖 Overview

Smart Farm is an embedded IoT system that monitors environmental conditions and soil moisture in real-time, providing intelligent watering recommendations to optimize plant growth. The system combines temperature, humidity, and soil moisture data to determine optimal irrigation levels, displayed through an intuitive OLED interface and LED indicators.

### 🎯 Key Highlights

- 🌡️ **Real-time Environmental Monitoring** - Tracks temperature and humidity using DHT11 sensor
- 💧 **Smart Soil Detection** - Digital moisture sensing with instant feedback
- 📊 **Visual Dashboard** - 128x64 OLED display with custom graphics
- 💡 **Intelligent Alerts** - Multi-colored LED system with blinking patterns
- ⚙️ **Bare-metal Implementation** - Register-level programming for maximum efficiency
- 🔄 **Adaptive Logic** - Context-aware watering recommendations

---

## ✨ Features

### Environmental Sensing
- **Temperature Monitoring**: 0-50°C range with ±2°C accuracy
- **Humidity Tracking**: 20-80% RH with ±5% accuracy
- **Soil Moisture Detection**: Binary dry/wet status
- **1-second Update Rate**: Near real-time monitoring

### Intelligent Watering System
The system analyzes multiple factors to recommend watering levels:

| Condition | Temperature | Humidity | Soil Status | Water Level |
|-----------|-------------|----------|-------------|-------------|
| 🔥 Critical | > 30°C | < 85% | Dry | **HIGH** |
| ⚠️ Warning | > 30°C OR < 85% | - | Dry | **MEDIUM** |
| ℹ️ Notice | Normal | Normal | Dry | **LOW** |
| ✅ Good | Any | Any | Wet | **OFF** |

### Visual Feedback

#### OLED Display
```
┌─────────────────────────┐
│  28°C                   │  ← Large temp display
│  Hum: 65%               │  ← Medium humidity
│  Water: MED  [■][■][ ]  │  ← Status + bar graph
│  Soil: DRY              │  ← Soil indicator
└─────────────────────────┘
```

#### LED Indicators
- 🔴 **LED_TEMP (PB0)**: Environmental stress indicator
  - Fast blink: Both temp high AND humidity low
  - Steady: Either condition present
  - Off: Normal conditions

- 💙 **LED_WATER (PB2)**: Watering recommendation
  - Fast blink (100ms): High water needed
  - Slow blink (500ms): Medium water needed
  - Steady: Low water needed
  - Off: No watering required

- 🟡 **LED_SOIL (PA8)**: Soil moisture status
  - On: Soil is dry
  - Off: Soil is wet

---

## 🔧 Hardware Requirements

### Main Components

| Component | Model | Quantity | Purpose |
|-----------|-------|----------|---------|
| Microcontroller | STM32F401CCU6 | 1 | Main processing unit |
| Programmer | ST-Link V2 | 1 | Firmware flashing |
| Temp/Humidity Sensor | DHT11 | 1 | Environmental monitoring |
| Soil Sensor | HS-S33A | 1 | Moisture detection |
| Display | SSD1306 128x64 OLED | 1 | Data visualization |
| LEDs | Standard 5mm | 3 | Status indicators |
| Resistors | 220Ω | 3 | LED current limiting |
| Breadboard/PCB | - | 1 | Circuit assembly |
| Jumper Wires | - | As needed | Connections |

### Pin Configuration

```
┌─────────────────────────────────┐
│       STM32F401CCU6             │
│                                 │
│  PA3 ←────────→ DHT11 Data      │
│  PA5 ←────────→ Soil Sensor     │
│  PA8 ─────────→ LED_SOIL        │
│  PB0 ─────────→ LED_TEMP        │
│  PB2 ─────────→ LED_WATER       │
│  PB6 ←────────→ OLED SCL (I2C1) │
│  PB7 ←────────→ OLED SDA (I2C1) │
│                                 │
└─────────────────────────────────┘
```

### Circuit Diagram

```
         +3.3V
           │
           ├──────[DHT11]──── PA3
           │
           ├──────[Soil]───── PA5
           │
           ├──[220Ω]──[LED]── PB0 (Temp)
           │
           ├──[220Ω]──[LED]── PB2 (Water)
           │
           ├──[220Ω]──[LED]── PA8 (Soil)
           │
           └──[OLED Display]
                  │     │
                 PB6   PB7
                (SCL) (SDA)
```

---

## 🚀 Getting Started

### Prerequisites

- **Hardware**: All components listed above
- **Software**: 
  - STM32CubeIDE / Keil MDK / IAR EWARM
  - ST-Link utility
  - ARM GCC toolchain
- **Drivers**: ST-Link drivers installed

### Installation

1. **Clone the Repository**
   ```bash
   git clone https://github.com/yourusername/smart-farm-stm32.git
   cd smart-farm-stm32
   ```

2. **Hardware Setup**
   - Assemble the circuit according to the pin configuration
   - Connect ST-Link V2 to STM32 (SWDIO, SWCLK, GND, 3.3V)
   - Power the system via USB or external 3.3V supply

3. **Compile the Code**
   ```bash
   # Using ARM GCC
   arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 \
     -DSTM32F401xC -c main.c -o main.o
   arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb \
     -T STM32F401CCUX_FLASH.ld main.o -o firmware.elf
   arm-none-eabi-objcopy -O binary firmware.elf firmware.bin
   ```

4. **Flash the Firmware**
   ```bash
   st-flash write firmware.bin 0x8000000
   ```
   Or use your IDE's built-in flash tool.

5. **Run the System**
   - Reset the board
   - Watch the "SMART FARM" splash screen
   - Monitor begins after 600ms

---

## 🎬 System Demo

### Startup Sequence
```
[Power On] → [Splash Screen: "SMART FARM"] → [Start Monitoring]
```

### Normal Operation
```
Read DHT11 → Read Soil → Calculate Water Level → Update LEDs → Update Display
     ↑                                                                │
     └────────────────────── Loop every 1 second ────────────────────┘
```

### Sample Output Scenarios

**Scenario 1: Hot & Dry Environment**
```
OLED Display:          LED Status:
  32°C                 🔴 TEMP: Fast Blink
  Hum: 70%             💙 WATER: Fast Blink
  Water: HIGH [■][■][■] 🟡 SOIL: ON
  Soil: DRY
```

**Scenario 2: Optimal Conditions**
```
OLED Display:          LED Status:
  24°C                 🔴 TEMP: OFF
  Hum: 88%             💙 WATER: OFF
  Water: OFF [ ][ ][ ] 🟡 SOIL: OFF
  Soil: WET
```

---

## 📚 Documentation

### Code Structure

```
main.c
├── Configuration & Definitions
├── Timing Functions (SysTick, DWT)
├── I2C Driver (100 kHz)
├── SSD1306 Display Driver
│   ├── Command functions
│   ├── Graphics primitives
│   └── Font rendering (5x7 with scaling)
├── DHT11 Sensor Driver
│   ├── Pin control
│   ├── Protocol implementation
│   └── Data validation
├── GPIO Initialization
├── Display Functions
│   ├── Water bar graph
│   └── Layout manager
└── Main Control Loop
```

### Key Algorithms

**DHT11 Communication Protocol**
```c
1. Host: Pull low 20ms → Pull high 30μs
2. DHT11: Respond 80μs low → 80μs high
3. Data: 40 bits (5 bytes)
   - Bit '0': 50μs high
   - Bit '1': 70μs high
4. Validate: checksum = sum(bytes 0-3)
```

**Water Level Decision Tree**
```
if (temp > 30°C && humidity < 85% && soil_dry)
    → WATER_HIGH
else if ((temp > 30°C || humidity < 85%) && soil_dry)
    → WATER_MED
else if (soil_dry)
    → WATER_LOW
else
    → WATER_OFF
```

### Customization

Modify thresholds in the configuration section:
```c
#define TEMP_THRESHOLD         30   // Temperature alert (°C)
#define HUMIDITY_THRESHOLD_LOW 85   // Humidity alert (%)
```

Change pin assignments:
```c
#define DHT11_PIN 3    // PAx
#define SOIL_PIN  5    // PAx
#define LED_TEMP  0    // PBx
#define LED_WATER 2    // PBx
#define LED_SOIL  8    // PAx
```

---

## 🔬 Technical Specifications

| Parameter | Value |
|-----------|-------|
| MCU | STM32F401CCU6 (ARM Cortex-M4) |
| Clock Speed | 84 MHz |
| Flash Memory | 256 KB |
| RAM | 64 KB |
| Operating Voltage | 3.3V |
| Current Draw | ~80-100mA typical |
| Temperature Range | 0-50°C (sensor limit) |
| Humidity Range | 20-80% RH (sensor limit) |
| Sampling Rate | 1 Hz |
| Display Resolution | 128x64 pixels |
| I2C Speed | 100 kHz (standard mode) |

---

## 🛠️ Troubleshooting

### Common Issues

**Problem**: Display shows nothing
- ✅ Check I2C connections (PB6/PB7)
- ✅ Verify OLED address (0x3C)
- ✅ Check power supply (3.3V)

**Problem**: DHT11 always fails
- ✅ Wait 1 second after power-on
- ✅ Check data pin connection (PA3)
- ✅ Verify pull-up resistor (~4.7kΩ)

**Problem**: Soil sensor always dry/wet
- ✅ Check sensor orientation in soil
- ✅ Verify digital output connection
- ✅ Test sensor independently

**Problem**: LEDs don't blink
- ✅ Check current-limiting resistors
- ✅ Verify GPIO pin assignments
- ✅ Test with multimeter

---

## 🎯 Future Enhancements

- [ ] Add real-time clock for scheduling
- [ ] Implement data logging to SD card
- [ ] Wi-Fi/Bluetooth connectivity for remote monitoring
- [ ] Mobile app integration
- [ ] Additional sensors (light, CO2, pH)
- [ ] Automatic pump/valve control
- [ ] Multi-zone monitoring
- [ ] Machine learning for pattern recognition
- [ ] Battery operation with low-power modes
- [ ] Weather forecast integration

---

**⭐ Star this repo if you find it helpful!**

Made with ❤️ for the maker community

</div>
