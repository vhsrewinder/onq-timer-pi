'use strict';

const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');
const SerialDevice = require('./serial-device');
const log = require('./logger');

class SerialManager extends EventEmitter {
  constructor(config) {
    super();
    this.baudRate = config.serial?.baudRate || 115200;
    this.scanIntervalMs = config.serial?.scanIntervalMs || 5000;
    this.devices = new Map(); // portPath -> SerialDevice
    this.scanTimer = null;
  }

  start() {
    log.info('SerialMgr', 'Starting serial device scanning');
    this._scan();
    this.scanTimer = setInterval(() => this._scan(), this.scanIntervalMs);
  }

  stop() {
    if (this.scanTimer) {
      clearInterval(this.scanTimer);
      this.scanTimer = null;
    }
    for (const [portPath, device] of this.devices) {
      device.close();
      log.info('SerialMgr', `Closed ${portPath}`);
    }
    this.devices.clear();
  }

  async _scan() {
    try {
      const ports = await SerialPort.list();
      const usbPorts = ports.filter(
        (p) => p.path.startsWith('/dev/ttyACM') || p.path.startsWith('/dev/ttyUSB')
      );

      const currentPaths = new Set(usbPorts.map((p) => p.path));

      // Remove devices that disappeared
      for (const [portPath, device] of this.devices) {
        if (!currentPaths.has(portPath)) {
          log.info('SerialMgr', `Device removed: ${portPath}`);
          device.close();
          this.devices.delete(portPath);
          this.emit('device-removed', portPath);
        }
      }

      // Add new devices
      for (const portInfo of usbPorts) {
        if (!this.devices.has(portInfo.path)) {
          this._addDevice(portInfo.path);
        }
      }
    } catch (err) {
      log.error('SerialMgr', `Scan error: ${err.message}`);
    }
  }

  _addDevice(portPath) {
    const device = new SerialDevice(portPath, this.baudRate);

    device.on('button-press', (data) => {
      log.info('SerialMgr', `Button press from ${portPath}: remote=${data.remoteId} button=${data.buttonId}`);
      this.emit('button-press', data);
    });

    device.on('set-time', (data) => {
      log.info('SerialMgr', `Set time from ${portPath}: remote=${data.remoteId} seconds=${data.seconds}`);
      this.emit('set-time', data);
    });

    device.on('heartbeat', (data) => {
      log.debug('SerialMgr', `Heartbeat from ${portPath}: remote=${data.remoteId} battery=${data.battery}%`);
      this.emit('heartbeat', data);
    });

    device.on('error', () => {
      // Device will be cleaned up on next scan
    });

    device.on('close', () => {
      this.devices.delete(portPath);
      this.emit('device-removed', portPath);
    });

    device.open();
    this.devices.set(portPath, device);
    log.info('SerialMgr', `Device added: ${portPath}`);
    this.emit('device-added', portPath);
  }

  broadcastToAll(obj) {
    for (const device of this.devices.values()) {
      device.write(obj);
    }
  }

  getDevices() {
    const result = [];
    for (const device of this.devices.values()) {
      result.push(device.getStatus());
    }
    return result;
  }
}

module.exports = SerialManager;
