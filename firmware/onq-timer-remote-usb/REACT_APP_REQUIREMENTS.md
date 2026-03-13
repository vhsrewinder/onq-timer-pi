# React App Integration Requirements - Firmware v2.1.0

**Date:** 2025-12-19
**Remote Firmware Version:** 2.1.0
**Features:** Preset Timers, Volume Control, Undo Reset

---

## Overview

The ESP32 remote firmware v2.1.0 introduces 3 new features that require React app integration:

1. **Preset Timer Selection** - Remotes can now set timer to preset values (1m, 2m, 3m, 5m, 10m, 15m)
2. **Volume Control** - Remotes can adjust system volume from settings screen
3. **Undo Reset** - Remotes can restore timer to value before last reset

This document specifies the new ESP-NOW message types and expected React app behavior.

---

## New ESP-NOW Message Types

### 1. MSG_SET_TIMER (0x08)

**Direction:** Remote → Bridge → React App

**Purpose:** Set timer to a specific value in seconds (used for presets and undo reset).

**Packet Structure (6 bytes):**
```c
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // 0x08 (MSG_SET_TIMER)
    uint8_t remoteId;   // 1-20 (which remote sent this)
    uint16_t seconds;   // Timer value in seconds (big-endian)
    uint8_t reserved;   // Reserved for future use
    uint8_t sequence;   // Incrementing packet counter
} SetTimerPacket;
```

**When Sent:**
- User selects a preset time button (1m, 2m, 3m, 5m, 10m, 15m)
- User selects "Last Reset" undo button (restores timer to value before last reset)

**Expected React App Behavior:**
1. Receive MSG_SET_TIMER from bridge
2. **Convert big-endian to host byte order:**
   ```javascript
   const seconds = (packet.seconds >> 8) | ((packet.seconds & 0xFF) << 8);
   ```
3. **Set timer to specified value:**
   - If timer is currently stopped: Set timer to value
   - If timer is running: **User decision** - either:
     - Option A: Ignore the request (timer keeps running)
     - Option B: Stop timer and set to new value
     - Option C: Add time to running timer
4. **Broadcast updated timer state** to all remotes (MSG_TIMER_STATE)
5. **Log the action** for debugging

**Example Values:**
- 1 minute = 60 seconds = `0x003C` (big-endian)
- 5 minutes = 300 seconds = `0x012C` (big-endian)
- 15 minutes = 900 seconds = `0x0384` (big-endian)

**Error Handling:**
- If seconds > max allowed (e.g., 60 minutes = 3600s), clamp to maximum
- If seconds = 0, log warning and ignore

---

### 2. MSG_SET_VOLUME (0x09)

**Direction:** Remote → Bridge → React App

**Purpose:** Adjust system audio volume (0-100%).

**Packet Structure (6 bytes):**
```c
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // 0x09 (MSG_SET_VOLUME)
    uint8_t remoteId;   // 1-20 (which remote sent this)
    uint8_t volume;     // Volume level 0-100%
    uint8_t reserved[2];// Reserved for future use
    uint8_t sequence;   // Incrementing packet counter
} SetVolumePacket;
```

**When Sent:**
- User is in settings screen on remote
- User presses physical button 1 (decrease) or button 3 (increase)
- Volume adjusts in 10% steps (e.g., 50% → 60% → 70%)

**Expected React App Behavior:**
1. Receive MSG_SET_VOLUME from bridge
2. **Set system volume** using appropriate API:
   ```javascript
   // Example using alsa (Linux)
   exec(`amixer -D pulse sset Master ${volume}%`);

   // Or using Web Audio API (browser)
   audioContext.destination.volume = volume / 100.0;
   ```
3. **Persist volume setting** (optional) to localStorage/config file
4. **Log the change** for debugging
5. **No timer state broadcast needed** (volume is independent of timer)

**Example Values:**
- 0% = Mute
- 50% = Half volume
- 100% = Maximum volume

**Error Handling:**
- If volume > 100, clamp to 100
- If volume < 0, clamp to 0
- If volume command fails, log error but don't crash

---

## Implementation Checklist

### Bridge ESP-NOW Handler
- [ ] Add MSG_SET_TIMER case to `onDataRecv()` handler
- [ ] Add MSG_SET_VOLUME case to `onDataRecv()` handler
- [ ] Forward both message types to React app via UART/Serial
- [ ] Log incoming messages for debugging

### React App Backend
- [ ] Parse MSG_SET_TIMER from bridge serial input
- [ ] Parse MSG_SET_VOLUME from bridge serial input
- [ ] Implement big-endian conversion for timer seconds field
- [ ] Add timer value validation (0 < seconds <= MAX_TIMER)
- [ ] Add volume validation (0 <= volume <= 100)
- [ ] Implement system volume control (platform-specific)

### React App Timer Logic
- [ ] **Decide policy:** What happens if MSG_SET_TIMER received while timer is running?
  - Recommended: Only allow if timer is stopped/expired
  - Alternative: Stop timer, set new value, send update
- [ ] Update timer state when MSG_SET_TIMER received
- [ ] Broadcast updated state to ALL remotes (not just requesting remote)
- [ ] Maintain "last timer value" for each remote (for undo feature consistency)

### React App Frontend (Optional)
- [ ] Add UI to show which remote triggered preset selection
- [ ] Add UI to show volume level (if displayed)
- [ ] Add button/slider for manual volume control
- [ ] Add preset buttons to web interface (for consistency with remote)

---

## Testing Plan

### Test 1: Preset Timer Selection
**Setup:**
1. Remote showing stopped timer (00:00)
2. React app connected to bridge

**Steps:**
1. Press physical button 2 on remote → Opens presets screen
2. Tap "5:00" preset button on remote touchscreen
3. Verify remote sends MSG_SET_TIMER with `seconds = 300`
4. Verify React app sets timer to 5:00
5. Verify React app broadcasts MSG_TIMER_STATE with 300 seconds
6. Verify ALL remotes update to show 5:00

**Expected Result:**
✅ Timer set to 5 minutes on all remotes
✅ No errors in logs
✅ Remote returns to main screen

### Test 2: Undo Reset
**Setup:**
1. Timer running at 8:47
2. User presses reset button

**Steps:**
1. Press physical button 1 on remote → Shows reset confirmation
2. Hold button 1 for 1 second → Timer resets to 00:00
3. Press physical button 2 → Opens presets
4. Verify "Last: 08:47" button is visible (orange)
5. Tap "Last: 08:47" button
6. Verify remote sends MSG_SET_TIMER with `seconds = 527`
7. Verify React app sets timer to 8:47
8. Verify timer displays 8:47 on all remotes

**Expected Result:**
✅ Timer restored to 8:47
✅ "Last Reset" button worked correctly
✅ All remotes synchronized

### Test 3: Volume Control
**Setup:**
1. Remote in settings screen (triple-tap to open)
2. Current volume at 50%

**Steps:**
1. Triple-tap remote screen → Opens settings
2. Press physical button 2 → Switches to "VOLUME" mode
3. Press physical button 3 (increase) → Volume should increase to 60%
4. Verify remote sends MSG_SET_VOLUME with `volume = 60`
5. Verify React app changes system volume to 60%
6. Press physical button 1 (decrease) → Volume should decrease to 50%
7. Verify MSG_SET_VOLUME with `volume = 50` sent
8. Verify system volume changes to 50%

**Expected Result:**
✅ Volume changes reflected in system audio
✅ Remote displays correct volume percentage
✅ No lag or errors

### Test 4: Multiple Remotes Preset Sync
**Setup:**
1. Two remotes connected to bridge
2. Timer stopped on both

**Steps:**
1. Remote 1: Select 10:00 preset
2. Verify React app broadcasts to Remote 2
3. Verify Remote 2 displays 10:00
4. Remote 2: Select 3:00 preset
5. Verify React app broadcasts to Remote 1
6. Verify Remote 1 displays 3:00

**Expected Result:**
✅ Both remotes stay synchronized
✅ Last preset selection wins
✅ No conflicts or race conditions

---

## Protocol Details

### Message Flow: Preset Selection

```
┌─────────┐         ┌────────┐         ┌──────────┐         ┌───────────┐
│ Remote  │         │ Bridge │         │ React App│         │ All       │
│ (ESP32) │         │(ESP32) │         │  (Pi)    │         │ Remotes   │
└────┬────┘         └───┬────┘         └────┬─────┘         └─────┬─────┘
     │                  │                   │                     │
     │ MSG_SET_TIMER   │                   │                     │
     │ (seconds=300)    │                   │                     │
     ├─────────────────>│                   │                     │
     │                  │ UART Forward      │                     │
     │                  ├──────────────────>│                     │
     │                  │                   │                     │
     │                  │                   │ Set timer to 300s   │
     │                  │                   │ (internal logic)    │
     │                  │                   │                     │
     │                  │ MSG_TIMER_STATE   │                     │
     │                  │<──────────────────┤                     │
     │                  │ (broadcast)       │                     │
     │ Timer Update     │                   │                     │
     │<─────────────────┤                   │                     │
     │                  │                   │                     │
     │                  │ Timer Update      │                     │
     │                  ├───────────────────┼────────────────────>│
     │                  │                   │                     │
     └──────────────────┴───────────────────┴─────────────────────┘
```

### Message Flow: Volume Adjustment

```
┌─────────┐         ┌────────┐         ┌──────────┐
│ Remote  │         │ Bridge │         │ React App│
│ (ESP32) │         │(ESP32) │         │  (Pi)    │
└────┬────┘         └───┬────┘         └────┬─────┘
     │                  │                   │
     │ MSG_SET_VOLUME   │                   │
     │ (volume=75)      │                   │
     ├─────────────────>│                   │
     │                  │ UART Forward      │
     │                  ├──────────────────>│
     │                  │                   │
     │                  │                   │ Set system volume
     │                  │                   │ (amixer/pactl)
     │                  │                   │
     │                  │                   │ (No response needed)
     │                  │                   │
     └──────────────────┴───────────────────┘
```

---

## FAQ

### Q: What if multiple remotes try to set different presets simultaneously?

**A:** The React app should process them in order received. The last MSG_SET_TIMER received wins. React app then broadcasts the final timer value to all remotes, ensuring synchronization.

### Q: Should volume changes be synced across remotes?

**A:** **No.** Volume is a system-wide setting, not per-remote. Each remote can independently adjust volume, and all changes affect the same system audio. Remotes do not display volume level on the main screen, only in settings.

### Q: What happens if React app is not running when MSG_SET_TIMER is received?

**A:** The bridge should buffer or drop the message (implementation-dependent). When React app reconnects, it should send current timer state to all remotes to resynchronize.

### Q: Can users set custom preset times from the React app web interface?

**A:** **Future enhancement.** Currently, presets are hardcoded in remote firmware (1m, 2m, 3m, 5m, 10m, 15m). Future versions could support configurable presets via web interface.

### Q: How does "undo reset" work with multiple remotes?

**A:** Each remote stores its own "last reset value" in RAM. If Remote 1 resets the timer, only Remote 1 will have the undo option. If Remote 2 then resets the timer, Remote 2 will have a different undo value. This is intentional to avoid conflicts.

---

## Security Considerations

1. **Input Validation:** Always validate `seconds` and `volume` values before applying:
   ```javascript
   if (seconds < 0 || seconds > 3600) {
       console.error(`Invalid timer value: ${seconds}`);
       return;
   }
   ```

2. **Rate Limiting:** Consider rate-limiting MSG_SET_TIMER to prevent spam:
   ```javascript
   const MIN_INTERVAL_MS = 500;  // No more than 1 preset per 0.5s
   ```

3. **Authentication:** If ESP-NOW encryption is enabled (see Config.h), ensure bridge properly validates encrypted packets.

---

## Dependencies

### React App Requirements:
- Node.js (for serial communication with bridge)
- Serialport library (for UART/USB bridge connection)
- Platform-specific audio library:
  - Linux: `alsa-utils` or `pulseaudio-utils`
  - macOS: `osascript` or `CoreAudio`
  - Windows: `nircmd` or `SoundVolumeView`

### Bridge Requirements:
- Firmware v2.1.0+ (supports MSG_SET_TIMER and MSG_SET_VOLUME)
- UART connection to Raspberry Pi

---

## Contact

**Firmware Developer:** (your name)
**React App Developer:** (to be assigned)
**Issue Tracker:** Link to GitHub issues or project management tool

---

**Document Version:** 1.0
**Last Updated:** 2025-12-19
