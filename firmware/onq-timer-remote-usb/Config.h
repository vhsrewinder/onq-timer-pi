#pragma once

/*
 * Queue-Master Remote Control - Central Configuration
 *
 * This file contains all configurable constants for the remote control firmware.
 * Modify these values to customize behavior without changing driver code.
 */

// ============================================================================
// POWER MODE SELECTION
// ============================================================================

// Uncomment ONE of these modes:
#define POWER_MODE_BATTERY_SAVER    // ✅ 6+ hours battery life (default)
// #define POWER_MODE_PERFORMANCE   // ⚡ Maximum range/speed, ~1.5 hours battery

// Mode-specific settings (don't edit these directly)
#ifdef POWER_MODE_PERFORMANCE
  #define WIFI_POWER_MODE WIFI_PS_NONE
  #define WIFI_TX_POWER WIFI_POWER_19_5dBm
  #define DEFAULT_BRIGHTNESS 100
  #define POWER_MODE_NAME "PERFORMANCE"
#else  // BATTERY_SAVER (default)
  #define WIFI_POWER_MODE WIFI_PS_MIN_MODEM
  #define WIFI_TX_POWER WIFI_POWER_15dBm  // 15dBm provides ~50-60m range (increased from 11dBm for 50m requirement)
  #define DEFAULT_BRIGHTNESS 40
  #define POWER_MODE_NAME "BATTERY_SAVER"
#endif

// ============================================================================
// DEVICE IDENTITY
// ============================================================================

#define REMOTE_ID 1               // Unique remote ID (1-20), change per device
#define FIRMWARE_VERSION "2.11.1"  // 2.11.1 = Touch optimization: 50Hz polling (20ms) for better responsiveness
#define ESPNOW_CHANNEL 8          // ESP-NOW WiFi channel (must match bridge) - FIXED: Changed from 9 to 8 to match bridge

// ============================================================================
// BRIDGE MAC ADDRESS (Required for encryption)
// ============================================================================

// IMPORTANT: Set this to your bridge's MAC address!
// Your bridge MAC: DC:54:75:F1:05:AC
//
// Format: Six hex bytes
#define BRIDGE_MAC_ADDR \
  { 0xDC, 0x54, 0x75, 0xF1, 0x05, 0xAC }

// ============================================================================
// ESP-NOW ENCRYPTION
// ============================================================================

// Enable AES-128-CCM encryption for ESP-NOW packets
// WARNING: Changing this requires matching change in bridge firmware!
#define ESPNOW_ENABLE_ENCRYPTION 0  // 1 = encrypted, 0 = unencrypted (DISABLED FOR TESTING)

// Primary Master Key (PMK) - 16 bytes
// CRITICAL: Must match bridge firmware exactly!
// SECURITY: Keep this key private, do not share or commit to public repos
#define ESPNOW_PMK \
  { \
    0xA7, 0x3E, 0xF2, 0x8B, 0x4C, 0xD9, 0x61, 0x05, \
      0xE4, 0x7A, 0x92, 0xC3, 0x58, 0x1F, 0xB6, 0x2D \
  }

// Bridge MAC Address Validation (MitM Protection)
// Set to 1 to enable MAC validation, 0 to accept any bridge
#define BRIDGE_MAC_VALIDATION_ENABLED 0  // Set to 1 for production

// Expected Bridge MAC Address (configure this with your bridge's actual MAC)
// Get this from bridge serial output during startup
#define BRIDGE_MAC_ADDRESS \
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

// ============================================================================
// BLE (BLUETOOTH LOW ENERGY) CONFIGURATION
// ============================================================================

// Communication modes
#define COMM_MODE_ESPNOW_ONLY  0  // ESP-NOW only (legacy)
#define COMM_MODE_BLE_ONLY     1  // BLE only
#define COMM_MODE_AUTO         2  // BLE priority with ESP-NOW fallback (recommended)
#define COMM_MODE_USB_SERIAL   3  // USB CDC serial to Relay Pi (wired, no radio)

// Select communication mode
#define COMMUNICATION_MODE COMM_MODE_USB_SERIAL

// BLE Service UUID for OnQ Timer Remote Service
#define BLE_SERVICE_UUID        "00001A00-0000-1000-8000-00805f9b34fb"

// BLE Characteristic UUIDs
#define BLE_BUTTON_CHAR_UUID    "00001A01-0000-1000-8000-00805f9b34fb"
#define BLE_PRESET_CHAR_UUID    "00001A02-0000-1000-8000-00805f9b34fb"
#define BLE_TIMER_STATE_UUID    "00001A03-0000-1000-8000-00805f9b34fb"
#define BLE_DEVICE_INFO_UUID    "00001A04-0000-1000-8000-00805f9b34fb"

// BLE Device Name (will be suffixed with Remote ID, e.g., "OnQTimer_01")
#define BLE_DEVICE_NAME_PREFIX "OnQTimer"

// BLE Parameters
#define BLE_SCAN_DURATION_MS 5000        // Scan duration (milliseconds)
#define BLE_AUTO_RECONNECT_ENABLED 1     // Auto-reconnect to last gateway
#define BLE_RECONNECT_INITIAL_DELAY 1000 // Initial reconnect delay (ms)
#define BLE_RECONNECT_MAX_DELAY 60000    // Max reconnect delay (ms)

// ============================================================================
// BUTTON GPIO PINS
// ============================================================================

// Physical buttons (active low, connect to GND when pressed)
#define BUTTON_1_PIN 13  // Button 1: Reset (with confirmation) - GPIO 13
#define BUTTON_2_PIN 12  // Button 2: Presets - GPIO 12 (SWAPPED)
#define BUTTON_3_PIN 1   // Button 3: Start/Pause Toggle - GPIO 1 (SWAPPED)

// ============================================================================
// BUTTON IDs (Sent via ESP-NOW)
// ============================================================================

// Physical button IDs (NEW MAPPING for watch-inspired UI)
// Button 1 = Reset (short press) / Deep Sleep (long press 2s)
// Button 2 = Presets Screen (short press) / Settings Menu (long press 2s)
// Button 3 = Start/Pause toggle (short press) / Toggle Offline Mode (long press 2s)
#define BTN_ID_PHYSICAL_1 1  // Reset (short press) / Deep Sleep (long press 2s)
#define BTN_ID_PHYSICAL_2 2  // Presets Screen (short press) / Settings Menu (long press 2s)
#define BTN_ID_PHYSICAL_3 3  // Start/Pause toggle (short press) / Toggle Offline Mode (long press 2s)

// Touch button IDs (watch-inspired dynamic layout)
#define BTN_ID_TOUCH_PLAY 10        // Play button (left in stopped state)
#define BTN_ID_TOUCH_EDIT 11        // Edit button (right in stopped state)
#define BTN_ID_TOUCH_PAUSE 12       // Pause button (wide button in running state)
#define BTN_ID_TOUCH_RESUME 13      // Resume button (left in paused state)
#define BTN_ID_TOUCH_STOP 14        // Stop button (right in paused state)
#define BTN_ID_TOUCH_PICKER_OK 15   // Time picker confirm button
#define BTN_ID_TOUCH_TEST_AUDIO 20  // Test audio playback (for debugging)

// Preset timer button IDs (20-29 range)
#define BTN_ID_PRESET_1MIN 21       // 1 minute preset
#define BTN_ID_PRESET_2MIN 22       // 2 minutes preset
#define BTN_ID_PRESET_3MIN 23       // 3 minutes preset
#define BTN_ID_PRESET_5MIN 24       // 5 minutes preset
#define BTN_ID_PRESET_10MIN 25      // 10 minutes preset
#define BTN_ID_PRESET_15MIN 26      // 15 minutes preset
#define BTN_ID_PRESET_LAST 27       // Last reset value (undo)

// ============================================================================
// BUTTON TIMING
// ============================================================================

#define BUTTON_DEBOUNCE_MS 50      // Debounce time in milliseconds
#define BUTTON_LONG_PRESS_MS 2000  // Long press threshold (2 seconds)

// ============================================================================
// ESP-NOW TIMING
// ============================================================================

#define HEARTBEAT_INTERVAL_MS 5000   // Send heartbeat every 5 seconds
#define DISCOVER_INTERVAL_MS 2000    // Send discover packet every 2s when not connected
#define CONNECTION_TIMEOUT_MS 7000   // Consider disconnected after 7s no packets (must be > HEARTBEAT_INTERVAL_MS)
#define CONNECTION_CHECK_INTERVAL_MS 500  // Check connection status every 500ms for real-time feedback

// ============================================================================
// CONNECTION QUALITY THRESHOLDS
// ============================================================================

// RSSI (signal strength) thresholds in dBm
#define RSSI_STRONG_THRESHOLD -70   // Above this = strong connection (green)
#define RSSI_WEAK_THRESHOLD   -85   // Below strong but above this = weak (orange)
// Below RSSI_WEAK_THRESHOLD or timeout = disconnected (red)

// Connection states for visual indicator
enum ConnectionState {
  CONN_DISCONNECTED = 0,  // Red - No connection
  CONN_WEAK = 1,          // Orange - Weak signal
  CONN_STRONG = 2         // Green - Strong signal
};

// ============================================================================
// POWER MANAGEMENT TIMING
// ============================================================================

// Time-based brightness and display management (v2.7.0+)
// System optimizes for battery life using idle timers instead of motion sensors
#define IDLE_DIM_TIMEOUT_MS 20000      // Auto-dim to 10% after 20 seconds of no button press
#define IDLE_SLEEP_TIMEOUT_MS 140000   // Turn screen off after 140 seconds of no button press
#define BRIGHTNESS_ACTIVE DEFAULT_BRIGHTNESS  // Active brightness (40% battery saver, 100% performance)
#define BRIGHTNESS_DIMMED 10           // Dimmed brightness (10%)
#define BRIGHTNESS_OFF 0               // Screen off (0%)

// Smart radio sleep/wake management (v2.7.0+)
// Radio stays awake after button press, then sleeps with periodic wake cycles
#define RADIO_ACTIVE_TIMEOUT_MS 5000         // Keep radio awake for 5s after button press
#define RADIO_WAKE_INTERVAL_MS 9000          // Wake every 9s when connected (for sync)
#define RADIO_WAKE_INTERVAL_DISCONNECTED_MS 15000  // Wake every 15s when disconnected (reconnection attempts)
#define RADIO_WAKE_DURATION_MS 150           // Listen window during wake cycle (100-200ms)

// ============================================================================
// BATTERY MONITORING
// ============================================================================

#define LOW_BATTERY_VOLTAGE 3.3  // Low battery threshold in volts
#define LOW_BATTERY_PERCENT 10   // Low battery threshold in percent
#define BATTERY_UPDATE_MS 10000  // Update battery reading every 10 seconds

// LiPo battery voltage curve (for percentage calculation)
#define BATTERY_VOLTAGE_MAX 4.2  // Fully charged
#define BATTERY_VOLTAGE_NOM 3.7  // Nominal voltage (50%)
#define BATTERY_VOLTAGE_MIN 3.0  // Empty (don't discharge below this)

// ============================================================================
// BACKLIGHT LEVELS
// ============================================================================

// POWER OPTIMIZATION: Brightness levels (configurable via power mode)
// User can adjust brightness in settings menu at runtime
#define BACKLIGHT_ACTIVE DEFAULT_BRIGHTNESS  // Active brightness (40% or 100% based on power mode)
#define BACKLIGHT_DIMMED 10                  // Dimmed brightness (0-100%)
#define BACKLIGHT_OFF 0                      // Off (deep sleep)

// ============================================================================
// ESP-NOW MESSAGE TYPES
// ============================================================================

#define MSG_TIMER_STATE 0x01  // Bridge → Remote (timer updates)
#define MSG_BUTTON 0x02       // Remote → Bridge (button press)
#define MSG_HEARTBEAT 0x03    // Remote → Bridge (keepalive)
#define MSG_ACK 0x04          // Bridge → Remote (acknowledgment)
#define MSG_TIME_SYNC 0x05    // Bridge → Remote (current time HH:MM)
#define MSG_DISCOVER 0x06     // Remote → Bridge (find bridge)
#define MSG_ANNOUNCE 0x07     // Bridge → Remote (bridge response)
#define MSG_SET_TIMER 0x08    // Remote → Bridge (set timer to specific value)
#define MSG_SET_VOLUME 0x09   // Remote → Bridge (set system volume 0-100)

// ============================================================================
// ESP-NOW STATE FLAGS
// ============================================================================

#define FLAG_RUNNING 0x01      // Timer is running
#define FLAG_EXPIRED 0x02      // Timer reached zero
#define FLAG_CONNECTED 0x04    // Bridge connected to Pi
#define FLAG_LOW_BATTERY 0x08  // Remote battery low

// ============================================================================
// DISPLAY CONFIGURATION
// ============================================================================

// Display is 412x412 circular, keep interactive elements within safe area
#define DISPLAY_WIDTH 412
#define DISPLAY_HEIGHT 412
#define DISPLAY_CENTER_X (DISPLAY_WIDTH / 2)
#define DISPLAY_CENTER_Y (DISPLAY_HEIGHT / 2)
#define SAFE_RADIUS 180  // Safe area for interactive elements

// Timer display position
#define TIMER_POS_X DISPLAY_CENTER_X
#define TIMER_POS_Y (DISPLAY_CENTER_Y - 20)

// Status text position
#define STATUS_POS_X DISPLAY_CENTER_X
#define STATUS_POS_Y (DISPLAY_CENTER_Y + 30)

// Connection indicator position
#define CONN_IND_POS_X DISPLAY_CENTER_X
#define CONN_IND_POS_Y 30

// Battery display position
#define BATTERY_POS_X (DISPLAY_WIDTH - 50)
#define BATTERY_POS_Y 30

// Touch button positions (circular arrangement)
#define BTN_RADIUS 50     // Button radius
#define BTN_DISTANCE 140  // Distance from center

// Start button (top)
#define BTN_START_X DISPLAY_CENTER_X
#define BTN_START_Y (DISPLAY_CENTER_Y - BTN_DISTANCE)

// Pause button (right)
#define BTN_PAUSE_X (DISPLAY_CENTER_X + BTN_DISTANCE)
#define BTN_PAUSE_Y DISPLAY_CENTER_Y

// Reset button (bottom)
#define BTN_RESET_X DISPLAY_CENTER_X
#define BTN_RESET_Y (DISPLAY_CENTER_Y + BTN_DISTANCE)

// Add time button (left)
#define BTN_ADD_X (DISPLAY_CENTER_X - BTN_DISTANCE)
#define BTN_ADD_Y DISPLAY_CENTER_Y

// ============================================================================
// UI COLORS (Watch-Inspired Design)
// ============================================================================

// Background colors (state-dependent)
#define COLOR_BG_NORMAL 0x000000    // Pure black (normal state, >30s)
#define COLOR_BG_WARNING 0xFFFF00   // Yellow (warning state, 30s-6s)
#define COLOR_BG_CRITICAL 0xFF0000  // Red (critical state, 5s-1s)

// Text colors (contrast with background)
#define COLOR_TEXT_ON_BLACK 0xFFFFFF   // White text on black
#define COLOR_TEXT_ON_YELLOW 0x000000  // Black text on yellow
#define COLOR_TEXT_ON_RED 0x000000     // Black text on red
#define COLOR_TEXT_DIM 0x808080        // Dimmed gray for secondary/instructional text

// Progress ring colors (watch-inspired lavender)
#define COLOR_PROGRESS_REMAIN 0xC8C8FF   // Light lavender (remaining time)
#define COLOR_PROGRESS_ELAPSED 0x404040  // Dark gray (elapsed time)

// Button colors (watch-inspired)
#define COLOR_BTN_PRIMARY 0xC8C8FF    // Lavender (play, resume)
#define COLOR_BTN_SECONDARY 0x606060  // Dark gray (edit, pause, stop)
#define COLOR_BTN_TEXT 0x000000       // Black text on buttons (for contrast)

// Status LEDs
#define COLOR_LED_GOOD 0x00FF00     // Green (connected, strong signal)
#define COLOR_LED_WARNING 0xFFA500  // Orange (connected, weak signal)
#define COLOR_LED_BAD 0xFF0000      // Red (disconnected)

// Time picker colors
#define COLOR_PICKER_BG 0x000000        // Black background
#define COLOR_PICKER_TEXT 0xFFFFFF      // White text
#define COLOR_PICKER_SELECTED 0xC8C8FF  // Lavender for selected value
#define COLOR_PICKER_DIM 0x606060       // Dimmed gray for other values

// ============================================================================
// UI LAYOUT POSITIONS (Watch-Inspired)
// ============================================================================

// Real-time clock (top center)
#define CLOCK_POS_Y 15

// Info section (below clock)
#define INFO_POS_Y 60    // Lowered to provide more spacing below larger clock font
#define INFO_LED_SIZE 8  // LED indicator size

// Timer display (large center)
#define TIMER_POS_Y (DISPLAY_CENTER_Y - 20)

// Sigma icon position (top center, like watch)
#define SIGMA_POS_Y 60

// Button layout (dynamic based on state) - WATCH-INSPIRED LAYOUT
#define BTN_Y_CENTER (DISPLAY_CENTER_Y + 130)  // Buttons much lower (near bottom edge, like watch)
#define BTN_SPACING 80                         // Space between 2 buttons
#define BTN_CIRCLE_RADIUS 50                   // Circular button radius
#define BTN_WIDE_WIDTH 180                     // Wide button width
#define BTN_WIDE_HEIGHT 60                     // Wide button height

// Reset confirmation
#define RESET_CONFIRM_TIMEOUT 5000  // 5 seconds to confirm reset
#define RESET_HOLD_TIME_MS 1000     // Reset requires 1 second hold

// ============================================================================
// PRESET TIMER VALUES
// ============================================================================

#define PRESET_TIME_1MIN 60       // 1 minute
#define PRESET_TIME_2MIN 120      // 2 minutes
#define PRESET_TIME_3MIN 180      // 3 minutes
#define PRESET_TIME_5MIN 300      // 5 minutes
#define PRESET_TIME_10MIN 600     // 10 minutes
#define PRESET_TIME_15MIN 900     // 15 minutes

// ============================================================================
// GESTURE DETECTION
// ============================================================================

#define TRIPLE_TAP_WINDOW_MS 500  // Triple-tap detection window (500ms)
#define SWIPE_THRESHOLD 80        // Minimum swipe distance in pixels

// Tap validation thresholds (prevent accidental menu opening)
#define TAP_MAX_MOVEMENT 20       // Maximum movement in pixels to count as tap (not swipe)
#define TAP_MAX_DURATION 300      // Maximum duration in ms to count as tap (not hold)

// ============================================================================
// ANIMATION TIMING
// ============================================================================

// Expired state flash animation (red/black alternating)
#define FLASH_INTERVAL_MS 500  // Flash between red and black every 500ms
// Legacy pulsate settings (now unused - replaced with flash)
#define PULSATE_PERIOD_MS 2000   // (deprecated)
#define PULSATE_MIN_OPACITY 50   // (deprecated)
#define PULSATE_MAX_OPACITY 255  // (deprecated)

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================

// Uncomment to enable debug output
#define DEBUG_ESPNOW
#define DEBUG_BUTTONS
// #define DEBUG_POWER
// #define DEBUG_UI

#ifdef DEBUG_ESPNOW
// Use printf for UART output (more reliable on ESP32-S3)
#define DEBUG_ESPNOW_PRINT(...) printf(__VA_ARGS__)
#define DEBUG_ESPNOW_PRINTLN(...) \
  printf(__VA_ARGS__); \
  printf("\n")
#else
#define DEBUG_ESPNOW_PRINT(...)
#define DEBUG_ESPNOW_PRINTLN(...)
#endif

// When using USB serial mode, ALL debug macros must use printf (UART)
// instead of Serial, because Serial is reserved for JSON protocol.
//
// Serial.print() accepts any type (uint8_t, int, const char*, String).
// printf() requires a format string. We use _dbgPrint() overloads to
// bridge the gap so existing DEBUG_*_PRINT(value) calls compile.
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL

  inline void _dbgPrint(const char* s)     { printf("%s", s); }
  inline void _dbgPrint(char c)            { printf("%c", c); }
  inline void _dbgPrint(uint8_t v)         { printf("%u", v); }
  inline void _dbgPrint(int v)             { printf("%d", v); }
  inline void _dbgPrint(unsigned int v)    { printf("%u", v); }
  inline void _dbgPrint(long v)            { printf("%ld", v); }
  inline void _dbgPrint(unsigned long v)   { printf("%lu", v); }
  inline void _dbgPrint(float v)           { printf("%.2f", v); }
  inline void _dbgPrint(double v)          { printf("%.2f", v); }

  // Single-arg: route through _dbgPrint overloads
  // Multi-arg (format string + args): route through printf directly
  #define _DBG_PRINT_1(x)      _dbgPrint(x)
  #define _DBG_PRINT_N(...)    printf(__VA_ARGS__)
  #define _DBG_SELECT(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
  #define _DBG_PRINT(...) _DBG_SELECT(__VA_ARGS__, \
      _DBG_PRINT_N, _DBG_PRINT_N, _DBG_PRINT_N, _DBG_PRINT_N, \
      _DBG_PRINT_N, _DBG_PRINT_N, _DBG_PRINT_N, _DBG_PRINT_1)(__VA_ARGS__)

  #ifdef DEBUG_BUTTONS
    #define DEBUG_BTN_PRINT(...)  _DBG_PRINT(__VA_ARGS__)
    #define DEBUG_BTN_PRINTLN(...) do { _DBG_PRINT(__VA_ARGS__); printf("\n"); } while(0)
  #else
    #define DEBUG_BTN_PRINT(...)
    #define DEBUG_BTN_PRINTLN(...)
  #endif

  #ifdef DEBUG_POWER
    #define DEBUG_PWR_PRINT(...)  _DBG_PRINT(__VA_ARGS__)
    #define DEBUG_PWR_PRINTLN(...) do { _DBG_PRINT(__VA_ARGS__); printf("\n"); } while(0)
  #else
    #define DEBUG_PWR_PRINT(...)
    #define DEBUG_PWR_PRINTLN(...)
  #endif

  #ifdef DEBUG_UI
    #define DEBUG_UI_PRINT(...)  _DBG_PRINT(__VA_ARGS__)
    #define DEBUG_UI_PRINTLN(...) do { _DBG_PRINT(__VA_ARGS__); printf("\n"); } while(0)
  #else
    #define DEBUG_UI_PRINT(...)
    #define DEBUG_UI_PRINTLN(...)
  #endif

#else  // Non-USB modes: use Serial as before

  #ifdef DEBUG_BUTTONS
    #define DEBUG_BTN_PRINT(...) Serial.print(__VA_ARGS__)
    #define DEBUG_BTN_PRINTLN(...) Serial.println(__VA_ARGS__)
  #else
    #define DEBUG_BTN_PRINT(...)
    #define DEBUG_BTN_PRINTLN(...)
  #endif

  #ifdef DEBUG_POWER
    #define DEBUG_PWR_PRINT(...) Serial.print(__VA_ARGS__)
    #define DEBUG_PWR_PRINTLN(...) Serial.println(__VA_ARGS__)
  #else
    #define DEBUG_PWR_PRINT(...)
    #define DEBUG_PWR_PRINTLN(...)
  #endif

  #ifdef DEBUG_UI
    #define DEBUG_UI_PRINT(...) Serial.print(__VA_ARGS__)
    #define DEBUG_UI_PRINTLN(...) Serial.println(__VA_ARGS__)
  #else
    #define DEBUG_UI_PRINT(...)
    #define DEBUG_UI_PRINTLN(...)
  #endif

#endif  // COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
