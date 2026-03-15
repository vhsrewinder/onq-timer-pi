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
const PRESETS = [60, 120, 300]; // 1, 2, 5 min

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
const COLOR_PURPLE = [150, 0, 150];
const COLOR_ORANGE = [220, 120, 0];

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
    this.currentTime = 0; // Track current timer time for adjustments
    this.lastResetTime = 0; // Track time at moment of reset for LAST preset
    this.currentControlButtonId = BUTTON_PLAY; // Track current control button action
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

    for (let i = 0; i < totalKeys; i++) {
      if (layout.presets[i]) {
        keys.push({ index: i, type: 'preset', label: layout.presets[i].label, seconds: layout.presets[i].seconds });
      } else if (layout.controls[i]) {
        keys.push({ index: i, type: 'control', label: layout.controls[i].label });
      } else if (layout.display[i]) {
        keys.push({ index: i, type: 'display', label: layout.display[i].label });
      } else if (layout.adjustments[i]) {
        keys.push({ index: i, type: 'adjustment', label: layout.adjustments[i].label });
      } else if (layout.special[i]) {
        keys.push({ index: i, type: layout.special[i].type, label: layout.special[i].label });
      } else {
        keys.push({ index: i, type: 'empty', label: '' });
      }
    }

    return { keys, cols: this.cols };
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

      // Set up button handler
      this.device.on('down', (event) => {
        log.info('StreamDeck', `Raw key down event: ${JSON.stringify(event)}, type: ${typeof event}`);

        // Try multiple possible formats
        let keyIndex;
        if (typeof event === 'number') {
          keyIndex = event;
        } else if (typeof event === 'object' && event !== null) {
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

    // Check control buttons (combined START/PAUSE/RESUME button)
    const controlAction = layout.controls[keyIndex];
    if (controlAction) {
      log.info('StreamDeck', `Control button: ${controlAction.label} (key ${keyIndex}, buttonId ${this.currentControlButtonId})`);
      this.emit('button-press', {
        remoteId: this.remoteId,
        buttonId: this.currentControlButtonId,
      });
      return;
    }

    // Check preset buttons
    const presetAction = layout.presets[keyIndex];
    if (presetAction) {
      log.info('StreamDeck', `Preset button: ${presetAction.label} (key ${keyIndex}, ${presetAction.seconds}s)`);
      this.emit('set-time', {
        remoteId: this.remoteId,
        seconds: presetAction.seconds,
      });
      return;
    }

    // Check adjustment buttons
    const adjustAction = layout.adjustments[keyIndex];
    if (adjustAction) {
      const newTime = Math.max(0, this.currentTime + adjustAction.adjust);
      log.info('StreamDeck', `Adjustment: ${adjustAction.label} (key ${keyIndex}, ${this.currentTime}s → ${newTime}s)`);
      this.emit('set-time', {
        remoteId: this.remoteId,
        seconds: newTime,
      });
      return;
    }

    // Check special buttons
    const specialAction = layout.special[keyIndex];
    if (specialAction) {
      if (specialAction.type === 'reset') {
        log.info('StreamDeck', `RESET button pressed (key ${keyIndex})`);
        // Capture current time for LAST preset
        this.lastResetTime = this.currentTime;
        log.info('StreamDeck', `Captured last reset time: ${this.lastResetTime}s`);
        // Send STOP
        this.emit('button-press', {
          remoteId: this.remoteId,
          buttonId: BUTTON_STOP,
        });
      } else if (specialAction.type === 'lastPreset') {
        log.info('StreamDeck', `LAST preset button pressed (key ${keyIndex}, ${this.lastResetTime}s)`);
        if (this.lastResetTime > 0) {
          this.emit('set-time', {
            remoteId: this.remoteId,
            seconds: this.lastResetTime,
          });
        }
      } else if (specialAction.type === 'status') {
        log.debug('StreamDeck', `STATUS display (key ${keyIndex}) - display only, no action`);
      }
      return;
    }

    log.debug('StreamDeck', `Unmapped key pressed: ${keyIndex}`);
  }

  _getLayout() {
    // Build key mappings based on device size
    const controls = {};
    const presets = {};
    const display = {};
    const adjustments = {};
    const special = {};

    if (this.keyCount <= 6) {
      // 6-key Mini: simplified layout
      presets[0] = { seconds: 60, label: '1:00' };
      presets[1] = { seconds: 120, label: '2:00' };
      presets[2] = { seconds: 300, label: '5:00' };
      controls[3] = { buttonId: BUTTON_PLAY, label: 'START' };
      controls[4] = { buttonId: BUTTON_PAUSE, label: 'PAUSE' };
      special[5] = { type: 'reset', label: 'RESET' };
    } else {
      // 15-key (standard) layout:
      // Row 0 (0-4): [+5s] [+10s] [1:00] [2:00] [5:00]
      // Row 1 (5-9): [-5s] [-10s] [LAST] [MINS] [SECS]
      // Row 2 (10-14): [STATUS] [empty] [empty] [RESET] [START/PAUSE/RESUME]

      // Row 0: Adjustments and presets
      adjustments[0] = { adjust: 5, label: '+5s' };
      adjustments[1] = { adjust: 10, label: '+10s' };
      presets[2] = { seconds: 60, label: '1:00' };
      presets[3] = { seconds: 120, label: '2:00' };
      presets[4] = { seconds: 300, label: '5:00' };

      // Row 1: Adjustments, LAST, and display
      adjustments[5] = { adjust: -5, label: '-5s' };
      adjustments[6] = { adjust: -10, label: '-10s' };
      special[7] = { type: 'lastPreset', label: 'LAST' };
      display[8] = { label: 'MINS' };
      display[9] = { label: 'SECS' };

      // Row 2: STATUS, RESET, and combined START/PAUSE/RESUME
      special[10] = { type: 'status', label: 'STATUS' };
      // 11 and 12 are empty
      special[13] = { type: 'reset', label: 'RESET' };
      controls[14] = { buttonId: BUTTON_PLAY, label: 'START' }; // Will be updated dynamically
    }

    return { controls, presets, display, adjustments, special };
  }

  async updateTimerDisplay(time, flags) {
    if (!this.connected || !this.device) return;
    this.lastFlags = flags;
    this.currentTime = time; // Track current time for adjustments

    // Determine state
    const isRunning = (flags & FLAG_RUNNING) !== 0;
    const isExpired = (flags & FLAG_EXPIRED) !== 0;
    const isConnected = (flags & FLAG_CONNECTED) !== 0;
    const isPaused = isConnected && !isRunning && !isExpired && time > 0;
    const isStopped = time === 0 && !isRunning;

    // Determine state color (traffic light)
    let stateColor;
    let statusText;
    if (isExpired) {
      stateColor = COLOR_RED;
      statusText = 'EXPIRED';
    } else if (isRunning) {
      stateColor = COLOR_GREEN;
      statusText = 'RUNNING';
    } else if (isPaused) {
      stateColor = COLOR_YELLOW;
      statusText = 'PAUSED';
    } else {
      stateColor = COLOR_GRAY;
      statusText = 'STOPPED';
    }

    const layout = this._getLayout();

    try {
      // Update the combined START/PAUSE/RESUME button
      for (const keyStr of Object.keys(layout.controls)) {
        const key = parseInt(keyStr, 10);
        const ctrl = layout.controls[key];

        // Determine button label and action based on state
        let buttonLabel;
        let buttonId;
        if (isStopped) {
          buttonLabel = 'START';
          buttonId = BUTTON_PLAY;
        } else if (isPaused) {
          buttonLabel = 'RESUME';
          buttonId = BUTTON_PLAY;
        } else if (isRunning) {
          buttonLabel = 'PAUSE';
          buttonId = BUTTON_PAUSE;
        } else {
          buttonLabel = 'START';
          buttonId = BUTTON_PLAY;
        }

        // Update the instance variable for future button presses
        this.currentControlButtonId = buttonId;

        await this._fillKeyWithText(key, buttonLabel, stateColor);
      }

      // Update preset buttons (cyan when connected)
      const presetColor = isConnected ? COLOR_CYAN : COLOR_GRAY;
      for (const keyStr of Object.keys(layout.presets)) {
        const key = parseInt(keyStr, 10);
        const preset = layout.presets[key];
        await this._fillKeyWithText(key, preset.label, presetColor);
      }

      // Update adjustment buttons (purple when connected)
      const adjColor = isConnected ? COLOR_PURPLE : COLOR_GRAY;
      for (const keyStr of Object.keys(layout.adjustments)) {
        const key = parseInt(keyStr, 10);
        const adj = layout.adjustments[key];
        await this._fillKeyWithText(key, adj.label, adjColor);
      }

      // Update special buttons
      for (const keyStr of Object.keys(layout.special)) {
        const key = parseInt(keyStr, 10);
        const special = layout.special[key];

        if (special.type === 'lastPreset') {
          // LAST preset - show captured time or "LAST"
          const lastMins = Math.floor(this.lastResetTime / 60);
          const lastSecs = this.lastResetTime % 60;
          const lastLabel = this.lastResetTime > 0
            ? `${lastMins}:${String(lastSecs).padStart(2, '0')}`
            : 'LAST';
          await this._fillKeyWithText(key, lastLabel, this.lastResetTime > 0 ? COLOR_ORANGE : COLOR_GRAY);
        } else if (special.type === 'reset') {
          // RESET button
          await this._fillKeyWithText(key, special.label, isConnected ? COLOR_RED : COLOR_GRAY);
        } else if (special.type === 'status') {
          // STATUS display with traffic light colors
          await this._fillKeyWithText(key, statusText, stateColor);
        }
      }

      // Update display buttons with timer values
      if (this.keyCount >= 15) {
        const mins = Math.floor(time / 60);
        const secs = time % 60;

        // Button 8: Current minutes
        await this._fillKeyWithText(8, mins.toString().padStart(2, '0'), stateColor);

        // Button 9: Current seconds
        await this._fillKeyWithText(9, secs.toString().padStart(2, '0'), stateColor);
      }
    } catch (err) {
      log.debug('StreamDeck', `Timer display update error: ${err.message}`);
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

      // Controls
      for (const keyStr of Object.keys(layout.controls)) {
        const key = parseInt(keyStr, 10);
        const ctrl = layout.controls[key];
        await this._fillKeyWithText(key, ctrl.label, COLOR_GRAY);
      }

      // Presets
      for (const keyStr of Object.keys(layout.presets)) {
        const key = parseInt(keyStr, 10);
        const preset = layout.presets[key];
        await this._fillKeyWithText(key, preset.label, COLOR_CYAN);
      }

      // Adjustments
      for (const keyStr of Object.keys(layout.adjustments)) {
        const key = parseInt(keyStr, 10);
        const adj = layout.adjustments[key];
        await this._fillKeyWithText(key, adj.label, COLOR_PURPLE);
      }

      // Special buttons
      for (const keyStr of Object.keys(layout.special)) {
        const key = parseInt(keyStr, 10);
        const special = layout.special[key];
        if (special.type === 'lastPreset') {
          await this._fillKeyWithText(key, 'LAST', COLOR_GRAY);
        } else if (special.type === 'reset') {
          await this._fillKeyWithText(key, 'RESET', COLOR_GRAY);
        } else if (special.type === 'status') {
          await this._fillKeyWithText(key, 'STOPPED', COLOR_GRAY);
        }
      }

      // Display buttons
      if (this.keyCount >= 15) {
        await this._fillKeyWithText(8, '--', COLOR_GRAY);
        await this._fillKeyWithText(9, '--', COLOR_GRAY);
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
            font-size="18"
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
    // Interface compatibility with SerialManager
  }
}

module.exports = StreamDeckManager;
