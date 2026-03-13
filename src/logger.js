'use strict';

const LEVELS = { debug: 0, info: 1, warn: 2, error: 3 };

let currentLevel = LEVELS.info;

function setLevel(level) {
  if (LEVELS[level] !== undefined) {
    currentLevel = LEVELS[level];
  }
}

function formatTime() {
  return new Date().toISOString();
}

function log(level, tag, message, data) {
  if (LEVELS[level] < currentLevel) return;

  const entry = {
    time: formatTime(),
    level: level.toUpperCase(),
    tag,
    msg: message,
  };
  if (data !== undefined) entry.data = data;

  const prefix = `${entry.time} [${entry.level}] [${tag}]`;
  if (data !== undefined) {
    console.log(`${prefix} ${message}`, typeof data === 'object' ? JSON.stringify(data) : data);
  } else {
    console.log(`${prefix} ${message}`);
  }
}

module.exports = {
  setLevel,
  debug: (tag, msg, data) => log('debug', tag, msg, data),
  info: (tag, msg, data) => log('info', tag, msg, data),
  warn: (tag, msg, data) => log('warn', tag, msg, data),
  error: (tag, msg, data) => log('error', tag, msg, data),
};
