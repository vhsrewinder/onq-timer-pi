'use strict';

const config = require('./config');
const log = require('./logger');
const SerialManager = require('./serial-manager');
const OnqClient = require('./onq-client');
const TimerRelay = require('./timer-relay');
const WebUI = require('./web-ui');

// Load config
const cfg = config.load();

log.info('Main', '=== OnQ Timer Pi Relay ===');
log.info('Main', `Gateway ID: ${config.getGatewayId()}`);
log.info('Main', `OnQ Server: ${cfg.onqServer.host}:${cfg.onqServer.port}`);

// Create subsystems
const serialManager = new SerialManager(cfg);
const onqClient = new OnqClient(cfg);
const timerRelay = new TimerRelay(serialManager, onqClient);
const webUI = new WebUI(cfg.webUiPort, serialManager, onqClient, timerRelay);

// Wire: serial button-press → OnQ HTTP POST
serialManager.on('button-press', (data) => {
  onqClient.postButtonPress(data.remoteId, data.buttonId);
});

// Wire: serial set-time → OnQ HTTP POST
serialManager.on('set-time', (data) => {
  onqClient.postPreset(data.remoteId, data.seconds);
});

// Start all subsystems
serialManager.start();
onqClient.start();
timerRelay.start();
webUI.start();

// Graceful shutdown
function shutdown(signal) {
  log.info('Main', `${signal} received, shutting down...`);
  webUI.stop();
  timerRelay.stop();
  onqClient.stop();
  serialManager.stop();
  process.exit(0);
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
