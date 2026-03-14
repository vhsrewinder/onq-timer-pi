'use strict';

const { EventEmitter } = require('events');
const log = require('./logger');
const sharp = require('sharp');

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

// Stream Deck original button size
const ICON_SIZE = 72;

class StreamDeckManager extends EventEmitter {
  constructor() {
    super();
    this.device = null;
    this.scanTimer = null;
    this.connected = false;
    this.model = null;
    this.keyCount = 0;
    this.cols = 5;
    this.remoteId = 249; // Numeric remote ID for Stream Deck (must be <= 255)
    this.lastFlags = 0;
    this.lastPresetTime = 0; // Track last preset time selected
    this._listStreamDecks = null;
    this._openStreamDeck = null;
    this.currentDevicePath = null; // Track current device path for reconnection detection
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
      } else if (layout.display[i]) {
        keys.push({ index: i, type: 'display', label: layout.display[i].label });
      } else {
        keys.push({ index: i, type: 'empty', label: '' });
      }
    }

    return { keys, cols };
  }

  async _scan() {
    if (!this._listStreamDecks) return;

    try {
      const decks = await this._listStreamDecks();

      // Check if currently connected device is still present
      if (this.connected && this.currentDevicePath) {
        const stillConnected = decks.some(d => d.path === this.currentDevicePath);
        if (!stillConnected) {
          log.info('StreamDeck', 'Device was unplugged, cleaning up...');
          this._handleDisconnect();
        }
      }

      // Try to connect if not connected
      if (!this.connected && decks.length > 0) {
        await this._openDevice(decks[0].path);
      }
    } catch (err) {
      log.debug('StreamDeck', `Scan: ${err.message}`);
    }
  }

  async _openDevice(devicePath) {
    try {
      // Close any existing stale device first
      if (this.device) {
        log.info('StreamDeck', 'Closing existing device before opening new one');
        this._closeDevice();
      }

      log.info('StreamDeck', `Opening device at path: ${devicePath}`);
      this.device = await this._openStreamDeck(devicePath);
      this.currentDevicePath = devicePath;
      this.connected = true;
      this.model = this.device.MODEL;
      this.keyCount = this.device.NUM_KEYS || this.device.KEY_COUNT || 15;
      this.cols = this.device.KEY_COLUMNS || this.device.ICON_SIZE || 5;

      log.info('StreamDeck', `Connected: ${this.model} (${this.keyCount} keys)`);
      this.emit('connected', { model: this.model, keyCount: this.keyCount });

      // Set up button handler with explicit logging
      // In @elgato-stream-deck/node v7.x, need to determine the event structure
      this.device.on('down', (event) => {
        log.info('StreamDeck', `Raw key down event: ${JSON.stringify(event)}, type: ${typeof event}`);

        // Try multiple possible formats
        let keyIndex;
        if (typeof event === 'number') {
          keyIndex = event;
        } else if (typeof event === 'object' && event !== null) {
          // Try various properties that might contain the key index
          keyIndex = event.key ?? event.keyIndex ?? event.index ?? event.button;
        }

        log.info('StreamDeck', `Extracted keyIndex: ${keyIndex}`);
        if (keyIndex !== undefined) {
          this._handleKeyDown(keyIndex);
        }
      });

      // Handle disconnect/error
      this.device.on('error', (err) => {
        log.warn('StreamDeck', `Device error: ${err.message}`);
        this._handleDisconnect();
      });

      // Set initial button colors and labels
      await this._setInitialColors();

      log.info('StreamDeck', 'Device fully initialized, listening for button presses');
    } catch (err) {
      log.error('StreamDeck', `Failed to open device: ${err.message}`);
      this.device = null;
      this.connected = false;
      this.currentDevicePath = null;
    }
  }

  _closeDevice() {
    if (this.device) {
      try {
        log.info('StreamDeck', 'Closing device and removing listeners');
        // Remove all event listeners to prevent memory leaks
        this.device.removeAllListeners();
        this.device.close();
        log.info('StreamDeck', 'Device closed successfully');
      } catch (err) {
        log.debug('StreamDeck', `Close error (ignored): ${err.message}`);
      }
      this.device = null;
    }
    this.connected = false;
    this.model = null;
    this.keyCount = 0;
    this.currentDevicePath = null;
  }

  _handleDisconnect() {
    log.info('StreamDeck', 'Handling device disconnection');
    this._closeDevice();
    this.emit('disconnected');
  }

  _handleKeyDown(keyIndex) {
    if (!this.connected) {
      log.warn('StreamDeck', `Key ${keyIndex} pressed but device marked as disconnected`);
      return;
    }

    log.info('StreamDeck', `Processing key press for index ${keyIndex}`);
    const layout = this._getLayout();

    // Check control buttons (bottom row)
    const controlAction = layout.controls[keyIndex];
    if (controlAction) {
      log.info('StreamDeck', `Control button: ${controlAction.label} (key ${keyIndex}, buttonId ${controlAction.buttonId})`);
      this.emit('button-press', {
        remoteId: this.remoteId,
        buttonId: controlAction.buttonId,
      });
      return;
    }

    // Check preset buttons (top row)
    const presetAction = layout.presets[keyIndex];
    if (presetAction) {
      log.info('StreamDeck', `Preset button: ${presetAction.label} (key ${keyIndex}, ${presetAction.seconds}s)`);
      this.lastPresetTime = presetAction.seconds; // Track preset selection
      this.emit('set-time', {
        remoteId: this.remoteId,
        seconds: presetAction.seconds,
      });
      return;
    }

    log.debug('StreamDeck', `Unmapped key pressed: ${keyIndex}`);
  }

  _getLayout() {
    // Build key mappings based on device size
    const controls = {};
    const presets = {};
    const display = {};

    if (this.keyCount <= 6) {
      // 6-key Mini: row 0 = presets (3), row 1 = controls (3)
      presets[0] = { seconds: 180, label: '3:00' };
      presets[1] = { seconds: 300, label: '5:00' };
      presets[2] = { seconds: 600, label: '10:00' };
      controls[3] = { buttonId: BUTTON_PLAY, label: 'PLAY' };
      controls[4] = { buttonId: BUTTON_PAUSE, label: 'PAUSE' };
      controls[5] = { buttonId: BUTTON_STOP, label: 'STOP' };
    } else {
      // 15-key (standard) or larger: row 0 = presets, row 1 = display, row 2 = controls
      const c = this.cols;

      // Row 0: presets
      for (let i = 0; i < Math.min(PRESETS.length, c); i++) {
        const mins = PRESETS[i] / 60;
        presets[i] = { seconds: PRESETS[i], label: `${mins}:00` };
      }

      // Row 1 (middle): timer display - center 3 buttons (6, 7, 8)
      display[6] = { label: 'PRESET' };
      display[7] = { label: 'MINS' };
      display[8] = { label: 'SECS' };

      // Row 2 (bottom): controls
      const row2 = 2 * c;
      controls[row2] = { buttonId: BUTTON_PLAY, label: 'PLAY' };
      controls[row2 + 1] = { buttonId: BUTTON_PAUSE, label: 'PAUSE' };
      controls[row2 + 2] = { buttonId: BUTTON_STOP, label: 'STOP' };
      controls[row2 + 3] = { buttonId: BUTTON_TOGGLE, label: 'TOGGLE' };
    }

    return { controls, presets, display };
  }

  async updateTimerDisplay(time, flags) {
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

    try {
      // Update control buttons with state color and text
      for (const keyStr of Object.keys(layout.controls)) {
        const key = parseInt(keyStr, 10);
        const ctrl = layout.controls[key];
        await this._fillKeyWithText(key, ctrl.label, controlColor);
      }

      // Presets always cyan when connected, gray otherwise
      const presetColor = flags & FLAG_CONNECTED ? COLOR_CYAN : COLOR_GRAY;
      for (const keyStr of Object.keys(layout.presets)) {
        const key = parseInt(keyStr, 10);
        const preset = layout.presets[key];
        await this._fillKeyWithText(key, preset.label, presetColor);
      }

      // Update display buttons (6, 7, 8) with timer text
      if (this.keyCount >= 15) {
        const mins = Math.floor(time / 60);
        const secs = time % 60;
        const presetMins = Math.floor(this.lastPresetTime / 60);

        // Button 6: Preset time
        await this._fillKeyWithText(6, `${presetMins}:00`, COLOR_BLUE);

        // Button 7: Current minutes
        await this._fillKeyWithText(7, mins.toString().padStart(2, '0'), controlColor);

        // Button 8: Current seconds
        await this._fillKeyWithText(8, secs.toString().padStart(2, '0'), controlColor);
      }
    } catch (err) {
      log.debug('StreamDeck', `Timer display update error: ${err.message}`);
      // Device may have been disconnected
      if (this.connected) {
        this._handleDisconnect();
      }
    }
  }

  async _setInitialColors() {
    if (!this.device) return;

    log.info('StreamDeck', 'Setting initial button colors and labels');

    try {
      // All keys black initially
      for (let i = 0; i < this.keyCount; i++) {
        this._fillKey(i, COLOR_OFF);
      }

      // Light up mapped keys with text labels
      const layout = this._getLayout();
      for (const keyStr of Object.keys(layout.controls)) {
        const key = parseInt(keyStr, 10);
        const ctrl = layout.controls[key];
        await this._fillKeyWithText(key, ctrl.label, COLOR_GRAY);
      }
      for (const keyStr of Object.keys(layout.presets)) {
        const key = parseInt(keyStr, 10);
        const preset = layout.presets[key];
        await this._fillKeyWithText(key, preset.label, COLOR_CYAN);
      }

      // Initialize display buttons with labels
      if (this.keyCount >= 15) {
        await this._fillKeyWithText(6, 'PRESET', COLOR_GRAY);
        await this._fillKeyWithText(7, '--', COLOR_GRAY);
        await this._fillKeyWithText(8, '--', COLOR_GRAY);
      }

      log.info('StreamDeck', 'Initial colors and labels set successfully');
    } catch (err) {
      log.error('StreamDeck', `Failed to set initial colors: ${err.message}`);
    }
  }

  _fillKey(keyIndex, [r, g, b]) {
    if (!this.device || keyIndex >= this.keyCount) return;
    try {
      this.device.fillKeyColor(keyIndex, r, g, b);
    } catch (err) {
      log.debug('StreamDeck', `fillKeyColor error for key ${keyIndex}: ${err.message}`);
      // Device may have been disconnected
      if (this.connected) {
        this._handleDisconnect();
      }
    }
  }

  async _fillKeyWithText(keyIndex, text, [r, g, b]) {
    if (!this.device || keyIndex >= this.keyCount) return;

    try {
      // Create a simple SVG with text
      const svg = `
        <svg width="${ICON_SIZE}" height="${ICON_SIZE}">
          <rect width="${ICON_SIZE}" height="${ICON_SIZE}" fill="rgb(${r},${g},${b})"/>
          <text
            x="50%"
            y="50%"
            text-anchor="middle"
            dominant-baseline="middle"
            font-family="Arial, sans-serif"
            font-size="20"
            font-weight="bold"
            fill="white"
          >${text}</text>
        </svg>
      `;

      // Convert SVG to raw RGBA buffer
      const imageBuffer = await sharp(Buffer.from(svg))
        .resize(ICON_SIZE, ICON_SIZE)
        .raw()
        .toBuffer();

      // Send to Stream Deck
      await this.device.fillKeyBuffer(keyIndex, imageBuffer, { format: 'rgba' });
    } catch (err) {
      log.debug('StreamDeck', `fillKeyWithText error for key ${keyIndex}: ${err.message}`);
      // Fallback to solid color if text rendering fails
      this._fillKey(keyIndex, [r, g, b]);
    }
  }

  broadcastToAll(_message) {
    // Interface compatibility with SerialManager — Stream Deck doesn't receive
    // raw serial messages. Timer display is handled via updateTimerDisplay().
  }
}

module.exports = StreamDeckManager;
