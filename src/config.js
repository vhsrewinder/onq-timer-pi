'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const log = require('./logger');

const CONFIG_DIR = '/etc/onq-timer-pi';
const CONFIG_PATH = path.join(CONFIG_DIR, 'config.json');

const DEFAULTS = {
  onqServer: {
    host: '192.168.45.40',
    port: 3000,
  },
  webUiPort: 3000,
  serial: {
    baudRate: 115200,
    scanIntervalMs: 5000,
  },
};

let config = null;

function getGatewayId() {
  return `relay-pi-${os.hostname()}`;
}

function load() {
  try {
    if (fs.existsSync(CONFIG_PATH)) {
      const raw = fs.readFileSync(CONFIG_PATH, 'utf8');
      const saved = JSON.parse(raw);
      config = deepMerge(structuredClone(DEFAULTS), saved);
      log.info('Config', `Loaded from ${CONFIG_PATH}`);
    } else {
      config = structuredClone(DEFAULTS);
      log.info('Config', 'Using defaults (no config file found)');
    }
  } catch (err) {
    log.error('Config', `Failed to load config: ${err.message}`);
    config = structuredClone(DEFAULTS);
  }
  return config;
}

function save(updates) {
  config = deepMerge(config, updates);

  try {
    if (!fs.existsSync(CONFIG_DIR)) {
      fs.mkdirSync(CONFIG_DIR, { recursive: true });
    }
    const tmpPath = CONFIG_PATH + '.tmp';
    fs.writeFileSync(tmpPath, JSON.stringify(config, null, 2) + '\n');
    fs.renameSync(tmpPath, CONFIG_PATH);
    log.info('Config', 'Saved config');
  } catch (err) {
    log.error('Config', `Failed to save config: ${err.message}`);
  }
  return config;
}

function get() {
  if (!config) load();
  return config;
}

function deepMerge(target, source) {
  for (const key of Object.keys(source)) {
    if (
      source[key] &&
      typeof source[key] === 'object' &&
      !Array.isArray(source[key]) &&
      target[key] &&
      typeof target[key] === 'object'
    ) {
      deepMerge(target[key], source[key]);
    } else {
      target[key] = source[key];
    }
  }
  return target;
}

module.exports = { load, save, get, getGatewayId };
