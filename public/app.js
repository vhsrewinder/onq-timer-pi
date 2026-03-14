'use strict';

// --- State ---
let status = null;
let sdTimerState = null; // track timer state for SD key colors
let eventLog = []; // Store Stream Deck events

// --- Init ---
fetchStatus();
connectSSE();
setInterval(fetchStatus, 10000); // fallback poll

// --- API calls ---

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    status = await res.json();
    renderStatus();
  } catch { /* ignore */ }
}

async function fetchConfig() {
  try {
    const res = await fetch('/api/config');
    const cfg = await res.json();
    document.getElementById('server-host').placeholder = cfg.onqServer.host;
    document.getElementById('server-port').placeholder = cfg.onqServer.port;
  } catch { /* ignore */ }
}
fetchConfig();

async function saveServer() {
  const host = document.getElementById('server-host').value.trim();
  const port = parseInt(document.getElementById('server-port').value, 10);

  const update = { onqServer: {} };
  if (host) update.onqServer.host = host;
  if (port > 0) update.onqServer.port = port;

  if (!host && !port) return;

  try {
    await fetch('/api/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(update),
    });
    document.getElementById('server-host').value = '';
    document.getElementById('server-port').value = '';
    fetchConfig();
    fetchStatus();
  } catch (err) {
    alert('Failed to save: ' + err.message);
  }
}

async function loadLogs() {
  try {
    const res = await fetch('/api/logs');
    const text = await res.text();
    const box = document.getElementById('log-box');
    box.textContent = text;
    box.scrollTop = box.scrollHeight;
  } catch { /* ignore */ }
}

async function restartService() {
  if (!confirm('Restart the relay service?')) return;
  try {
    await fetch('/api/restart', { method: 'POST' });
  } catch { /* expected — service restarts */ }
  setTimeout(() => location.reload(), 3000);
}

// --- Manual Timer Control ---

async function controlTimer(action) {
  const statusEl = document.getElementById('timer-control-status');

  try {
    statusEl.textContent = 'Sending command...';
    statusEl.style.color = '#888';

    const res = await fetch('/api/timer/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action }),
    });

    const result = await res.json();

    if (result.success) {
      statusEl.textContent = `✓ ${action.toUpperCase()} sent successfully`;
      statusEl.style.color = '#0a0';
      setTimeout(() => {
        statusEl.textContent = '';
      }, 3000);
    } else {
      statusEl.textContent = `✗ Failed: ${result.error || 'Unknown error'}`;
      statusEl.style.color = '#c00';
    }
  } catch (err) {
    statusEl.textContent = `✗ Error: ${err.message}`;
    statusEl.style.color = '#c00';
  }
}

// --- Event Log ---

function addEventToLog(type, data) {
  const timestamp = new Date().toLocaleTimeString();
  const event = { timestamp, type, data };

  eventLog.unshift(event); // Add to beginning
  if (eventLog.length > 50) eventLog.pop(); // Keep last 50 events

  renderEventLog();
}

function clearEventLog() {
  eventLog = [];
  renderEventLog();
}

function renderEventLog() {
  const container = document.getElementById('event-log');

  if (eventLog.length === 0) {
    container.innerHTML = '<p class="empty">No events yet. Press buttons on the Stream Deck.</p>';
    return;
  }

  let html = '<div style="font-size:0.9em;">';
  for (const evt of eventLog) {
    const typeLabel = evt.type === 'button' ? '🔘 Button' : '⏱ Preset';
    let description = '';

    if (evt.type === 'button') {
      description = `Button ID ${evt.data.buttonId} pressed (remote: ${evt.data.remoteId})`;
    } else if (evt.type === 'preset') {
      const mins = Math.floor(evt.data.seconds / 60);
      description = `Preset ${mins}:00 selected (${evt.data.seconds}s, remote: ${evt.data.remoteId})`;
    }

    html += `<div style="padding:0.3rem 0; border-bottom:1px solid #333;">`;
    html += `<span style="color:#888;">[${evt.timestamp}]</span> `;
    html += `<strong>${typeLabel}</strong>: ${description}`;
    html += `</div>`;
  }
  html += '</div>';

  container.innerHTML = html;
}

// --- SSE ---

function connectSSE() {
  const es = new EventSource('/api/events');
  es.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      handleSSE(msg);
    } catch { /* ignore */ }
  };
  es.onerror = () => {
    setTimeout(connectSSE, 5000);
    es.close();
  };
}

function handleSSE(msg) {
  switch (msg.type) {
    case 'timer-sync':
      renderTimer(msg);
      break;
    case 'device-added':
    case 'device-removed':
    case 'config-updated':
      fetchStatus();
      break;
    case 'streamdeck-connected':
    case 'streamdeck-disconnected':
      fetchStatus();
      break;
    case 'streamdeck-button':
      addEventToLog('button', { buttonId: msg.buttonId, remoteId: msg.remoteId });
      break;
    case 'streamdeck-preset':
      addEventToLog('preset', { seconds: msg.seconds, remoteId: msg.remoteId });
      break;
  }
}

// --- Rendering ---

function renderStatus() {
  if (!status) return;

  // OnQ connection
  const dot = document.getElementById('onq-status-dot');
  const text = document.getElementById('onq-status-text');
  const url = document.getElementById('onq-server-url');

  if (status.onq.connected) {
    dot.className = 'status-dot green';
    text.textContent = 'Connected';
  } else {
    dot.className = 'status-dot red';
    text.textContent = 'Disconnected';
  }
  url.textContent = status.onq.server;

  // Devices
  const container = document.getElementById('devices-container');
  const devices = status.devices;

  if (devices.length === 0) {
    container.innerHTML = '<p class="empty">No devices connected</p>';
  } else {
    let html = '<table><tr><th>Port</th><th>Remote</th><th>Battery</th><th>Last Seen</th></tr>';
    for (const d of devices) {
      const ago = d.lastSeen ? Math.round((Date.now() - d.lastSeen) / 1000) + 's ago' : '-';
      const bat = d.battery !== null ? d.battery + '%' : '-';
      html += `<tr><td>${d.port}</td><td>${d.remoteId || '-'}</td><td>${bat}</td><td>${ago}</td></tr>`;
    }
    html += '</table>';
    container.innerHTML = html;
  }

  // Stream Deck
  renderStreamDeck(status.streamDeck);

  // Timer
  if (status.timerState) {
    renderTimer(status.timerState);
  }
}

function renderStreamDeck(sd) {
  const dot = document.getElementById('sd-status-dot');
  const text = document.getElementById('sd-status-text');
  const modelText = document.getElementById('sd-model-text');
  const layoutContainer = document.getElementById('sd-layout');

  if (!sd || !sd.connected) {
    dot.className = 'status-dot red';
    text.textContent = 'Not connected';
    modelText.textContent = '';
    layoutContainer.innerHTML = '<p class="empty">Plug in a Stream Deck to auto-detect</p>';
    return;
  }

  dot.className = 'status-dot green';
  text.textContent = 'Connected';
  modelText.textContent = `(${sd.model}, ${sd.keyCount} keys)`;

  // Render grid layout
  if (sd.layout) {
    renderStreamDeckGrid(layoutContainer, sd.layout, sd.cols || 5);
  }
}

function renderStreamDeckGrid(container, layout, cols) {
  const grid = document.createElement('div');
  grid.className = 'sd-grid';
  grid.style.gridTemplateColumns = `repeat(${cols}, 60px)`;

  for (const key of layout.keys) {
    const el = document.createElement('div');
    el.className = `sd-key ${key.type}`;
    el.textContent = key.label || '';

    // Apply timer state colors to control keys
    if (key.type === 'control' && sdTimerState) {
      const cls = getTimerColorClass(sdTimerState);
      if (cls) el.classList.add(cls);
    }

    grid.appendChild(el);
  }

  container.innerHTML = '';
  container.appendChild(grid);
}

function getTimerColorClass(timerState) {
  if (!timerState) return '';
  const flags = timerState.flags || 0;
  if (flags & 0x02) return 'expired';
  if (flags & 0x01) return 'running';
  if ((flags & 0x04) && timerState.time > 0) return 'paused';
  return '';
}

function renderTimer(data) {
  const el = document.getElementById('timer-display');
  const time = data.time !== undefined ? data.time : 0;
  const flags = data.flags !== undefined ? data.flags : 0;

  const mins = Math.floor(time / 60);
  const secs = time % 60;
  el.textContent = `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;

  const isRunning = (flags & 0x01) !== 0;
  const isExpired = (flags & 0x02) !== 0;
  const isConnected = (flags & 0x04) !== 0;

  el.className = 'timer-display';
  if (isExpired) el.classList.add('expired');
  else if (!isRunning && !isConnected) el.classList.add('stopped');

  // Save for SD grid color updates
  sdTimerState = { time, flags };

  // Re-render SD grid colors if connected
  if (status && status.streamDeck && status.streamDeck.connected && status.streamDeck.layout) {
    const layoutContainer = document.getElementById('sd-layout');
    renderStreamDeckGrid(layoutContainer, status.streamDeck.layout, status.streamDeck.cols || 5);
  }
}
