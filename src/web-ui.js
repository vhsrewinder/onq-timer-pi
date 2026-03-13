'use strict';

const express = require('express');
const { execSync } = require('child_process');
const path = require('path');
const config = require('./config');
const log = require('./logger');

class WebUI {
  constructor(port, serialManager, onqClient, timerRelay) {
    this.port = port;
    this.serialManager = serialManager;
    this.onqClient = onqClient;
    this.timerRelay = timerRelay;
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
        uptime: process.uptime(),
      });
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
