# OnQ Timer Pi — Wired Stopwatch Relay

Relay service that connects USB stopwatch remotes to an OnQ server over LAN. Replaces the wireless ESP-NOW bridge for environments where wired connections are preferred.

## Architecture

```
Stopwatch ──USB Serial (115200, JSON\n)──> Relay Pi
Relay Pi ──POST /api/bridge/button──> OnQ Server (192.168.45.40:3000)
Relay Pi ──POST /api/bridge/preset──> OnQ Server
Relay Pi <──Socket.IO timer-sync──── OnQ Server
Relay Pi ──USB Serial JSON──> Stopwatch (timer state feedback)
```

## Quick Start

### Install on Relay Pi

```bash
git clone <repo-url> onq-timer-pi
cd onq-timer-pi
sudo ./install.sh
```

The installer prompts for the OnQ server IP/port, installs Node.js 20 if needed, and sets up the systemd service.

### Manual Run (Development)

```bash
npm install
node src/index.js
```

### Config UI

Open `http://<relay-pi-ip>:3000` in a browser to:
- View connected stopwatches and OnQ connection status
- Change OnQ server address
- View service logs
- Restart the service

## Firmware

The `firmware/onq-timer-remote-usb/` directory contains the stopwatch firmware modified for USB serial mode.

To flash:
1. Open `firmware/onq-timer-remote-usb/` in Arduino IDE
2. In `Config.h`, verify `COMMUNICATION_MODE` is set to `COMM_MODE_USB_SERIAL` (3)
3. Set `REMOTE_ID` for the specific device
4. Flash to ESP32-S3

## Serial Protocol

### Stopwatch → Relay Pi
```json
{"type":"button-press","remoteId":1,"buttonId":10,"timestamp":12345}
{"type":"set-time","remoteId":1,"seconds":300}
{"type":"remote-status","remoteId":1,"battery":85,"rssi":0,"connected":true}
```

### Relay Pi → Stopwatch
```json
{"type":"timer-state","time":120,"flags":5}
{"type":"time-sync","hours":14,"minutes":30}
```

## Service Management

```bash
sudo systemctl status onq-timer-pi
sudo systemctl restart onq-timer-pi
sudo journalctl -u onq-timer-pi -f
```

## Uninstall

```bash
sudo ./uninstall.sh
```

## Configuration

Config file: `/etc/onq-timer-pi/config.json`

```json
{
  "onqServer": {
    "host": "192.168.45.40",
    "port": 3000
  },
  "webUiPort": 3000,
  "serial": {
    "baudRate": 115200,
    "scanIntervalMs": 5000
  }
}
```
