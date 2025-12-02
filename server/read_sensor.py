#!/usr/bin/env python3
"""
Đọc thông tin cảm biến từ hệ thống Linux và ghi ra file txt hoặc gửi qua USB serial tới ESP32.

Chức năng:
- Đọc các thông tin cảm biến từ hệ thống Linux (/sys, /proc)
- Ghi dữ liệu ra file txt
- Gửi dữ liệu qua USB serial tới ESP32 (tự động tìm device hoặc chỉ định thủ công)
- Chỉ gửi dữ liệu khi ESP32 bật màn hình (backlight on)
- Phân loại dữ liệu: storage data chỉ gửi 1 lần khi wake up, dynamic data gửi định kỳ
- OTA HTTP Server: Serve firmware file cho ESP32 OTA update (port 8888 mặc định)

CÁCH SỬ DỤNG:

1. Chỉ ghi file (không gửi qua serial):
   python3 read_sensor.py --file-only --output sensors.txt
   # Hoặc chỉ cần: python3 read_sensor.py --output sensors.txt

2. Tự động tìm ESP32 USB device và gửi dữ liệu:
   python3 read_sensor.py
   
   Script sẽ tự động tìm ESP32 USB device với vendor ID 303a và model ID 4001.

3. Chỉ định USB device thủ công:
   python3 read_sensor.py --serial-device /dev/ttyACM0

4. Tùy chỉnh vendor/model ID:
   python3 read_sensor.py --vendor-id 303a --model-id 4001

5. Tùy chỉnh interval gửi dữ liệu:
   python3 read_sensor.py --interval 1.0  # Gửi mỗi 1 giây

6. Gửi serial mỗi 5s, nhưng chỉ ghi file mỗi 30s:
   python3 read_sensor.py --file-interval 30.0

7. Tắt OTA HTTP server:
   python3 read_sensor.py --ota-port 0

8. Thay đổi port OTA server:
   python3 read_sensor.py --ota-port 9000

ARGUMENTS:

--output PATH
    File output để ghi dữ liệu sensor (default: sensors.txt)
    Ví dụ: --output /tmp/sensors.txt

--serial-device PATH
    Thiết bị serial để gửi dữ liệu tới ESP32 (ví dụ: /dev/ttyACM0)
    Nếu không chỉ định, script sẽ tự động tìm ESP32 USB device
    Ví dụ: --serial-device /dev/ttyACM0

--vendor-id HEX
    Vendor ID để tìm USB device (hex, default: 303a)
    Chỉ áp dụng khi không chỉ định --serial-device
    Ví dụ: --vendor-id 303a

--model-id HEX
    Model ID để tìm USB device (hex, default: 4001)
    Chỉ áp dụng khi không chỉ định --serial-device
    Ví dụ: --model-id 4001

--interval SECONDS
    Khoảng thời gian (giây) giữa các lần gửi dữ liệu khi dùng serial (default: 5.0)
    Chỉ áp dụng khi có --serial-device hoặc tự động tìm được USB device
    Ví dụ: --interval 1.0  # Gửi mỗi 1 giây

--file-interval SECONDS
    Khoảng thời gian (giây) giữa các lần ghi file (default: None = ghi mỗi lần gửi serial)
    Nếu set, sẽ ghi file ít thường xuyên hơn so với gửi serial
    Ví dụ: --file-interval 30.0  # Ghi file mỗi 30 giây

--file-only
    Chế độ chỉ ghi file rồi thoát (không tìm USB/serial)
    Có thể bỏ qua nếu chỉ dùng --output mà không có tuỳ chọn serial

--serial-retries COUNT
    Số lần thử lại khi ghi serial thất bại (default: 3)
    Ví dụ: --serial-retries 5

--serial-retry-delay SECONDS
    Delay giữa các lần retry serial tính bằng giây (default: 0.5)
    Ví dụ: --serial-retry-delay 1.0

--ota-port PORT
    Port cho OTA HTTP server (default: 8888). Set 0 để tắt OTA server.
    OTA server serve firmware file tại http://localhost:PORT/firmware.bin
    Ví dụ: --ota-port 8888 hoặc --ota-port 0 (tắt)

VÍ DỤ:

# Chỉ ghi file
python3 read_sensor.py --output sensors.txt

# Tự động tìm ESP32 và gửi dữ liệu mỗi 5 giây
python3 read_sensor.py

# Chỉ định device thủ công, gửi mỗi 1 giây
python3 read_sensor.py --serial-device /dev/ttyACM0 --interval 1.0

# Gửi serial mỗi 5s, ghi file mỗi 60s
python3 read_sensor.py --file-interval 60.0

# Tùy chỉnh vendor/model ID
python3 read_sensor.py --vendor-id 303a --model-id 4001 --interval 2.0

LƯU Ý:

- Khi ESP32 tắt màn hình (backlight off), script sẽ dừng đọc và gửi dữ liệu sensor
- Khi ESP32 bật màn hình (backlight on), script sẽ:
  + Gửi storage data (label_storage_*) 1 lần duy nhất
  + Sau đó gửi dynamic data (fan, CPU, RAM, GPU, temp, network) định kỳ
- Script tự động tìm USB device trong:
  + /dev/serial/by-id/
  + /sys/bus/usb/devices/
  + Fallback: /dev/ttyACM* và /dev/ttyUSB*
- Cần quyền truy cập USB device (có thể cần sudo hoặc thêm user vào group dialout)
"""

from __future__ import annotations

import argparse
import atexit
import fcntl
import json
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path
from threading import Thread
from typing import Dict, Optional, List, Tuple

def get_version_from_cmake() -> str:
    """
    Đọc version từ CMakeLists.txt (PROJECT_VER) để đồng bộ với firmware.
    Fallback về version mặc định nếu không đọc được.
    """
    try:
        # Tìm CMakeLists.txt ở thư mục gốc project (lên 1 level từ server/)
        cmake_path = Path(__file__).parent.parent / "CMakeLists.txt"
        if not cmake_path.exists():
            # Thử tìm ở thư mục hiện tại
            cmake_path = Path("CMakeLists.txt")
        
        if cmake_path.exists():
            with open(cmake_path, 'r', encoding='utf-8') as f:
                content = f.read()
                # Tìm dòng: set(PROJECT_VER "x.x.x")
                match = re.search(r'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', content)
                if match:
                    return match.group(1)
    except Exception as e:
        print(f"⚠ Cảnh báo: Không thể đọc version từ CMakeLists.txt: {e}", file=sys.stderr)
    
    # Fallback về version mặc định
    return "1.0.1"

# Version của script và firmware - đọc từ CMakeLists.txt để đồng bộ
VERSION = get_version_from_cmake()

# Dùng termios để set DTR/RTS trên Linux (built-in, không cần pip)
try:
    import termios
    TERMIOS_AVAILABLE = True
except ImportError:
    TERMIOS_AVAILABLE = False

# Constants cho ioctl (Linux) - từ <linux/serial.h>
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004
TIOCMGET = 0x5415
TIOCMSET = 0x5418


def set_serial_dtr_rts(file_obj, dtr: bool = True, rts: bool = True) -> bool:
    """Set DTR và RTS cho serial port bằng termios/ioctl (Linux built-in).
    
    Args:
        file_obj: File object đã mở của serial port
        dtr: Set DTR (True = set, False = clear)
        rts: Set RTS (True = set, False = clear)
    
    Returns:
        True nếu thành công, False nếu thất bại
    """
    if not TERMIOS_AVAILABLE:
        return False
    
    try:
        # Lấy file descriptor
        fd = file_obj.fileno()
        
        # Đọc trạng thái hiện tại
        status = fcntl.ioctl(fd, TIOCMGET, '\0' * 4)
        status = int.from_bytes(status, 'little', signed=False)
        
        # Set hoặc clear DTR/RTS
        if dtr:
            status |= TIOCM_DTR
        else:
            status &= ~TIOCM_DTR
        
        if rts:
            status |= TIOCM_RTS
        else:
            status &= ~TIOCM_RTS
        
        # Ghi trạng thái mới
        fcntl.ioctl(fd, TIOCMSET, status.to_bytes(4, 'little'))
        return True
    except (OSError, AttributeError, ValueError):
        return False

# Thứ tự label quan trọng vì ESP32 parser mong đợi thứ tự này
# Tách thành 2 nhóm:
# - WAKEUP_LABELS: Chỉ gửi 1 lần duy nhất khi ESP32 wake up (màn hình sáng)
# - DYNAMIC_LABELS: Gửi định kỳ mỗi interval giây khi ESP32 đang on

WAKEUP_LABELS = [
    "label_storage_1",
    "label_storage_total_1",
    "label_storage_2",
    "label_storage_total_2",
    "label_storage_3",
    "label_storage_total_3",
    "label_storage_4",
    "label_storage_total_4",

    # System status
    "label_system_status",
    "label_thermal_status",
    "label_upgrade_available",
    "label_power_status",
    "label_system_fan_status",
    "label_version",

    # Ping 
    "label_ping_total",
]

DYNAMIC_LABELS = [
    "label_fan1_value",
    "label_fan2_value",
    "label_fan3_value",
    "label_fan4_value",
    "label_fan5_value",
    "label_fan6_value",
    "label_fan7_value",
    "label_cpu_usage",
    "label_cpu_usage_per",
    "bar_cpu_usage",
    "label_ram_usage",
    "label_ram_usage_per",
    "bar_ram_usage",
    "label_gpu_usage",
    "label_gpu_usage_per",
    "label_gpu_fan_speed",
    "bar_gpu_usage",
    "label_temp_drive0",
    "label_temp_drive1",
    "label_temp_drive2",
    "label_temp_drive3",
    "label_temp_drive4",
    "label_temp_drive5",
    "label_temp_nvme1",
    "label_temp_nvme2",
    "label_temp_nvme3",
    "label_temp_nvme4",
    "label_temp_nvme5",

    "label_status_drive0",
    "label_status_drive1",
    "label_status_drive2",
    "label_status_drive3",
    "label_status_drive4",
    "label_status_drive5",
    "label_status_nvme1",
    "label_status_nvme2",
    "label_status_nvme3",
    "label_status_nvme4",
    "label_status_nvme5",

    "label_temp_motherboard",
    "label_temp_chipset",
    "label_temp_cpu",
    "label_temp_gpu",
    "label_temp_ram",
    "label_hostname",
    "label_account",
    "label_download_total",
    "label_upload_total",
    "arc_download_total",
    "arc_upload_total",

    # Disk I/O (cập nhật liên tục)
    "label_disk_iops",
    "label_disk_read",
    "label_disk_write",
]

# LABEL_ORDER = WAKEUP_LABELS + DYNAMIC_LABELS (để tương thích với code cũ)
LABEL_ORDER = WAKEUP_LABELS + DYNAMIC_LABELS

# Mapping cho các trạng thái system/upgrade khi đọc SNMP
SYSTEM_STATUS_MAP = {
    0: "Unknown",
    1: "Normal",
    2: "Failed",
    3: "Recovering",
}
UPGRADE_STATUS_MAP = {
    0: "Unknown",
    1: "Available",
    2: "Unavailable",
    3: "Connecting",
    4: "Disconnected",
    5: "Others",
}

CPU_MHZ_RE = re.compile(r"cpu MHz\s*:\s*([0-9.]+)", re.IGNORECASE)

# Flags cho biết user muốn chạy chế độ serial (giữ nguyên behaviour mặc định)
SERIAL_MODE_HINT_FLAGS = (
    "--serial-device",
    "--vendor-id",
    "--model-id",
    "--interval",
    "--serial-retries",
    "--serial-retry-delay",
    "--no-wait-signal",
    "--auto-start-timeout",
    "--debug",
)


def cli_flag_provided(flag: str) -> bool:
    """Kiểm tra xem một flag CLI có được user truyền vào hay không."""
    prefix = f"{flag}="
    for arg in sys.argv[1:]:
        if arg == flag or arg.startswith(prefix):
            return True
    return False


# SNMP constants for storage volumes
SNMP_LINE_RE = re.compile(
    r"^(?P<oid>[\w\.]+)\s*=\s*(?P<type>[A-Za-z0-9]+):\s*(?P<value>.*)$"
)
VOLUME_NAME_PREFIX = "1.3.6.1.4.1.6574.3.1.1.2."
VOLUME_USED_PREFIX = "1.3.6.1.4.1.6574.3.1.1.4."  # Actually free space
VOLUME_TOTAL_PREFIX = "1.3.6.1.4.1.6574.3.1.1.5."
VOLUME_LABELS = {
    "Volume 1": ("label_storage_1", "label_storage_total_1"),
    "Volume 2": ("label_storage_2", "label_storage_total_2"),
    "Volume 3": ("label_storage_3", "label_storage_total_3"),
}

# SNMP OIDs for CPU and RAM
CPU_IDLE_OID = "1.3.6.1.4.1.2021.11.11.0"  # ssCpuIdle
RAM_TOTAL_KB_OID = "1.3.6.1.4.1.2021.4.5.0"  # memTotalReal
RAM_AVAIL_KB_OID = "1.3.6.1.4.1.2021.4.6.0"  # memAvailReal

# SNMP OIDs for Synology System Status (SYNOLOGY-SYSTEM-MIB)
SYSTEM_STATUS_OID = "1.3.6.1.4.1.6574.1.1.0"  # systemStatus.0
POWER_STATUS_OID = "1.3.6.1.4.1.6574.1.3.0"  # powerStatus.0
SYSTEM_FAN_STATUS_OID = "1.3.6.1.4.1.6574.1.4.1.0"  # systemFanStatus.0
THERMAL_STATUS_OID = "1.3.6.1.4.1.6574.1.5.7.0"  # thermalStatus.0
UPGRADE_AVAILABLE_OID = "1.3.6.1.4.1.6574.1.5.4.0"  # upgradeAvailable.0
VERSION_OID = "1.3.6.1.4.1.6574.1.5.3.0"  # version.0


def normalize_snmp_oid(oid: str) -> str:
    """Chuẩn hóa OID về dạng numeric (ví dụ: iso.3.x -> 1.3.x, .1.3.x -> 1.3.x)."""
    oid = oid.strip()
    if oid.startswith("iso."):
        oid = "1" + oid[3:]
    if oid.startswith("."):
        oid = oid[1:]
    return oid


def human_bytes(value: int) -> str:
    """Chuyển đổi bytes sang định dạng human-readable (B, KB, MB, GB, TB, PB).
    
    Args:
        value: Giá trị bytes cần chuyển đổi
    
    Returns:
        Chuỗi đã format với unit phù hợp (ví dụ: "1.5 GB", "500 MB")
    """
    suffixes = ["B", "KB", "MB", "GB", "TB", "PB"]
    base = 1024.0
    val = float(value)
    idx = 0
    while val >= base and idx < len(suffixes) - 1:
        val /= base
        idx += 1
    if idx == 0:
        return f"{int(val)} {suffixes[idx]}"
    return f"{val:.1f} {suffixes[idx]}"


def format_storage_size(size_str: str) -> str:
    """Format storage size với unit Gb hoặc Tb, có dấu cách. Ví dụ: '1 Tb', '30 Gb'.
    
    Args:
        size_str: Chuỗi size từ df command (ví dụ: "874.5G", "7.0T") hoặc "N/A"
    
    Returns:
        Chuỗi đã format với unit Gb hoặc Tb, hoặc "N/A" nếu không parse được
    """
    if not size_str or size_str == "N/A":
        return "N/A"
    
    # Parse size string từ df (ví dụ: "874.5G", "7.0T", "96.9G", "111.79G")
    size_match = re.match(r"^(\d+\.?\d*)([KMGT]?)$", size_str.upper())
    if not size_match:
        return size_str
    
    value_str = size_match.group(1)
    unit = size_match.group(2) or ""
    
    try:
        value = float(value_str)
    except ValueError:
        return size_str
    
    # Convert tất cả sang GB trước
    if unit == "K":
        value = value / (1024 * 1024)  # KB -> GB
    elif unit == "M":
        value = value / 1024  # MB -> GB
    elif unit == "T":
        value = value * 1024  # TB -> GB
    # unit == "G" hoặc "" thì giữ nguyên
    
    # Nếu >= 1000 GB thì chuyển sang TB
    if value >= 1000:
        value_tb = value / 1024.0
        # Format: làm tròn nếu là số nguyên
        if value_tb == int(value_tb):
            return f"{int(value_tb)} Tb"
        else:
            return f"{value_tb:.1f} Tb"
    else:
        # Format GB: làm tròn nếu là số nguyên
        if value == int(value):
            return f"{int(value)} Gb"
        else:
            return f"{value:.1f} Gb"


def read_fan_speeds() -> Dict[str, str]:
    """Đọc tốc độ quạt từ /sys/class/hwmon/hwmon*/fan*_input.
    
    Returns:
        Dictionary chứa label_fan1_value đến label_fan7_value với giá trị RPM hoặc "N/A"
    """
    fans: Dict[str, str] = {}
    try:
        hwmon_path = Path("/sys/class/hwmon")
        if not hwmon_path.exists():
            # Initialize all 7 fans to N/A
            return {f"label_fan{i}_value": "N/A" for i in range(1, 8)}
        
        fan_inputs: List[Tuple[int, Path]] = []
        for hwmon_dir in sorted(hwmon_path.glob("hwmon*")):
            for fan_file in sorted(hwmon_dir.glob("fan*_input")):
                try:
                    # Extract fan number from filename (e.g., fan1_input -> 1)
                    match = re.search(r"fan(\d+)_input", fan_file.name)
                    if match:
                        fan_num = int(match.group(1))
                        fan_inputs.append((fan_num, fan_file))
                except (ValueError, OSError):
                    continue
        
        # Sort by fan number and take first 7
        fan_inputs.sort(key=lambda x: x[0])
        
        for idx, (fan_num, fan_file) in enumerate(fan_inputs[:7], 1):
            try:
                value = int(fan_file.read_text(encoding="utf-8").strip())
                if value > 0:
                    fans[f"label_fan{idx}_value"] = f"{value} RPM"
                else:
                    fans[f"label_fan{idx}_value"] = "N/A"
            except (ValueError, OSError):
                fans[f"label_fan{idx}_value"] = "N/A"
        
        # Ensure we have all 7 fan labels
        for i in range(1, 8):
            fans.setdefault(f"label_fan{i}_value", "N/A")
        
    except (OSError, FileNotFoundError):
        # Initialize all 7 fans to N/A
        fans = {f"label_fan{i}_value": "N/A" for i in range(1, 8)}
    
    return fans


def read_cpu_clock_ghz() -> Optional[float]:
    """Đọc CPU clock từ /proc/cpuinfo.
    
    Returns:
        CPU clock speed tính bằng GHz, hoặc None nếu không đọc được
    """
    try:
        cpuinfo = Path("/proc/cpuinfo").read_text(encoding="utf-8")
        match = CPU_MHZ_RE.search(cpuinfo)
        if match:
            mhz = float(match.group(1))
            return mhz / 1000.0
    except (FileNotFoundError, ValueError, OSError):
        pass
    return None


def read_cpu_usage() -> Optional[float]:
    """Đọc CPU usage từ /proc/stat bằng cách tính phần trăm từ CPU times.
    
    Returns:
        CPU usage percentage (0-100), hoặc None nếu không đọc được
    """
    try:
        stat_file = Path("/proc/stat")
        if not stat_file.exists():
            return None
        
        # Read first line (cpu summary)
        first_line = stat_file.read_text(encoding="utf-8").splitlines()[0]
        parts = first_line.split()
        if len(parts) < 8:
            return None
        
        # Parse CPU times: user, nice, system, idle, iowait, irq, softirq, steal
        user = int(parts[1])
        nice = int(parts[2])
        system = int(parts[3])
        idle = int(parts[4])
        iowait = int(parts[5]) if len(parts) > 5 else 0
        irq = int(parts[6]) if len(parts) > 6 else 0
        softirq = int(parts[7]) if len(parts) > 7 else 0
        
        total = user + nice + system + idle + iowait + irq + softirq
        if total == 0:
            return None
        
        # Calculate usage percentage
        used = total - idle
        usage_percent = (used / total) * 100.0
        return max(0.0, min(100.0, usage_percent))
    except (OSError, ValueError, IndexError):
        return None


def read_ram_info() -> Optional[Tuple[int, int]]:
    """Đọc RAM info từ /proc/meminfo. Trả về (used_kb, total_kb).
    
    Returns:
        Tuple (used_kb, total_kb) tính bằng KB, hoặc None nếu không đọc được
    """
    try:
        meminfo = Path("/proc/meminfo").read_text(encoding="utf-8")
        total_match = re.search(r"MemTotal:\s+(\d+)\s+kB", meminfo)
        available_match = re.search(r"MemAvailable:\s+(\d+)\s+kB", meminfo)
        
        if total_match and available_match:
            total_kb = int(total_match.group(1))
            available_kb = int(available_match.group(1))
            used_kb = max(0, total_kb - available_kb)
            return (used_kb, total_kb)
    except (OSError, ValueError, FileNotFoundError):
        pass
    return None


def read_gpu_clock_mhz() -> Optional[int]:
    """Đọc GPU current frequency từ nvidia-smi hoặc /sys/class/drm/card*/gt_cur_freq_mhz.
    
    Thử nvidia-smi trước, sau đó fallback sang /sys/class/drm nếu không có NVIDIA GPU.
    
    Returns:
        GPU clock speed tính bằng MHz, hoặc None nếu không đọc được
    """
    # Try nvidia-smi first (for NVIDIA GPUs) - đọc current clock, không phải max
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=clocks.current.graphics", "--format=csv,noheader,nounits"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            clock_str = result.stdout.strip().split("\n")[0].strip()
            try:
                mhz = int(clock_str)
                if mhz > 0:
                    return mhz
            except ValueError:
                pass
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    
    # Fallback: Try /sys/class/drm/card*/gt_cur_freq_mhz (current frequency)
    try:
        drm_path = Path("/sys/class/drm")
        if not drm_path.exists():
            return None
        
        # Get first card (like cpuinfo.sh: head -1)
        cards = sorted(drm_path.glob("card*"))
        if not cards:
            return None
        
        card_path = cards[0]
        # Try current frequency first
        freq_file = card_path / "gt_cur_freq_mhz"
        if freq_file.exists():
            try:
                mhz = int(freq_file.read_text(encoding="utf-8").strip())
                if mhz > 0:
                    return mhz
            except (ValueError, OSError):
                pass
        
        # Try device/gt_cur_freq_mhz
        freq_file = card_path / "device" / "gt_cur_freq_mhz"
        if freq_file.exists():
            try:
                mhz = int(freq_file.read_text(encoding="utf-8").strip())
                if mhz > 0:
                    return mhz
            except (ValueError, OSError):
                pass
        
        # Fallback to max frequency if current not available
        freq_file = card_path / "gt_max_freq_mhz"
        if freq_file.exists():
            try:
                mhz = int(freq_file.read_text(encoding="utf-8").strip())
                if mhz > 0:
                    return mhz
            except (ValueError, OSError):
                pass
        
        # Also try other possible locations
        for card_path in cards:
            # Try device/gt_max_freq_mhz as last resort
            freq_file = card_path / "device" / "gt_max_freq_mhz"
            if freq_file.exists():
                try:
                    mhz = int(freq_file.read_text(encoding="utf-8").strip())
                    if mhz > 0:
                        return mhz
                except (ValueError, OSError):
                    continue
    except (OSError, FileNotFoundError):
        pass
    return None


def read_gpu_usage_percent() -> Tuple[Optional[int], Optional[int]]:
    """Đọc GPU utilization và fan speed từ nvidia-smi nếu có (một lần gọi duy nhất).
    
    Returns:
        Tuple (gpu_usage_percent, gpu_fan_speed):
        - gpu_usage_percent: GPU usage percentage (0-100), hoặc None nếu không đọc được
        - gpu_fan_speed: GPU fan speed percentage (0-100), hoặc None nếu không đọc được
    """
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,fan.speed", "--format=csv,noheader,nounits"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            lines = result.stdout.strip().split("\n")
            if lines and lines[0]:
                values = lines[0].strip().split(", ")
                if len(values) >= 2:
                    usage = int(values[0].strip()) if values[0].strip() else None
                    fan_speed = int(values[1].strip()) if values[1].strip() else None
                    return (usage, fan_speed)
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, IndexError):
        pass
    return (None, None)


def read_disk_temps() -> Dict[str, str]:
    """Đọc nhiệt độ ổ cứng từ synodisk --enum hoặc /sys/block.
    
    Hỗ trợ cả HDD (drive0-5) và NVMe (nvme1-5). Thử synodisk trước cho HDD,
    sau đó dùng nvme smart-log cho NVMe, fallback sang /sys/block nếu cần.
    
    Returns:
        Dictionary chứa label_temp_drive0-5 và label_temp_nvme1-5 với giá trị nhiệt độ hoặc "N/A"
    """
    temps: Dict[str, str] = {}
    
    # Initialize all drive temps to N/A (drive0-5)
    # Note: synodisk disk_id starts from 1, we map to drive0-5
    for i in range(0, 6):
        temps[f"label_temp_drive{i}"] = "N/A"
    for i in range(1, 6):
        temps[f"label_temp_nvme{i}"] = "N/A"
    
    # Try synodisk first (Synology specific)
    try:
        result = subprocess.run(
            ["synodisk", "--enum"],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.returncode == 0:
            output = result.stdout
            # Parse disk info: ">> Disk id: X" and ">> Tempeture: Y C"
            disk_id = None
            for line in output.splitlines():
                disk_match = re.search(r">>\s*Disk id:\s*(\d+)", line)
                if disk_match:
                    disk_id = int(disk_match.group(1))
                temp_match = re.search(r">>\s*Tempeture:\s*(\d+)\s*C", line)
                if temp_match and disk_id is not None:
                    temp = int(temp_match.group(1))
                    # Map disk_id directly: disk_id 1->drive1, 2->drive2, etc.
                    # But we have drive0-5, so disk_id 1-6 maps to drive0-5
                    # Based on user feedback, adjust mapping as needed
                    if 1 <= disk_id <= 6:
                        # Current mapping: disk_id 1->drive1, 2->drive2, etc.
                        # But we need to support drive0, so map: disk_id 1->drive0, 2->drive1, etc.
                        drive_idx = disk_id - 1
                        if 0 <= drive_idx <= 5:
                            temps[f"label_temp_drive{drive_idx}"] = f"{temp}°C"
                    disk_id = None
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError):
        pass
    
    # Try reading NVMe temperatures using nvme smart-log command
    # Command: nvme smart-log $device | grep "temperature" | head -1 | awk '{print $3}'
    try:
        # Get list of NVMe devices (only main devices: nvme0, nvme1, etc.)
        nvme_devices = sorted(Path("/dev").glob("nvme[0-9]"))
        nvme_idx = 1
        
        for nvme_device in nvme_devices:
            if nvme_idx > 5:
                break
            
            try:
                # Run nvme smart-log and parse temperature
                result = subprocess.run(
                    ["nvme", "smart-log", str(nvme_device)],
                    capture_output=True,
                    text=True,
                    timeout=5,
                )
                if result.returncode == 0:
                    # Parse temperature from output
                    # Command equivalent: grep "temperature" | head -1 | awk '{print $3}'
                    for line in result.stdout.splitlines():
                        if "temperature" in line.lower():
                            # Try to extract temperature value
                            # Method 1: Split by whitespace and get 3rd field (awk '{print $3}')
                            parts = line.split()
                            if len(parts) >= 3:
                                try:
                                    temp = int(parts[2])  # 3rd field (0-indexed: 2)
                                    if temp > 0:
                                        temps[f"label_temp_nvme{nvme_idx}"] = f"{temp}°C"
                                        break
                                except (ValueError, IndexError):
                                    pass
                            
                            # Method 2: Find first number in the line after "temperature"
                            # This handles various formats
                            temp_match = re.search(r"temperature[^0-9]*(\d+)", line, re.IGNORECASE)
                            if temp_match:
                                try:
                                    temp = int(temp_match.group(1))
                                    if temp > 0:
                                        temps[f"label_temp_nvme{nvme_idx}"] = f"{temp}°C"
                                        break
                                except ValueError:
                                    pass
            except (subprocess.TimeoutExpired, FileNotFoundError, ValueError):
                pass
            
            nvme_idx += 1
    except (OSError, FileNotFoundError):
        pass
    
    # Fallback: Try reading from /sys/block for NVMe drives if nvme command didn't work
    if all(temps.get(f"label_temp_nvme{i}", "N/A") == "N/A" for i in range(1, 6)):
        try:
            nvme_idx = 1
            for nvme_path in sorted(Path("/sys/block").glob("nvme*")):
                if nvme_idx > 5:
                    break
                temp_file = nvme_path / "device" / "temp1_input"
                if temp_file.exists():
                    try:
                        temp_millidegrees = int(temp_file.read_text(encoding="utf-8").strip())
                        temp_celsius = temp_millidegrees // 1000
                        if temp_celsius > 0:
                            temps[f"label_temp_nvme{nvme_idx}"] = f"{temp_celsius}°C"
                    except (ValueError, OSError):
                        pass
                nvme_idx += 1
        except (OSError, FileNotFoundError):
            pass
    
    return temps


def read_disk_status() -> Dict[str, str]:
    """Đọc drive status từ SNMP OID 1.3.6.1.4.1.6574.2.1.1.13.
    
    OID này trả về SMART status của các ổ đĩa:
    - Index 0-5: drive0-5 (HDD)
    - Index 6-10: nvme1-5 (NVMe)
    
    Returns:
        Dictionary chứa label_status_drive0-5 và label_status_nvme1-5 với giá trị INTEGER
    """
    status: Dict[str, str] = {}
    
    # Initialize all drive status to "N/A"
    for i in range(0, 6):
        status[f"label_status_drive{i}"] = "N/A"
    for i in range(1, 6):
        status[f"label_status_nvme{i}"] = "N/A"
    
    try:
        # Chạy snmpwalk để lấy drive status từ SNMP
        snmp_output = run_snmpwalk(host="localhost", community="public", version="2c",
                                   base_oid="1.3.6.1.4.1.6574.2.1.1.13", timeout=10)
        if not snmp_output:
            return status
        
        # Parse SNMP output
        snmp_data = parse_snmpwalk(snmp_output)
        
        # OID prefix cho drive status (đã normalize về dạng 1.3.x)
        STATUS_OID_PREFIX = "1.3.6.1.4.1.6574.2.1.1.13."
        
        # Map SNMP index to drive labels
        for oid, value in snmp_data.items():
            if not oid.startswith(STATUS_OID_PREFIX):
                continue
            
            # Lấy index từ OID (ví dụ: iso.3.6.1.4.1.6574.2.1.1.13.1 -> index = 1)
            try:
                index = int(oid.split(".")[-1])
            except (ValueError, IndexError):
                continue
            
            # Lấy giá trị INTEGER
            if isinstance(value, int):
                status_value = str(value)
            else:
                status_value = str(value)
            
            # Map index to drive labels
            # Index 0-5 -> drive0-5
            drive_idx: Optional[int] = None
            nvme_idx: Optional[int] = None
            
            # NVMe có thể bắt đầu từ 6 hoặc các offset khác (11, 101, ...)
            if 6 <= index <= 10:
                nvme_idx = index - 5  # 6->1, 7->2, ..., 10->5
            elif 11 <= index <= 15:
                nvme_idx = index - 10  # 11->1, 12->2, ..., 15->5
            elif 101 <= index <= 105:
                nvme_idx = index - 100  # Một số firmware dùng 100+ cho NVMe

            if nvme_idx is not None and 1 <= nvme_idx <= 5:
                status[f"label_status_nvme{nvme_idx}"] = status_value
                continue

            # Một số NAS trả về index 0-based (0-5), một số khác 1-based (1-6)
            if 0 <= index <= 5:
                drive_idx = index
            elif 1 <= index <= 6:
                drive_idx = index - 1
            
            if drive_idx is not None and 0 <= drive_idx <= 5:
                status[f"label_status_drive{drive_idx}"] = status_value
    
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, OSError):
        pass
    
    return status


def read_system_temps() -> Dict[str, str]:
    """Đọc nhiệt độ hệ thống từ /sys/class/hwmon (CPU, motherboard, chipset, GPU, RAM).
    
    Quét tất cả hwmon devices và map labels (TDIE, TCTL, SYSTIN, etc.) vào các sensor tương ứng.
    GPU temperature có thể đọc từ nvidia-smi hoặc /sys/class/drm.
    
    Returns:
        Dictionary chứa label_temp_motherboard, label_temp_chipset, label_temp_cpu,
        label_temp_gpu, label_temp_ram với giá trị nhiệt độ hoặc "N/A"
    """
    temps: Dict[str, str] = {
        "label_temp_motherboard": "N/A",
        "label_temp_chipset": "N/A",
        "label_temp_cpu": "N/A",
        "label_temp_gpu": "N/A",
        "label_temp_ram": "N/A",
    }
    
    try:
        hwmon_path = Path("/sys/class/hwmon")
        if not hwmon_path.exists():
            return temps
        
        # Scan all hwmon devices
        for hwmon_dir in sorted(hwmon_path.glob("hwmon*")):
            try:
                # Get device name
                name_file = hwmon_dir / "name"
                if not name_file.exists():
                    continue
                device_name = name_file.read_text(encoding="utf-8").strip()
                
                # Scan all temp inputs
                for temp_input in sorted(hwmon_dir.glob("temp*_input")):
                    try:
                        # Get corresponding label file
                        temp_base = temp_input.stem.replace("_input", "")
                        label_file = hwmon_dir / f"{temp_base}_label"
                        
                        label = ""
                        if label_file.exists():
                            label = label_file.read_text(encoding="utf-8").strip()
                        
                        # Read temperature value (in millidegrees)
                        temp_value = int(temp_input.read_text(encoding="utf-8").strip())
                        temp_celsius = temp_value // 1000
                        
                        # Skip invalid temperatures
                        if temp_celsius <= 0 or temp_celsius > 200:
                            continue
                        
                        # Map labels to our system temps
                        label_upper = label.upper()
                        
                        # CPU temperature: Tdie, Tctl (k10temp), CPUTIN
                        if not temps["label_temp_cpu"] or temps["label_temp_cpu"] == "N/A":
                            if "TDIE" in label_upper or "TCTL" in label_upper:
                                temps["label_temp_cpu"] = f"{temp_celsius}°C"
                            elif "CPUTIN" in label_upper:
                                temps["label_temp_cpu"] = f"{temp_celsius}°C"
                        
                        # Chipset temperature: SYSTIN (System Input - chipset temperature)
                        if not temps["label_temp_chipset"] or temps["label_temp_chipset"] == "N/A":
                            if "SYSTIN" in label_upper:
                                temps["label_temp_chipset"] = f"{temp_celsius}°C"
                        
                        # Motherboard temperature: Try SMBUSMASTER or other system sensors
                        if not temps["label_temp_motherboard"] or temps["label_temp_motherboard"] == "N/A":
                            if "SMBUSMASTER" in label_upper or ("SYSTEM" in label_upper and "SYSTIN" not in label_upper):
                                temps["label_temp_motherboard"] = f"{temp_celsius}°C"
                            # Fallback: PCH_CHIP_TEMP if SYSTIN is not available (but usually 0)
                            elif "PCH_CHIP" in label_upper and temp_celsius > 0:
                                temps["label_temp_motherboard"] = f"{temp_celsius}°C"
                        
                        # RAM temperature: Look for RAM-related labels or AUXTIN (may be RAM)
                        if not temps["label_temp_ram"] or temps["label_temp_ram"] == "N/A":
                            if "RAM" in label_upper or "MEMORY" in label_upper:
                                temps["label_temp_ram"] = f"{temp_celsius}°C"
                            # AUXTIN might be RAM temp on some systems
                            elif "AUXTIN" in label_upper and temp_celsius > 20:  # Reasonable RAM temp
                                temps["label_temp_ram"] = f"{temp_celsius}°C"
                    
                    except (ValueError, OSError, IndexError):
                        continue
            
            except (OSError, FileNotFoundError):
                continue
        
        # Try to get GPU temperature from nvidia-smi
        if not temps["label_temp_gpu"] or temps["label_temp_gpu"] == "N/A":
            try:
                result = subprocess.run(
                    ["nvidia-smi", "--query-gpu=temperature.gpu", "--format=csv,noheader,nounits"],
                    capture_output=True,
                    text=True,
                    timeout=5,
                )
                if result.returncode == 0:
                    temp_str = result.stdout.strip().split("\n")[0].strip()
                    try:
                        gpu_temp = int(temp_str)
                        if gpu_temp > 0:
                            temps["label_temp_gpu"] = f"{gpu_temp}°C"
                    except ValueError:
                        pass
            except (FileNotFoundError, subprocess.TimeoutExpired):
                pass
        
        # Try to get GPU temperature from /sys/class/drm
        if not temps["label_temp_gpu"] or temps["label_temp_gpu"] == "N/A":
            try:
                drm_path = Path("/sys/class/drm")
                if drm_path.exists():
                    for card_path in sorted(drm_path.glob("card*")):
                        # Try device/temp1_input
                        temp_file = card_path / "device" / "temp1_input"
                        if temp_file.exists():
                            try:
                                temp_millidegrees = int(temp_file.read_text(encoding="utf-8").strip())
                                temp_celsius = temp_millidegrees // 1000
                                if temp_celsius > 0:
                                    temps["label_temp_gpu"] = f"{temp_celsius}°C"
                                    break
                            except (ValueError, OSError):
                                continue
            except (OSError, FileNotFoundError):
                pass
    
    except (OSError, FileNotFoundError):
        pass
    
    return temps


def run_snmpwalk(host: str = "localhost", community: str = "public", version: str = "2c", 
                 base_oid: str = "1.3.6.1.4.1.6574.3.1.1", timeout: int = 10) -> str:
    """Chạy snmpwalk để lấy thông tin storage volumes từ SNMP.
    
    Args:
        host: SNMP host (default: localhost)
        community: SNMP community string (default: public)
        version: SNMP version (default: 2c)
        base_oid: Base OID để walk (default: Synology volume branch)
        timeout: Timeout tính bằng giây (default: 10)
    
    Returns:
        Output từ snmpwalk command, hoặc empty string nếu lỗi
    """
    cmd = [
        "snmpwalk",
        "-v", version,
        "-c", community,
        "-On",  # Force numeric OIDs so downstream parsing matches expectations
        host,
        base_oid,
    ]
    try:
        completed = subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return completed.stdout
    except FileNotFoundError:
        # snmpwalk không tìm thấy, có thể không cài đặt
        return ""
    except subprocess.CalledProcessError as exc:
        # SNMP error, có thể là permission hoặc OID không đúng
        # Log stderr để debug nếu cần
        return ""
    except subprocess.TimeoutExpired:
        return ""


def parse_snmpwalk(output: str) -> Dict[str, object]:
    """Parse output từ snmpwalk thành dictionary với OID làm key.
    
    Args:
        output: Output từ snmpwalk command
    
    Returns:
        Dictionary với OID làm key và giá trị đã parse
    """
    data: Dict[str, object] = {}
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = SNMP_LINE_RE.match(line)
        if not match:
            continue
        value = match.group("value").strip()
        vtype = match.group("type").upper()
        if vtype in {"INTEGER", "COUNTER32", "COUNTER64", "GAUGE32"}:
            try:
                value = int(value)
            except ValueError:
                pass
        elif vtype == "STRING" and value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        oid = normalize_snmp_oid(match.group("oid"))
        data[oid] = value
    return data


def run_snmpget(host: str = "localhost", community: str = "public", version: str = "2c",
                oid: str = "", timeout: int = 10) -> str:
    """Chạy snmpget để lấy giá trị từ một OID cụ thể.
    
    Args:
        host: SNMP host (default: localhost)
        community: SNMP community string (default: public)
        version: SNMP version (default: 2c)
        oid: OID cần query
        timeout: Timeout tính bằng giây (default: 10)
    
    Returns:
        Output từ snmpget command, hoặc empty string nếu lỗi
    """
    if not oid:
        return ""
    
    cmd = [
        "snmpget",
        "-v", version,
        "-c", community,
        "-On",  # Force numeric OIDs for consistent parsing
        host,
        oid,
    ]
    try:
        completed = subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return completed.stdout
    except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return ""


def read_storage_volumes() -> Dict[str, str]:
    """Đọc thông tin storage volumes từ SNMP (thay vì df -h để nhanh hơn).
    
    Đọc thông tin từ SNMP OIDs và format thành label_storage_X
    (usage percentage) và label_storage_total_X (total size với unit Gb/Tb).
    
    Returns:
        Dictionary chứa label_storage_1-4 và label_storage_total_1-4 với giá trị đã format
    """
    volumes: Dict[str, str] = {}
    
    # Initialize defaults
    for i in range(1, 5):
        volumes[f"label_storage_{i}"] = "N/A"
        volumes[f"label_storage_total_{i}"] = "N/A"
    
    # Volume 4 defaults to 0
    volumes["label_storage_4"] = "0%"
    volumes["label_storage_total_4"] = "0 Gb"
    
    try:
        # Chạy snmpwalk để lấy thông tin volumes từ SNMP
        # Dùng localhost vì script chạy trên NAS
        snmp_output = run_snmpwalk(host="localhost", community="public", version="2c", 
                                   base_oid="1.3.6.1.4.1.6574.3.1.1", timeout=10)
        
        if snmp_output:
            # Parse SNMP output (giữ nguyên format OID như snmpwalk.py)
            snmp_data = parse_snmpwalk(snmp_output)
            
            # Tìm các volume từ SNMP data (giống hệt logic trong snmpwalk.py)
            found_any = False
            for oid, value in snmp_data.items():
                if not oid.startswith(VOLUME_NAME_PREFIX):
                    continue
                
                # Lấy tên volume (ví dụ: "Volume 1")
                name = str(value)
                labels = VOLUME_LABELS.get(name)
                if not labels:
                    continue
                
                usage_label, total_label = labels
                
                # Lấy index từ OID (ví dụ: iso.3.6.1.4.1.6574.3.1.1.2.1 -> index = 1)
                index = oid.split(".")[-1]
                
                # Lấy free space và total space từ các OID tương ứng
                free = snmp_data.get(f"{VOLUME_USED_PREFIX}{index}")
                total = snmp_data.get(f"{VOLUME_TOTAL_PREFIX}{index}")
                
                if isinstance(free, int) and isinstance(total, int) and total > 0:
                    found_any = True
                    # Tính used space = total - free (giống snmpwalk.py)
                    used = max(total - free, 0)
                    percent = (used / total) * 100.0
                    
                    # Format percentage
                    volumes[usage_label] = f"{percent:.1f}%"
                    
                    # Format total size với unit Gb/Tb (convert từ bytes)
                    # Convert bytes sang GB trước
                    total_gb = total / (1024.0 ** 3)
                    
                    # Nếu >= 1000 GB thì chuyển sang TB
                    if total_gb >= 1000:
                        total_tb = total_gb / 1024.0
                        # Format: làm tròn nếu là số nguyên
                        if total_tb == int(total_tb):
                            volumes[total_label] = f"{int(total_tb)} Tb"
                        else:
                            volumes[total_label] = f"{total_tb:.1f} Tb"
                    else:
                        # Format GB: làm tròn nếu là số nguyên
                        if total_gb == int(total_gb):
                            volumes[total_label] = f"{int(total_gb)} Gb"
                        else:
                            volumes[total_label] = f"{total_gb:.1f} Gb"
                else:
                    volumes[usage_label] = "N/A"
                    volumes[total_label] = "N/A"
            
            # Nếu tìm thấy ít nhất 1 volume từ SNMP, return kết quả
            if found_any:
                # Đảm bảo tất cả volumes 1-3 đều có giá trị
                for idx in range(1, 4):
                    usage_label = f"label_storage_{idx}"
                    total_label = f"label_storage_total_{idx}"
                    volumes.setdefault(usage_label, "N/A")
                    volumes.setdefault(total_label, "N/A")
                return volumes
        
        # Fallback: Nếu SNMP không hoạt động, dùng df -h (cách cũ)
        # Sử dụng awk để parse chính xác df -h output
        result = subprocess.run(
            ["sh", "-c", "df -h | awk '/\\/volume[123]$/ {print $6, $2, $3, $5}'"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            lines = result.stdout.strip().splitlines()
            volume_mounts = {}
            
            for line in lines:
                if not line.strip():
                    continue
                
                # Parse output: mountpoint size used use%
                # Example: /volume1 875G 224G 26%
                parts = line.split()
                if len(parts) < 4:
                    continue
                
                try:
                    mountpoint = parts[0]
                    size_str = parts[1]      # size (total)
                    used_str = parts[2]      # used
                    use_pct_str = parts[3].rstrip("%")  # use% (bỏ dấu %)
                    
                    # Extract volume number từ mountpoint
                    volume_match = re.match(r"/volume(\d+)", mountpoint)
                    if not volume_match:
                        continue
                    
                    vol_num = int(volume_match.group(1))
                    if not (1 <= vol_num <= 3):
                        continue
                    
                    # Store with volume number as key
                    volume_mounts[vol_num] = {
                        "total": size_str,
                        "used": used_str,
                        "percent": use_pct_str,
                    }
                except (ValueError, IndexError, AttributeError):
                    continue
            
            # Fill in the volumes we found (sort by volume number to ensure correct order)
            for vol_num in sorted(volume_mounts.keys()):
                info = volume_mounts[vol_num]
                volumes[f"label_storage_{vol_num}"] = f"{info['percent']}%"
                # Format storage total với unit Gb hoặc Tb, có dấu cách
                volumes[f"label_storage_total_{vol_num}"] = format_storage_size(info["total"])
    
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, OSError):
        pass
    
    return volumes


def format_cpu_metrics(cpu_clock_ghz: Optional[float], cpu_usage_percent: Optional[float]) -> Dict[str, str]:
    """Format CPU metrics thành dictionary với các label: label_cpu_usage, label_cpu_usage_per, bar_cpu_usage.
    
    Args:
        cpu_clock_ghz: CPU clock speed tính bằng GHz (có thể None)
        cpu_usage_percent: CPU usage percentage (có thể None)
    
    Returns:
        Dictionary chứa các label đã format cho CPU metrics
    """
    if cpu_clock_ghz is not None:
        clock_label = f"{cpu_clock_ghz:.2f} GHz"
    else:
        clock_label = "N/A"
    
    if cpu_usage_percent is not None:
        usage_percent = max(0, min(100, int(cpu_usage_percent)))
        usage_label = f"{usage_percent}%"
        bar_value = str(usage_percent)
    else:
        usage_label = "N/A"
        bar_value = "0"
    
    return {
        "label_cpu_usage": clock_label,
        "label_cpu_usage_per": usage_label,
        "bar_cpu_usage": bar_value,
    }


def format_ram_metrics(ram_info: Optional[Tuple[int, int]]) -> Dict[str, str]:
    """Format RAM metrics thành dictionary với các label: label_ram_usage, label_ram_usage_per, bar_ram_usage.
    
    Args:
        ram_info: Tuple (used_kb, total_kb) hoặc None nếu không đọc được
    
    Returns:
        Dictionary chứa các label đã format cho RAM metrics
    """
    if ram_info is not None:
        used_kb, total_kb = ram_info
        percent = (used_kb / total_kb) * 100.0 if total_kb > 0 else 0.0
        used_label = human_bytes(used_kb * 1024)
        total_label = human_bytes(total_kb * 1024)
        label = f"{used_label} / {total_label}"
        pct_label = f"{int(percent)}"
        return {
            "label_ram_usage": label,
            "label_ram_usage_per": f"{pct_label}%",
            "bar_ram_usage": pct_label,
        }
    return {
        "label_ram_usage": "N/A",
        "label_ram_usage_per": "N/A",
        "bar_ram_usage": "0",
    }


def format_gpu_metrics(gpu_clock_mhz: Optional[int], gpu_usage_percent: Optional[int], gpu_fan_speed: Optional[int] = None) -> Dict[str, str]:
    """Format GPU metrics thành dictionary với các label: label_gpu_usage, label_gpu_usage_per, bar_gpu_usage, label_gpu_fan_speed.
    
    Args:
        gpu_clock_mhz: GPU clock speed tính bằng MHz (có thể None)
        gpu_usage_percent: GPU usage percentage (có thể None)
        gpu_fan_speed: GPU fan speed percentage (có thể None)
    
    Returns:
        Dictionary chứa các label đã format cho GPU metrics
    """
    if gpu_clock_mhz is not None:
        clock_label = f"{gpu_clock_mhz} MHz"
    else:
        clock_label = "N/A"
    
    if gpu_usage_percent is not None:
        usage_percent = max(0, min(100, gpu_usage_percent))
        usage_label = f"{usage_percent}%"
        bar_value = str(usage_percent)
    else:
        usage_label = "N/A"
        bar_value = "0"
    
    if gpu_fan_speed is not None:
        fan_speed_percent = max(0, min(100, gpu_fan_speed))
        fan_speed_label = f"{fan_speed_percent}%"
    else:
        fan_speed_label = "N/A"
    
    return {
        "label_gpu_usage": clock_label,
        "label_gpu_usage_per": usage_label,
        "bar_gpu_usage": bar_value,
        "label_gpu_fan_speed": fan_speed_label,
    }


def get_network_interface() -> Optional[str]:
    """Tìm network interface chính (ưu tiên ovs_eth0, sau đó tìm interface có IP và không phải loopback).
    
    Ưu tiên các interface: ovs_eth0, eth0, ens33, enp0s3, wlan0. Nếu không tìm thấy,
    sẽ quét tất cả interface và chọn interface đầu tiên có IP và không phải loopback.
    
    Returns:
        Tên network interface, hoặc None nếu không tìm thấy
    """
    # Danh sách interface ưu tiên
    preferred_interfaces = ["ovs_eth0", "eth0", "ens33", "enp0s3", "wlan0"]
    
    # Thử các interface ưu tiên trước
    for iface in preferred_interfaces:
        iface_path = Path(f"/sys/class/net/{iface}")
        if iface_path.exists():
            # Kiểm tra xem có IP không
            try:
                result = subprocess.run(
                    ["ip", "-4", "addr", "show", iface],
                    capture_output=True,
                    text=True,
                    timeout=2,
                )
                if result.returncode == 0 and "inet" in result.stdout:
                    return iface
            except Exception:
                pass
    
    # Nếu không tìm thấy, tìm interface đầu tiên có IP và không phải loopback
    try:
        result = subprocess.run(
            ["ip", "-4", "addr", "show"],
            capture_output=True,
            text=True,
            timeout=2,
        )
        if result.returncode == 0:
            current_iface = None
            for line in result.stdout.splitlines():
                # Dòng bắt đầu với số là interface name
                if_match = re.match(r"^\d+:\s+(\w+):", line)
                if if_match:
                    current_iface = if_match.group(1)
                    if current_iface and current_iface != "lo":
                        # Kiểm tra xem có IP không
                        if "inet" in line or any("inet" in l for l in result.stdout.splitlines() if current_iface in l):
                            return current_iface
    except Exception:
        pass
    
    return None


def read_system_info() -> Dict[str, str]:
    """Đọc thông tin hệ thống: hostname, account (nối với IP address).
    
    Đọc hostname từ socket.gethostname() hoặc hostname command.
    Đọc username từ environment variable hoặc whoami command.
    Đọc IP address từ network interface (ưu tiên ovs_eth0).
    Nối username và IP address thành label_account với format "username - IP".
    
    Returns:
        Dictionary chứa label_hostname và label_account
    """
    info: Dict[str, str] = {
        "label_hostname": "N/A",
        "label_account": "N/A",
    }
    
    # Hostname
    try:
        hostname = socket.gethostname()
        if hostname:
            info["label_hostname"] = hostname
    except Exception:
        try:
            result = subprocess.run(
                ["hostname"],
                capture_output=True,
                text=True,
                timeout=2,
            )
            if result.returncode == 0:
                info["label_hostname"] = result.stdout.strip()
        except Exception:
            pass
    
    # Account (username) và IP address - nối lại thành một chuỗi
    account_parts = []
    
    # Đọc username
    try:
        username = os.getenv("USER") or os.getenv("USERNAME")
        if not username:
            # Fallback: dùng getpass hoặc whoami
            try:
                result = subprocess.run(
                    ["whoami"],
                    capture_output=True,
                    text=True,
                    timeout=2,
                )
                if result.returncode == 0:
                    username = result.stdout.strip()
            except Exception:
                pass
        if username:
            account_parts.append(username)
    except Exception:
        pass
    
    # Đọc IP address - lấy IP từ interface cụ thể (ưu tiên ovs_eth0)
    ip_address = None
    iface = get_network_interface()
    if iface:
        try:
            result = subprocess.run(
                ["ip", "-4", "addr", "show", iface],
                capture_output=True,
                text=True,
                timeout=2,
            )
            if result.returncode == 0:
                # Parse IP từ output: inet 192.168.1.3/24
                ip_match = re.search(r"inet\s+(\d+\.\d+\.\d+\.\d+)", result.stdout)
                if ip_match:
                    ip = ip_match.group(1)
                    if ip and not ip.startswith("127."):
                        ip_address = ip
        except Exception:
            pass
    
    # Fallback: nếu không tìm thấy IP từ interface cụ thể
    if not ip_address:
        try:
            # Thử lấy IP từ socket connection
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                s.connect(("8.8.8.8", 80))
                ip = s.getsockname()[0]
                if ip and ip != "127.0.0.1":
                    ip_address = ip
            except Exception:
                pass
            finally:
                s.close()
        except Exception:
            pass
    
    # Nối account và IP address lại
    if ip_address:
        account_parts.append(ip_address)
    
    # Gán vào label_account (nối bằng khoảng trắng)
    if account_parts:
        info["label_account"] = " - ".join(account_parts)
    
    return info


def read_system_status() -> Dict[str, str]:
    """Đọc system status từ SNMP (SYNOLOGY-SYSTEM-MIB).
    
    Đọc các giá trị từ OID 1.3.6.1.4.1.6574.1:
    - systemStatus.0: System status (INTEGER: 1=Normal, 2=Failed)
    - thermalStatus.0: Thermal status (INTEGER: 1=Normal, 2=Failed)
    - powerStatus.0: Power status (INTEGER: 1=Normal, 2=Failed)
    - systemFanStatus.0: System fan status (INTEGER: 1=Normal, 2=Failed)
    - upgradeAvailable.0: Upgrade available (INTEGER: 1=Yes, 0=No)
    - version.0: DSM version (STRING)
    
    Returns:
        Dictionary chứa các label system status
    """
    status: Dict[str, str] = {
        "label_system_status": "N/A",
        "label_thermal_status": "N/A",
        "label_power_status": "N/A",
        "label_system_fan_status": "N/A",
        "label_upgrade_available": "N/A",
        "label_version": "N/A",
    }
    
    def _value_to_int(raw: object) -> Optional[int]:
        if isinstance(raw, int):
            return raw
        if isinstance(raw, str):
            match = re.search(r"-?\d+", raw)
            if match:
                try:
                    return int(match.group(0))
                except ValueError:
                    return None
        return None

    def _map_status(raw: object, mapping: Dict[int, str]) -> Optional[str]:
        code = _value_to_int(raw)
        if code is None:
            return str(raw) if raw is not None else None
        return mapping.get(code, str(code))

    try:
        # Chạy snmpwalk để lấy system status từ SNMP
        snmp_output = run_snmpwalk(host="localhost", community="public", version="2c",
                                   base_oid="1.3.6.1.4.1.6574.1", timeout=10)
        if not snmp_output:
            return status
        
        # Parse SNMP output
        snmp_data = parse_snmpwalk(snmp_output)

        def _get_value(oid: str) -> Optional[object]:
            """Lấy giá trị OID từ dữ liệu SNMP, fallback sang snmpget nếu cần."""
            if not oid:
                return None
            if oid in snmp_data:
                return snmp_data[oid]
            # Fallback: thử snmpget để lấy trực tiếp từng OID
            try:
                raw = run_snmpget(host="localhost", community="public", version="2c", oid=oid, timeout=5)
                if raw:
                    parsed = parse_snmpwalk(raw)
                    if oid in parsed:
                        value = parsed[oid]
                        # Cache lại để lần sau khỏi gọi snmpget
                        snmp_data[oid] = value
                        return value
            except Exception:
                pass
            return None
        
        system_status_raw = _get_value(SYSTEM_STATUS_OID)
        thermal_status_raw = _get_value(THERMAL_STATUS_OID)
        power_status_raw = _get_value(POWER_STATUS_OID)
        system_fan_status_raw = _get_value(SYSTEM_FAN_STATUS_OID)
        upgrade_status_raw = _get_value(UPGRADE_AVAILABLE_OID)
        version_value = _get_value(VERSION_OID)

        system_status = _map_status(system_status_raw, SYSTEM_STATUS_MAP)
        thermal_status = _map_status(thermal_status_raw, SYSTEM_STATUS_MAP)
        power_status = _map_status(power_status_raw, SYSTEM_STATUS_MAP)
        system_fan_status = _map_status(system_fan_status_raw, SYSTEM_STATUS_MAP)
        upgrade_status = _map_status(upgrade_status_raw, UPGRADE_STATUS_MAP)

        if system_status:
            status["label_system_status"] = system_status
        if thermal_status:
            status["label_thermal_status"] = thermal_status
        if power_status:
            status["label_power_status"] = power_status
        if system_fan_status:
            status["label_system_fan_status"] = system_fan_status
        if upgrade_status:
            status["label_upgrade_available"] = upgrade_status
        if version_value:
            status["label_version"] = str(version_value)
    
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, OSError):
        pass
    
    return status


def get_physical_nic() -> Optional[str]:
    """Tìm NIC vật lý (không phải virtual như ovs_eth0, veth, bridge).
    
    Ưu tiên các interface: eth0, eth1, enp0s3, ens33, enp0s8. Kiểm tra xem interface
    có phải virtual không bằng cách resolve symlink và kiểm tra tốc độ từ ethtool.
    
    Returns:
        Tên NIC vật lý, hoặc None nếu không tìm thấy
    """
    # Danh sách interface ưu tiên (NIC vật lý thường là eth0, eth1, enp*, ens*)
    # Ưu tiên eth0 trước vì thường là NIC vật lý chính
    physical_interfaces = ["eth0", "eth1", "enp0s3", "ens33", "enp0s8"]
    
    # Thử các interface ưu tiên trước
    for iface in physical_interfaces:
        iface_path = Path(f"/sys/class/net/{iface}")
        if not iface_path.exists():
            continue
        
        # Kiểm tra xem có phải là NIC vật lý không (không phải virtual)
        # Virtual interface thường có symlink đến ../devices/virtual/...
        try:
            real_path = iface_path.resolve()
            if "virtual" in str(real_path):
                continue
            
            # Kiểm tra xem có tốc độ không
            try:
                result = subprocess.run(
                    ["ethtool", iface],
                    capture_output=True,
                    text=True,
                    timeout=2,
                )
                if result.returncode == 0:
                    # Kiểm tra xem có giá trị số trong Speed không (không phải "Unknown!")
                    speed_match = re.search(r"Speed:\s*(\d+)\s*Mb/s", result.stdout, re.IGNORECASE)
                    if speed_match:
                        return iface
            except Exception:
                continue
        except Exception:
            continue
    
    # Nếu không tìm thấy, tìm tất cả interface và loại bỏ virtual
    try:
        net_path = Path("/sys/class/net")
        if net_path.exists():
            for iface_dir in net_path.iterdir():
                iface = iface_dir.name
                # Bỏ qua loopback và các interface ảo phổ biến
                if iface == "lo" or iface.startswith("ovs_") or iface.startswith("veth") or iface.startswith("br-"):
                    continue
                
                # Kiểm tra xem có phải là NIC vật lý không
                try:
                    real_path = iface_dir.resolve()
                    if "virtual" in str(real_path):
                        continue
                    
                    # Kiểm tra xem có tốc độ không
                    try:
                        result = subprocess.run(
                            ["ethtool", iface],
                            capture_output=True,
                            text=True,
                            timeout=2,
                        )
                        if result.returncode == 0:
                            # Kiểm tra xem có giá trị số trong Speed không
                            speed_match = re.search(r"Speed:\s*(\d+)\s*Mb/s", result.stdout, re.IGNORECASE)
                            if speed_match:
                                return iface
                    except Exception:
                        continue
                except Exception:
                    continue
    except Exception:
        pass
    
    return None


def get_nic_max_speed_mbps(physical_iface: Optional[str] = None) -> Optional[float]:
    """Đọc tốc độ tối đa của NIC vật lý từ /sys/class/net/<iface>/speed (Mbps).
    
    Nếu physical_iface không được cung cấp, sẽ tự động tìm NIC vật lý (ưu tiên eth0).
    Thử đọc từ /sys/class/net/<iface>/speed trước, fallback sang ethtool nếu không đọc được.
    
    Args:
        physical_iface: Tên interface cần đọc (nếu None sẽ tự động tìm)
    
    Returns:
        Tốc độ tối đa tính bằng Mbps, hoặc None nếu không đọc được
    """
    # Nếu không có physical_iface, tìm NIC vật lý
    if not physical_iface:
        physical_iface = get_physical_nic()
        # Fallback: nếu không tìm được, thử trực tiếp với eth0 (NIC vật lý phổ biến nhất)
        if not physical_iface:
            physical_iface = "eth0"
    
    # Thử đọc tốc độ từ /sys/class/net/<iface>/speed
    interfaces_to_try = [physical_iface] if physical_iface else []
    # Nếu physical_iface không phải eth0, thêm eth0 vào danh sách thử
    if physical_iface != "eth0":
        interfaces_to_try.append("eth0")
    
    for test_iface in interfaces_to_try:
        try:
            speed_path = Path(f"/sys/class/net/{test_iface}/speed")
            if speed_path.exists():
                speed_str = speed_path.read_text().strip()
                # File speed chứa tốc độ tính bằng Mbps (ví dụ: "2500")
                try:
                    speed_mbps = float(speed_str)
                    if speed_mbps > 0:
                        return speed_mbps
                except ValueError:
                    continue
        except (OSError, ValueError):
            continue
    
    # Fallback: thử dùng ethtool nếu không đọc được từ /sys
    for test_iface in interfaces_to_try:
        try:
            result = subprocess.run(
                ["ethtool", test_iface],
                capture_output=True,
                text=True,
                timeout=3,
            )
            if result.returncode == 0:
                # Tìm dòng "Speed: 2500Mb/s"
                speed_match = re.search(r"Speed:\s*(\d+)\s*Mb/s", result.stdout, re.IGNORECASE)
                if speed_match:
                    speed_mbps = float(speed_match.group(1))
                    return speed_mbps
        except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, AttributeError):
            continue
    
    return None


def read_disk_io() -> Dict[str, str]:
    """Đọc disk I/O statistics từ iostat command.
    
    Chạy iostat -dy và parse output để lấy:
    - tps (transfers per second) -> label_disk_iops
    - kB_read/s (tổng read) -> label_disk_read
    - kB_wrtn/s (tổng write) -> label_disk_write
    
    Format output:
    - label_disk_iops: "471" (IOPS)
    - label_disk_read: "2.36 MB/s" (đã convert từ kB/s sang MB/s)
    - label_disk_write: "6.93 MB/s" (đã convert từ kB/s sang MB/s)
    
    Returns:
        Dictionary chứa label_disk_iops, label_disk_read, label_disk_write
    """
    result: Dict[str, str] = {
        "label_disk_iops": "N/A",
        "label_disk_read": "N/A",
        "label_disk_write": "N/A",
    }
    
    try:
        # Chạy iostat -dy và parse với awk
        # Format: iostat -dy | awk 'NR>3 {tps+=$2; read+=$3; write+=$4} END {printf "%.0f %.2f %.2f\n",tps,read/1024,write/1024}'
        cmd = [
            "sh", "-c",
            "iostat -dy | awk 'NR>3 {tps+=$2; read+=$3; write+=$4} END {printf \"%.0f %.2f %.2f\\n\",tps,read/1024,write/1024}'"
        ]
        
        completed = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=5,
        )
        
        if completed.returncode == 0:
            output = completed.stdout.strip()
            if output:
                # Parse output: "471 2.36 6.93" (iops read_mb write_mb)
                parts = output.split()
                if len(parts) >= 3:
                    try:
                        iops = int(float(parts[0]))
                        read_mb = float(parts[1])
                        write_mb = float(parts[2])
                        
                        result["label_disk_iops"] = str(iops)
                        result["label_disk_read"] = f"{read_mb:.2f} MB/s"
                        result["label_disk_write"] = f"{write_mb:.2f} MB/s"
                    except (ValueError, IndexError):
                        pass
    
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, OSError):
        pass
    
    return result


def read_network_speed() -> Dict[str, str]:
    """Đọc tốc độ download và upload từ network interface (tự động chọn Kbps hoặc Mbps).
    
    Đọc statistics từ /sys/class/net/<iface>/statistics/rx_bytes và tx_bytes,
    đợi 1 giây rồi đọc lại để tính tốc độ. Tự động chọn Kbps nếu < 1 Mbps, ngược lại dùng Mbps.
    Tính phần trăm cho arc bằng cách so sánh với tốc độ tối đa của NIC vật lý.
    
    Returns:
        Dictionary chứa label_download_total, label_upload_total, arc_download_total, arc_upload_total
    """
    result: Dict[str, str] = {
        "label_download_total": "N/A",
        "label_upload_total": "N/A",
        "arc_download_total": "0",
        "arc_upload_total": "0",
    }
    
    iface = get_network_interface()
    if not iface:
        return result
    
    try:
        # Đường dẫn đến statistics
        rx_path = Path(f"/sys/class/net/{iface}/statistics/rx_bytes")
        tx_path = Path(f"/sys/class/net/{iface}/statistics/tx_bytes")
        
        if not rx_path.exists() or not tx_path.exists():
            return result
        
        # Đọc lần 1
        try:
            rx1 = int(rx_path.read_text().strip())
            tx1 = int(tx_path.read_text().strip())
        except (ValueError, OSError):
            return result
        
        # Đợi 1 giây
        time.sleep(1)
        
        # Đọc lần 2
        try:
            rx2 = int(rx_path.read_text().strip())
            tx2 = int(tx_path.read_text().strip())
        except (ValueError, OSError):
            return result
        
        # Tính tốc độ: bytes_diff * 8 = bits
        rx_diff = rx2 - rx1
        tx_diff = tx2 - tx1
        
        rx_bits = rx_diff * 8
        tx_bits = tx_diff * 8
        
        # Format: tự động chọn Kbps hoặc Mbps
        # Nếu < 1024 Kbps (1 Mbps) thì dùng Kbps, ngược lại dùng Mbps
        # Gộp giá trị và unit vào một label (ví dụ: "60.0 Kbps")
        if rx_bits < 1024 * 1024:  # < 1 Mbps
            rx_kbps = rx_bits / 1024
            result["label_download_total"] = f"{rx_kbps:.1f} Kbps"
            rx_mbps = rx_kbps / 1024  # Convert sang Mbps để tính phần trăm
        else:
            rx_mbps = rx_bits / (1024 * 1024)
            result["label_download_total"] = f"{rx_mbps:.1f} Mbps"
        
        if tx_bits < 1024 * 1024:  # < 1 Mbps
            tx_kbps = tx_bits / 1024
            result["label_upload_total"] = f"{tx_kbps:.1f} Kbps"
            tx_mbps = tx_kbps / 1024  # Convert sang Mbps để tính phần trăm
        else:
            tx_mbps = tx_bits / (1024 * 1024)
            result["label_upload_total"] = f"{tx_mbps:.1f} Mbps"
        
        # Tính phần trăm cho arc: tốc độ hiện tại / tốc độ tối đa * 100
        # Lưu ý: 
        # - Đọc statistics từ interface ảo (ovs_eth0) - đã đúng
        # - Đọc tốc độ tối đa từ NIC vật lý (eth0) - từ /sys/class/net/eth0/speed
        try:
            max_speed_mbps = get_nic_max_speed_mbps()  # Tự động tìm NIC vật lý (eth0)
            if max_speed_mbps and max_speed_mbps > 0:
                # Tính phần trăm với độ chính xác cao hơn (giống như awk trong test)
                # Format: mbps*100/speed
                download_percent_float = (rx_mbps / max_speed_mbps) * 100.0
                upload_percent_float = (tx_mbps / max_speed_mbps) * 100.0
                
                # Clamp trong khoảng 0-100 và làm tròn (có thể dùng 1 chữ số thập phân nếu cần)
                # Nhưng vì arc thường dùng số nguyên, nên làm tròn thành int
                download_percent = min(100, max(0, int(round(download_percent_float))))
                upload_percent = min(100, max(0, int(round(upload_percent_float))))
                
                result["arc_download_total"] = str(download_percent)
                result["arc_upload_total"] = str(upload_percent)
        except Exception as e:
            # Nếu không tìm được tốc độ tối đa, giữ giá trị mặc định (0)
            pass
        
    except Exception:
        pass
    
    return result


def read_ping() -> Dict[str, str]:
    """Ping tới google.com để lấy latency.
    
    Returns:
        Dictionary chứa label_ping_total với giá trị như "10 ms" hoặc "N/A"
    """
    result: Dict[str, str] = {
        "label_ping_total": "N/A",
    }
    
    try:
        # Ping google.com với 1 packet, timeout 3 giây
        ping_result = subprocess.run(
            ["ping", "-c", "1", "-W", "3", "google.com"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        
        if ping_result.returncode == 0:
            # Parse output để lấy time, ví dụ: "time=10.123 ms" hoặc "time=10 ms"
            time_match = re.search(r"time[=<](\d+(?:\.\d+)?)\s*ms", ping_result.stdout, re.IGNORECASE)
            if time_match:
                ping_time = float(time_match.group(1))
                # Format với 1 chữ số thập phân nếu cần, nhưng nếu là số nguyên thì không cần
                if ping_time == int(ping_time):
                    result["label_ping_total"] = f"{int(ping_time)} ms"
                else:
                    result["label_ping_total"] = f"{ping_time:.1f} ms"
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, OSError):
        pass
    
    return result


def aggregate_metrics() -> Dict[str, str]:
    """Thu thập tất cả metrics và trả về dictionary.
    
    Gọi tất cả các hàm đọc sensor và format metrics, sau đó đảm bảo tất cả labels
    trong LABEL_ORDER đều có trong dictionary (mặc định "N/A" nếu thiếu).
    
    Returns:
        Dictionary chứa tất cả metrics theo thứ tự LABEL_ORDER
    """
    metrics: Dict[str, str] = {}
    
    # Read all sensor data
    metrics.update(read_storage_volumes())
    metrics.update(read_fan_speeds())
    
    cpu_clock = read_cpu_clock_ghz()
    cpu_usage = read_cpu_usage()
    metrics.update(format_cpu_metrics(cpu_clock, cpu_usage))
    
    ram_info = read_ram_info()
    metrics.update(format_ram_metrics(ram_info))
    
    gpu_clock = read_gpu_clock_mhz()
    gpu_usage, gpu_fan_speed = read_gpu_usage_percent()
    metrics.update(format_gpu_metrics(gpu_clock, gpu_usage, gpu_fan_speed))
    
    metrics.update(read_disk_temps())
    metrics.update(read_disk_status())
    metrics.update(read_system_temps())
    metrics.update(read_system_info())
    metrics.update(read_system_status())
    metrics.update(read_network_speed())
    metrics.update(read_disk_io())
    metrics.update(read_ping())
    
    # Ensure all labels exist
    for label in LABEL_ORDER:
        metrics.setdefault(label, "N/A")
    
    return metrics


def _format_metrics_payload(metrics: Dict[str, str], labels: List[str]) -> str:
    """Format metrics thành chuỗi theo thứ tự labels được chỉ định.
    
    Args:
        metrics: Dictionary chứa tất cả metrics
        labels: Danh sách label cần format (có thể là WAKEUP_LABELS hoặc DYNAMIC_LABELS)
    
    Returns:
        Chuỗi đã format với các dòng "label: value"
    """
    lines = [f"{label}: {metrics[label]}" for label in labels]
    return "\n".join(lines) + "\n"


def _format_all_metrics_payload(metrics: Dict[str, str]) -> str:
    """Format tất cả metrics thành chuỗi theo thứ tự LABEL_ORDER (tương thích với code cũ)."""
    return _format_metrics_payload(metrics, LABEL_ORDER)


def write_output(metrics: Dict[str, str], output_file: Path) -> None:
    """Ghi metrics ra file theo thứ tự LABEL_ORDER.
    
    Args:
        metrics: Dictionary chứa tất cả metrics
        output_file: Đường dẫn file output cần ghi
    """
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(_format_all_metrics_payload(metrics), encoding="utf-8")


def write_serial(metrics: Dict[str, str], serial_path: str, retries: int = 3, retry_delay: float = 0.5, labels: Optional[List[str]] = None) -> bool:
    """Ghi metrics xuống thiết bị serial (ví dụ /dev/ttyACM0) để ESP32 đọc được.
    
    Args:
        metrics: Dictionary chứa tất cả metrics
        serial_path: Đường dẫn thiết bị serial (ví dụ: /dev/ttyACM0)
        retries: Số lần thử lại ghi nếu gặp lỗi tạm thời
        retry_delay: Thời gian (giây) giữa các lần thử lại
        labels: Danh sách label cần gửi (None = gửi tất cả theo LABEL_ORDER)
    
    Returns:
        True nếu ghi thành công, False nếu thất bại
    """
    labels_to_send = labels if labels is not None else LABEL_ORDER
    payload_bytes = _format_metrics_payload(metrics, labels_to_send).encode("utf-8")
    for attempt in range(1, retries + 1):
        try:
            with open(serial_path, "wb", buffering=0) as serial_file:
                # Set DTR/RTS để ESP32 biết host đã mở port
                set_serial_dtr_rts(serial_file, dtr=True, rts=True)
                time.sleep(0.1)  # Đợi ESP32 nhận biết
                serial_file.write(payload_bytes)
                serial_file.flush()  # Đảm bảo dữ liệu được gửi ngay
                return True
        except OSError as exc:
            if attempt == retries:
                print(
                    f"Không thể ghi dữ liệu ra serial {serial_path}: {exc}",
                    file=sys.stderr,
                )
                return False
            time.sleep(retry_delay)
    return False


def write_serial_optimized(serial_file, metrics: Dict[str, str], labels: Optional[List[str]] = None) -> bool:
    """Ghi metrics xuống serial file đã mở (tối ưu cho chế độ loop).
    
    Args:
        serial_file: File handle đã mở của thiết bị serial
        metrics: Dictionary chứa tất cả metrics
        labels: Danh sách label cần gửi (None = gửi tất cả theo LABEL_ORDER)
    
    Returns:
        True nếu ghi thành công, False nếu thất bại
    """
    try:
        labels_to_send = labels if labels is not None else LABEL_ORDER
        payload_bytes = _format_metrics_payload(metrics, labels_to_send).encode("utf-8")
        serial_file.write(payload_bytes)
        serial_file.flush()  # Đảm bảo dữ liệu được gửi ngay
        return True
    except OSError:
        return False


def write_serial_with_retry(
    serial_file,
    metrics: Dict[str, str],
    labels: Optional[List[str]] = None,
    retries: int = 3,
    retry_delay: float = 0.5,
    dataset_name: str = "serial data",
) -> bool:
    """Ghi dữ liệu xuống serial với cơ chế retry.
    
    Args:
        serial_file: Serial file handle đã mở
        metrics: Dictionary chứa metrics
        labels: Danh sách label cần gửi
        retries: Số lần retry khi thất bại (không tính lần gửi đầu tiên)
        retry_delay: Delay giữa các lần retry (giây)
        dataset_name: Tên dữ liệu để log khi retry
    
    Returns:
        True nếu ghi thành công, False nếu hết retry mà vẫn thất bại
    """
    max_attempts = max(1, int(retries) + 1)
    delay_seconds = max(0.0, retry_delay)
    for attempt in range(1, max_attempts + 1):
        if write_serial_optimized(serial_file, metrics, labels):
            return True
        if attempt < max_attempts:
            print(
                f"⚠ Không gửi được {dataset_name} (attempt {attempt}/{max_attempts}), "
                f"đang retry sau {delay_seconds:.2f}s..."
            )
            time.sleep(delay_seconds)
    return False


def find_esp32_usb_device(vendor_id: str = "303a", model_id: str = "4001") -> Optional[str]:
    """Tự động tìm USB device ESP32 theo vendor ID và model ID.
    
    Tìm kiếm trong:
    1. /dev/serial/by-id/ - tìm symlink có chứa vendor/model ID
    2. /sys/bus/usb/devices/ - tìm device có vendor/model ID rồi map tới tty
    
    Args:
        vendor_id: Vendor ID (hex, ví dụ: "303a")
        model_id: Model ID (hex, ví dụ: "4001")
    
    Returns:
        Đường dẫn tới tty device (ví dụ: "/dev/ttyACM0") hoặc None nếu không tìm thấy
    """
    vendor_id_lower = vendor_id.lower()
    model_id_lower = model_id.lower()
    
    # Cách 1: Tìm trong /dev/serial/by-id/
    by_id_path = Path("/dev/serial/by-id")
    if by_id_path.exists():
        try:
            for symlink in by_id_path.iterdir():
                if symlink.is_symlink():
                    link_name = symlink.name.lower()
                    # Kiểm tra xem tên có chứa vendor/model ID không
                    # Format có thể là: usb-..._303a_4001-... hoặc tương tự
                    if vendor_id_lower in link_name and model_id_lower in link_name:
                        # Resolve symlink để lấy device thực
                        real_path = symlink.resolve()
                        if real_path.exists():
                            return str(real_path)
        except (OSError, PermissionError):
            pass
    
    # Cách 2: Tìm trong /sys/bus/usb/devices/
    usb_devices_path = Path("/sys/bus/usb/devices")
    if usb_devices_path.exists():
        try:
            for device_dir in usb_devices_path.iterdir():
                if not device_dir.is_dir():
                    continue
                
                # Đọc vendor và model ID
                vendor_file = device_dir / "idVendor"
                model_file = device_dir / "idProduct"
                
                if vendor_file.exists() and model_file.exists():
                    try:
                        device_vendor = vendor_file.read_text().strip().lower()
                        device_model = model_file.read_text().strip().lower()
                        
                        if device_vendor == vendor_id_lower and device_model == model_id_lower:
                            # Tìm tty device tương ứng
                            # USB device thường có subdirectory như "1-1.4:1.0"
                            for subdir in device_dir.iterdir():
                                if subdir.is_dir() and ":" in subdir.name:
                                    # Tìm tty device trong subdirectory
                                    tty_path = subdir / "tty"
                                    if tty_path.exists():
                                        # Tìm tty device name
                                        for tty_name in tty_path.iterdir():
                                            if tty_name.name.startswith("tty"):
                                                device_path = Path(f"/dev/{tty_name.name}")
                                                if device_path.exists():
                                                    return str(device_path)
                                    
                                    # Hoặc tìm trong /sys/class/tty/
                                    tty_class_path = Path("/sys/class/tty")
                                    if tty_class_path.exists():
                                        for tty_class in tty_class_path.iterdir():
                                            device_link = tty_class / "device"
                                            if device_link.exists() and device_link.is_symlink():
                                                device_real = device_link.resolve()
                                                if str(device_real).startswith(str(subdir.resolve())):
                                                    tty_name = tty_class.name
                                                    device_path = Path(f"/dev/{tty_name}")
                                                    if device_path.exists():
                                                        return str(device_path)
                    except (OSError, ValueError, PermissionError):
                        continue
        except (OSError, PermissionError):
            pass
    
    # Cách 3: Fallback - tìm trong /dev/ttyACM* và /dev/ttyUSB*
    # Kiểm tra xem có device nào match không (ít chính xác hơn)
    for pattern in ["/dev/ttyACM*", "/dev/ttyUSB*"]:
        try:
            import glob
            for device_path in glob.glob(pattern):
                device = Path(device_path)
                if device.exists() and device.is_char_device():
                    # Có thể kiểm tra thêm bằng cách đọc từ sysfs
                    # Nhưng cách này không chắc chắn, chỉ dùng làm fallback
                    return device_path
        except Exception:
            pass
    
    return None


def check_usb_device_exists(device_path: str) -> bool:
    """Kiểm tra xem USB device có tồn tại và có thể truy cập được không.
    
    Args:
        device_path: Đường dẫn tới USB device (ví dụ: /dev/ttyACM0)
    
    Returns:
        True nếu device tồn tại và có thể truy cập, False nếu không
    """
    try:
        device = Path(device_path)
        # Kiểm tra file có tồn tại và là character device
        return device.exists() and device.is_char_device()
    except Exception:
        return False


def wait_for_usb_connection(vendor_id: str, model_id: str, serial_device: Optional[str] = None, 
                            check_interval: float = 2.0, max_wait: Optional[float] = None) -> Optional[str]:
    """Đợi USB device xuất hiện (ESP32 được cắm vào).
    
    Args:
        vendor_id: Vendor ID để tìm USB device
        model_id: Model ID để tìm USB device
        serial_device: Đường dẫn device cố định (nếu có), nếu None sẽ tự động tìm
        check_interval: Khoảng thời gian (giây) giữa các lần kiểm tra
        max_wait: Thời gian tối đa (giây) để đợi, None = đợi vô hạn
    
    Returns:
        Đường dẫn tới USB device nếu tìm thấy, None nếu timeout
    """
    start_time = time.time()
    last_log_time = 0.0
    log_interval = 10.0  # Log mỗi 10 giây
    
    while True:
        current_time = time.time()
        elapsed = current_time - start_time
        
        # Kiểm tra timeout
        if max_wait and elapsed >= max_wait:
            return None
        
        # Nếu có serial_device cố định, kiểm tra nó
        if serial_device:
            if check_usb_device_exists(serial_device):
                return serial_device
        else:
            # Tự động tìm USB device
            found_device = find_esp32_usb_device(vendor_id, model_id)
            if found_device and check_usb_device_exists(found_device):
                return found_device
        
        # Log định kỳ để biết script vẫn đang chạy
        if current_time - last_log_time >= log_interval:
            if serial_device:
                print(f"Đang đợi USB device {serial_device} xuất hiện... (đã đợi {elapsed:.0f}s)")
            else:
                print(f"Đang đợi ESP32 USB device (vendor: {vendor_id}, model: {model_id})... (đã đợi {elapsed:.0f}s)")
            last_log_time = current_time
        
        time.sleep(check_interval)


def check_esp32_backlight_state(serial_file, debug: bool = False) -> Optional[bool]:
    """Kiểm tra trạng thái backlight của ESP32 bằng cách đọc từ serial.
    
    ESP32 sẽ gửi:
    - 'W' (byte 0x57) khi backlight bật (bsp_display_backlight_on)
    - 'S' (byte 0x53) khi backlight tắt (bsp_display_backlight_off)
    
    Args:
        serial_file: File handle đã mở của thiết bị serial (ở chế độ r+b)
        debug: Nếu True, sẽ in ra các byte nhận được để debug
    
    Returns:
        True nếu backlight bật, False nếu tắt, None nếu không có tín hiệu mới
    """
    try:
        # Kiểm tra xem có dữ liệu sẵn sàng không (dùng select cho file object)
        ready, _, _ = select.select([serial_file], [], [], 0)
        if not ready:
            return None  # Không có dữ liệu
        
        # Đọc tối đa 64 bytes để tìm tín hiệu backlight
        data = serial_file.read(64)
        if not data:
            return None
        
        if debug:
            # Debug: in ra các byte nhận được
            byte_str = ' '.join([f'{b:02x}' if isinstance(b, int) else f'{ord(b):02x}' for b in data])
            print(f"[DEBUG] Nhận được {len(data)} bytes: {byte_str}")
        
        # Tìm byte 'W' (wake/backlight on) hoặc 'S' (sleep/backlight off) trong buffer
        # Ưu tiên tín hiệu mới nhất (byte cuối cùng)
        for byte in reversed(data):
            if isinstance(byte, int):
                byte_val = byte
            else:
                byte_val = ord(byte)
            
            if byte_val == ord('W'):
                if debug:
                    print("[DEBUG] Tìm thấy tín hiệu 'W' (backlight on)")
                return True  # Backlight on
            elif byte_val == ord('S'):
                if debug:
                    print("[DEBUG] Tìm thấy tín hiệu 'S' (backlight off)")
                return False  # Backlight off
        
        if debug:
            print(f"[DEBUG] Không tìm thấy tín hiệu 'W' hoặc 'S' trong {len(data)} bytes")
        return None  # Không tìm thấy tín hiệu
    except (OSError, AttributeError, ValueError) as e:
        # Lỗi đọc hoặc không hỗ trợ select
        if debug:
            print(f"[DEBUG] Lỗi đọc serial: {e}")
        return None


def acquire_lock(lock_file_path: Path) -> Optional[object]:
    """Tạo và khóa lock file để tránh chạy nhiều process cùng lúc.
    
    Args:
        lock_file_path: Đường dẫn đến lock file (ví dụ: read_sensor.lock)
    
    Returns:
        File handle của lock file nếu thành công, None nếu đã có process khác đang chạy
    
    Raises:
        SystemExit: Nếu không thể tạo lock file hoặc đã có process khác đang chạy
    """
    lock_file = None
    
    try:
        # Tạo thư mục chứa lock file nếu chưa có
        lock_file_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Mở lock file ở chế độ read+write
        lock_file = open(lock_file_path, "w+")
        
        # Thử khóa file (non-blocking)
        try:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            # File đã bị khóa bởi process khác
            lock_file.close()
            print(
                f"Lỗi: Đã có process khác đang chạy (lock file: {lock_file_path})",
                file=sys.stderr,
            )
            print("Vui lòng đợi process hiện tại kết thúc hoặc xóa lock file nếu process đã bị kill.", file=sys.stderr)
            sys.exit(1)
        
        # Ghi PID vào lock file để có thể kiểm tra stale lock
        lock_file.seek(0)
        lock_file.truncate()
        lock_file.write(f"{os.getpid()}\n")
        lock_file.flush()
        
        # Đăng ký cleanup function để xóa lock khi thoát
        def cleanup_lock():
            try:
                if lock_file:
                    fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
                    lock_file.close()
                if lock_file_path.exists():
                    lock_file_path.unlink()
            except Exception:
                pass
        
        atexit.register(cleanup_lock)
        
        return lock_file
        
    except OSError as exc:
        if lock_file:
            try:
                lock_file.close()
            except Exception:
                pass
        print(
            f"Lỗi: Không thể tạo lock file {lock_file_path}: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)


def check_stale_lock(lock_file_path: Path) -> bool:
    """Kiểm tra xem lock file có phải là stale lock (process đã chết) không.
    
    Args:
        lock_file_path: Đường dẫn đến lock file
    
    Returns:
        True nếu lock file là stale (process không còn chạy), False nếu process vẫn đang chạy
    """
    if not lock_file_path.exists():
        return False
    
    try:
        # Đọc PID từ lock file
        pid_str = lock_file_path.read_text(encoding="utf-8").strip()
        if not pid_str:
            return True  # Lock file rỗng, coi như stale
        
        pid = int(pid_str)
        
        # Kiểm tra xem process có còn chạy không
        try:
            # Gửi signal 0 để kiểm tra process (không kill process)
            os.kill(pid, 0)
            return False  # Process vẫn đang chạy
        except ProcessLookupError:
            return True  # Process không tồn tại (stale lock)
        except PermissionError:
            # Không có quyền kiểm tra, giả sử process vẫn chạy
            return False
    except (ValueError, OSError):
        return True  # Không đọc được, coi như stale


def kill_existing_process(lock_file_path: Path) -> bool:
    """Kill process cũ nếu đang chạy (dựa trên PID trong lock file).
    
    Hàm này cho phép restart script bằng cách kill process cũ trước khi chạy mới.
    Hữu ích khi chạy trong DSM Task Scheduler (chỉ có nút Run, không có nút Stop).
    
    Args:
        lock_file_path: Đường dẫn đến lock file chứa PID của process cũ
    
    Returns:
        True nếu đã kill process cũ, False nếu không có process cũ hoặc không thể kill
    """
    if not lock_file_path.exists():
        return False
    
    try:
        # Đọc PID từ lock file
        pid_str = lock_file_path.read_text(encoding="utf-8").strip()
        if not pid_str:
            return False
        
        pid = int(pid_str)
        
        # Kiểm tra xem process có còn chạy không
        try:
            # Gửi signal 0 để kiểm tra process (không kill process)
            os.kill(pid, 0)
            # Process vẫn đang chạy, kill nó
            print(f"Phát hiện process cũ đang chạy (PID: {pid}) - Đang kill để restart với code mới...")
            try:
                # Gửi SIGTERM trước (graceful shutdown)
                os.kill(pid, signal.SIGTERM)
                # Đợi một chút để process tự dừng
                time.sleep(2.0)
                # Kiểm tra lại xem process đã dừng chưa
                try:
                    os.kill(pid, 0)
                    # Vẫn còn chạy, dùng SIGKILL (force kill)
                    print(f"Process chưa dừng, đang force kill (SIGKILL)...")
                    os.kill(pid, signal.SIGKILL)
                    time.sleep(0.5)
                except ProcessLookupError:
                    # Process đã dừng
                    pass
            except ProcessLookupError:
                # Process đã dừng trước đó
                pass
            except PermissionError:
                print(f"Không có quyền kill process {pid}", file=sys.stderr)
                return False
            
            # Đợi một chút để lock file được giải phóng
            time.sleep(0.5)
            
            # Xóa lock file nếu vẫn còn
            try:
                if lock_file_path.exists():
                    lock_file_path.unlink()
            except OSError:
                pass
            
            print(f"Đã kill process cũ (PID: {pid}) - Sẵn sàng chạy với code mới")
            return True
            
        except ProcessLookupError:
            # Process không tồn tại, lock file là stale
            print(f"Lock file có PID {pid} nhưng process không còn chạy - Xóa stale lock file")
            try:
                lock_file_path.unlink()
            except OSError:
                pass
            return False
        except PermissionError:
            # Không có quyền kiểm tra/kill
            print(f"Không có quyền kiểm tra/kill process {pid}", file=sys.stderr)
            return False
            
    except (ValueError, OSError) as exc:
        # Không đọc được lock file
        print(f"Không thể đọc lock file: {exc}", file=sys.stderr)
        return False


def create_version_json(script_dir: Path) -> None:
    """Tạo file version.json với thông tin version và firmware.
    
    Args:
        script_dir: Thư mục chứa script (để lưu version.json)
    """
    firmware_path = script_dir / 'JonsboN4Monitor.bin'
    version_json_path = script_dir / 'version.json'
    
    try:
        firmware_size = firmware_path.stat().st_size if firmware_path.exists() else 0
        version_info = {
            "version": VERSION,
            "firmware_size": firmware_size,
            "firmware_path": str(firmware_path),
            "available": firmware_path.exists(),
            "build_date": datetime.now().isoformat(),
            "server_script": "read_sensor.py"
        }
        
        with open(version_json_path, 'w', encoding='utf-8') as f:
            json.dump(version_info, f, indent=2, ensure_ascii=False)
        
        print(f"✓ Đã tạo version.json: version {VERSION}, firmware size: {firmware_size} bytes")
    except Exception as e:
        print(f"⚠ Không thể tạo version.json: {e}", file=sys.stderr)


def load_version_json(script_dir: Path) -> Optional[Dict]:
    """Đọc version.json nếu có.
    
    Args:
        script_dir: Thư mục chứa version.json
        
    Returns:
        Dict chứa version info hoặc None nếu không đọc được
    """
    version_json_path = script_dir / 'version.json'
    try:
        if version_json_path.exists():
            with open(version_json_path, 'r', encoding='utf-8') as f:
                return json.load(f)
    except Exception:
        pass
    return None


class OTARequestHandler(BaseHTTPRequestHandler):
    """HTTP Request Handler cho OTA server.
    
    Serve firmware file và version info cho ESP32 OTA update.
    """
    
    def do_GET(self):
        """Handle GET requests."""
        script_dir = Path(__file__).parent
        firmware_path = script_dir / 'JonsboN4Monitor.bin'
        
        if self.path == '/firmware.bin' or self.path == '/':
            # Serve firmware file
            if firmware_path.exists():
                try:
                    file_size = firmware_path.stat().st_size
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(file_size))
                    self.send_header('Content-Disposition', 'attachment; filename="JonsboN4Monitor.bin"')
                    self.end_headers()
                    
                    # Stream file in chunks
                    with open(firmware_path, 'rb') as f:
                        while True:
                            chunk = f.read(8192)  # 8KB chunks
                            if not chunk:
                                break
                            self.wfile.write(chunk)
                except Exception as e:
                    self.send_response(500)
                    self.end_headers()
                    self.wfile.write(f"Error serving file: {e}".encode())
            else:
                self.send_response(404)
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(b'Firmware file not found')
        
        elif self.path == '/version' or self.path == '/version.json':
            # Return version info as JSON (đọc từ version.json nếu có)
            try:
                version_info = load_version_json(script_dir)
                if version_info is None:
                    # Fallback nếu không có version.json
                    firmware_size = firmware_path.stat().st_size if firmware_path.exists() else 0
                    version_info = {
                        "version": VERSION,
                        "firmware_size": firmware_size,
                        "firmware_path": str(firmware_path),
                        "available": firmware_path.exists()
                    }
                
                # Convert to JSON string first to get length
                json_data = json.dumps(version_info, indent=2).encode('utf-8')
                
                self.send_response(200)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Content-Length', str(len(json_data)))
                self.end_headers()
                self.wfile.write(json_data)
            except Exception as e:
                self.send_response(500)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"error": str(e)}).encode())
        
        elif self.path == '/health' or self.path == '/status':
            # Health check endpoint
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok", "service": "OTA Server"}).encode())
        
        else:
            self.send_response(404)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'Not Found')
    
    def log_message(self, format, *args):
        """Suppress default logging to avoid cluttering output."""
        # Uncomment để enable HTTP access logs
        # print(f"[HTTP] {format % args}")
        pass


def start_ota_server(port: int = 8888) -> None:
    """Start OTA HTTP server trong background thread.
    
    Args:
        port: Port để bind HTTP server (default: 8888)
    """
    try:
        server = HTTPServer(('0.0.0.0', port), OTARequestHandler)
        print(f"✓ OTA HTTP Server đang chạy trên port {port}")
        print(f"  - Firmware: http://localhost:{port}/firmware.bin")
        print(f"  - Version:  http://localhost:{port}/version")
        print(f"  - Health:   http://localhost:{port}/health")
        server.serve_forever()
    except OSError as e:
        if e.errno == 98:  # Address already in use
            print(f"⚠ Port {port} đã được sử dụng, bỏ qua OTA server", file=sys.stderr)
        else:
            print(f"⚠ Không thể khởi động OTA server trên port {port}: {e}", file=sys.stderr)
    except Exception as e:
        print(f"⚠ Lỗi OTA server: {e}", file=sys.stderr)


def parse_args() -> argparse.Namespace:
    """Parse command line arguments.
    
    Returns:
        Namespace object chứa các arguments đã parse (output file path)
    """
    parser = argparse.ArgumentParser(
        description="Đọc cảm biến từ hệ thống và ghi ra file txt"
    )
    parser.add_argument(
        "--output",
        default="sensors.txt",
        help="File output (default: sensors.txt)",
    )
    parser.add_argument(
        "--serial-device",
        default=None,
        help="Thiết bị serial để gửi dữ liệu (ví dụ: /dev/ttyACM0). Nếu không chỉ định, sẽ tự động tìm ESP32 USB device (vendor ID 303a, model ID 4001).",
    )
    parser.add_argument(
        "--vendor-id",
        default="303a",
        help="Vendor ID để tìm USB device (hex, default: 303a)",
    )
    parser.add_argument(
        "--model-id",
        default="4001",
        help="Model ID để tìm USB device (hex, default: 4001)",
    )
    parser.add_argument(
        "--serial-retries",
        type=int,
        default=3,
        help="Số lần thử lại khi ghi serial thất bại (default: 3)",
    )
    parser.add_argument(
        "--serial-retry-delay",
        type=float,
        default=0.5,
        help="Delay giữa các lần retry serial tính bằng giây (default: 0.5)",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=5.0,
        help="Khoảng thời gian (giây) giữa các lần gửi dữ liệu khi dùng serial (default: 5.0). Chỉ áp dụng khi có --serial-device.",
    )
    parser.add_argument(
        "--file-interval",
        type=float,
        default=None,
        help="Khoảng thời gian (giây) giữa các lần ghi file. Nếu không set, sẽ ghi file mỗi lần gửi serial. Nếu set, sẽ ghi file ít thường xuyên hơn (ví dụ: 30.0 để ghi file mỗi 30s).",
    )
    parser.add_argument(
        "--file-only",
        action="store_true",
        help="Chỉ ghi file rồi thoát (không kết nối USB/serial).",
    )
    parser.add_argument(
        "--no-wait-signal",
        action="store_true",
        help="Không chờ tín hiệu từ ESP32, tự động bắt đầu gửi dữ liệu ngay (fallback mode).",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Bật debug mode để xem các byte nhận được từ ESP32.",
    )
    parser.add_argument(
        "--auto-start-timeout",
        type=float,
        default=10.0,
        help="Thời gian (giây) chờ tín hiệu từ ESP32 trước khi tự động bắt đầu gửi dữ liệu (default: 10.0). Set 0 để tắt tính năng này.",
    )
    parser.add_argument(
        "--ota-port",
        type=int,
        default=8888,
        help="Port cho OTA HTTP server (default: 8888). Set 0 để tắt OTA server.",
    )
    return parser.parse_args()


def main() -> None:
    """Main function - entry point của script.
    
    Parse arguments, thu thập metrics, và ghi ra file output.
    Nếu có serial device, sẽ chạy trong vòng lặp liên tục gửi dữ liệu mỗi interval giây.
    """
    args = parse_args()
    file_only_mode = bool(args.file_only)

    # Heuristic: nếu user chỉ truyền --output (hoặc mặc định tương tự) mà không có flag serial,
    # coi như họ muốn chạy chế độ file-only giống hướng dẫn ban đầu.
    if not file_only_mode and args.serial_device:
        # Nếu vừa chọn file-only vừa chỉ định serial, ưu tiên serial (user chủ động).
        file_only_mode = False
    elif not file_only_mode:
        explicit_output_flag = cli_flag_provided("--output")
        serial_hints_present = any(cli_flag_provided(flag) for flag in SERIAL_MODE_HINT_FLAGS)
        if explicit_output_flag and not serial_hints_present and args.serial_device is None:
            file_only_mode = True
            print("ℹ️ Đã bật chế độ file-only (phát hiện dùng --output mà không có tuỳ chọn serial).")

    if file_only_mode and args.serial_device:
        print("⚠ Bỏ qua --serial-device vì đang chạy chế độ file-only.", file=sys.stderr)
    
    if file_only_mode:
        output_path = Path(args.output)
        metrics = aggregate_metrics()
        write_output(metrics, output_path)
        print(f"Đã ghi {len(LABEL_ORDER)} label vào {output_path}")
        return
    
    # Tạo lock file để tránh chạy nhiều process cùng lúc
    # Lock file sẽ được tạo trong cùng thư mục với script
    script_dir = Path(__file__).parent
    lock_file_path = script_dir / "read_sensor.lock"
    
    # QUAN TRỌNG: Nếu có process cũ đang chạy, kill nó trước
    # Điều này cho phép restart script bằng cách nhấn Run lại trong DSM Task Scheduler
    # (DSM Task Scheduler chỉ có nút Run, không có nút Stop)
    if lock_file_path.exists():
        if not check_stale_lock(lock_file_path):
            # Process cũ vẫn đang chạy, kill nó để restart với code mới
            kill_existing_process(lock_file_path)
        else:
            # Stale lock, xóa nó
            print(f"Cảnh báo: Phát hiện stale lock file, đang xóa...")
            try:
                lock_file_path.unlink()
            except OSError:
                pass
    
    # Acquire lock
    lock_file = acquire_lock(lock_file_path)
    if lock_file is None:
        sys.exit(1)
    
    # Tạo version.json khi script start
    create_version_json(script_dir)
    
    # Start OTA HTTP server nếu được enable
    ota_thread = None
    if args.ota_port > 0:
        ota_thread = Thread(target=start_ota_server, args=(args.ota_port,), daemon=True)
        ota_thread.start()
        # Đợi một chút để server khởi động
        time.sleep(0.5)
    
    output_path = Path(args.output)
    interval = max(0.1, args.interval)
    file_interval = args.file_interval if args.file_interval else interval
    serial_retry_count = max(0, args.serial_retries)
    serial_retry_delay = max(0.0, args.serial_retry_delay)
    
    # Lưu vendor_id và model_id để dùng khi reconnect
    vendor_id = args.vendor_id
    model_id = args.model_id
    fixed_serial_device = args.serial_device  # Device cố định nếu user chỉ định
    
    # Serial mode: chạy liên tục và tự động kết nối USB device
    print("=" * 60)
    print("Chế độ 24/7: Script sẽ tự động đợi và kết nối USB device")
    print("=" * 60)
    if args.no_wait_signal:
        print("Chế độ: --no-wait-signal (tự động gửi dữ liệu khi kết nối)")
    else:
        print("Chế độ: Chờ tín hiệu từ ESP32 (chỉ gửi khi backlight on)")
        if args.debug:
            print("Debug mode: BẬT - sẽ hiển thị các byte nhận được từ ESP32")
    print("Nhấn Ctrl+C để dừng.")
    print()

    last_file_write_time = 0.0
    iteration = 0
    serial_file = None
    serial_device = None  # Sẽ được tìm khi có USB
    backlight_is_on = args.no_wait_signal  # Nếu --no-wait-signal, tự động bật
    previous_backlight_state = False  # Trạng thái backlight lần trước, để detect wake up
    storage_sent_this_wake = False  # Đánh dấu đã gửi storage trong lần wake này chưa
    connection_lost_count = 0  # Đếm số lần mất kết nối liên tiếp

    try:
        # Vòng lặp chính: đợi USB -> kết nối -> hoạt động -> mất kết nối -> lặp lại
        while True:
            # Bước 1: Đợi USB device xuất hiện (nếu chưa có hoặc đã mất kết nối)
            if not serial_file or not serial_device:
                if serial_device:
                    print(f"⚠ Mất kết nối với {serial_device} - Đang đợi kết nối lại...")
                else:
                    if fixed_serial_device:
                        print(f"Đang đợi USB device {fixed_serial_device} xuất hiện...")
                    else:
                        print(f"Đang đợi ESP32 USB device (vendor: {vendor_id}, model: {model_id})...")

                # Đợi USB device (vô hạn, không timeout)
                found_device = wait_for_usb_connection(
                    vendor_id,
                    model_id,
                    fixed_serial_device,
                    check_interval=2.0,
                    max_wait=None,
                )

                if found_device:
                    serial_device = found_device
                    print(f"✓ Đã tìm thấy USB device: {serial_device}")
                    connection_lost_count = 0  # Reset counter
                else:
                    # Không nên xảy ra vì max_wait=None, nhưng để an toàn
                    time.sleep(2.0)
                    continue

            # Bước 2: Mở kết nối serial
            if not serial_file:
                try:
                    # Kiểm tra device vẫn tồn tại trước khi mở
                    if not check_usb_device_exists(serial_device):
                        print(f"⚠ USB device {serial_device} không còn tồn tại")
                        serial_file = None
                        continue

                    serial_file = open(serial_device, "r+b", buffering=0)
                    # QUAN TRỌNG: Set DTR và RTS thành True để ESP32 biết host đã mở port
                    if set_serial_dtr_rts(serial_file, dtr=True, rts=True):
                        print(f"✓ Đã kết nối tới {serial_device} với DTR=True, RTS=True")
                    else:
                        print(f"⚠ Đã kết nối tới {serial_device} (không thể set DTR/RTS)")
                    # Đợi một chút để ESP32 nhận biết trạng thái
                    time.sleep(0.1)

                    # Reset các state khi kết nối mới
                    backlight_is_on = args.no_wait_signal
                    previous_backlight_state = False
                    storage_sent_this_wake = False
                    start_time = time.time()
                    auto_start_triggered = args.no_wait_signal

                    if args.no_wait_signal:
                        print("Đã bật chế độ --no-wait-signal, bắt đầu gửi dữ liệu ngay...")
                    elif args.auto_start_timeout > 0:
                        print(
                            f"Sẽ tự động bắt đầu gửi dữ liệu sau {args.auto_start_timeout} giây "
                            "nếu không nhận được tín hiệu từ ESP32..."
                        )

                except OSError as exc:
                    print(f"⚠ Lỗi mở {serial_device}: {exc}")
                    print("Đang đợi và thử lại...")
                    serial_file = None
                    connection_lost_count += 1
                    time.sleep(2.0)
                    continue

            # Bước 3: Hoạt động bình thường - gửi/nhận dữ liệu
            try:
                iteration += 1
                current_time = time.time()
                elapsed_time = current_time - start_time

                # Kiểm tra device vẫn tồn tại
                if not check_usb_device_exists(serial_device):
                    raise OSError(f"USB device {serial_device} không còn tồn tại")

                # Kiểm tra trạng thái backlight từ ESP32 (trừ khi dùng --no-wait-signal)
                if not args.no_wait_signal:
                    backlight_state = check_esp32_backlight_state(serial_file, debug=args.debug)
                    if backlight_state is not None:
                        previous_backlight_state = backlight_is_on
                        backlight_is_on = backlight_state

                        # Detect wake up: chuyển từ off -> on
                        if backlight_is_on and not previous_backlight_state:
                            print(f"[{iteration}] ESP32: Màn hình đã bật - Bắt đầu gửi dữ liệu")
                            storage_sent_this_wake = False  # Reset flag để gửi storage lần này
                            auto_start_triggered = True  # Đã nhận được tín hiệu, không cần auto-start
                        elif not backlight_is_on and previous_backlight_state:
                            print(f"[{iteration}] ESP32: Màn hình đã tắt - Dừng gửi dữ liệu")
                            storage_sent_this_wake = False  # Reset flag cho lần wake tiếp theo

                    # Auto-start nếu không nhận được tín hiệu sau timeout
                    if (
                        not auto_start_triggered
                        and args.auto_start_timeout > 0
                        and elapsed_time >= args.auto_start_timeout
                    ):
                        print(
                            f"[{iteration}] Không nhận được tín hiệu từ ESP32 sau {elapsed_time:.1f} giây - "
                            "Tự động bắt đầu gửi dữ liệu"
                        )
                        backlight_is_on = True
                        auto_start_triggered = True
                        storage_sent_this_wake = False
                # Nếu dùng --no-wait-signal, backlight_is_on đã được set = True ở trên

                # Chỉ gửi dữ liệu khi backlight bật
                if backlight_is_on:
                    # Đọc metrics
                    metrics = aggregate_metrics()

                    # Gửi qua serial
                    send_success = False

                    # Nếu là lần đầu wake up (chưa gửi storage), gửi cả storage và dynamic
                    if not storage_sent_this_wake:
                        # Gửi storage labels (1 lần duy nhất)
                        if write_serial_with_retry(
                            serial_file,
                            metrics,
                            WAKEUP_LABELS,
                            retries=serial_retry_count,
                            retry_delay=serial_retry_delay,
                            dataset_name="storage data",
                        ):
                            print(f"[{iteration}] Đã gửi storage data (1 lần duy nhất)")
                            storage_sent_this_wake = True
                            send_success = True
                        else:
                            raise OSError("Không gửi được storage data")

                        # Sau đó gửi dynamic labels ngay
                        if write_serial_with_retry(
                            serial_file,
                            metrics,
                            DYNAMIC_LABELS,
                            retries=serial_retry_count,
                            retry_delay=serial_retry_delay,
                            dataset_name="dynamic data",
                        ):
                            print(f"[{iteration}] Đã gửi dynamic data")
                            send_success = True
                        else:
                            raise OSError("Không gửi được dynamic data")
                    else:
                        # Đã gửi storage rồi, chỉ gửi dynamic labels
                        if write_serial_with_retry(
                            serial_file,
                            metrics,
                            DYNAMIC_LABELS,
                            retries=serial_retry_count,
                            retry_delay=serial_retry_delay,
                            dataset_name="dynamic data",
                        ):
                            print(f"[{iteration}] Đã gửi dynamic data tới {serial_device}")
                            send_success = True
                        else:
                            raise OSError("Không gửi được dữ liệu")

                    # Reset connection lost counter nếu gửi thành công
                    if send_success:
                        connection_lost_count = 0

                    # Ghi file nếu đã đến thời gian
                    if current_time - last_file_write_time >= file_interval:
                        write_output(metrics, output_path)
                        print(f"Đã ghi {len(LABEL_ORDER)} label vào {output_path}")
                        last_file_write_time = current_time
                else:
                    # Backlight tắt, không gửi dữ liệu
                    if iteration % 10 == 0:  # Chỉ log mỗi 10 lần để không spam
                        print(f"[{iteration}] Đang chờ ESP32 bật màn hình...")

                # Đợi đến lần kiểm tra tiếp theo
                time.sleep(interval)

            except (OSError, IOError, ValueError) as exc:
                # Mất kết nối hoặc lỗi I/O - đóng kết nối và đợi reconnect
                connection_lost_count += 1
                print(f"⚠ Lỗi kết nối (lần {connection_lost_count}): {exc}")

                # Đóng kết nối hiện tại
                if serial_file:
                    try:
                        serial_file.close()
                    except Exception:
                        pass
                    serial_file = None

                # Đợi một chút trước khi thử lại
                time.sleep(1.0)
                continue

    except KeyboardInterrupt:
        print("\nĐang dừng...")
        # Đóng kết nối serial
        if serial_file:
            try:
                serial_file.close()
            except Exception:
                pass
        # Ghi file lần cuối trước khi thoát (nếu backlight đang bật)
        if backlight_is_on:
            try:
                metrics = aggregate_metrics()
                write_output(metrics, output_path)
                print(f"Đã ghi dữ liệu cuối cùng vào {output_path}")
            except Exception:
                pass
        sys.exit(0)
    except Exception as exc:
        # Đóng kết nối serial nếu có lỗi
        if serial_file:
            try:
                serial_file.close()
            except Exception:
                pass
        raise


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)

