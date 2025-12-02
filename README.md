# Jonsbo N4 Screen

[English](README.md) | [Tiếng Việt](README.vi.md)

A custom display system for Jonsbo N4 PC case, built with ESP32-P4 and LVGL, designed to monitor and display system information in real-time.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-green.svg)
![LVGL](https://img.shields.io/badge/LVGL-v9-orange.svg)
![Platform](https://img.shields.io/badge/platform-ESP32--P4-red.svg)
![Status](https://img.shields.io/badge/status-active-success.svg)

## 🎯 Overview

The **Jonsbo N4 Screen** project transforms your PC case into a smart monitoring system with a beautiful, touch-enabled display. Built on the powerful ESP32-P4 microcontroller and leveraging LVGL's advanced graphics capabilities, this system provides real-time visualization of your computer's vital statistics.

### Why This Project?

- **Seamless Integration**: Designed specifically for Jonsbo N4 case front panel
- **Professional UI**: Created with NXP's GUI Guider for a polished, modern interface
- **Real-Time Data**: Monitor CPU, GPU, RAM, temperatures, and more at a glance
- **Open Source**: Fully customizable and extensible for your needs
- **Easy Development**: Includes simulator for testing without hardware

### Key Highlights

- 🎨 **Beautiful Interface**: Modern, responsive UI design with smooth animations
- 📊 **Comprehensive Monitoring**: Track all system metrics in real-time
- 🖐️ **Touch-Enabled**: Intuitive capacitive touch interface
- 🔌 **USB Communication**: Fast data transfer via USB CDC
- 🎮 **LVGL v9**: Latest graphics library with hardware acceleration
- 🛠️ **GUI Guider**: Visual UI editor for easy customization

## 📋 Table of Contents

- [Overview](#overview)
- [Screenshots](#screenshots)
- [Where to Get Hardware](#where-to-get-hardware)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Quick Start](#quick-start)
- [Installation](#installation)
- [Building](#building)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [Configuration](#configuration)
- [Architecture](#architecture)
- [Development](#development)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)
- [Links](#links)

## ✨ Features

### Display & Interface
- 🎨 **Modern UI**: Beautiful LVGL v9-based interface with smooth animations
- 🖐️ **Capacitive Touch**: GT911 touch controller for intuitive interaction
- 📺 **MIPI DSI Display**: High-quality display with JD9365 controller
- 🔄 **Responsive Design**: Optimized for 270-degree rotation
- ⚡ **60 FPS**: Smooth refresh rate at 16ms intervals

### System Monitoring
- 🖥️ **CPU Monitoring**: Real-time usage, temperature, and frequency
- 🎮 **GPU Tracking**: Graphics card stats and temperature
- 💾 **Memory Usage**: RAM and system memory statistics
- 🌡️ **Temperature Sensors**: CPU, GPU, and system temperatures
- 🌀 **Fan Control**: Monitor fan speeds and RPM
- 💿 **Storage Info**: Disk usage and I/O statistics
- 🔌 **Power Stats**: Power consumption monitoring (if available)

### Communication & Data
- 🔌 **USB CDC**: Fast USB communication for data transfer
- 🐍 **Python Server**: Flexible sensor data collection server (no pip dependencies!)
- 🖥️ **Synology NAS**: Native support with Task Scheduler integration
- 📡 **SNMP Support**: Network device monitoring capability
- 🔄 **Real-Time Updates**: Sub-second data refresh rates
- 📊 **Mock Data**: Testing mode with simulated data

### Development Tools
- 🖥️ **LVGL Simulator**: Test UI without hardware
- 🎨 **GUI Guider**: Visual editor for UI design
- 🔧 **CMake Build**: Standard build system
- 📝 **ESP-IDF v5.5**: Latest ESP framework support

## 📸 Screenshots

### Running Application
The monitor displaying real-time system information with a clean, modern interface.

![Application Running](screenshots/165955.png)

### GUI Guider Editor
Visual UI editor showing the design process using NXP's GUI Guider tool.

![GUI Guider UI Editor](screenshots/144454.png)

### Hardware Installation
The final product installed in Jonsbo N4 case, showing the display in action.

![Jonsbo N4 Monitor Display](screenshots/IMG_5105.jpg)

## 🛒 Where to Get Hardware

Ready to build your own? Here are the essential components:

### 🖨️ 3D Printed Case Adapter
> **Jonsbo N4 Front Panel Adapter**  
> Download the 3D model and print it yourself, or order from a 3D printing service.  
> 🔗 [Get 3D Model on Printables](https://www.printables.com/model/1501258-jonsbo-n4-front-panel-custom)

### 📺 ESP32-P4 Development Board with Display
> **ESP32-P4 with MIPI DSI LCD Display**  
> Complete development board with integrated display, touch controller, and ESP32-P4 MCU.  
> 🔗 [Buy on AliExpress](https://ja.aliexpress.com/item/1005009618259341.html?spm=a2g0o.order_list.order_list_main.47.4aea1802AEhWxO&gatewayAdapt=glo2jpn)

### 💡 What You'll Need
- ESP32-P4 development board (link above)
- 3D printed front panel adapter
- USB-C cable for power and data
- Jonsbo N4 PC case

## 🔧 Hardware Requirements

### Core Components

- **MCU**: ESP32-P4 (Dual-core RISC-V, up to 400MHz)
- **Display**: MIPI DSI LCD (JD9365 controller with ST7701)
- **Touch**: GT911 capacitive touch controller (I2C)
- **Flash**: 16MB external flash memory
- **PSRAM**: External PSRAM (200MHz, 8MB or more)
- **Interface**: USB-C for power and data communication
- **Case**: Jonsbo N4 Front Panel

### Specifications

| Component | Specification |
|-----------|--------------|
| MCU | ESP32-P4 @ 400MHz |
| Display | MIPI DSI, RGB color |
| Touch | GT911 (10-point capacitive) |
| Memory | 16MB Flash + 8MB PSRAM |
| Communication | USB 2.0 Full Speed (CDC) |
| Power | 5V via USB-C |

## 💻 Software Requirements

- **ESP-IDF**: v5.5 or later
- **Python**: 3.7+ (for sensor server, no external packages needed)
- **CMake**: 3.16 or later
- **Git**: For cloning dependencies
- **UI Design**: [GUI Guider](https://www.nxp.jp/design/design-center/software/development-software/gui-guider:GUI-GUIDER)
- **Sensor Server OS**: Linux (tested on Synology NAS DSM 7.x)

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

## 🚀 Quick Start

Get up and running in minutes:

```bash
# 1. Setup ESP-IDF environment
C:\Espressif\frameworks\esp-idf-v5.5\export.bat

# 2. Clone the project
git clone https://github.com/keitetran/JonsboN4Monitor.git
cd JonsboN4Monitor

# 3. Create your local configuration
copy sdkconfig.example sdkconfig  # Windows
# cp sdkconfig.example sdkconfig  # Linux/Mac

# 4. Configure WiFi/MQTT (Important!)
idf.py menuconfig
# Navigate to: Component config → HomeLabMonitor Configuration
# Set your WiFi SSID and Password

# 5. Set target and build
idf.py set-target esp32p4
idf.py build

# 6. Flash to device
idf.py -p COM3 flash monitor

# 7. Start the sensor server (in another terminal)
cd server
sudo python3 read_sensor.py  # Requires root for sensor access

# For Synology NAS: Use Task Scheduler (see Usage section)
```

That's it! Your monitor should now be displaying system information.

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

3. **No Python dependencies needed!** The server uses only Python 3 built-in libraries.

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

The Python server collects sensor data from the Linux system and sends it to the ESP32.

📖 **Detailed Guide**: See [server/README.md](server/README.md) for complete instructions.

#### Method 1: Manual Run (Testing)

```bash
cd server
sudo python3 read_sensor.py
```

**Note**: Requires `sudo` (root) to read hardware sensors.

#### Method 2: Synology NAS Task Scheduler (Recommended for Production)

For automatic startup on Synology NAS:

1. **Open Control Panel** → **Task Scheduler**

2. **Create** → **Scheduled Task** → **User-defined script**

3. **General Settings**:
   - Task name: `ESP32 Monitor Server`
   - User: `root` (important for sensor access)
   - Enabled: ✓

4. **Schedule**:
   - Run on the following days: Daily
   - First run time: 00:00
   - Frequency: Every minute (or as needed)
   - OR select: **Run on boot-up** (recommended)

5. **Task Settings** → **Run command**:
   ```bash
   # User-defined script
   /usr/bin/python3 /volume1/your_path/HomeLabMonitor/server/read_sensor.py
   ```

   **Finding your path on Synology**:
   - If you cloned to a shared folder "homes": `/volume1/homes/username/HomeLabMonitor/server/read_sensor.py`
   - If you cloned to "docker" folder: `/volume1/docker/HomeLabMonitor/server/read_sensor.py`
   - Use SSH and `pwd` command to find your exact path
   
   Replace `/volume1/your_path/` with your actual installation path.

6. **Advanced Settings**:
   - Send run details by email: Optional
   - Send run details only when script terminates abnormally: ✓

7. Click **OK** to save.

8. **Test the task**: Right-click → **Run** to test immediately.

#### Method 3: Linux systemd Service

For other Linux systems, create a systemd service:

```bash
sudo nano /etc/systemd/system/esp32-monitor.service
```

Add:

```ini
[Unit]
Description=ESP32 Home Lab Monitor Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/path/to/HomeLabMonitor/server
ExecStart=/usr/bin/python3 /path/to/HomeLabMonitor/server/read_sensor.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable esp32-monitor
sudo systemctl start esp32-monitor
sudo systemctl status esp32-monitor
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

### WiFi & MQTT Configuration

**⚠️ Important**: Before building, you need to configure your WiFi and MQTT settings.

#### Step 1: Create Local Configuration

The repository provides `sdkconfig.example` with placeholder values. You need to create your own local configuration:

```bash
# Windows
copy sdkconfig.example sdkconfig

# Linux/Mac
cp sdkconfig.example sdkconfig
```

**Note**: `sdkconfig` is in `.gitignore`, so your passwords will never be committed!

#### Step 2: Configure Your Settings

**Option A: Using menuconfig (Recommended)**

```bash
idf.py menuconfig
```

Navigate to:
- **Component config → HomeLabMonitor Configuration**
  - Set your WiFi SSID
  - Set your WiFi Password
  - Set MQTT Broker IP (if using MQTT)
  - Set MQTT credentials (if needed)

**Option B: Direct Edit**

Edit `sdkconfig` file and change these values:

```ini
CONFIG_WIFI_SSID="Your_WiFi_Name"
CONFIG_WIFI_PASSWORD="Your_WiFi_Password"
CONFIG_MQTT_BROKER_IP="192.168.1.x"
CONFIG_MQTT_USERNAME="your_username"  # Optional
CONFIG_MQTT_PASSWORD="your_password"  # Optional
```

#### How It Works

```
GitHub Repository          Your Local Machine
─────────────────         ──────────────────
sdkconfig.example    →    Copy →    sdkconfig
(placeholders)                       (your real passwords)
✓ Committed to Git                   ✗ Ignored by Git (.gitignore)
```

- `sdkconfig.example` = Template with placeholders (committed to Git)
- `sdkconfig` = Your local config with real passwords (ignored by Git)
- Each developer has their own `sdkconfig` with their own passwords
- Your sensitive data stays on your machine only! 🔒

**For you (project owner):**
- Your local `sdkconfig` already has your real password "Ngocanh01"
- You can build normally without any changes
- When you commit, only `sdkconfig.example` will be pushed (not `sdkconfig`)

**For others (cloning the repo):**
- They get `sdkconfig.example` with placeholders
- They copy it to `sdkconfig` and set their own WiFi credentials
- Their passwords never leave their machine

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
- **Bootloader**: ESP32 bootloader
- **NVS**: Non-volatile storage for configuration
- **Application**: Main firmware (up to 16MB)
- **OTA**: Over-the-air update partitions (if enabled)

### USB Communication

The system uses USB CDC (Communication Device Class) for data transfer:
- **Speed**: USB 2.0 Full Speed (12 Mbps)
- **Protocol**: Custom packet-based protocol
- **Data Format**: Binary sensor data packets
- **Latency**: < 100ms update interval

## 🏗️ Architecture

### System Overview

```
┌─────────────────┐         USB CDC          ┌──────────────────┐
│   Host PC       │◄─────────────────────────►│   ESP32-P4       │
│  (Linux/Win)    │      Sensor Data         │   Microcontroller│
└─────────────────┘                          └──────────────────┘
        │                                             │
        │                                             │
   ┌────▼────┐                                   ┌────▼─────┐
   │ Python  │                                   │  LVGL    │
   │ Server  │                                   │  GUI     │
   └─────────┘                                   └──────────┘
        │                                             │
   ┌────▼────────┐                             ┌─────▼──────┐
   │  Sensors    │                             │  MIPI DSI  │
   │ (lm-sensors)│                             │  Display   │
   └─────────────┘                             └────────────┘
```

### Data Flow

1. **Sensor Collection**: Python server reads system sensors (CPU, GPU, temps, etc.)
2. **Data Transmission**: Server sends data via USB CDC to ESP32
3. **Data Processing**: ESP32 parses and validates incoming data
4. **UI Update**: LVGL updates display widgets with new values
5. **Touch Input**: User interactions are processed and responded to

### Component Layers

```
┌─────────────────────────────────────┐
│      Application Layer              │  ← Main logic, sensor handling
├─────────────────────────────────────┤
│      LVGL Graphics Layer            │  ← UI rendering, widgets
├─────────────────────────────────────┤
│      ESP-IDF HAL Layer              │  ← Hardware abstraction
├─────────────────────────────────────┤
│      Hardware Layer                 │  ← ESP32-P4, LCD, Touch
└─────────────────────────────────────┘
```

## 💻 Development

### Prerequisites for Development

- ESP-IDF v5.5+ (properly configured)
- Python 3.11+ with pip
- Git for version control
- GUI Guider (for UI modifications)
- Code editor (VS Code recommended)

### Development Workflow

1. **UI Design**:
   ```bash
   # Open project in GUI Guider
   # Modify UI elements
   # Export to generated/ folder
   ```

2. **Code Development**:
   ```bash
   # Build and flash
   idf.py build flash monitor
   
   # Or use simulator for faster iteration
   cd lvgl-simulator
   make && ./simulator
   ```

3. **Testing**:
   ```bash
   # Test with mock data
   cd server
   python send_mock.py
   ```

### Modifying the UI

The UI is designed with GUI Guider:

1. Open GUI Guider and load the project
2. Modify widgets, add new screens, or change styles
3. Export the project (generates `generated/` folder)
4. Build and flash to see changes

**Important Files**:
- `generated/gui_guider.c/h`: Main GUI initialization
- `generated/events_init.c`: Event handlers for buttons/touch
- `custom/usb_comm.c`: USB communication and sensor parsing

### Adding New Sensors

1. **Server Side** (`server/read_sensor.py`):
   ```python
   # Add sensor reading logic
   new_sensor_value = read_new_sensor()
   ```

2. **ESP32 Side** (`custom/usb_comm.c`):
   ```c
   // Add sensor ID in enum
   // Add parsing logic
   // Update UI widget
   ```

3. **Update UI** (in GUI Guider):
   - Add label/chart for new sensor
   - Export and rebuild

## 🐛 Troubleshooting

### Build Issues

**Problem**: `idf.py not found`
```bash
# Solution: Source the ESP-IDF environment
C:\Espressif\frameworks\esp-idf-v5.5\export.bat
```

**Problem**: `CMake error: target not set`
```bash
# Solution: Set target before building
idf.py set-target esp32p4
```

**Problem**: `Out of memory during build`
```bash
# Solution: Increase partition size in partitions.csv
# Or reduce embedded assets
```

### Hardware Issues

**Problem**: Display not working
- Check MIPI DSI connections
- Verify power supply (5V, sufficient current)
- Check `sdkconfig` for display settings

**Problem**: Touch not responding
- Verify GT911 I2C address
- Check touch controller power
- Calibrate touch if needed

**Problem**: USB not recognized
- Install USB CDC drivers (Windows)
- Check USB cable (must support data)
- Verify COM port in Device Manager

### Runtime Issues

**Problem**: No data on display
```bash
# Check sensor server is running
cd server
sudo python3 read_sensor.py

# Check USB connection
# Windows: Device Manager → COM ports
# Linux: ls /dev/ttyACM*
```

**Problem**: Slow/laggy display
- Reduce update frequency in server
- Check USB cable quality
- Optimize LVGL buffer settings

**Problem**: Incorrect sensor values
- Check sensor mapping in `docs/sensor-mapping.txt`
- Verify server is reading correct sensors
- Check endianness of data packets

### Synology NAS Issues

**Problem**: Task doesn't start on boot
- Make sure you selected "Run on the following boot-up" in Schedule settings
- Check task is using `root` user (not your regular user)
- Verify Python3 path: `/usr/bin/python3` (use `which python3` via SSH)

**Problem**: Task fails to run
```bash
# Test manually via SSH first
cd /volume1/your_path/HomeLabMonitor/server
sudo /usr/bin/python3 read_sensor.py

# Check if sensors are readable
sudo sensors

# If sensors command not found, install lm-sensors:
# (Usually pre-installed on Synology)
```

**Problem**: Cannot find ESP32 USB device
```bash
# Check if device is connected
ls -l /dev/ttyACM*

# Or check with:
dmesg | grep tty

# Make sure USB device is passed through if using VM/Docker
```

**Problem**: Permission denied errors
- Task must run as `root` user
- Some sensor files require root access
- Check file permissions: `ls -l /sys/class/hwmon/`

### Common Errors

```bash
# Error: "idf_component_register" not found
# Solution: Must use idf.py, not plain cmake

# Error: "GT911 not found"
# Solution: Check I2C wiring and pull-ups

# Error: "Guru Meditation Error"
# Solution: Check stack size, memory allocation
```

## 🔮 Future Plans

- [ ] WiFi connectivity for wireless monitoring
- [ ] Web interface for configuration
- [ ] Support for multiple screens/pages
- [ ] Customizable themes and color schemes
- [ ] Historical data logging and graphs
- [ ] OTA firmware updates
- [ ] Mobile app for remote monitoring
- [ ] Support for other case models

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
