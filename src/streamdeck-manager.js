'use strict';

const { EventEmitter } = require('events');
const log = require('./logger');

// Button ID constants (match existing stopwatch remote protocol)
const BUTTON_PLAY = 10;
const BUTTON_PAUSE = 12;
const BUTTON_STOP = 14;
const BUTTON_TOGGLE = 2;

// Preset durations in seconds
const PRESETS = [180, 300, 600, 900, 1200]; // 3, 5, 10, 15, 20 min

// Flag bits from timer-relay
const FLAG_RUNNING = 0x01;
const FLAG_EXPIRED = 0x02;
const FLAG_CONNECTED = 0x04;

// Colors (RGB)
const COLOR_OFF = [0, 0, 0];
const COLOR_GREEN = [0, 180, 0];
const COLOR_YELLOW = [200, 180, 0];
const COLOR_RED = [200, 0, 0];
const COLOR_GRAY = [60, 60, 60];
const COLOR_BLUE = [0, 80, 200];
const COLOR_WHITE = [180, 180, 180];
const COLOR_CYAN = [0, 150, 150];

class StreamDeckManager extends EventEmitter {
  constructor() {
    super();
    this.device = null;
    this.scanTimer = null;
    this.connected = false;
    this.model = null;
    this.keyCount = 0;
    this.cols = 5;
    this.remoteId = 'streamdeck';
    this.lastFlags = 0;
    this._listStreamDecks = null;
    this._openStreamDeck = null;
  }

  async start() {
    try {
      const sdk = await import('@elgato-stream-deck/node');
      this._listStreamDecks = sdk.listStreamDecks;
      this._openStreamDeck = sdk.openStreamDeck;
    } catch (err) {
      log.warn('StreamDeck', `@elgato-stream-deck/node not available: ${err.message}`);
      log.warn('StreamDeck', 'Stream Deck support disabled. Install with: npm install @elgato-stream-deck/node');
      return;
    }

    log.info('StreamDeck', 'Starting Stream Deck scanning');
    await this._scan();
    this.scanTimer = setInterval(() => this._scan(), 5000);
  }

  stop() {
    if (this.scanTimer) {
      clearInterval(this.scanTimer);
      this.scanTimer = null;
    }
    this._closeDevice();
  }

  getStatus() {
    return {
      connected: this.connected,
      model: this.model,
      keyCount: this.keyCount,
      cols: this.cols,
      layout: this._getLayoutForStatus(),
    };
  }

  _getLayoutForStatus() {
    // Return layout suitable for web UI rendering
    const layout = this._getLayout();
    const keys = [];
    const totalKeys = this.connected ? this.keyCount : (this.keyCount || 15);
    const cols = this.connected ? this.cols : 5;

    for (let i = 0; i < totalKeys; i++) {
      if (layout.presets[i]) {
        keys.push({ index: i, type: 'preset', label: layout.presets[i].label, seconds: layout.presets[i].seconds });
      } else if (layout.controls[i]) {
        keys.push({ index: i, type: 'control', label: layout.controls[i].label });
      } else {
        keys.push({ index: i, type: 'empty', label: '' });
      }
    }

    return { keys, cols };
  }

  async _scan() {
    if (this.connected) return;
    if (!this._listStreamDecks) return;

    try {
      const decks = await this._listStreamDecks();
      if (decks.length > 0) {
        await this._openDevice(decks[0].path);
      }
    } catch (err) {
      log.debug('StreamDeck', `Scan: ${err.message}`);
    }
  }

  async _openDevice(devicePath) {
    try {
      this.device = await this._openStreamDeck(devicePath);
      this.connected = true;
      this.model = this.device.MODEL;
      this.keyCount = this.device.NUM_KEYS || this.device.KEY_COUNT || 15;
      this.cols = this.device.KEY_COLUMNS || this.device.ICON_SIZE || 5;

      log.info('StreamDeck', `Connected: ${this.model} (${this.keyCount} keys)`);
      this.emit('connected', { model: this.model, keyCount: this.keyCount });

      // Set up button handler
      this.device.on('down', (keyIndex) => this._handleKeyDown(keyIndex));

      // Handle disconnect
      this.device.on('error', (err) => {
        log.warn('StreamDeck', `Device error: ${err.message}`);
        this._handleDisconnect();
      });

      // Set initial button colors
      this._setInitialColors();
    } catch (err) {
      log.error('StreamDeck', `Failed to open: ${err.message}`);
      this.device = null;
      this.connected = false;
    }
  }

  _closeDevice() {
    if (this.device) {
      try {
        this.device.close();
      } catch {
        // ignore close errors
      }
      this.device = null;
    }
    this.connected = false;
    this.model = null;
    this.keyCount = 0;
  }

  _handleDisconnect() {
    log.info('StreamDeck', 'Device disconnected');
    this._closeDevice();
    this.emit('disconnected');
  }

  _handleKeyDown(keyIndex) {
    if (!this.connected) return;

    const layout = this._getLayout();

    // Check control buttons (bottom row)
    const controlAction = layout.controls[keyIndex];
    if (controlAction) {
      log.info('StreamDeck', `Button: ${controlAction.label} (key ${keyIndex})`);
      this.emit('button-press', {
        remoteId: this.remoteId,
        buttonId: controlAction.buttonId,
      });
      return;
    }

    // Check preset buttons (top row)
    const presetAction = layout.presets[keyIndex];
    if (presetAction) {
      log.info('StreamDeck', `Preset: ${presetAction.label} (key ${keyIndex})`);
      this.emit('set-time', {
        remoteId: this.remoteId,
        seconds: presetAction.seconds,
      });
      return;
    }

    log.debug('StreamDeck', `Unmapped key: ${keyIndex}`);
  }

  _getLayout() {
    // Build key mappings based on device size
    const controls = {};
    const presets = {};

    if (this.keyCount <= 6) {
      // 6-key Mini: row 0 = presets (3), row 1 = controls (3)
      presets[0] = { seconds: 180, label: '3:00' };
      presets[1] = { seconds: 300, label: '5:00' };
      presets[2] = { seconds: 600, label: '10:00' };
      controls[3] = { buttonId: BUTTON_PLAY, label: 'PLAY' };
      controls[4] = { buttonId: BUTTON_PAUSE, label: 'PAUSE' };
      controls[5] = { buttonId: BUTTON_STOP, label: 'STOP' };
    } else {
      // 15-key (standard) or larger: row 0 = presets, row 2 = controls
      const c = this.cols;

      // Row 0: presets
      for (let i = 0; i < Math.min(PRESETS.length, c); i++) {
        const mins = PRESETS[i] / 60;
        presets[i] = { seconds: PRESETS[i], label: `${mins}:00` };
      }

      // Row 2 (bottom): controls
      const row2 = 2 * c;
      controls[row2] = { buttonId: BUTTON_PLAY, label: 'PLAY' };
      controls[row2 + 1] = { buttonId: BUTTON_PAUSE, label: 'PAUSE' };
      controls[row2 + 2] = { buttonId: BUTTON_STOP, label: 'STOP' };
      controls[row2 + 3] = { buttonId: BUTTON_TOGGLE, label: 'TOGGLE' };
    }

    return { controls, presets };
  }

  updateTimerDisplay(time, flags) {
    if (!this.connected || !this.device) return;
    this.lastFlags = flags;

    // Determine state color
    let controlColor;
    if (flags & FLAG_EXPIRED) {
      controlColor = COLOR_RED;
    } else if (flags & FLAG_RUNNING) {
      controlColor = COLOR_GREEN;
    } else if (flags & FLAG_CONNECTED) {
      // Connected but stopped/paused — check if paused (time > 0) or stopped
      controlColor = time > 0 ? COLOR_YELLOW : COLOR_GRAY;
    } else {
      controlColor = COLOR_GRAY;
    }

    const layout = this._getLayout();

    // Update control buttons with state color
    for (const keyStr of Object.keys(layout.controls)) {
      const key = parseInt(keyStr, 10);
      this._fillKey(key, controlColor);
    }

    // Presets always cyan when connected, gray otherwise
    const presetColor = flags & FLAG_CONNECTED ? COLOR_CYAN : COLOR_GRAY;
    for (const keyStr of Object.keys(layout.presets)) {
      const key = parseInt(keyStr, 10);
      this._fillKey(key, presetColor);
    }
  }

  _setInitialColors() {
    if (!this.device) return;

    // All keys gray initially
    for (let i = 0; i < this.keyCount; i++) {
      this._fillKey(i, COLOR_OFF);
    }

    // Light up mapped keys
    const layout = this._getLayout();
    for (const keyStr of Object.keys(layout.controls)) {
      this._fillKey(parseInt(keyStr, 10), COLOR_GRAY);
    }
    for (const keyStr of Object.keys(layout.presets)) {
      this._fillKey(parseInt(keyStr, 10), COLOR_CYAN);
    }
  }

  _fillKey(keyIndex, [r, g, b]) {
    if (!this.device || keyIndex >= this.keyCount) return;
    try {
      this.device.fillKeyColor(keyIndex, r, g, b);
    } catch (err) {
      log.debug('StreamDeck', `fillKeyColor error: ${err.message}`);
    }
  }

  broadcastToAll(_message) {
    // Interface compatibility with SerialManager — Stream Deck doesn't receive
    // raw serial messages. Timer display is handled via updateTimerDisplay().
  }
}

module.exports = StreamDeckManager;
