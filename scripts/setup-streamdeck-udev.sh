#!/bin/bash
set -e

echo "=== Stream Deck USB Permissions Setup ==="

if [ "$EUID" -ne 0 ]; then
  echo "Please run with sudo: sudo ./scripts/setup-streamdeck-udev.sh"
  exit 1
fi

echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="0fd9", MODE="0666"' > /etc/udev/rules.d/50-elgato.rules
udevadm control --reload-rules && udevadm trigger

echo "Done. Elgato Stream Deck USB permissions set."
echo "Unplug and replug your Stream Deck if it was already connected."
