# Preset Timer Quick Reference Card

**For React Developers - Quick Implementation Guide**

---

## Message to Handle: MSG_SET_TIMER (0x08)

### Packet Format (6 bytes)
```
[0] msgType  = 0x08
[1] remoteId = 1-20
[2] seconds  = High byte (big-endian)
[3] seconds  = Low byte (big-endian)
[4] reserved = 0x00
[5] sequence = Packet counter
```

---

## Code to Add

### 1. Parse Packet

```javascript
function handleSetTimer(buffer) {
  const remoteId = buffer[1];
  const seconds = (buffer[2] << 8) | buffer[3];  // Big-endian conversion!

  if (seconds <= 0 || seconds > 3600) return;  // Validate
  if (timerState.isRunning) return;  // Only allow when stopped

  timerState.currentSeconds = seconds;
  broadcastTimerState();  // Send to ALL remotes
}
```

### 2. Broadcast Timer State

```javascript
function broadcastTimerState() {
  const packet = Buffer.alloc(6);
  packet[0] = 0x01;  // MSG_TIMER_STATE
  packet[1] = (timerState.currentSeconds >> 8) & 0xFF;
  packet[2] = timerState.currentSeconds & 0xFF;
  packet[3] = timerState.isRunning ? 0x01 : 0x00;
  packet[4] = 0x00;
  packet[5] = getNextSequence();

  sendToBridge(packet);
}
```

---

## Preset Values

| Button    | Seconds | Hex Bytes   | Display |
|-----------|---------|-------------|---------|
| 1 min     | 60      | `00 3C`     | 01:00   |
| 2 min     | 120     | `00 78`     | 02:00   |
| 3 min     | 180     | `00 B4`     | 03:00   |
| 5 min     | 300     | `01 2C`     | 05:00   |
| 10 min    | 600     | `02 58`     | 10:00   |
| 15 min    | 900     | `03 84`     | 15:00   |
| Last Reset| Variable| Variable    | Variable|

---

## Testing

### Test Command (Node.js example)
```javascript
// Simulate MSG_SET_TIMER for 5 minutes
const testPacket = Buffer.from([
  0x08,  // MSG_SET_TIMER
  0x01,  // Remote ID 1
  0x01,  // Seconds high byte (300 >> 8)
  0x2C,  // Seconds low byte (300 & 0xFF)
  0x00,  // Reserved
  0x42   // Sequence
]);

handleBridgeMessage(testPacket);
// Expected: Timer set to 05:00
```

---

## Common Mistakes

❌ **WRONG:**
```javascript
const seconds = buffer.readUInt16LE(2);  // Little-endian - WRONG!
```

✅ **CORRECT:**
```javascript
const seconds = (buffer[2] << 8) | buffer[3];  // Big-endian - CORRECT!
```

---

## Flow Summary

```
Remote taps preset → MSG_SET_TIMER → React App
                                        ↓
                               Convert big-endian
                                        ↓
                                Validate (0-3600)
                                        ↓
                          Check timer stopped?
                                        ↓
                               Set timer value
                                        ↓
                        Broadcast to ALL remotes
```

---

## Key Points

1. **Big-endian conversion is REQUIRED**
2. **Validate range**: 0 < seconds ≤ 3600
3. **Broadcast to ALL remotes** (not just sender)
4. **Recommended**: Only allow when timer stopped
5. **Undo reset uses same message** (just different seconds value)

---

**Full Documentation:** See `PRESET_TIMER_INTEGRATION_GUIDE.md`
