'use strict';

const config = require('./config');
const log = require('./logger');
const SerialManager = require('./serial-manager');
const OnqClient = require('./onq-client');
const TimerRelay = require('./timer-relay');
const WebUI = require('./web-ui');
const StreamDeckManager = require('./streamdeck-manager');

// Load config
const cfg = config.load();

log.info('Main', '=== OnQ Timer Pi Relay ===');
log.info('Main', `Gateway ID: ${config.getGatewayId()}`);
log.info('Main', `OnQ Server: ${cfg.onqServer.host}:${cfg.onqServer.port}`);

// Create subsystems
const serialManager = new SerialManager(cfg);
const onqClient = new OnqClient(cfg);
const streamDeckManager = new StreamDeckManager();
const timerRelay = new TimerRelay(serialManager, onqClient, streamDeckManager);
const webUI = new WebUI(cfg.webUiPort, serialManager, onqClient, timerRelay, streamDeckManager);

// Wire: serial button-press → OnQ HTTP POST
serialManager.on('button-press', (data) => {
  onqClient.postButtonPress(data.remoteId, data.buttonId);
});

// Wire: serial set-time → OnQ HTTP POST
serialManager.on('set-time', (data) => {
  onqClient.postPreset(data.remoteId, data.seconds);
});

// Wire: Stream Deck button-press → OnQ HTTP POST
streamDeckManager.on('button-press', (data) => {
  onqClient.postButtonPress(data.remoteId, data.buttonId);
});

// Wire: Stream Deck set-time → OnQ HTTP POST
streamDeckManager.on('set-time', (data) => {
  onqClient.postPreset(data.remoteId, data.seconds);
});

// Wire: device count changes → OnQ heartbeat
serialManager.on('device-added', () => {
  onqClient.setConnectedDeviceCount(serialManager.getDevices().length);
});
serialManager.on('device-removed', () => {
  onqClient.setConnectedDeviceCount(serialManager.getDevices().length);
});

// Wire: Stream Deck status → OnQ heartbeat peripherals
function updatePeripherals() {
  const sd = streamDeckManager.getStatus();
  onqClient.setPeripheralStatus({
    streamDeck: { connected: sd.connected, model: sd.model, keyCount: sd.keyCount },
  });
}
streamDeckManager.on('connected', updatePeripherals);
streamDeckManager.on('disconnected', updatePeripherals);

// Start all subsystems
serialManager.start();
onqClient.start();
streamDeckManager.start();
timerRelay.start();
webUI.start();

// Graceful shutdown
function shutdown(signal) {
  log.info('Main', `${signal} received, shutting down...`);
  webUI.stop();
  timerRelay.stop();
  onqClient.stop();
  streamDeckManager.stop();
  serialManager.stop();
  process.exit(0);
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
