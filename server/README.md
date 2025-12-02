# Sensor Server for ESP32 Monitor

This Python server collects system sensor data and sends it to the ESP32 display via USB CDC.

## Features

- 📊 Reads system sensors (CPU, GPU, RAM, temperatures, fans)
- 🔌 Sends data to ESP32 via USB CDC (auto-detect device)
- 🐍 Pure Python 3 - **No external dependencies required!**
- 🖥️ Optimized for **Synology NAS** (DSM 7.x)
- 🔄 Auto-reconnect on USB disconnect
- 🌙 Power-aware: Only sends data when ESP32 display is awake

## Requirements

- Python 3.7 or later (built-in on Synology DSM 7.x)
- Linux system with `/sys` and `/proc` filesystems
- Root access for reading hardware sensors
- ESP32 connected via USB

**No pip install needed!** Uses only Python standard library.

## Quick Start

### Manual Test Run

```bash
cd server
sudo python3 read_sensor.py
```

Press `Ctrl+C` to stop.

### Synology NAS Setup (Recommended)

#### Step 1: Clone Repository

SSH into your Synology and clone the repo:

```bash
# SSH to your Synology
ssh your_username@synology_ip

# Clone to a shared folder
cd /volume1/docker  # or /volume1/homes/your_username
git clone https://github.com/keitetran/JonsboN4Monitor.git
```

#### Step 2: Test Manually

```bash
cd /volume1/docker/HomeLabMonitor/server
sudo python3 read_sensor.py
```

Make sure it can find your ESP32 device and read sensors.

#### Step 3: Create Scheduled Task

1. Open **Control Panel** → **Task Scheduler**

2. Click **Create** → **Scheduled Task** → **User-defined script**

3. **General Tab**:
   - Task: `ESP32 Monitor Server`
   - User: **root** ⚠️ Important!
   - Enabled: ✓

4. **Schedule Tab**:
   - Select: **Run on the following boot-up** ✓
   - This ensures the server starts automatically when NAS boots

5. **Task Settings Tab**:
   - User-defined script:
     ```bash
     /usr/bin/python3 /volume1/docker/HomeLabMonitor/server/read_sensor.py
     ```
   - Replace `/volume1/docker/` with your actual path

6. Click **OK** to save

7. **Test it**: Right-click the task → **Run**

8. **Check status**: Look at task history in Task Scheduler

#### Finding Your Path

If you're not sure of the full path:

```bash
# SSH to Synology
cd HomeLabMonitor/server
pwd
# Copy the output and use it in Task Scheduler
```

Common paths:
- `/volume1/docker/HomeLabMonitor/server/read_sensor.py`
- `/volume1/homes/username/HomeLabMonitor/server/read_sensor.py`

## Command Line Options

```bash
# Auto-detect ESP32 USB device (default)
python3 read_sensor.py

# Specify USB device manually
python3 read_sensor.py --serial-device /dev/ttyACM0

# Write to file only (no USB)
python3 read_sensor.py --file-only --output sensors.txt

# Custom update interval (default: 2 seconds)
python3 read_sensor.py --interval 5

# Custom USB vendor/model ID
python3 read_sensor.py --vendor-id 303a --model-id 4001

# Enable debug logging
python3 read_sensor.py --debug

# Show all options
python3 read_sensor.py --help
```

## Troubleshooting

### "Permission denied" when reading sensors

**Solution**: Run with `sudo`:
```bash
sudo python3 read_sensor.py
```

In Task Scheduler, make sure User is set to **root**.

### "ESP32 device not found"

**Solution 1**: Check if device is connected:
```bash
ls -l /dev/ttyACM*
# or
dmesg | grep tty
```

**Solution 2**: Manually specify device:
```bash
python3 read_sensor.py --serial-device /dev/ttyACM0
```

### "No such file or directory: /sys/class/hwmon/..."

**Solution**: Some sensors may not be available on your system. The script will skip unavailable sensors automatically.

### Task doesn't start on Synology NAS boot

**Check**:
1. Task is enabled (✓)
2. User is **root** (not your regular user)
3. Schedule is set to "Run on the following boot-up"
4. Python path is correct: `/usr/bin/python3`
5. Script path is absolute (not relative)

### How to stop the server on Synology

**Option 1**: Via Task Scheduler
- Right-click task → **Stop**

**Option 2**: Via SSH
```bash
# Find the process
sudo ps aux | grep read_sensor.py

# Kill it (replace PID with actual process ID)
sudo kill <PID>
```

## Architecture

```
┌─────────────────────┐
│   Synology NAS      │
│                     │
│  ┌───────────────┐  │
│  │ read_sensor.py│  │  USB CDC
│  │ (Python 3)    │──┼──────────► ESP32-P4
│  │               │  │            Display
│  │ Reads:        │  │
│  │ - /sys/class/ │  │
│  │ - /proc/      │  │
│  │ - sensors cmd │  │
│  └───────────────┘  │
└─────────────────────┘
```

## Files

- `read_sensor.py` - Main server script (this is all you need!)
- `test-sensor-result.py` - Test sensor reading without ESP32
- `test-usb-comn.py` - Test USB communication
- `sensors.txt` - Example sensor output (for reference)

## Supported Sensors

The server attempts to read:
- CPU temperature, usage, frequency
- GPU temperature, usage
- RAM usage
- System temperatures (motherboard, etc.)
- Fan speeds
- Disk usage
- Network statistics

Not all sensors may be available on every system. The script gracefully handles missing sensors.

## Notes

- Designed for Linux systems (tested on Synology DSM 7.x)
- Uses USB CDC protocol for communication
- Automatically detects ESP32 with vendor ID `303a` and model ID `4001`
- Implements power management (stops sending when display sleeps)
- Auto-reconnects if USB cable is unplugged/replugged

## License

MIT License - See main project LICENSE.txt

---

**Made with ❤️ for Synology NAS users**

