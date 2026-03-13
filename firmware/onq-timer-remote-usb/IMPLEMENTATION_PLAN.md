# Implementation Plan - All 4 Features for v2.1.0

**Date:** 2025-12-19
**Target Firmware:** v2.1.0
**Features:** Settings Screen, Reset Hold, Preset Buttons, Undo Reset

---

## Architecture Overview

### UI State Machine (Expanded)

```
Current States:
- UI_STATE_STOPPED
- UI_STATE_RUNNING
- UI_STATE_PAUSED
- UI_STATE_EXPIRED
- UI_STATE_TIME_PICKER
- UI_STATE_RESET_CONFIRM (removed, will re-add)

NEW States to Add:
- UI_STATE_PRESETS      → Preset selection screen
- UI_STATE_SETTINGS     → Device settings screen
- UI_STATE_RESET_CONFIRM → Reset confirmation overlay
```

### Button Behavior by Screen

| Button | Main Screen | Presets | Settings | Time Picker | Reset Confirm |
|--------|-------------|---------|----------|-------------|---------------|
| Button 1 | Reset (hold 1s) | Exit | Decrease | N/A | Cancel |
| Button 2 | Open Presets | Exit | Toggle Mode | Exit | Exit |
| Button 3 | Start/Pause | Exit | Increase | N/A | Confirm |

### Gesture Detection

- **Triple-Tap** (main screen only, 500ms window) → Open Settings
- **Swipe Left-to-Right** (settings screen, >80px) → Exit to Main

---

## Feature 1: Settings Screen

### UI Components

1. **Title:** "Settings"
2. **Device Stats Section:**
   - Battery: "87% (4.1V)"
   - Firmware: "2.1.0"
   - Remote ID: "1"
   - Uptime: "02:34:17"
   - Bridge: "Connected" / "Disconnected"
   - Bridge MAC: "XX:XX:XX:XX:XX:XX"
   - ESP-NOW Ch: "1"
   - Free RAM: "145 KB"

3. **Brightness Control:**
   - Label: "Display Brightness"
   - Slider: 10-100%
   - Current value label
   - Adjustment: Button 1 (-10%), Button 3 (+10%)

4. **Volume Control:**
   - Label: "System Volume"
   - Slider: 0-100%
   - Current value label
   - Adjustment: Button 1 (-10%), Button 3 (+10%)

5. **Mode Indicator:**
   - "BTN1/3: BRIGHTNESS" or "BTN1/3: VOLUME"
   - Button 2 toggles mode

6. **Instructions:**
   - "Swipe right to exit"

### Implementation Details

**New Variables in Timer_UI:**
```cpp
// Settings screen state
bool m_settingsVisible;
SettingMode m_currentSettingMode;  // BRIGHTNESS or VOLUME
uint8_t m_systemVolume;  // 0-100

// Settings UI objects
lv_obj_t* m_settingsScreen;
lv_obj_t* m_settingsContainer;
lv_obj_t* m_settingsBatteryLabel;
lv_obj_t* m_settingsBrightnessSlider;
lv_obj_t* m_settingsVolumeSlider;
// ... etc
```

**New Functions:**
```cpp
void createSettingsScreen();
void updateSettingsScreen();
void showSettings();
void hideSettings();
void adjustSetting(int8_t direction);  // -1 or +1
void toggleSettingMode();
```

---

## Feature 2: Preset Buttons Screen

### UI Components

1. **Title:** "Presets"
2. **Last Reset Button** (if available):
   - Large orange button at top
   - Text: "↻ Last: 08:47"
   - Hidden if no last reset value
3. **Preset Grid** (3x2):
   - Button 1: "1:00" (60s)
   - Button 2: "2:00" (120s)
   - Button 3: "3:00" (180s)
   - Button 4: "5:00" (300s)
   - Button 5: "10:00" (600s)
   - Button 6: "15:00" (900s)
4. **Back Button:** "← Back" (returns to main)

### Implementation Details

**New Variables:**
```cpp
// Preset screen state
bool m_presetsVisible;
uint16_t m_lastResetValue;  // Seconds
bool m_hasLastResetValue;

// Preset UI objects
lv_obj_t* m_presetsScreen;
lv_obj_t* m_presetButtons[6];
lv_obj_t* m_lastResetButton;
lv_obj_t* m_backButton;
```

**Preset Constants (Config.h):**
```cpp
#define PRESET_TIME_1MIN   60
#define PRESET_TIME_2MIN   120
#define PRESET_TIME_3MIN   180
#define PRESET_TIME_5MIN   300
#define PRESET_TIME_10MIN  600
#define PRESET_TIME_15MIN  900
```

**New Functions:**
```cpp
void createPresetsScreen();
void showPresets();
void hidePresets();
void onPresetSelected(uint16_t seconds);
void onLastResetSelected();
static void presetButtonCallback(lv_event_t* e);
static void lastResetButtonCallback(lv_event_t* e);
```

**Button 2 Behavior Change:**
- OLD: Opens time picker
- NEW: Opens presets screen
- Time picker accessible from presets screen back button menu (or we remove it)

---

## Feature 3: Reset Hold (1 Second)

### UI Components

**Reset Confirmation Overlay:**
1. **Dark overlay** (semi-transparent)
2. **Centered panel:**
   - Title: "Reset Timer?"
   - Timer value: "08:47"
   - Countdown indicator: "Hold for 1s... (0.5s)"
   - Instructions: "BTN1: Cancel | BTN3: Confirm"

### Implementation Details

**New Variables:**
```cpp
// Reset confirmation state
bool m_resetConfirmVisible;
uint32_t m_resetConfirmStartTime;
uint16_t m_resetConfirmValue;  // Timer value being reset

// Reset confirmation UI objects
lv_obj_t* m_resetConfirmOverlay;
lv_obj_t* m_resetConfirmPanel;
lv_obj_t* m_resetConfirmLabel;
lv_obj_t* m_resetConfirmProgressBar;
```

**New Functions:**
```cpp
void showResetConfirm(uint16_t currentValue);
void hideResetConfirm();
void updateResetConfirmProgress();
```

**Logic:**
1. Button 1 short press → Show reset confirmation overlay
2. Button 1 held for 1s → Save last value, send reset command
3. Button 1 released before 1s → Cancel
4. Button 3 during confirm → Instant confirm
5. Button 2 during confirm → Cancel

---

## Feature 4: Undo Reset

### Logic

**When Reset Happens:**
1. Before sending reset command, check if timer > 0
2. If yes: `m_lastResetValue = m_currentSeconds`
3. Set `m_hasLastResetValue = true`
4. When presets screen opens, show "Last Reset" button

**When Last Reset Selected:**
1. Send `MSG_SET_TIMER` with `m_lastResetValue`
2. Return to main screen
3. Optionally clear saved value (or keep for re-use)

**Storage:**
- Stored in RAM only (lost on reboot)
- Future: Could save to NVS for persistence

---

## File Modifications Required

### 1. Config.h

**Add Constants:**
```cpp
// Reset confirmation
#define RESET_HOLD_TIME_MS 1000  // 1 second hold

// Preset timers
#define PRESET_TIME_1MIN   60
#define PRESET_TIME_2MIN   120
#define PRESET_TIME_3MIN   180
#define PRESET_TIME_5MIN   300
#define PRESET_TIME_10MIN  600
#define PRESET_TIME_15MIN  900

// Gesture detection
#define TRIPLE_TAP_WINDOW_MS 500  // 500ms for 3 taps
#define SWIPE_THRESHOLD      80   // 80 pixels minimum

// Settings
#define BACKLIGHT_MIN 10
#define BACKLIGHT_MAX 100
#define VOLUME_MIN 0
#define VOLUME_MAX 100
```

**Add Message Types:**
```cpp
#define MSG_SET_TIMER  0x08   // Remote → Bridge (set timer value)
#define MSG_SET_VOLUME 0x09   // Remote → Bridge (set system volume)
```

**Add Button IDs:**
```cpp
// Preset button IDs (20-29)
#define BTN_ID_PRESET_1MIN   20
#define BTN_ID_PRESET_2MIN   21
#define BTN_ID_PRESET_3MIN   22
#define BTN_ID_PRESET_5MIN   23
#define BTN_ID_PRESET_10MIN  24
#define BTN_ID_PRESET_15MIN  25
#define BTN_ID_PRESET_LAST   26  // Undo reset
```

### 2. Timer_UI.h

**Add Enums:**
```cpp
enum TimerUIState {
    // ... existing states ...
    UI_STATE_PRESETS,        // NEW
    UI_STATE_SETTINGS,       // NEW
    UI_STATE_RESET_CONFIRM   // RE-ADD
};

enum SettingMode {
    SETTING_MODE_BRIGHTNESS = 0,
    SETTING_MODE_VOLUME = 1
};
```

**Add Member Variables:**
```cpp
// Presets screen
bool m_presetsVisible;
uint16_t m_lastResetValue;
bool m_hasLastResetValue;
lv_obj_t* m_presetsScreen;
lv_obj_t* m_presetButtons[6];
lv_obj_t* m_lastResetButton;

// Settings screen
bool m_settingsVisible;
SettingMode m_currentSettingMode;
uint8_t m_systemVolume;
lv_obj_t* m_settingsScreen;
lv_obj_t* m_settingsContainer;
// ... settings UI objects ...

// Reset confirmation
bool m_resetConfirmVisible;
uint32_t m_resetConfirmStartTime;
uint16_t m_resetConfirmValue;
lv_obj_t* m_resetConfirmOverlay;
// ... reset confirm UI objects ...
```

**Add Member Functions:**
```cpp
// Presets
void createPresetsScreen();
void showPresets();
void hidePresets();
void onPresetSelected(uint16_t seconds);

// Settings
void createSettingsScreen();
void showSettings();
void hideSettings();
void updateSettingsScreen();
void adjustSetting(int8_t direction);
void toggleSettingMode();

// Reset confirmation
void showResetConfirm(uint16_t currentValue);
void hideResetConfirm();
void updateResetConfirmProgress();

// Gesture callbacks (called from touch driver)
void handleTripleTap();
void handleSwipe(int16_t dx, int16_t dy);
```

### 3. Timer_UI.cpp

**Major Additions:**
- `createPresetsScreen()` - Build preset UI
- `createSettingsScreen()` - Build settings UI
- `createResetConfirmOverlay()` - Build confirmation UI
- Update `tick()` to handle reset confirmation countdown
- Update `onPhysicalButton1()` for reset hold logic
- Update `onPhysicalButton2()` to open presets instead of time picker
- Implement all new functions

### 4. Touch_SPD2010.cpp

**Add Gesture Detection:**
```cpp
// Global state
static uint32_t s_lastTapTimes[3] = {0};
static uint8_t s_tapCount = 0;
static lv_point_t s_swipeStart = {0};
static bool s_swipeInProgress = false;
static bool s_wasTouched = false;

// In touch read callback:
// - Detect touch press/release transitions
// - On main screen + press: Track triple-tap timing
// - On settings screen + press: Start swipe tracking
// - On settings screen + release: Calculate swipe distance/direction
// - Call g_timerUI.handleTripleTap() or handleSwipe()
```

### 5. Button_Driver.cpp

**Add Context-Aware Handling:**
```cpp
// In handleShortPress():
UIScreen currentScreen = g_timerUI.getCurrentScreen();

if (currentScreen == UI_SCREEN_SETTINGS) {
    if (buttonId == BTN_ID_PHYSICAL_1) {
        g_timerUI.adjustSetting(-1);  // Decrease
    } else if (buttonId == BTN_ID_PHYSICAL_3) {
        g_timerUI.adjustSetting(+1);  // Increase
    } else if (buttonId == BTN_ID_PHYSICAL_2) {
        g_timerUI.toggleSettingMode();  // Switch mode
    }
} else if (currentScreen == UI_SCREEN_PRESETS) {
    // Any button exits presets
    g_timerUI.hidePresets();
} else {
    // Normal behavior - send to React app
    if (m_shortPressCallback != nullptr) {
        m_shortPressCallback(buttonId);
    }
}
```

### 6. ESPNOW_Driver.cpp

**Add New Message Senders:**
```cpp
void ESPNOWDriver::sendSetTimer(uint16_t seconds) {
    TimerPacket packet;
    packet.msgType = MSG_SET_TIMER;
    packet.deviceId = REMOTE_ID;
    packet.payload = (seconds << 8) | (seconds >> 8);  // Big-endian
    packet.flags = 0;
    packet.sequence = m_txSequence++;

    sendPacket(&packet);
}

void ESPNOWDriver::sendSetVolume(uint8_t volume) {
    TimerPacket packet;
    packet.msgType = MSG_SET_VOLUME;
    packet.deviceId = REMOTE_ID;
    packet.payload = volume;
    packet.flags = 0;
    packet.sequence = m_txSequence++;

    sendPacket(&packet);
}
```

---

## Implementation Order

1. ✅ **Config.h** - Add all constants and message types
2. ✅ **Timer_UI.h** - Add enums, variables, function declarations
3. ✅ **Presets Screen** - Implement in Timer_UI.cpp
4. ✅ **Settings Screen** - Implement in Timer_UI.cpp
5. ✅ **Reset Hold** - Implement in Timer_UI.cpp
6. ✅ **Undo Reset** - Integrate with presets and reset logic
7. ✅ **Gesture Detection** - Add to Touch_SPD2010.cpp
8. ✅ **Button Context** - Update Button_Driver.cpp
9. ✅ **ESP-NOW** - Add sendSetTimer() and sendSetVolume()
10. ✅ **React App Requirements** - Document what React needs to handle

---

## Testing Plan

### 1. Preset Screen
- [ ] Button 2 opens presets
- [ ] Each preset button sets correct time
- [ ] Back button returns to main
- [ ] Last Reset button appears after reset
- [ ] Last Reset button sets correct value

### 2. Settings Screen
- [ ] Triple-tap opens settings
- [ ] All device stats display correctly
- [ ] Button 1/3 adjust brightness
- [ ] Button 2 toggles to volume mode
- [ ] Button 1/3 adjust volume in volume mode
- [ ] Swipe right exits to main

### 3. Reset Hold
- [ ] Button 1 tap shows confirmation
- [ ] Holding for 1s triggers reset
- [ ] Releasing early cancels
- [ ] Progress indicator works
- [ ] Value saved to undo before reset

### 4. Undo Reset
- [ ] Last value captured before reset
- [ ] Last Reset button shows in presets
- [ ] Last Reset button displays correct time
- [ ] Selecting last reset restores value

---

## React App Requirements (To Document Later)

**New Messages to Handle:**
1. `MSG_SET_TIMER` - Set timer to specific value (from preset/undo)
2. `MSG_SET_VOLUME` - Adjust system volume

**Expected Behavior:**
- When preset selected, React app should set timer to that value
- When volume adjusted, React app should change system volume
- React app should broadcast updated timer state to all remotes

---

**Document Version:** 1.0
**Status:** READY TO IMPLEMENT
**Estimated Time:** 3-4 hours
