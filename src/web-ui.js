'use strict';

const express = require('express');
const { execSync } = require('child_process');
const path = require('path');
const config = require('./config');
const log = require('./logger');

class WebUI {
  constructor(port, serialManager, onqClient, timerRelay, streamDeckManager) {
    this.port = port;
    this.serialManager = serialManager;
    this.onqClient = onqClient;
    this.timerRelay = timerRelay;
    this.streamDeckManager = streamDeckManager || null;
    this.app = express();
    this.server = null;
    this.sseClients = new Set();

    this._setupRoutes();
  }

  _setupRoutes() {
    this.app.use(express.json());
    this.app.use(express.static(path.join(__dirname, '..', 'public')));

    // Status
    this.app.get('/api/status', (req, res) => {
      res.json({
        devices: this.serialManager.getDevices(),
        onq: this.onqClient.getStatus(),
        timerState: this.timerRelay.getLastTimerState(),
        streamDeck: this.streamDeckManager ? this.streamDeckManager.getStatus() : null,
        uptime: process.uptime(),
      });
    });

    // Stream Deck layout
    this.app.get('/api/streamdeck', (req, res) => {
      if (!this.streamDeckManager) {
        return res.json({ available: false });
      }
      res.json({ available: true, ...this.streamDeckManager.getStatus() });
    });

    // Config
    this.app.get('/api/config', (req, res) => {
      res.json(config.get());
    });

    this.app.put('/api/config', (req, res) => {
      const updates = req.body;
      const newConfig = config.save(updates);

      // Reconnect if server changed
      if (updates.onqServer) {
        const cfg = config.get();
        this.onqClient.updateServerUrl(cfg.onqServer.host, cfg.onqServer.port);
      }

      this._broadcastSSE('config-updated', newConfig);
      res.json(newConfig);
    });

    // Logs
    this.app.get('/api/logs', (req, res) => {
      try {
        const output = execSync('journalctl -u onq-timer-pi -n 50 --no-pager 2>/dev/null || echo "Service logs not available (not running under systemd)"', {
          encoding: 'utf8',
          timeout: 5000,
        });
        res.type('text/plain').send(output);
      } catch {
        res.type('text/plain').send('Failed to read logs');
      }
    });

    // Restart
    this.app.post('/api/restart', (req, res) => {
      log.info('WebUI', 'Restart requested via web UI');
      res.json({ success: true, message: 'Restarting...' });
      setTimeout(() => process.exit(0), 500);
    });

    // Manual timer control
    this.app.post('/api/timer/control', async (req, res) => {
      const { action } = req.body;
      const remoteId = 250; // Numeric remote ID for web UI

      // Button IDs from streamdeck-manager.js
      const BUTTON_PLAY = 10;
      const BUTTON_PAUSE = 12;
      const BUTTON_STOP = 14;

      try {
        switch (action) {
          case 'play':
            await this.onqClient.postButtonPress(remoteId, BUTTON_PLAY);
            log.info('WebUI', 'Manual timer control: PLAY');
            break;
          case 'pause':
            await this.onqClient.postButtonPress(remoteId, BUTTON_PAUSE);
            log.info('WebUI', 'Manual timer control: PAUSE');
            break;
          case 'stop':
            await this.onqClient.postButtonPress(remoteId, BUTTON_STOP);
            log.info('WebUI', 'Manual timer control: STOP');
            break;
          case 'reset-start':
            await this.onqClient.postButtonPress(remoteId, BUTTON_STOP);
            setTimeout(async () => {
              await this.onqClient.postButtonPress(remoteId, BUTTON_PLAY);
            }, 100);
            log.info('WebUI', 'Manual timer control: RESET & START');
            break;
          default:
            return res.status(400).json({ error: 'Invalid action' });
        }
        res.json({ success: true, action });
      } catch (err) {
        log.error('WebUI', `Timer control failed: ${err.message}`);
        res.status(500).json({ error: err.message });
      }
    });

    // Stream Deck button simulation
    this.app.post('/api/streamdeck/simulate', (req, res) => {
      if (!this.streamDeckManager || !this.streamDeckManager.connected) {
        return res.status(400).json({
          success: false,
          error: 'Stream Deck not connected'
        });
      }

      const { index, type } = req.body;

      if (index === undefined || type === undefined) {
        return res.status(400).json({
          success: false,
          error: 'Missing index or type'
        });
      }

      try {
        // Simulate button press by calling the internal handler
        log.info('WebUI', `Simulating Stream Deck button press: index=${index}, type=${type}`);
        this.streamDeckManager._handleKeyDown(index);
        res.json({ success: true, index, type });
      } catch (err) {
        log.error('WebUI', `Stream Deck simulation failed: ${err.message}`);
        res.status(500).json({ success: false, error: err.message });
      }
    });

    // SSE
    this.app.get('/api/events', (req, res) => {
      res.writeHead(200, {
        'Content-Type': 'text/event-stream',
        'Cache-Control': 'no-cache',
        Connection: 'keep-alive',
      });

      res.write('data: {"type":"connected"}\n\n');

      this.sseClients.add(res);
      req.on('close', () => this.sseClients.delete(res));
    });
  }

  start() {
    // Wire up events to SSE
    this.serialManager.on('device-added', (port) => {
      this._broadcastSSE('device-added', { port });
    });
    this.serialManager.on('device-removed', (port) => {
      this._broadcastSSE('device-removed', { port });
    });
    this.onqClient.on('timer-sync', (data) => {
      this._broadcastSSE('timer-sync', data);
    });

    if (this.streamDeckManager) {
      this.streamDeckManager.on('connected', (data) => {
        this._broadcastSSE('streamdeck-connected', data);
      });
      this.streamDeckManager.on('disconnected', () => {
        this._broadcastSSE('streamdeck-disconnected', {});
      });
      // Broadcast button events for logging
      this.streamDeckManager.on('button-press', (data) => {
        this._broadcastSSE('streamdeck-button', { ...data, type: 'button' });
      });
      this.streamDeckManager.on('set-time', (data) => {
        this._broadcastSSE('streamdeck-preset', { ...data, type: 'preset' });
      });
    }

    this.server = this.app.listen(this.port, () => {
      log.info('WebUI', `Config UI at http://0.0.0.0:${this.port}`);
    });
  }

  stop() {
    for (const client of this.sseClients) {
      client.end();
    }
    this.sseClients.clear();
    if (this.server) {
      this.server.close();
    }
  }

  _broadcastSSE(type, data) {
    const msg = `data: ${JSON.stringify({ type, ...data })}\n\n`;
    for (const client of this.sseClients) {
      client.write(msg);
    }
  }
}

module.exports = WebUI;
