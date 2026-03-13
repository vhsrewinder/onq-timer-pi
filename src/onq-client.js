'use strict';

const { EventEmitter } = require('events');
const { io } = require('socket.io-client');
const config = require('./config');
const log = require('./logger');

class OnqClient extends EventEmitter {
  constructor(cfg) {
    super();
    this.host = cfg.onqServer.host;
    this.port = cfg.onqServer.port;
    this.gatewayId = config.getGatewayId();
    this.socket = null;
    this.connected = false;
    this._heartbeatInterval = null;
    this._connectedDeviceCount = 0;
    this._startTime = Date.now();
    this.stats = {
      buttonsSent: 0,
      presetsSent: 0,
      timerSyncsReceived: 0,
      httpErrors: 0,
    };
  }

  start() {
    this._connectSocket();
  }

  stop() {
    this._stopHeartbeat();
    if (this.socket) {
      this.socket.disconnect();
      this.socket = null;
    }
    this.connected = false;
  }

  _getBaseUrl() {
    return `http://${this.host}:${this.port}`;
  }

  _connectSocket() {
    const url = this._getBaseUrl();
    log.info('OnqClient', `Connecting Socket.IO to ${url}`);

    this.socket = io(url, {
      path: '/socket.io',
      reconnection: true,
      reconnectionDelay: 1000,
      reconnectionDelayMax: 30000,
      reconnectionAttempts: Infinity,
      transports: ['websocket', 'polling'],
    });

    this.socket.on('connect', () => {
      this.connected = true;
      log.info('OnqClient', `Socket.IO connected (id: ${this.socket.id})`);

      // Register as gateway display
      this.socket.emit('register', {
        participantId: this.gatewayId,
        appType: 'gateway-display',
      });

      // Register as a gateway in OnQ so we appear in Settings → Gateways
      this._registerGateway();
      this._startHeartbeat();
    });

    this.socket.on('disconnect', (reason) => {
      this.connected = false;
      this._stopHeartbeat();
      log.warn('OnqClient', `Socket.IO disconnected: ${reason}`);
      this.emit('disconnected');
    });

    this.socket.on('timer-sync', (data) => {
      this.stats.timerSyncsReceived++;
      this.emit('timer-sync', data);
    });

    this.socket.on('connect_error', (err) => {
      log.debug('OnqClient', `Socket.IO connect error: ${err.message}`);
    });
  }

  async postButtonPress(remoteId, buttonId) {
    const url = `${this._getBaseUrl()}/api/bridge/button`;
    const body = {
      remoteId,
      buttonId,
      gatewayId: this.gatewayId,
      timestamp: Date.now(),
    };

    try {
      const res = await this._postWithRetry(url, body);
      if (res.ok) {
        this.stats.buttonsSent++;
        log.info('OnqClient', `Button press sent: remote=${remoteId} button=${buttonId}`);
      } else {
        const text = await res.text();
        log.warn('OnqClient', `Button press rejected (${res.status}): ${text}`);
      }
    } catch (err) {
      this.stats.httpErrors++;
      log.error('OnqClient', `Button press failed: ${err.message}`);
    }
  }

  async postPreset(remoteId, seconds) {
    const url = `${this._getBaseUrl()}/api/bridge/preset`;
    const body = {
      remoteId,
      seconds,
      gatewayId: this.gatewayId,
      timestamp: Date.now(),
    };

    try {
      const res = await this._postWithRetry(url, body);
      if (res.ok) {
        this.stats.presetsSent++;
        log.info('OnqClient', `Preset sent: remote=${remoteId} seconds=${seconds}`);
      } else {
        const text = await res.text();
        log.warn('OnqClient', `Preset rejected (${res.status}): ${text}`);
      }
    } catch (err) {
      this.stats.httpErrors++;
      log.error('OnqClient', `Preset failed: ${err.message}`);
    }
  }

  async _postWithRetry(url, body, retries = 1) {
    try {
      return await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
        signal: AbortSignal.timeout(5000),
      });
    } catch (err) {
      if (retries > 0) {
        await new Promise((r) => setTimeout(r, 500));
        return this._postWithRetry(url, body, retries - 1);
      }
      throw err;
    }
  }

  updateServerUrl(host, port) {
    this.host = host;
    this.port = port;
    log.info('OnqClient', `Server URL updated to ${host}:${port}, reconnecting...`);
    this.stop();
    this.start();
  }

  setConnectedDeviceCount(count) {
    this._connectedDeviceCount = count;
  }

  async _registerGateway() {
    const url = `${this._getBaseUrl()}/api/gateways/register`;
    const body = {
      gatewayId: this.gatewayId,
    };

    try {
      const res = await this._postWithRetry(url, body);
      if (res.ok) {
        log.info('OnqClient', `Gateway registered: ${this.gatewayId}`);
      } else {
        const text = await res.text();
        log.warn('OnqClient', `Gateway registration failed (${res.status}): ${text}`);
      }
    } catch (err) {
      log.error('OnqClient', `Gateway registration error: ${err.message}`);
    }
  }

  _startHeartbeat() {
    this._stopHeartbeat();
    this._heartbeatInterval = setInterval(() => this._sendHeartbeat(), 30000);
  }

  _stopHeartbeat() {
    if (this._heartbeatInterval) {
      clearInterval(this._heartbeatInterval);
      this._heartbeatInterval = null;
    }
  }

  async _sendHeartbeat() {
    const url = `${this._getBaseUrl()}/api/gateways/heartbeat`;
    const body = {
      gatewayId: this.gatewayId,
      uptime: Math.floor((Date.now() - this._startTime) / 1000),
      connectedNodes: this._connectedDeviceCount,
    };

    try {
      const res = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
        signal: AbortSignal.timeout(5000),
      });
      if (!res.ok) {
        log.debug('OnqClient', `Heartbeat rejected (${res.status})`);
      }
    } catch (err) {
      log.debug('OnqClient', `Heartbeat failed: ${err.message}`);
    }
  }

  getStatus() {
    return {
      connected: this.connected,
      server: `${this.host}:${this.port}`,
      gatewayId: this.gatewayId,
      socketId: this.socket?.id || null,
      stats: { ...this.stats },
    };
  }
}

module.exports = OnqClient;
