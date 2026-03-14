'use strict';

// --- State ---
let timerState = { time: 0, flags: 0 };
let onqConnected = false;

// --- Init ---
fetchStatus();
connectSSE();
setInterval(fetchStatus, 10000); // fallback poll
initFullscreen();

// --- Fullscreen Handling ---

function initFullscreen() {
  const btn = document.getElementById('fullscreen-btn');

  // Check if Fullscreen API is available
  const isFullscreenAvailable =
    document.fullscreenEnabled ||
    document.webkitFullscreenEnabled ||
    document.mozFullScreenEnabled ||
    document.msFullscreenEnabled;

  if (!isFullscreenAvailable) {
    btn.style.display = 'none';
    return;
  }

  btn.addEventListener('click', toggleFullscreen);

  // Listen for fullscreen changes to update button text
  document.addEventListener('fullscreenchange', updateFullscreenButton);
  document.addEventListener('webkitfullscreenchange', updateFullscreenButton);
  document.addEventListener('mozfullscreenchange', updateFullscreenButton);
  document.addEventListener('msfullscreenchange', updateFullscreenButton);
}

function toggleFullscreen() {
  const elem = document.documentElement;

  if (!isFullscreen()) {
    // Enter fullscreen
    if (elem.requestFullscreen) {
      elem.requestFullscreen();
    } else if (elem.webkitRequestFullscreen) {
      elem.webkitRequestFullscreen();
    } else if (elem.mozRequestFullScreen) {
      elem.mozRequestFullScreen();
    } else if (elem.msRequestFullscreen) {
      elem.msRequestFullscreen();
    }
  } else {
    // Exit fullscreen
    if (document.exitFullscreen) {
      document.exitFullscreen();
    } else if (document.webkitExitFullscreen) {
      document.webkitExitFullscreen();
    } else if (document.mozCancelFullScreen) {
      document.mozCancelFullScreen();
    } else if (document.msExitFullscreen) {
      document.msExitFullscreen();
    }
  }
}

function isFullscreen() {
  return !!(
    document.fullscreenElement ||
    document.webkitFullscreenElement ||
    document.mozFullScreenElement ||
    document.msFullscreenElement
  );
}

function updateFullscreenButton() {
  const btn = document.getElementById('fullscreen-btn');
  if (isFullscreen()) {
    btn.textContent = 'Exit Fullscreen';
  } else {
    btn.textContent = 'Fullscreen';
  }
}

// --- API calls ---

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    const status = await res.json();

    onqConnected = status.onq.connected;
    updateConnectionStatus();

    if (status.timerState) {
      updateTimer(status.timerState);
    }

    // Always render the grid, using status layout if available
    renderGrid(status.streamDeck?.layout);
  } catch (err) {
    console.error('Failed to fetch status:', err);
    onqConnected = false;
    updateConnectionStatus();
    renderGrid(); // Render with default layout
  }
}

async function simulateButtonPress(buttonData) {
  try {
    const res = await fetch('/api/streamdeck/simulate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(buttonData),
    });

    const result = await res.json();
    if (!result.success) {
      console.error('Button press failed:', result.error);
    }
  } catch (err) {
    console.error('Error simulating button press:', err);
  }
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
      updateTimer(msg);
      break;
    case 'config-updated':
    case 'device-added':
    case 'device-removed':
      fetchStatus();
      break;
  }
}

// --- Rendering ---

function updateConnectionStatus() {
  const statusEl = document.getElementById('onq-status');
  if (onqConnected) {
    statusEl.textContent = 'OnQ: Connected';
    statusEl.className = 'status-indicator connected';
  } else {
    statusEl.textContent = 'OnQ: Disconnected';
    statusEl.className = 'status-indicator disconnected';
  }
}

function updateTimer(data) {
  timerState = data;
  const time = data.time !== undefined ? data.time : 0;
  const flags = data.flags !== undefined ? data.flags : 0;

  const mins = Math.floor(time / 60);
  const secs = time % 60;

  const timerEl = document.getElementById('timer-display');
  const statusEl = document.getElementById('timer-status');

  timerEl.textContent = `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;

  const isRunning = (flags & 0x01) !== 0;
  const isExpired = (flags & 0x02) !== 0;
  const isConnected = (flags & 0x04) !== 0;
  const isPaused = isConnected && !isRunning && !isExpired && time > 0;

  // Update timer display class
  timerEl.className = 'timer-display';
  statusEl.className = 'timer-status';

  if (isExpired) {
    timerEl.classList.add('expired');
    statusEl.classList.add('expired');
    statusEl.textContent = 'EXPIRED';
  } else if (isRunning) {
    statusEl.textContent = 'RUNNING';
  } else if (isPaused) {
    timerEl.classList.add('paused');
    statusEl.classList.add('paused');
    statusEl.textContent = 'PAUSED';
  } else {
    timerEl.classList.add('stopped');
    statusEl.classList.add('stopped');
    statusEl.textContent = 'STOPPED';
  }

  // Re-render grid to update button states
  renderGrid();
}

function getTimerColorClass() {
  const flags = timerState.flags || 0;
  const time = timerState.time || 0;

  if (flags & 0x02) return 'expired';
  if (flags & 0x01) return 'running';
  if ((flags & 0x04) && time > 0) return 'paused';
  return '';
}

function renderGrid(layout) {
  const container = document.getElementById('sd-grid');

  // Use provided layout or create default 15-key layout
  const keys = layout ? layout.keys : createDefaultLayout();

  container.innerHTML = '';

  keys.forEach((key) => {
    const el = document.createElement('div');
    el.className = `sd-key ${key.type}`;
    el.textContent = key.label || '';

    // Apply timer state colors
    if (key.type === 'control' || key.type === 'status') {
      const colorClass = getTimerColorClass();
      if (colorClass) el.classList.add(colorClass);
    }

    // Make button clickable if it's interactive
    if (['control', 'preset', 'adjustment', 'reset', 'lastPreset'].includes(key.type)) {
      el.addEventListener('click', () => {
        handleButtonClick(key);
      });
    }

    container.appendChild(el);
  });
}

function handleButtonClick(key) {
  const buttonData = {
    index: key.index,
    type: key.type,
  };

  if (key.seconds !== undefined) {
    buttonData.seconds = key.seconds;
  }

  simulateButtonPress(buttonData);
}

function createDefaultLayout() {
  // Default 15-key layout matching the Stream Deck layout
  // Row 0 (0-4): [+5s] [+10s] [1:00] [2:00] [5:00]
  // Row 1 (5-9): [-5s] [-10s] [LAST] [MINS] [SECS]
  // Row 2 (10-14): [STATUS] [empty] [empty] [RESET] [START/PAUSE/RESUME]

  return [
    // Row 0
    { index: 0, type: 'adjustment', label: '+5s' },
    { index: 1, type: 'adjustment', label: '+10s' },
    { index: 2, type: 'preset', label: '1:00', seconds: 60 },
    { index: 3, type: 'preset', label: '2:00', seconds: 120 },
    { index: 4, type: 'preset', label: '5:00', seconds: 300 },

    // Row 1
    { index: 5, type: 'adjustment', label: '-5s' },
    { index: 6, type: 'adjustment', label: '-10s' },
    { index: 7, type: 'lastPreset', label: 'LAST' },
    { index: 8, type: 'display', label: 'MINS' },
    { index: 9, type: 'display', label: 'SECS' },

    // Row 2
    { index: 10, type: 'status', label: 'STATUS' },
    { index: 11, type: 'empty', label: '' },
    { index: 12, type: 'empty', label: '' },
    { index: 13, type: 'reset', label: 'RESET' },
    { index: 14, type: 'control', label: 'START' },
  ];
}
