# Jonsbo N4 Screen

A custom display system for Jonsbo N4 PC case, built with ESP32-P4 and LVGL, designed to monitor and display system information in real-time.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-green.svg)
![LVGL](https://img.shields.io/badge/LVGL-v9-orange.svg)

## 📋 Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation](#installation)
- [Building](#building)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [Configuration](#configuration)
- [Contributing](#contributing)
- [License](#license)
- [Links](#links)

## ✨ Features

- Real-time system monitoring (CPU, GPU, RAM, temperatures, etc.)
- Beautiful LVGL-based user interface
- Touch screen support (GT911)
- MIPI DSI display support
- Custom sensor data integration
- Python server for sensor data collection
- LVGL simulator for development

## Screenshot
image.png

## 🔧 Hardware Requirements

- **MCU**: ESP32-P4
- **Display**: MIPI DSI LCD (JD9365 controller with ST7701)
- **Touch**: GT911 capacitive touch controller
- **Flash**: 16MB
- **PSRAM**: External PSRAM (200MHz)
- **Case**: Jonsbo N4 Front Panel

> 📌 3D Model: [Jonsbo N4 Front Panel on Printables](https://www.printables.com/model/1298708-jonsbo-n4-front-panel)

> 📌 LCD Screen: [ESP32P4 development board ESP32-C6](https://ja.aliexpress.com/item/1005009618259341.html?spm=a2g0o.order_list.order_list_main.47.4aea1802AEhWxO&gatewayAdapt=glo2jpn)

## 💻 Software Requirements

- **ESP-IDF**: v5.5 or later
- **Python**: 3.11+ (for sensor server)
- **CMake**: 3.16 or later
- **Git**: For cloning dependencies
- **UI Design**: [GUI Guider](https://www.nxp.jp/design/design-center/software/development-software/gui-guider:GUI-GUIDER)

### ESP-IDF Installation

1. Download and install ESP-IDF v5.5:
   ```bash
   # Windows
   C:\Espressif\frameworks\esp-idf-v5.5\export.bat
   
   # Linux/Mac
   . $HOME/esp/esp-idf/export.sh
   ```

2. Verify installation:
   ```bash
   idf.py --version
   ```

## 📦 Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/keitetran/JonsboN4Monitor.git
   cd JonsboN4Monitor
   ```

2. Initialize submodules (if any):
   ```bash
   git submodule update --init --recursive
   ```

3. Install Python dependencies for sensor server:
   ```bash
   cd server
   pip install -r requirements.txt  # If requirements.txt exists
   ```

## 🏗️ Building

### Using idf.py (Recommended)

```bash
# Set target
idf.py set-target esp32p4

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash
idf.py -p COM3 flash

# Monitor
idf.py -p COM3 monitor
```

### Using CMake Directly

⚠️ **Note**: You must set up ESP-IDF environment first:

```bash
# Windows
C:\Espressif\frameworks\esp-idf-v5.5\export.bat

# Then run CMake
mkdir build
cd build
cmake ..
cmake --build .
```

## 🚀 Usage

### Running the Sensor Server

The Python server collects sensor data from the Linux system and sends it to the ESP32:

```bash
cd server
python read_sensor.py
```

### Simulator

For development and testing without hardware:

```bash
cd lvgl-simulator
make
./simulator
```

## 📁 Project Structure

```
JonsboN4Monitor/
├── main/                  # Main application code
├── custom/                # Custom LVGL port and host communication
├── generated/             # Auto-generated GUI code from GUI-Guider
│   ├── gui_guider.c/h     # Main GUI logic
│   ├── widgets_init.c/h   # Widget initialization
│   ├── events_init.c/h    # Event handlers
│   └── images/            # Embedded images
├── server/                # Python sensor server
│   ├── read_sensor.py     # Main sensor reader
│   ├── send_mock.py       # Mock data sender
│   └── snmpwalk.py        # SNMP sensor reader
├── lvgl/                  # LVGL graphics library
├── lvgl-simulator/        # LVGL simulator for development
├── docs/                  # Documentation and sensor mappings
├── import/                 # Assets (fonts, images)
│   ├── font/              # TTF font files
│   └── image/             # PNG/GIF images
├── CMakeLists.txt         # Main CMake configuration
├── sdkconfig.defaults     # Default ESP-IDF configuration
└── partitions.csv         # Flash partition table
```

## ⚙️ Configuration

### Display Configuration

The display is configured for:
- **Rotation**: 270 degrees
- **Resolution**: Defined by LCD controller
- **Refresh Rate**: 16ms (60 FPS)
- **Tear Avoidance**: Mode 3 enabled

### Sensor Mapping

Sensor data mapping is defined in `docs/sensor-mapping.txt`. The system supports:
- CPU temperature and usage
- GPU temperature and usage
- RAM usage
- System temperatures
- Fan speeds
- And more...

### Partition Table

Custom partition table is defined in `partitions.csv`:
- Bootloader
- NVS
- Application
- OTA partitions (if enabled)

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE.txt](LICENSE.txt) file for details.

## 🔗 Links

- [Jonsbo N4 Front Panel 3D Model](https://www.printables.com/model/1298708-jonsbo-n4-front-panel)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [LVGL Documentation](https://docs.lvgl.io/)
- [GUI-Guider](https://www.nxp.com/design/software/development-software/gui-guider)

## 📝 Notes

- Make sure to set `IDF_TARGET=esp32p4` before building
- The project uses LVGL v9 with custom port implementation
- Touch calibration may be required for first-time setup
- Sensor server must be running on the host system for data display

---

**Made with ❤️ for the Jonsbo N4 community**
