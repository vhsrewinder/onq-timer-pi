#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Preferences.h>  // For persistent brightness/volume storage (NVS)
#include "Config.h"

/*
 * Queue-Master Remote Control - Timer UI (Watch-Inspired Redesign v2.0)
 *
 * Complete UI redesign based on Galaxy Watch stopwatch app aesthetics.
 *
 * UI Features:
 * - Pure black background with dynamic color states (yellow @ 30s, red @ 5s)
 * - Large centered MM:SS timer display with bold white text
 * - Circular progress ring (lavender blue, decrements clockwise)
 * - Real-time clock at top (synced from bridge/Pi)
 * - Consolidated info section: Battery %, Bridge LED, React LED
 * - Dynamic button layout (changes based on timer state)
 * - Vertical swipe time picker (LVGL roller widgets)
 * - Reset confirmation (5-second timeout)
 * - Smooth pulsating animation when timer expires
 *
 * Physical Button Mapping:
 * - Button 1 (GPIO 12): Reset (with confirmation)
 * - Button 2 (GPIO 13): Enter time picker
 * - Button 3 (GPIO 1): Start/Pause toggle
 *
 * Display States:
 * - STOPPED (at preset): Play + Edit buttons (2 circular)
 * - RUNNING: Pause button (1 wide)
 * - PAUSED (mid-countdown): Resume + Stop buttons (2 circular)
 * - TIME_PICKER: Full-screen roller picker
 * - RESET_CONFIRM: Confirmation overlay
 */

// ============================================================================
// CALLBACK TYPE
// ============================================================================

typedef void (*TouchButtonCallback)(uint8_t buttonId);

// ============================================================================
// UI STATE ENUM
// ============================================================================

enum TimerUIState {
    UI_STATE_STOPPED,           // Timer at 0 or preset, not running
    UI_STATE_RUNNING,           // Timer counting down
    UI_STATE_PAUSED,            // Timer paused mid-countdown
    UI_STATE_EXPIRED,           // Timer reached 0 and stopped
    UI_STATE_TIME_PICKER,       // Time picker overlay active
    UI_STATE_PRESETS,           // Preset selection screen active
    UI_STATE_SETTINGS,          // Settings screen active
    UI_STATE_BLE_PAIRING        // BLE pairing screen active
    // UI_STATE_RESET_CONFIRM   // DISABLED: Reset now works immediately without confirmation
};

// ============================================================================
// BACKGROUND COLOR STATE
// ============================================================================

enum BackgroundState {
    BG_STATE_NORMAL,            // Black (>30s)
    BG_STATE_WARNING,           // Yellow (30s-6s)
    BG_STATE_CRITICAL,          // Red (5s-1s)
    BG_STATE_EXPIRED_PULSATE    // Pulsating red/black (0s)
};

// ============================================================================
// SETTING MODE ENUM
// ============================================================================

enum SettingMode {
    SETTING_MODE_BRIGHTNESS = 0,    // Adjusting display brightness
    SETTING_MODE_VOLUME = 1,        // Adjusting system volume
    SETTING_MODE_AUTO_ROTATE = 2    // Toggling auto-rotation ON/OFF
};

// ============================================================================
// TIMER UI CLASS
// ============================================================================

class TimerUI {
public:
    // Initialization
    void init();

    // Update functions
    void update(uint16_t seconds, uint8_t flags);
    void setBattery(uint8_t percentage, bool lowBattery);
    void setConnectionStatus(bool bridgeConnected, bool reactConnected);
    void setCurrentTime(uint8_t hours, uint8_t minutes);  // Real-time clock from bridge

    // Callback setter
    void setTouchCallback(TouchButtonCallback callback);

    // Periodic update (for animations and pulsating)
    void tick();

    // Physical button handlers (called from Button_Driver)
    void onPhysicalButton1();  // Reset (short press) - Long press handled in Button_Driver (Deep Sleep)
    void onPhysicalButton2();  // Enter/exit presets screen (short press) - Long press handled in Button_Driver (Settings)
    void onPhysicalButton3();  // Start/Pause toggle (short press) - Long press handled in Button_Driver (Offline Mode)

    // Gesture handlers (called from Touch driver)
    void handleTripleTap();           // Triple-tap detected (DISABLED - settings now via Button 2 long press)
    void handleSwipe(int16_t dx, int16_t dy);  // Swipe gesture detected (left-to-right closes settings)

    // Settings screen functions
    void adjustSetting(int8_t direction);  // Adjust current setting (-1 or +1)
    void toggleSettingMode();              // Toggle between brightness and volume
    void showSettings();                   // Show settings screen (called from Button 2 long press)
    void hideSettings();                   // Hide settings screen

    // Screen state query
    TimerUIState getCurrentUIState() const { return m_uiState; }

    // Offline mode control
    void toggleOfflineMode();               // Toggle between Bridge and Offline modes
    bool isOfflineMode() const { return m_offlineMode; }

private:
    // ========================================================================
    // UI CREATION
    // ========================================================================
    void createUI();
    void createProgressRing();
    void createRealTimeClock();
    void createInfoSection();
    void createTimerDisplay();
    void createStatusLabel();  // Status text (PAUSED/RUNNING/STOPPED)
    void createSigmaIcon();
    void createTimePicker();
    void createPresetsScreen();      // Preset selection screen
    void createSettingsScreen();     // Device settings screen
    void createBLEPairingScreen();   // BLE pairing screen
    void createResetConfirmOverlay();  // Reset confirmation overlay

    // ========================================================================
    // BLE PAIRING FUNCTIONS
    // ========================================================================
    void showBLEPairing();           // Show BLE pairing screen
    void hideBLEPairing();           // Hide BLE pairing screen
    void updateBLEPairingUI();       // Update BLE connection status
    void populateGatewayList();      // Populate discovered gateways list
    String getRSSIBar(int rssi);     // Convert RSSI to signal bars

    // BLE UI callbacks
    void onBLEScanClick();
    void onBLEConnectClick();
    void onBLEForgetClick();
    void onBLEBackClick();
    void onGatewayItemClick(const String& gatewayAddress);

    // ========================================================================
    // UI UPDATE HELPERS
    // ========================================================================
    void updateTimerDisplay(uint16_t seconds);
    void updateTotalTimeLabel(uint16_t totalSeconds);
    void updateProgressRing(uint16_t seconds, uint16_t maxSeconds);
    void updateStatusLabel(TimerUIState state);
    void updateBackgroundColor(BackgroundState bgState);
    void updateTextColor(BackgroundState bgState);
    void updateRealTimeClock(uint8_t hours, uint8_t minutes);
    void updateInfoSection();

    // ========================================================================
    // STATE HELPERS
    // ========================================================================
    TimerUIState determineUIState(uint16_t seconds, uint8_t flags);
    BackgroundState determineBackgroundState(uint16_t seconds, uint8_t flags);
    uint32_t getTextColor(BackgroundState bgState);
    uint32_t getBackgroundColor(BackgroundState bgState);

    // ========================================================================
    // ANIMATION
    // ========================================================================
    void updatePulsatingAnimation();
    uint8_t calculatePulsateOpacity();
    void updateProgressRingFlash();

    // ========================================================================
    // MOTION DETECTION & DISPLAY AUTO-OFF (REMOVED - gyroscope removed in v2.7.0)
    // ========================================================================
    // void checkMotion();                // Check for device motion
    // void turnDisplayOff();             // Turn off display backlight (battery saving)
    // void turnDisplayOn();              // Turn on display backlight

    // ========================================================================
    // AUTO-ROTATION (REMOVED - gyroscope removed in v2.7.0)
    // ========================================================================
    // void checkOrientation();           // Check device orientation for auto-rotation
    // void rotateDisplay(bool upsideDown); // Rotate display 180 degrees

    // ========================================================================
    // TIME PICKER
    // ========================================================================
    void showTimePicker();
    void hideTimePicker();
    void applyTimePickerValue();

    // ========================================================================
    // PRESETS SCREEN
    // ========================================================================
    void showPresets();
    void hidePresets();
    void onPresetSelected(uint16_t seconds);

    // ========================================================================
    // SETTINGS SCREEN
    // ========================================================================
    void updateSettingsScreen();

    // ========================================================================
    // RESET CONFIRMATION (DISABLED - Reset now works immediately)
    // ========================================================================
    // void showResetConfirm(uint16_t currentValue);
    // void hideResetConfirm();
    // void updateResetConfirmProgress();

    // ========================================================================
    // LVGL STATIC EVENT HANDLERS
    // ========================================================================
    static void onPickerOkButtonClicked(lv_event_t* e);
    static void onPresetButtonClicked(lv_event_t* e);
    static void onLastResetButtonClicked(lv_event_t* e);

    // ========================================================================
    // LVGL OBJECTS - MAIN UI
    // ========================================================================
    lv_obj_t* m_screen;                 // Main screen (for background color changes)
    lv_obj_t* m_progressRing;           // Circular progress ring
    lv_obj_t* m_clockLabel;             // Real-time clock (HH:MM)
    lv_obj_t* m_infoContainer;          // Info section container
    lv_obj_t* m_batteryLabel;           // Battery percentage text
    lv_obj_t* m_batteryLED;             // Battery status LED
    lv_obj_t* m_bridgeLabel;            // Bridge label text ("BR")
    lv_obj_t* m_bridgeLED;              // Bridge connection LED
    lv_obj_t* m_reactLabel;             // React label text ("APP")
    lv_obj_t* m_reactLED;               // React connection LED
    lv_obj_t* m_radioLabel;             // Radio sleep label text ("RDO")
    lv_obj_t* m_radioLED;               // Radio sleep status LED (orange = sleeping)
    lv_obj_t* m_remoteIdLabel;          // Remote ID label (e.g., "ID 1")
    lv_obj_t* m_offlineModeLabel;       // "Offline Mode" label (replaces info section)
    lv_obj_t* m_timerLabel;             // MM:SS timer display
    lv_obj_t* m_totalTimeLabel;         // Total/starting time label (above timer)
    lv_obj_t* m_statusLabel;            // Status text (PAUSED/RUNNING/STOPPED)
    lv_obj_t* m_sigmaIcon;              // Sigma icon (top center)

    // Touch buttons removed - physical buttons only

    // ========================================================================
    // LVGL OBJECTS - TIME PICKER
    // ========================================================================
    lv_obj_t* m_pickerContainer;        // Full-screen picker overlay
    lv_obj_t* m_pickerLabel;            // "Minutes" or "Seconds" label
    lv_obj_t* m_pickerRollerMin;        // Minute roller (00-59)
    lv_obj_t* m_pickerRollerSec;        // Second roller (00-59)
    lv_obj_t* m_pickerOkButton;         // Checkmark confirm button

    // ========================================================================
    // LVGL OBJECTS - PRESETS SCREEN
    // ========================================================================
    lv_obj_t* m_presetsScreen;          // Presets screen container
    lv_obj_t* m_presetsTitle;           // "Presets" title label
    lv_obj_t* m_presetButtons[6];       // 6 preset time buttons
    lv_obj_t* m_lastResetButton;        // "Last Reset" undo button
    lv_obj_t* m_presetsBackButton;      // Back to main button

    // ========================================================================
    // LVGL OBJECTS - SETTINGS SCREEN
    // ========================================================================
    lv_obj_t* m_settingsScreen;         // Settings screen container
    lv_obj_t* m_settingsTitle;          // "Settings" title label
    lv_obj_t* m_settingsContainer;      // Scrollable container for settings
    lv_obj_t* m_settingsBatteryLabel;   // Battery stat label
    lv_obj_t* m_settingsFirmwareLabel;  // Firmware version label
    lv_obj_t* m_settingsModeLabel;      // Mode label (Bridge/Offline)
    lv_obj_t* m_settingsRemoteIDLabel;  // Remote ID label
    lv_obj_t* m_settingsUptimeLabel;    // Uptime label
    lv_obj_t* m_settingsConnectionLabel;// Connection status label
    lv_obj_t* m_settingsBridgeMACLabel; // Bridge MAC address label
    lv_obj_t* m_settingsChannelLabel;   // ESP-NOW channel label
    lv_obj_t* m_settingsRAMLabel;       // Free RAM label
    lv_obj_t* m_settingsBrightnessSlider; // Brightness slider
    lv_obj_t* m_settingsBrightnessValue;  // Brightness percentage label
    lv_obj_t* m_settingsVolumeSlider;   // Volume slider
    lv_obj_t* m_settingsVolumeValue;    // Volume percentage label
    lv_obj_t* m_settingsAutoRotateValue; // Auto-rotation ON/OFF label
    lv_obj_t* m_settingsModeIndicator;  // "BTN1/3: BRIGHTNESS/VOLUME/AUTO-ROTATE" label
    lv_obj_t* m_settingsInstructions;   // "Swipe right to exit" label
    lv_obj_t* m_settingsBLEButton;      // Bluetooth button

    // ========================================================================
    // LVGL OBJECTS - BLE PAIRING SCREEN
    // ========================================================================
    lv_obj_t* m_blePairingScreen;        // BLE pairing screen container
    lv_obj_t* m_blePairingTitle;         // "BLUETOOTH" title
    lv_obj_t* m_bleStatusLabel;          // Status text (Connected/Disconnected/Scanning)
    lv_obj_t* m_bleGatewayInfoLabel;     // Connected gateway name + RSSI
    lv_obj_t* m_bleScanButton;           // "Scan for Gateways" button
    lv_obj_t* m_bleGatewayListContainer; // Scrollable gateway list
    lv_obj_t* m_bleConnectButton;        // "Connect" button
    lv_obj_t* m_bleForgetButton;         // "Forget Gateway" button
    lv_obj_t* m_bleBackButton;           // "Back" button
    String m_selectedGatewayAddress;     // Currently selected gateway MAC

    // ========================================================================
    // LVGL OBJECTS - RESET CONFIRMATION
    // ========================================================================
    lv_obj_t* m_resetConfirmOverlay;    // Semi-transparent overlay
    lv_obj_t* m_resetConfirmPanel;      // Centered confirmation panel
    lv_obj_t* m_resetConfirmTitle;      // "Reset Timer?" label
    lv_obj_t* m_resetConfirmValueLabel; // Timer value being reset (label)
    lv_obj_t* m_resetConfirmProgress;   // Progress bar for hold timing
    lv_obj_t* m_resetConfirmInstructions; // Button instructions

    // ========================================================================
    // STATE VARIABLES
    // ========================================================================
    TimerUIState m_uiState;             // Current UI state
    BackgroundState m_bgState;          // Current background color state
    uint16_t m_currentSeconds;          // Current timer value
    uint16_t m_maxSeconds;              // Max value for progress ring
    uint16_t m_presetSeconds;           // Preset time (for reset)
    bool m_isRunning;                   // Timer running flag
    bool m_bridgeConnected;             // Bridge ESP-NOW connection
    bool m_reactConnected;              // React app connection
    uint8_t m_batteryPercent;           // Battery percentage
    bool m_lowBattery;                  // Low battery flag

    // Real-time clock
    uint8_t m_currentHours;             // 0-23
    uint8_t m_currentMinutes;           // 0-59

    // Presets screen state
    uint16_t m_lastResetValue;          // Timer value before last reset (for undo)
    bool m_hasLastResetValue;           // True if we have a stored last reset value

    // Offline mode state (standalone stopwatch mode)
    bool m_offlineMode;                 // True = Offline Mode, False = Bridge Mode
    uint16_t m_offlineSeconds;          // Current offline timer value
    uint16_t m_offlinePreset;           // Preset for offline reset
    bool m_offlineRunning;              // Offline timer running flag
    uint32_t m_offlineLastTick;         // Last 1-second tick timestamp

    // Settings screen state
    SettingMode m_currentSettingMode;   // Current setting being adjusted
    uint8_t m_systemVolume;             // System volume (0-100)
    // bool m_autoRotationEnabled;      // REMOVED: Auto-rotation feature (gyroscope removed in v2.7.0)

    // Reset confirmation state
    bool m_resetConfirmVisible;         // Is reset confirmation shown?
    uint32_t m_resetConfirmStartTime;   // When reset confirmation started
    uint16_t m_resetConfirmValue;       // Timer value being reset

    // Animation state
    uint32_t m_lastTickTime;            // Last tick() call
    uint32_t m_pulsateStartTime;        // When pulsating started
    uint32_t m_ringFlashStartTime;      // When progress ring flashing started (paused state)

    // REMOVED: Motion detection and display auto-off state (gyroscope removed in v2.7.0)
    // uint32_t m_lastMotionTime;          // Last time significant motion was detected
    // uint32_t m_lastMotionCheckTime;     // Last time we checked for motion
    // float m_lastAccelX;                 // Previous accelerometer X value
    // float m_lastAccelY;                 // Previous accelerometer Y value
    // float m_lastAccelZ;                 // Previous accelerometer Z value
    // bool m_displayOn;                   // Display backlight state (true = on, false = off)

    // REMOVED: Auto-rotation state (gyroscope removed in v2.7.0)
    // bool m_isUpsideDown;                // Current orientation (true = upside down, false = right-side up)
    // uint32_t m_upsideDownStartTime;     // When device was first detected upside down
    // bool m_displayRotated;              // Current display rotation (true = 180°, false = 0°)

    // Time picker state
    bool m_pickerVisible;               // Is picker shown?

    // Callback
    TouchButtonCallback m_touchCallback;

    // Persistent storage (NVS) for brightness/volume
    Preferences m_preferences;

    // Thread safety (FreeRTOS mutex for multi-core protection)
    SemaphoreHandle_t m_mutex;

    // Singleton instance (for static callbacks)
    static TimerUI* s_instance;
};

// Global instance
extern TimerUI g_timerUI;
