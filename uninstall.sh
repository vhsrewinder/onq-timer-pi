#!/bin/bash
set -e

echo "=== OnQ Timer Pi Relay Uninstaller ==="
echo ""

if [ "$EUID" -ne 0 ]; then
  echo "Please run with sudo: sudo ./uninstall.sh"
  exit 1
fi

# Stop and disable service
echo "Stopping service..."
systemctl stop onq-timer-pi 2>/dev/null || true
systemctl disable onq-timer-pi 2>/dev/null || true
rm -f /etc/systemd/system/onq-timer-pi.service
systemctl daemon-reload

# Remove install directory
echo "Removing /opt/onq-timer-pi/..."
rm -rf /opt/onq-timer-pi

# Optionally remove config
read -rp "Remove config at /etc/onq-timer-pi/? [y/N]: " REMOVE_CONFIG
if [ "$REMOVE_CONFIG" = "y" ] || [ "$REMOVE_CONFIG" = "Y" ]; then
  rm -rf /etc/onq-timer-pi
  echo "Config removed"
else
  echo "Config preserved at /etc/onq-timer-pi/"
fi

echo ""
echo "=== Uninstall Complete ==="
