#!/bin/bash
set -e

echo "=== OnQ Timer Pi Relay Installer ==="
echo ""

# Check sudo
if [ "$EUID" -ne 0 ]; then
  echo "Please run with sudo: sudo ./install.sh"
  exit 1
fi

# Prompt: OnQ server IP
read -rp "OnQ server IP [192.168.45.40]: " ONQ_HOST
ONQ_HOST=${ONQ_HOST:-192.168.45.40}

# Prompt: OnQ server port
read -rp "OnQ server port [3000]: " ONQ_PORT
ONQ_PORT=${ONQ_PORT:-3000}

# Prompt: Set hostname
read -rp "Set hostname (blank to skip): " NEW_HOSTNAME
if [ -n "$NEW_HOSTNAME" ]; then
  hostnamectl set-hostname "$NEW_HOSTNAME"
  echo "Hostname set to $NEW_HOSTNAME"
fi

# Install Node.js 20 if needed
if ! command -v node &>/dev/null || [ "$(node -v | cut -d. -f1 | tr -d v)" -lt 20 ]; then
  echo ""
  echo "Installing Node.js 20 LTS..."
  curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
  apt-get install -y nodejs
fi

echo "Node.js version: $(node -v)"

# Copy to /opt
echo ""
echo "Installing to /opt/onq-timer-pi/..."
mkdir -p /opt/onq-timer-pi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
rsync -a --delete --exclude=node_modules --exclude=.git "$SCRIPT_DIR/" /opt/onq-timer-pi/

# Install dependencies
echo "Installing dependencies..."
cd /opt/onq-timer-pi
npm install --production --no-fund --no-audit

# Create config
echo ""
echo "Writing config..."
mkdir -p /etc/onq-timer-pi
cat > /etc/onq-timer-pi/config.json <<EOF
{
  "onqServer": {
    "host": "$ONQ_HOST",
    "port": $ONQ_PORT
  },
  "webUiPort": 3000,
  "serial": {
    "baudRate": 115200,
    "scanIntervalMs": 5000
  }
}
EOF

# Add user to dialout group (for serial access)
if id -nG "$SUDO_USER" 2>/dev/null | grep -qw dialout; then
  echo "User $SUDO_USER already in dialout group"
else
  usermod -a -G dialout "$SUDO_USER" 2>/dev/null || true
  echo "Added $SUDO_USER to dialout group (re-login required)"
fi

# Install systemd service
echo ""
echo "Installing systemd service..."
cp /opt/onq-timer-pi/systemd/onq-timer-pi.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable onq-timer-pi
systemctl start onq-timer-pi

echo ""
echo "=== Installation Complete ==="
echo ""
echo "  OnQ Server:  http://$ONQ_HOST:$ONQ_PORT"
echo "  Config UI:   http://$(hostname -I | awk '{print $1}'):3000"
echo "  Config file: /etc/onq-timer-pi/config.json"
echo ""
echo "  Service commands:"
echo "    sudo systemctl status onq-timer-pi"
echo "    sudo journalctl -u onq-timer-pi -f"
echo ""
