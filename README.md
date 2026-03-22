Oto kompletny, profesjonalny plik `README.md` napisany w języku angielskim, gotowy do wklejenia do Twojego repozytorium.

Ten dokument uwzględnia wszystkie szczegóły techniczne z Twoich plików (`main.cpp`, `schematv2`, `platformio.ini`), strukturę katalogów oraz logikę działania (fuzja sensorów, algorytmy spalania).

---

# ESP32 Hyundai Tiburon CAN Bus Multi-Gauge

*Note: Replace `gauge_assets/preview.jpg` with a real photo of your device.*

## 📖 Overview

This project is a modern, digital replacement for the factory analog "Multigauge" (Torque/Fuel/Volts) found in the **Hyundai Tiburon / Coupe (GK FL 2002-2008)**.

Powered by an **ESP32** and a round **GC9A01 IPS Display**, this device acts as a passive CAN bus sniffer. It interprets raw data from the **Siemens SIMK43 / SIMK4x ECU** to display real-time telemetry with a smooth, 60 FPS interface built on **LVGL**.

### 🚀 Key Features

* **🏎 Real-Time Telemetry:** Reads Coolant Temperature, Speed, and RPM directly from the CAN Bus.
* **⛽ Dynamic Fuel Consumption:**
* **Idle:** Displays **L/h** (Liters per hour).
* **Driving:** Displays **L/100km** (Average) with a custom "Time Window Accumulation" algorithm for stability.


* **⚡ Hybrid Voltmeter (Sensor Fusion):** Combines fast hardware ADC readings (via voltage divider) with stable ECU voltage reports (via CAN) using a weighted average algorithm (70% Hardware / 30% CAN).
* **🌙 Auto Night Mode:** Detects the vehicle's illumination signal (ILL) to automatically dim the display and change the UI color palette.
* **🖥 Smooth UI:** Optimized SPI communication (80MHz) running the LVGL library for fluid animations.

---

## 🛠 Hardware Required (BOM)

To build this project, you need the following components. The design is intended to fit into the factory gauge housing.

| Component | Description | Notes |
| --- | --- | --- |
| **Microcontroller** | **ESP32 NodeMCU-32s** | Do not use ESP8266 (not enough RAM/Pins). |
| **Display** | **1.28" GC9A01 Round LCD** | SPI Interface version. |
| **CAN Transceiver** | **SN65HVD230** | 3.3V Logic. **⚠️ Remove the 120Ω resistor if present!** |
| **Power Supply** | **DC-DC Buck Converter** | 12V to 5V (e.g., LM2596 or Mini-360). |
| **Capacitors** | **100µF / 25V** & **100µF / 10V** | Input/Output filtering for the Buck Converter. |
| **Resistors** | **100kΩ & 20kΩ** | For Voltage Divider (1:6 ratio). |
| **Resistors** | **20kΩ** | For Illumination input protection. |
| **Diode** | **3.3V Zener Diode** | Protection for GPIO 13 (ILL). |
| **Capacitor** | **1µF** | Smoothing capacitor for the Voltmeter ADC. |

---

## 🔌 Wiring Diagram & Pinout

This project connects to the factory **M15-B connector** (Multigauge plug). Refer to `schematv2.pdf` in the `docs/` folder for the visual schematic.

### 1. Power Supply Section

| Source (Car Plug) | Component | Destination (ESP32) |
| --- | --- | --- |
| **Pin 3 (IGN +12V)** | → **Buck Converter IN+** |  |
| **Pin 12 (GND)** | → **Buck Converter IN-** |  |
| **Buck Converter OUT+** | → | **ESP32 VIN (5V)** |
| **Buck Converter OUT-** | → | **ESP32 GND** |

*> **Note:** Ensure grounds (Car GND, ESP32 GND, CAN Module GND) are all connected together.*

### 2. CAN Bus Interface (SN65HVD230)

| Car Plug | Module Pin | ESP32 Pin | Logic |
| --- | --- | --- | --- |
| **Pin 7 (CAN H)** | **H** | - | - |
| **Pin 1 (CAN L)** | **L** | - | - |
| - | **CTX** | **GPIO 17** | TX |
| - | **CRX** | **GPIO 16** | RX |
| - | **3V3** | **3V3** | Power |
| - | **GND** | **GND** | Ground |

### 3. Sensors & Inputs

| Function | Car Plug Pin | Circuit Logic | ESP32 Pin |
| --- | --- | --- | --- |
| **Voltmeter** | **Pin 6 (BAT +12V)** | Divider: 100kΩ (Series) / 20kΩ (GND) + 1µF Cap | **GPIO 34** |
| **Night Mode** | **Pin 9 (ILL +12V)** | Resistor: 20kΩ (Series) + 3.3V Zener (to GND) | **GPIO 13** |

### 4. Display (GC9A01)

| Display Pin | ESP32 Pin | Function |
| --- | --- | --- |
| **SDA (MOSI)** | **GPIO 23** | SPI Data |
| **SCL (SCK)** | **GPIO 18** | SPI Clock |
| **CS** | **GPIO 5** | Chip Select |
| **DC** | **GPIO 2** | Data/Command |
| **RES** | **GPIO 4** | Reset |
| **VCC** | **3V3** | Power |
| **GND** | **GND** | Ground |

---

## 🖨 3D Enclosure

Files for 3D printing a housing that fits directly into the Tiburon's dashboard are located in the `stl/` directory.

* **Recommended Material:** **PETG**, **ASA**, or **ABS**.
* *Do not use PLA, as it will deform in a hot car interior.*


* **Layer Height:** 0.16mm or 0.2mm.
* **Infill:** 20-40%.

---

## 💻 Installation & Firmware

This project uses **PlatformIO** within VS Code.

1. **Clone the Repository:**
```bash
git clone https://github.com/your-username/esp32-tiburon-gauge.git

```


2. **Open in PlatformIO:**
Open the project folder in Visual Studio Code.
3. **Configure Port:**
Check `platformio.ini`. Ensure the upload port matches your ESP32 (e.g., `COM10` or `/dev/ttyUSB0`).
```ini
upload_port = COM10

```


4. **Build & Upload:**
Click the **PlatformIO: Upload** button (Arrow icon).

### Configuration (`main.cpp`)

You can toggle specific features at the top of `src/main.cpp`:

* `#define DEBUG_CAN_SNIFFER true/false`: Set to `true` to view raw CAN frames in the Serial Monitor (115200 baud). Set to `false` for normal operation (better performance).
* `#define VOLT_CALIBRATION 6.532f`: Adjust this value to calibrate the voltage divider if the screen reading slightly differs from a multimeter.

---

## ⚙️ Calibration

### 1. Temperature Offset

If the displayed temperature differs from a professional OBD2 scanner:

* Go to `src/main.cpp`.
* Find the `case 0x329:` block inside `processCAN()`.
* Adjust the offset in the formula: `(HEX * 0.75) - 48.0`.
* Example: Change `- 48.0` to `- 46.0` to increase the reading by 2°C.



### 2. Voltage Reading

The voltmeter uses a "Sensor Fusion" logic:

* **70% Weight:** Hardware ADC (GPIO 34).
* **30% Weight:** CAN Bus data (ID 0x545).
If the reading is consistently off, adjust the `VOLT_CALIBRATION` define in `main.cpp`.

---

## 📂 Repository Structure

```
├── .pio/               # PlatformIO build files
├── docs/               # Schematics and documentation
├── gauge_assets/       # Project images and assets
├── lib/                # Local libraries
├── src/                # Source code
│   ├── main.cpp        # Main logic, CAN sniffer, Sensor Fusion
│   ├── logic.cpp       # Gauge behavior, colors, calculations
│   ├── ui.c            # LVGL generated UI code
│   ├── components/     # UI Components
│   └── images/         # UI Bitmaps
├── stl/                # 3D Printable enclosure files (.stl)
└── platformio.ini      # Project configuration

```

## 📜 License

This project is open-source. Feel free to modify and adapt it for your own vehicle.

* **Credits:** UI designed using SquareLine Studio.
* **CAN Decoding:** Based on open documentation for Siemens SIMK43 ECU. https://opengk.org/index.php?title=CAN_Bus_messages