'use strict';

const log = require('./logger');

const FLAG_RUNNING = 0x01;
const FLAG_EXPIRED = 0x02;
const FLAG_CONNECTED = 0x04;

class TimerRelay {
  constructor(serialManager, onqClient, streamDeckManager) {
    this.serialManager = serialManager;
    this.onqClient = onqClient;
    this.streamDeckManager = streamDeckManager || null;
    this.timeSyncTimer = null;
    this.lastTimerState = null;
  }

  start() {
    // Listen for timer-sync from OnQ server
    this.onqClient.on('timer-sync', (data) => this._handleTimerSync(data));

    // Listen for disconnection to clear displays
    this.onqClient.on('disconnected', () => this._handleDisconnect());

    // Send time-of-day sync every 60 seconds
    this.timeSyncTimer = setInterval(() => this._sendTimeSync(), 60000);

    log.info('TimerRelay', 'Started');
  }

  stop() {
    if (this.timeSyncTimer) {
      clearInterval(this.timeSyncTimer);
      this.timeSyncTimer = null;
    }
  }

  _handleTimerSync(data) {
    // data: { time, isRunning, preset, ... }
    let flags = FLAG_CONNECTED; // always set when receiving data
    if (data.isRunning) flags |= FLAG_RUNNING;
    if (data.time === 0 && !data.isRunning) flags |= FLAG_EXPIRED;

    const msg = {
      type: 'timer-state',
      time: data.time,
      flags,
    };

    this.lastTimerState = msg;
    this.serialManager.broadcastToAll(msg);
    if (this.streamDeckManager) {
      this.streamDeckManager.updateTimerDisplay(data.time, flags);
    }

    log.debug('TimerRelay', `Forwarded timer: ${data.time}s, flags=0x${flags.toString(16).padStart(2, '0')}`);
  }

  _handleDisconnect() {
    // Clear connected flag on all stopwatches
    const msg = {
      type: 'timer-state',
      time: 0,
      flags: 0, // no FLAG_CONNECTED
    };

    this.lastTimerState = msg;
    this.serialManager.broadcastToAll(msg);
    if (this.streamDeckManager) {
      this.streamDeckManager.updateTimerDisplay(0, 0);
    }
    log.info('TimerRelay', 'OnQ disconnected, cleared stopwatch displays');
  }

  _sendTimeSync() {
    const now = new Date();
    const msg = {
      type: 'time-sync',
      hours: now.getHours(),
      minutes: now.getMinutes(),
    };

    this.serialManager.broadcastToAll(msg);
    log.debug('TimerRelay', `Time sync: ${now.getHours()}:${String(now.getMinutes()).padStart(2, '0')}`);
  }

  getLastTimerState() {
    return this.lastTimerState;
  }
}

module.exports = TimerRelay;
