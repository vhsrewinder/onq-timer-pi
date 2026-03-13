'use strict';

const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const log = require('./logger');

class SerialDevice extends EventEmitter {
  constructor(portPath, baudRate = 115200) {
    super();
    this.portPath = portPath;
    this.baudRate = baudRate;
    this.port = null;
    this.parser = null;
    this.lastRemoteId = null;
    this.lastBattery = null;
    this.lastSeen = null;
    this.connected = false;
  }

  open() {
    try {
      this.port = new SerialPort({
        path: this.portPath,
        baudRate: this.baudRate,
        autoOpen: false,
      });

      this.parser = this.port.pipe(new ReadlineParser({ delimiter: '\n' }));

      this.parser.on('data', (line) => this._handleLine(line));

      this.port.on('error', (err) => {
        log.error('Serial', `${this.portPath} error: ${err.message}`);
        this.connected = false;
        this.emit('error', err);
      });

      this.port.on('close', () => {
        log.info('Serial', `${this.portPath} closed`);
        this.connected = false;
        this.emit('close');
      });

      this.port.open((err) => {
        if (err) {
          log.error('Serial', `Failed to open ${this.portPath}: ${err.message}`);
          this.emit('error', err);
          return;
        }
        this.connected = true;
        this.lastSeen = Date.now();
        log.info('Serial', `Opened ${this.portPath} at ${this.baudRate} baud`);
        this.emit('open');
      });
    } catch (err) {
      log.error('Serial', `Failed to create port ${this.portPath}: ${err.message}`);
      this.emit('error', err);
    }
  }

  _handleLine(line) {
    const trimmed = line.trim();
    if (!trimmed) return;

    let msg;
    try {
      msg = JSON.parse(trimmed);
    } catch {
      log.debug('Serial', `Non-JSON from ${this.portPath}: ${trimmed}`);
      return;
    }

    this.lastSeen = Date.now();

    switch (msg.type) {
      case 'button-press':
        this.lastRemoteId = msg.remoteId;
        this.emit('button-press', {
          remoteId: msg.remoteId,
          buttonId: msg.buttonId,
        });
        break;

      case 'set-time':
        this.lastRemoteId = msg.remoteId;
        this.emit('set-time', {
          remoteId: msg.remoteId,
          seconds: msg.seconds,
        });
        break;

      case 'heartbeat':
      case 'remote-status':
        this.lastRemoteId = msg.remoteId;
        this.lastBattery = msg.battery;
        this.emit('heartbeat', {
          remoteId: msg.remoteId,
          battery: msg.battery,
        });
        break;

      default:
        log.debug('Serial', `Unknown message type from ${this.portPath}: ${msg.type}`);
    }
  }

  write(obj) {
    if (!this.port || !this.connected) return false;
    try {
      const data = JSON.stringify(obj) + '\n';
      this.port.write(data);
      return true;
    } catch (err) {
      log.error('Serial', `Write error on ${this.portPath}: ${err.message}`);
      return false;
    }
  }

  close() {
    this.connected = false;
    if (this.port && this.port.isOpen) {
      try {
        this.port.close();
      } catch {
        // ignore close errors
      }
    }
  }

  getStatus() {
    return {
      port: this.portPath,
      connected: this.connected,
      remoteId: this.lastRemoteId,
      battery: this.lastBattery,
      lastSeen: this.lastSeen,
    };
  }
}

module.exports = SerialDevice;
