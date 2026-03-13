# Preset Timer Integration Guide for React App

**Firmware Version:** 2.1.0
**Last Updated:** 2025-12-21
**Quick Start Guide for React Developers**

---

## Overview

The ESP32 remote now sends **preset timer values** directly to the React app using the `MSG_SET_TIMER` message type. This allows users to quickly set the timer to common values (1m, 2m, 3m, 5m, 10m, 15m) or restore the timer to its value before the last reset.

---

## 1. Message Type to Handle

### MSG_SET_TIMER (0x08)

**When it's sent:**
- User opens preset screen (Physical Button 2)
- User taps a preset button (1min, 2min, 3min, 5min, 10min, 15min)
- User taps "Last Reset" undo button (restores previous timer value)

**What you need to do:**
- Receive the message from bridge
- Parse the timer value (convert from big-endian)
- Set the timer to that value
- Broadcast updated state to all remotes

---

## 2. Packet Structure

### Binary Format (6 bytes total)

```
Byte 0: msgType     = 0x08 (MSG_SET_TIMER)
Byte 1: remoteId    = 1-20 (which remote sent this)
Byte 2: seconds_hi  = High byte of timer value
Byte 3: seconds_lo  = Low byte of timer value
Byte 4: reserved    = 0x00 (unused)
Byte 5: sequence    = Incrementing counter
```

### JavaScript/TypeScript Structure

```typescript
interface SetTimerPacket {
  msgType: number;    // 0x08
  remoteId: number;   // 1-20
  seconds: number;    // Timer value in seconds (big-endian uint16)
  reserved: number;   // Always 0
  sequence: number;   // Packet sequence number
}
```

---

## 3. Implementation Steps

### Step 1: Detect MSG_SET_TIMER in Bridge Handler

```javascript
// In your bridge message parser
const MSG_SET_TIMER = 0x08;

function handleBridgeMessage(buffer) {
  const msgType = buffer[0];

  if (msgType === MSG_SET_TIMER) {
    handleSetTimer(buffer);
  }
  // ... other message types
}
```

### Step 2: Parse the Packet

```javascript
function handleSetTimer(buffer) {
  const msgType = buffer[0];      // 0x08
  const remoteId = buffer[1];     // 1-20
  const secondsHi = buffer[2];    // High byte
  const secondsLo = buffer[3];    // Low byte
  const reserved = buffer[4];     // Unused
  const sequence = buffer[5];     // Sequence number

  // CRITICAL: Convert from big-endian to host byte order
  const seconds = (secondsHi << 8) | secondsLo;

  console.log(`[SET_TIMER] Remote ${remoteId} set timer to ${seconds}s (${formatTime(seconds)})`);

  // Validate range
  if (seconds < 0 || seconds > 3600) {
    console.error(`[SET_TIMER] Invalid timer value: ${seconds}s (max 3600s)`);
    return;
  }

  if (seconds === 0) {
    console.warn(`[SET_TIMER] Timer value is 0, ignoring request`);
    return;
  }

  // Update timer
  setTimerValue(seconds, remoteId);
}
```

### Step 3: Update Timer State

```javascript
function setTimerValue(seconds, remoteId) {
  // POLICY DECISION: What to do if timer is running?
  // Option A: Only allow if timer is stopped (RECOMMENDED)
  if (timerState.isRunning) {
    console.warn(`[SET_TIMER] Timer is running, ignoring preset request`);
    return;
  }

  // Option B: Stop timer and set new value
  // if (timerState.isRunning) {
  //   stopTimer();
  // }

  // Option C: Add time to running timer
  // if (timerState.isRunning) {
  //   seconds += timerState.currentSeconds;
  // }

  // Update internal timer state
  timerState.currentSeconds = seconds;
  timerState.initialSeconds = seconds;
  timerState.isRunning = false;
  timerState.isPaused = false;

  console.log(`[SET_TIMER] Timer set to ${seconds}s by remote ${remoteId}`);

  // Broadcast to ALL remotes (not just the one that sent the request)
  broadcastTimerState();
}
```

### Step 4: Broadcast Updated State to All Remotes

```javascript
function broadcastTimerState() {
  const packet = {
    msgType: 0x01,  // MSG_TIMER_STATE
    seconds: timerState.currentSeconds,
    flags: calculateFlags(),
  };

  // Send to bridge, which forwards to ALL remotes
  sendToBridge(packet);

  console.log(`[BROADCAST] Sent timer state: ${timerState.currentSeconds}s to all remotes`);
}

function calculateFlags() {
  let flags = 0;
  if (timerState.isRunning) flags |= 0x01;  // FLAG_RUNNING
  if (timerState.currentSeconds === 0) flags |= 0x02;  // FLAG_EXPIRED
  if (bridgeConnected) flags |= 0x04;  // FLAG_CONNECTED
  return flags;
}
```

---

## 4. Preset Values Reference

When the user taps a preset button, the remote sends these values:

| Preset Button | Seconds | Hex (Big-Endian) | Display |
|---------------|---------|------------------|---------|
| 1 minute      | 60      | `0x00 0x3C`      | 01:00   |
| 2 minutes     | 120     | `0x00 0x78`      | 02:00   |
| 3 minutes     | 180     | `0x00 0xB4`      | 03:00   |
| 5 minutes     | 300     | `0x01 0x2C`      | 05:00   |
| 10 minutes    | 600     | `0x02 0x58`      | 10:00   |
| 15 minutes    | 900     | `0x03 0x84`      | 15:00   |
| Last Reset    | Variable| Varies           | Variable|

---

## 5. "Last Reset" Undo Feature

### How It Works

1. **User resets timer** (Physical Button 1)
   - Remote saves current timer value (e.g., 527 seconds = 8:47)
   - Remote sends `MSG_BUTTON` with reset command
   - Timer goes to 0:00

2. **User opens presets** (Physical Button 2)
   - Remote shows orange "↻ Last: 08:47" button

3. **User taps "Last Reset"**
   - Remote sends `MSG_SET_TIMER` with `seconds = 527`
   - React app receives and sets timer to 8:47
   - Timer restored to value before reset ✅

### Example Scenario

```
Timer at 08:47 (527 seconds)
  ↓
User presses Button 1 (Reset)
  ↓
Remote saves: lastResetValue = 527
Timer resets to 00:00
  ↓
User presses Button 2 (Open Presets)
  ↓
Remote shows: "↻ Last: 08:47" button
  ↓
User taps "Last: 08:47"
  ↓
Remote sends: MSG_SET_TIMER with seconds = 527
  ↓
React app sets timer to 527s (08:47)
  ↓
Timer restored! ✅
```

---

## 6. Complete Code Example

### Full Implementation (JavaScript)

```javascript
// Message type constants
const MSG_TIMER_STATE = 0x01;
const MSG_BUTTON = 0x02;
const MSG_SET_TIMER = 0x08;

// Timer state
let timerState = {
  currentSeconds: 0,
  initialSeconds: 0,
  isRunning: false,
  isPaused: false,
};

// Handle incoming messages from bridge
function handleBridgeMessage(buffer) {
  const msgType = buffer[0];

  switch (msgType) {
    case MSG_SET_TIMER:
      handleSetTimer(buffer);
      break;
    case MSG_BUTTON:
      handleButton(buffer);
      break;
    // ... other message types
  }
}

// Parse and handle MSG_SET_TIMER
function handleSetTimer(buffer) {
  const remoteId = buffer[1];
  const secondsHi = buffer[2];
  const secondsLo = buffer[3];
  const sequence = buffer[5];

  // Convert big-endian to native byte order
  const seconds = (secondsHi << 8) | secondsLo;

  console.log(`[MSG_SET_TIMER] Remote ${remoteId}, Seconds: ${seconds}, Seq: ${sequence}`);

  // Validate
  if (seconds <= 0 || seconds > 3600) {
    console.error(`[MSG_SET_TIMER] Invalid value: ${seconds}s`);
    return;
  }

  // Only allow if timer is stopped
  if (timerState.isRunning) {
    console.warn(`[MSG_SET_TIMER] Timer running, ignoring preset`);
    return;
  }

  // Update timer
  timerState.currentSeconds = seconds;
  timerState.initialSeconds = seconds;
  timerState.isRunning = false;
  timerState.isPaused = false;

  console.log(`[MSG_SET_TIMER] Timer set to ${formatTime(seconds)} by remote ${remoteId}`);

  // Broadcast to all remotes
  broadcastTimerState();
}

// Broadcast timer state to all remotes
function broadcastTimerState() {
  const flags =
    (timerState.isRunning ? 0x01 : 0) |
    (timerState.currentSeconds === 0 ? 0x02 : 0) |
    (bridgeConnected ? 0x04 : 0);

  const packet = Buffer.alloc(6);
  packet[0] = MSG_TIMER_STATE;
  packet[1] = (timerState.currentSeconds >> 8) & 0xFF;  // High byte
  packet[2] = timerState.currentSeconds & 0xFF;         // Low byte
  packet[3] = flags;
  packet[4] = 0;  // Reserved
  packet[5] = getNextSequence();

  sendToBridge(packet);
  console.log(`[BROADCAST] Sent state: ${formatTime(timerState.currentSeconds)}, flags: 0x${flags.toString(16)}`);
}

// Helper function to format seconds as MM:SS
function formatTime(seconds) {
  const mins = Math.floor(seconds / 60);
  const secs = seconds % 60;
  return `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
}
```

---

## 7. Testing Checklist

### Test 1: Set Preset Timer (Stopped State)
- ✅ Timer is at 0:00 (stopped)
- ✅ Remote: Press Button 2 → Opens presets
- ✅ Remote: Tap "5:00" preset
- ✅ React app receives MSG_SET_TIMER with seconds = 300
- ✅ React app sets timer to 5:00
- ✅ React app broadcasts MSG_TIMER_STATE
- ✅ All remotes display 5:00

**Expected Serial Output (Remote):**
```
[BUTTON] Physical button 2 pressed
[BTN2] Showing presets screen...
[PRESET] 5:00 button pressed
[ESPNOW] Sending SET_TIMER: 300 seconds
```

**Expected Console Output (React App):**
```
[MSG_SET_TIMER] Remote 1, Seconds: 300, Seq: 42
[MSG_SET_TIMER] Timer set to 05:00 by remote 1
[BROADCAST] Sent state: 05:00, flags: 0x4
```

---

### Test 2: Undo Reset
- ✅ Timer is at 8:47 (stopped)
- ✅ Remote: Press Button 1 → Timer resets to 0:00
- ✅ Remote: Press Button 2 → Opens presets
- ✅ Remote: See orange "↻ Last: 08:47" button
- ✅ Remote: Tap "Last: 08:47"
- ✅ React app receives MSG_SET_TIMER with seconds = 527
- ✅ React app sets timer to 8:47
- ✅ All remotes display 8:47

**Expected Serial Output (Remote):**
```
[BTN1] Reset timer - saved value 527 for undo
[BTN2] Showing presets screen...
[PRESET] Last reset button pressed (527 seconds)
[ESPNOW] Sending SET_TIMER: 527 seconds
```

**Expected Console Output (React App):**
```
[MSG_SET_TIMER] Remote 1, Seconds: 527, Seq: 45
[MSG_SET_TIMER] Timer set to 08:47 by remote 1
[BROADCAST] Sent state: 08:47, flags: 0x4
```

---

### Test 3: Ignore Preset While Running
- ✅ Timer is at 3:00 and RUNNING
- ✅ Remote: Press Button 2 → Opens presets
- ✅ Remote: Tap "10:00" preset
- ✅ React app receives MSG_SET_TIMER with seconds = 600
- ✅ React app IGNORES request (timer keeps running)
- ✅ Timer continues counting down from 3:00

**Expected Console Output (React App):**
```
[MSG_SET_TIMER] Remote 1, Seconds: 600, Seq: 48
[MSG_SET_TIMER] Timer running, ignoring preset
```

---

### Test 4: Multiple Remotes
- ✅ Two remotes connected
- ✅ Remote 1: Set 10:00 preset
- ✅ React app broadcasts to Remote 2
- ✅ Remote 2 displays 10:00
- ✅ Remote 2: Set 3:00 preset
- ✅ React app broadcasts to Remote 1
- ✅ Remote 1 displays 3:00 (last preset wins)

---

## 8. Common Issues and Solutions

### Issue 1: Timer value is wrong (e.g., 15360 instead of 60)

**Cause:** Not converting from big-endian byte order.

**Fix:**
```javascript
// ❌ WRONG - directly reading 16-bit value
const seconds = buffer.readUInt16LE(2);  // Little-endian

// ✅ CORRECT - manual big-endian conversion
const seconds = (buffer[2] << 8) | buffer[3];
```

---

### Issue 2: Only one remote updates, others don't

**Cause:** Not broadcasting MSG_TIMER_STATE to all remotes.

**Fix:** Always call `broadcastTimerState()` after updating timer, which sends to **all** remotes, not just the requesting one.

---

### Issue 3: Preset works but "Last Reset" doesn't show up

**Cause:** This is a remote-side issue. The remote firmware saves the last reset value locally in RAM. If the remote restarts, the value is lost.

**Fix:** This is expected behavior. Undo only works until the remote is power-cycled.

---

### Issue 4: Timer gets set while running

**Cause:** Missing check for running state.

**Fix:**
```javascript
if (timerState.isRunning) {
  console.warn('[MSG_SET_TIMER] Timer running, ignoring preset');
  return;  // Don't allow presets while running
}
```

---

## 9. Policy Decisions

You need to decide what happens when MSG_SET_TIMER is received while the timer is running:

### Option A: Ignore (RECOMMENDED)
```javascript
if (timerState.isRunning) {
  console.warn('Timer running, ignoring preset');
  return;
}
```
**Pros:** Prevents accidental timer changes
**Cons:** User must stop timer first

---

### Option B: Stop and Set
```javascript
if (timerState.isRunning) {
  stopTimer();
}
setTimerValue(seconds);
```
**Pros:** More flexible
**Cons:** User might accidentally stop running timer

---

### Option C: Add Time
```javascript
if (timerState.isRunning) {
  seconds += timerState.currentSeconds;
}
setTimerValue(seconds);
```
**Pros:** Useful for adding time while running
**Cons:** Confusing UX (preset says "5:00" but timer goes to 8:00)

**Recommendation:** Use **Option A** for simplest, most predictable behavior.

---

## 10. Message Flow Diagram

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│ Remote 1     │         │ Bridge       │         │ React App    │         │ All Remotes  │
│ (ESP32)      │         │ (ESP32)      │         │ (Pi)         │         │              │
└──────┬───────┘         └──────┬───────┘         └──────┬───────┘         └──────┬───────┘
       │                        │                        │                        │
       │ User taps "5:00"       │                        │                        │
       │ preset button          │                        │                        │
       │                        │                        │                        │
       │ MSG_SET_TIMER          │                        │                        │
       │ (seconds=300)          │                        │                        │
       ├───────────────────────>│                        │                        │
       │                        │                        │                        │
       │                        │ UART/Serial Forward    │                        │
       │                        ├───────────────────────>│                        │
       │                        │                        │                        │
       │                        │                        │ Parse packet           │
       │                        │                        │ Convert big-endian     │
       │                        │                        │ Validate (0 < 300 ≤ 3600) │
       │                        │                        │ Check if stopped       │
       │                        │                        │ Set timer to 300s      │
       │                        │                        │                        │
       │                        │ MSG_TIMER_STATE        │                        │
       │                        │ (broadcast to ALL)     │                        │
       │                        │<───────────────────────┤                        │
       │                        │                        │                        │
       │ Timer Update           │                        │                        │
       │ (shows 05:00)          │                        │                        │
       │<───────────────────────┤                        │                        │
       │                        │                        │                        │
       │                        │ Timer Update           │                        │
       │                        │ (to other remotes)     │                        │
       │                        ├────────────────────────┼───────────────────────>│
       │                        │                        │                        │
       │                        │                        │                        │ All remotes
       │                        │                        │                        │ show 05:00
       └────────────────────────┴────────────────────────┴────────────────────────┘
```

---

## 11. Summary

### What You Need to Implement:

1. ✅ Detect `MSG_SET_TIMER (0x08)` in bridge message handler
2. ✅ Parse 6-byte packet and convert seconds from big-endian
3. ✅ Validate timer value (0 < seconds ≤ 3600)
4. ✅ Update timer state (only if stopped - recommended)
5. ✅ Broadcast `MSG_TIMER_STATE` to ALL remotes
6. ✅ Handle both presets (60, 120, 180, 300, 600, 900) and undo reset (any value)

### Key Points:

- **Big-endian conversion is critical**: `(hi << 8) | lo`
- **Broadcast to all remotes**, not just the one that sent the request
- **Validate range**: 0 < seconds ≤ 3600
- **Policy decision**: What to do if timer is running? (recommend: ignore)
- **Undo reset works automatically** using the same MSG_SET_TIMER message

---

## 12. Contact & Support

**Firmware Developer:** (your name)
**Integration Questions:** (contact info)
**Issue Tracker:** (GitHub link)

---

**Document Version:** 1.0
**Last Updated:** 2025-12-21
**Firmware Version:** 2.1.0
