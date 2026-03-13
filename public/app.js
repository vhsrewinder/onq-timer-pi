'use strict';

// --- State ---
let status = null;

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

  // Timer
  if (status.timerState) {
    renderTimer(status.timerState);
  }
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
}
