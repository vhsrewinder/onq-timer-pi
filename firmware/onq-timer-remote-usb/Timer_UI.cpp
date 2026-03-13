#include "Timer_UI.h"
#include "montserrat_96.h"  // Custom 96pt font for timer display
#include "ESPNOW_Driver.h"  // For sending volume/timer commands
#include "Display_SPD2010.h"  // For Set_Backlight()
#include "Button_Driver.h"  // For checking button state during reset hold
// #include "Gyro_QMI8658.h"  // For accelerometer/gyroscope data (motion detection & rotation) - REMOVED: Gyroscope removed in v2.7.0
#include <math.h>

// External references
extern ESPNOWDriver g_espnow;  // Global ESP-NOW instance
extern ButtonDriver g_buttons;  // Global button driver instance

// Global instance
TimerUI g_timerUI;

// Static instance pointer for callbacks
TimerUI* TimerUI::s_instance = nullptr;

// ============================================================================
// INITIALIZATION
// ============================================================================

void TimerUI::init() {
    // Set singleton instance
    s_instance = this;

    // Create FreeRTOS mutex for thread safety (multi-core protection)
    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        DEBUG_UI_PRINTLN("ERROR: Failed to create TimerUI mutex!");
    } else {
        DEBUG_UI_PRINTLN("TimerUI mutex created successfully");
    }

    // Initialize state variables
    m_uiState = UI_STATE_STOPPED;
    m_bgState = BG_STATE_NORMAL;
    m_currentSeconds = 0;
    m_maxSeconds = 0;
    m_presetSeconds = 120;  // Default 2 minutes
    m_isRunning = false;
    m_bridgeConnected = false;
    m_reactConnected = false;
    m_batteryPercent = 100;
    m_lowBattery = false;

    // Real-time clock
    m_currentHours = 0;
    m_currentMinutes = 0;

    // Presets screen
    m_lastResetValue = 0;
    m_hasLastResetValue = false;

    // Settings screen
    m_currentSettingMode = SETTING_MODE_BRIGHTNESS;
    m_systemVolume = 50;  // Default 50%
    // m_autoRotationEnabled = true;  // REMOVED: Auto-rotation disabled (gyroscope removed in v2.7.0)

    // Reset confirmation
    m_resetConfirmVisible = false;
    m_resetConfirmStartTime = 0;
    m_resetConfirmValue = 0;

    // Animation
    m_lastTickTime = 0;
    m_pulsateStartTime = 0;
    m_ringFlashStartTime = 0;

    // REMOVED: Motion detection and display auto-off (gyroscope removed in v2.7.0)
    // m_lastMotionTime = millis();
    // m_lastMotionCheckTime = 0;
    // m_lastAccelX = 0.0f;
    // m_lastAccelY = 0.0f;
    // m_lastAccelZ = 0.0f;
    // m_displayOn = true;  // Display starts on

    // REMOVED: Auto-rotation (gyroscope removed in v2.7.0)
    // m_isUpsideDown = false;
    // m_upsideDownStartTime = 0;
    // m_displayRotated = false;

    // Time picker
    m_pickerVisible = false;

    // Offline mode state (standalone stopwatch mode)
    m_offlineMode = false;           // Default to Bridge Mode
    m_offlineSeconds = 0;
    m_offlinePreset = 120;           // Default 2 minutes
    m_offlineRunning = false;
    m_offlineLastTick = 0;

    // Callback
    m_touchCallback = nullptr;

    // Initialize all LVGL object pointers to null
    m_screen = nullptr;
    m_progressRing = nullptr;
    m_clockLabel = nullptr;
    m_infoContainer = nullptr;
    m_batteryLabel = nullptr;
    m_batteryLED = nullptr;
    m_bridgeLabel = nullptr;
    m_bridgeLED = nullptr;
    m_reactLabel = nullptr;
    m_reactLED = nullptr;
    m_remoteIdLabel = nullptr;
    m_offlineModeLabel = nullptr;
    m_timerLabel = nullptr;
    m_totalTimeLabel = nullptr;
    m_statusLabel = nullptr;
    m_sigmaIcon = nullptr;

    // Touch buttons removed - physical buttons only

    m_pickerContainer = nullptr;
    m_pickerLabel = nullptr;
    m_pickerRollerMin = nullptr;
    m_pickerRollerSec = nullptr;
    m_pickerOkButton = nullptr;

    // Reset confirmation removed - direct reset

    DEBUG_UI_PRINTLN("Creating Watch-Inspired Timer UI...");

    // Load persistent settings from NVS (brightness and volume)
    m_preferences.begin("qm-remote", false);  // Namespace: "qm-remote", read-write mode

    // Load brightness (default 10% for new firmware uploads)
    uint8_t savedBrightness = m_preferences.getUChar("brightness", 10);
    if (savedBrightness < 10) savedBrightness = 10;    // Minimum 10%
    if (savedBrightness > 100) savedBrightness = 100;  // Maximum 100%
    Set_Backlight(savedBrightness);  // Apply saved brightness
    printf("[Timer_UI] Loaded brightness from NVS: %d%%\n", savedBrightness);

    // Load volume (default 50% for new firmware uploads)
    m_systemVolume = m_preferences.getUChar("volume", 50);
    if (m_systemVolume > 100) m_systemVolume = 100;  // Clamp to 0-100
    printf("[Timer_UI] Loaded volume from NVS: %d%%\n", m_systemVolume);

    // REMOVED: Load auto-rotation setting (gyroscope removed in v2.7.0)
    // m_autoRotationEnabled = m_preferences.getBool("autoRotate", true);
    // printf("[Timer_UI] Loaded auto-rotation from NVS: %s\n", m_autoRotationEnabled ? "ON" : "OFF");

    // Note: Preferences object stays open for save operations during runtime

    // Create UI elements
    createUI();

    DEBUG_UI_PRINTLN("Timer UI initialized (v2.0)");
}

// ============================================================================
// UPDATE FUNCTIONS
// ============================================================================

// Helper function to determine connection quality based on RSSI
ConnectionState getConnectionState() {
    // Determine connection quality based on RSSI and connection status
    if (!g_espnow.isConnected()) {
        return CONN_DISCONNECTED;  // Not connected
    }

    // Get RSSI from ESP-NOW driver
    int8_t rssi = g_espnow.getLastRSSI();

    // Classify connection quality based on signal strength
    if (rssi >= RSSI_STRONG_THRESHOLD) {
        return CONN_STRONG;  // Strong signal (>= -70 dBm)
    } else if (rssi >= RSSI_WEAK_THRESHOLD) {
        return CONN_WEAK;    // Weak signal (>= -85 dBm)
    } else {
        return CONN_DISCONNECTED;  // Very weak or no signal (< -85 dBm)
    }
}

void TimerUI::update(uint16_t seconds, uint8_t flags) {
    // THREAD SAFETY: Acquire mutex before modifying UI state
    // Timeout after 50ms to prevent deadlock (increased from 10ms to handle rapid button presses)
    if (m_mutex == nullptr || xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        // Failed to acquire mutex - skip this update to avoid blocking
        printf("[MUTEX] WARNING: Failed to acquire mutex in update() after 50ms\n");
        return;
    }

    // Cache previous values to avoid redundant updates
    static uint16_t lastSeconds = 0xFFFF;  // Force first update
    static BackgroundState lastBgState = BG_STATE_NORMAL;
    static TimerUIState lastUiState = UI_STATE_STOPPED;
    static uint16_t lastMaxSeconds = 0;

    // Validate timer range (prevent integer overflow and invalid values)
    if (seconds > 3600) {
        printf("[TIMER_UI] WARNING: Timer value %d exceeds 1 hour, clamping to 3600\n", seconds);
        seconds = 3600;  // Clamp to maximum (1 hour)
    }

    m_currentSeconds = seconds;
    m_isRunning = (flags & FLAG_RUNNING) != 0;

    // Detect timer restart: timer value INCREASED (timer normally counts DOWN)
    // This happens when React app sends a new timer preset
    if (seconds > lastSeconds && lastSeconds != 0xFFFF) {
        // New timer started - reset max to the new value
        m_maxSeconds = seconds;
        m_presetSeconds = seconds;
        printf("[TIMER_UI] New timer detected: %d seconds (was %d), resetting progress ring\n",
               seconds, lastSeconds);
    }
    // Track max seconds for normal countdown (shouldn't normally trigger after startup)
    else if (seconds > m_maxSeconds) {
        m_maxSeconds = seconds;
        m_presetSeconds = seconds;
    }

    // Reset max if timer goes back to 0 (not running)
    if (seconds == 0 && !m_isRunning && m_maxSeconds > 0) {
        m_maxSeconds = 0;
    }

    // Determine states
    m_uiState = determineUIState(seconds, flags);
    m_bgState = determineBackgroundState(seconds, flags);

    // Check what changed BEFORE updating cache
    bool secondsChanged = (seconds != lastSeconds);
    bool maxSecondsChanged = (m_maxSeconds != lastMaxSeconds);
    bool bgStateChanged = (m_bgState != lastBgState);
    bool uiStateChanged = (m_uiState != lastUiState);

    // Update timer display ONLY if seconds changed
    if (secondsChanged) {
        updateTimerDisplay(seconds);
    }

    // Update total time label ONLY if max changed
    if (maxSecondsChanged) {
        updateTotalTimeLabel(m_maxSeconds);
    }

    // Update progress ring ONLY if seconds or max changed
    if (secondsChanged || maxSecondsChanged) {
        updateProgressRing(seconds, m_maxSeconds);
    }

    // Update colors ONLY if background state changed
    if (bgStateChanged) {
        updateBackgroundColor(m_bgState);
        updateTextColor(m_bgState);
    }

    // Update status label ONLY if UI state changed
    if (uiStateChanged) {
        updateStatusLabel(m_uiState);
    }

    // Update cache values
    lastSeconds = seconds;
    lastMaxSeconds = m_maxSeconds;
    lastBgState = m_bgState;
    lastUiState = m_uiState;

    // Start pulsating timer if expired
    if (m_bgState == BG_STATE_EXPIRED_PULSATE && m_pulsateStartTime == 0) {
        m_pulsateStartTime = millis();

        // REMOVED: Wake display if timer expired (gyroscope/motion detection removed in v2.7.0)
        // if (!m_displayOn) {
        //     printf("[TIMER] Timer expired - waking display for alert\n");
        //     turnDisplayOn();
        // }
    } else if (m_bgState != BG_STATE_EXPIRED_PULSATE) {
        m_pulsateStartTime = 0;
    }

    // Start progress ring flash timer if paused
    if (m_uiState == UI_STATE_PAUSED && m_ringFlashStartTime == 0) {
        m_ringFlashStartTime = millis();
    } else if (m_uiState != UI_STATE_PAUSED && m_ringFlashStartTime != 0) {
        // Exiting paused state - stop flash and restore ring color
        m_ringFlashStartTime = 0;
        // Force ring color update to restore proper color
        updateProgressRing(m_currentSeconds, m_maxSeconds);
    }

    // THREAD SAFETY: Release mutex
    xSemaphoreGive(m_mutex);
}

void TimerUI::setBattery(uint8_t percentage, bool lowBattery) {
    // OPTIMIZATION: Only update display if battery status changed
    if (m_batteryPercent != percentage || m_lowBattery != lowBattery) {
        m_batteryPercent = percentage;
        m_lowBattery = lowBattery;
        updateInfoSection();
    }
}

void TimerUI::setConnectionStatus(bool bridgeConnected, bool reactConnected) {
    // OPTIMIZATION: Only update display if connection status changed
    if (m_bridgeConnected != bridgeConnected || m_reactConnected != reactConnected) {
        m_bridgeConnected = bridgeConnected;
        m_reactConnected = reactConnected;
        updateInfoSection();
    }
}

void TimerUI::setCurrentTime(uint8_t hours, uint8_t minutes) {
    // DEBUG: Log call frequency (v2.9.7 diagnostic)
    static uint32_t callCounter = 0;
    static uint32_t lastReportTime = 0;
    callCounter++;
    uint32_t now_debug = millis();
    if (now_debug - lastReportTime >= 5000) {
        printf("[DEBUG_v2.9.7] setCurrentTime() called %lu times in last 5s\n", callCounter);
        callCounter = 0;
        lastReportTime = now_debug;
    }

    // OPTIMIZATION: Only update display if time changed
    if (m_currentHours != hours || m_currentMinutes != minutes) {
        m_currentHours = hours;
        m_currentMinutes = minutes;
        updateRealTimeClock(hours, minutes);
    }
}

void TimerUI::setTouchCallback(TouchButtonCallback callback) {
    m_touchCallback = callback;
}

void TimerUI::tick() {
    // DEBUG: Log call frequency (v2.9.6 diagnostic)
    static uint32_t callCounter = 0;
    static uint32_t lastReportTime = 0;
    callCounter++;
    uint32_t now_debug = millis();
    if (now_debug - lastReportTime >= 5000) {
        printf("[DEBUG_v2.9.6] tick() called %lu times in last 5s\n", callCounter);
        callCounter = 0;
        lastReportTime = now_debug;
    }

    // THREAD SAFETY: Acquire mutex before modifying UI (NON-BLOCKING)
    // Use 0ms timeout to prevent blocking the main loop (critical for touch responsiveness)
    // If mutex is busy, skip this tick - we'll try again next frame (16ms later)
    if (m_mutex == nullptr || xSemaphoreTake(m_mutex, 0) != pdTRUE) {
        // Mutex busy - skip this tick to avoid blocking LVGL touch event processing
        return;  // Skip tick if mutex unavailable (will retry next frame)
    }

    uint32_t now = millis();
    m_lastTickTime = now;

    // CRITICAL FIX: Skip UI updates when not on main screen (prevents touch interference)
    // When on presets/time picker screen, updating main screen elements causes LVGL
    // to miss touch events on the active screen (race condition)
    if (m_uiState == UI_STATE_PRESETS || m_uiState == UI_STATE_TIME_PICKER) {
        // REMOVED: Motion-based display auto-off (replaced with time-based power mgmt in v2.7.0)
        // checkMotion();
        xSemaphoreGive(m_mutex);
        return;
    }

    // Settings screen handles its own updates (allow those to proceed)
    if (m_uiState == UI_STATE_SETTINGS) {
        updateSettingsScreen();
        // REMOVED: Motion-based display auto-off (replaced with time-based power mgmt in v2.7.0)
        // checkMotion();
        xSemaphoreGive(m_mutex);
        return;
    }

    // === MAIN SCREEN UPDATES (only when on main timer screen) ===

    // REMOVED: RSSI quality polling (v2.9.4 fix for excessive redraws)
    // Connection status changes are handled by setConnectionStatus()
    // No need to poll every 500ms - only update when info actually changes

    // Update pulsating animation if expired
    if (m_bgState == BG_STATE_EXPIRED_PULSATE) {
        updatePulsatingAnimation();
    }

    // Update progress ring flash animation if paused
    if (m_uiState == UI_STATE_PAUSED) {
        updateProgressRingFlash();
    }

    // REMOVED: Motion-based display auto-off (replaced with time-based power mgmt in v2.7.0)
    // checkMotion();

    // REMOVED: Gyroscope-based auto-rotation (gyroscope removed in v2.7.0 for power savings)
    // checkOrientation();

    // Offline mode: 1-second countdown timer
    if (m_offlineMode && m_offlineRunning) {
        if (now - m_offlineLastTick >= 1000) {  // 1 second elapsed
            m_offlineLastTick = now;

            if (m_offlineSeconds > 0) {
                m_offlineSeconds--;
                printf("[OFFLINE] Timer: %d seconds\n", m_offlineSeconds);

                // Update display
                uint8_t flags = FLAG_RUNNING;
                if (m_offlineSeconds == 0) {
                    flags |= FLAG_EXPIRED;
                    m_offlineRunning = false;
                    printf("[OFFLINE] Timer expired!\n");
                }

                // THREAD SAFETY: Release mutex before calling update() (which needs to acquire it)
                xSemaphoreGive(m_mutex);
                update(m_offlineSeconds, flags);
                return;  // Exit tick() - mutex already released
            }
        }
    }

    // THREAD SAFETY: Release mutex
    xSemaphoreGive(m_mutex);
}

// ============================================================================
// PHYSICAL BUTTON HANDLERS
// ============================================================================

void TimerUI::onPhysicalButton1() {
    // REMOVED: Wake display if it's off (gyroscope/motion detection removed in v2.7.0)
    // if (!m_displayOn) {
    //     printf("[BTN1] Waking display from sleep\n");
    //     turnDisplayOn();
    //     return;  // Consume button press for wake-up only
    // }

    // Context-aware: Reset button, Settings decrease
    printf("*** PHYSICAL BUTTON 1 PRESSED ***\n");
    printf("[BTN1] UI State: %d, Current seconds: %d, Is running: %d\n",
           m_uiState, m_currentSeconds, m_isRunning);
    DEBUG_UI_PRINTLN("Physical Button 1");

    // NEW: Context-aware handling
    if (m_uiState == UI_STATE_SETTINGS) {
        // Decrease current setting (brightness or volume)
        printf("[BTN1] Settings mode: Decrease setting\n");
        adjustSetting(-1);
        return;
    }

    if (m_uiState == UI_STATE_PRESETS) {
        // Exit presets screen
        printf("[BTN1] Exiting presets screen\n");
        hidePresets();
        return;
    }

    // Default behavior: Reset timer immediately
    if (m_offlineMode) {
        // OFFLINE MODE: Reset timer locally
        if (m_offlineSeconds > 0) {
            m_lastResetValue = m_offlineSeconds;
            m_hasLastResetValue = true;
            printf("[BTN1] Saved offline value %d for undo\n", m_lastResetValue);
        }

        m_offlineSeconds = m_offlinePreset;
        m_offlineRunning = false;
        printf("[OFFLINE] Reset to %d seconds\n", m_offlinePreset);
        update(m_offlineSeconds, 0);
    } else {
        // BRIDGE MODE: Send reset command to bridge
        // Save current value for undo feature (if non-zero)
        if (m_currentSeconds > 0) {
            m_lastResetValue = m_currentSeconds;
            m_hasLastResetValue = true;
            printf("[BTN1] Saved current value %d for undo\n", m_lastResetValue);
        }

        printf("[BTN1] Sending RESET command (BTN_ID_PHYSICAL_1)\n");
        if (m_touchCallback) {
            m_touchCallback(BTN_ID_PHYSICAL_1);
        } else {
            printf("[BTN1] ERROR: No touch callback registered!\n");
        }
    }
}

void TimerUI::onPhysicalButton2() {
    // REMOVED: Wake display if it's off (gyroscope/motion detection removed in v2.7.0)
    // if (!m_displayOn) {
    //     printf("[BTN2] Waking display from sleep\n");
    //     turnDisplayOn();
    //     return;  // Consume button press for wake-up only
    // }

    // NEW: Open presets screen (replaces time picker)
    printf("*** PHYSICAL BUTTON 2 PRESSED *** (Presets)\n");
    printf("[BTN2] Current state: uiState=%d\n", m_uiState);
    DEBUG_UI_PRINTLN("Physical Button 2: Presets");

    // Context-aware behavior
    if (m_uiState == UI_STATE_PRESETS) {
        printf("[BTN2] Hiding presets screen...\n");
        hidePresets();
    } else if (m_uiState == UI_STATE_SETTINGS) {
        printf("[BTN2] Toggle setting mode (brightness/volume)...\n");
        toggleSettingMode();
    } else {
        printf("[BTN2] Showing presets screen...\n");
        showPresets();
    }
}

void TimerUI::onPhysicalButton3() {
    // REMOVED: Wake display if it's off (gyroscope/motion detection removed in v2.7.0)
    // if (!m_displayOn) {
    //     printf("[BTN3] Waking display from sleep\n");
    //     turnDisplayOn();
    //     return;  // Consume button press for wake-up only
    // }

    // Context-aware: Start/Pause toggle, Settings adjustment
    printf("*** PHYSICAL BUTTON 3 PRESSED ***\n");
    printf("[BTN3] Current state: isRunning=%d, uiState=%d\n",
           m_isRunning, m_uiState);
    DEBUG_UI_PRINTLN("Physical Button 3");

    // NEW: Context-aware handling
    if (m_uiState == UI_STATE_SETTINGS) {
        // Increase current setting (brightness or volume)
        printf("[BTN3] Settings mode: Increase setting\n");
        adjustSetting(+1);
        return;
    }

    // Default behavior: Start/Pause toggle
    if (m_offlineMode) {
        // OFFLINE MODE: Toggle timer running state locally
        if (m_offlineSeconds > 0) {
            m_offlineRunning = !m_offlineRunning;
            m_offlineLastTick = millis();  // Reset tick timer
            printf("[OFFLINE] Timer %s\n", m_offlineRunning ? "STARTED" : "PAUSED");
            update(m_offlineSeconds, m_offlineRunning ? FLAG_RUNNING : 0);
        } else {
            printf("[OFFLINE] Cannot start - timer at 0:00\n");
        }
    } else {
        // BRIDGE MODE: Send start/pause commands via ESP-NOW
        if (m_touchCallback != nullptr) {
            if (m_isRunning) {
                // Trigger pause
                printf("[BTN3] Sending PAUSE command (BTN_ID %d)\n", BTN_ID_TOUCH_PAUSE);
                m_touchCallback(BTN_ID_TOUCH_PAUSE);
            } else {
                // Trigger play/resume
                if (m_uiState == UI_STATE_PAUSED) {
                    printf("[BTN3] Sending RESUME command (BTN_ID %d)\n", BTN_ID_TOUCH_RESUME);
                    m_touchCallback(BTN_ID_TOUCH_RESUME);
                } else {
                    printf("[BTN3] Sending PLAY command (BTN_ID %d)\n", BTN_ID_TOUCH_PLAY);
                    m_touchCallback(BTN_ID_TOUCH_PLAY);
                }
            }
        }
    }
}

// ============================================================================
// UI CREATION
// ============================================================================

void TimerUI::createUI() {
    // Get active screen
    m_screen = lv_scr_act();

    // Set pure black background
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(COLOR_BG_NORMAL), 0);

    // Create UI components in layering order
    createProgressRing();
    createRealTimeClock();
    createInfoSection();
    // Sigma icon removed - matches watch design
    createTimerDisplay();
    createStatusLabel();
    // Touch buttons removed - physical buttons only
    createTimePicker();
    createPresetsScreen();
    createSettingsScreen();
    // createResetConfirmOverlay();  // DISABLED: Reset now works immediately without confirmation

    DEBUG_UI_PRINTLN("UI created (with presets and settings)");
}

void TimerUI::createProgressRing() {
    // DUAL-ARC DESIGN: White (elapsed) chases green/yellow/red (remaining)
    // - MAIN (background) = Green/yellow/red arc (remaining time) - changes color based on time
    // - INDICATOR (foreground) = White arc - grows as time ELAPSES
    // Creates "white chasing color" effect

    int ringSize = DISPLAY_WIDTH - 20;

    m_progressRing = lv_arc_create(m_screen);
    lv_obj_set_size(m_progressRing, ringSize, ringSize);
    lv_obj_center(m_progressRing);

    // Gap at TOP (12 o'clock) where the clock displays
    // Rotation 270° makes 0° point to the top
    // Arc from 45° to 315° creates a 270° arc with 90° gap at top
    lv_arc_set_rotation(m_progressRing, 270);  // 0° points to top (12 o'clock)
    lv_arc_set_bg_angles(m_progressRing, 45, 315);  // 270° arc, gap centered at top
    lv_arc_set_range(m_progressRing, 0, 100);
    lv_arc_set_value(m_progressRing, 0);  // Starts at 0% elapsed

    // Thicker arc (18px)
    lv_obj_set_style_arc_width(m_progressRing, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(m_progressRing, 18, LV_PART_MAIN);

    // Colors: MAIN = green (will change to yellow/red), INDICATOR = white (elapsed)
    lv_obj_set_style_arc_color(m_progressRing, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);  // White
    lv_obj_set_style_arc_color(m_progressRing, lv_color_hex(0x00FF00), LV_PART_MAIN);  // Green (initial)

    // Rounded ends RE-ENABLED (v2.10.3) - safe now with arc value/color caching (v2.10.1)
    // Caching prevents redundant lv_arc_set_value() calls that previously caused excessive redraws
    lv_obj_set_style_arc_rounded(m_progressRing, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(m_progressRing, true, LV_PART_MAIN);

    // Remove knob and make non-interactive
    lv_obj_remove_style(m_progressRing, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(m_progressRing, LV_OBJ_FLAG_CLICKABLE);

    // Disable animations for instant updates (v2.9.1) - eliminates unnecessary redraws
    lv_obj_set_style_anim_time(m_progressRing, 0, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(m_progressRing, 0, LV_PART_MAIN);

    DEBUG_UI_PRINTLN("Progress ring created (white chases color)");
}

void TimerUI::createRealTimeClock() {
    m_clockLabel = lv_label_create(m_screen);

    // Set initial text
    lv_label_set_text(m_clockLabel, "00:00");

    // Set font (doubled from 20pt to 40pt)
    lv_obj_set_style_text_font(m_clockLabel, &lv_font_montserrat_40, 0);

    // White text
    lv_obj_set_style_text_color(m_clockLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);

    // Position at top center
    lv_obj_align(m_clockLabel, LV_ALIGN_TOP_MID, 0, CLOCK_POS_Y);

    DEBUG_UI_PRINTLN("Real-time clock created");
}

void TimerUI::createInfoSection() {
    // Info section is a horizontal layout: "Battery: XX% [LED] | Bridge: [LED] | React: [LED]"

    // Battery label
    m_batteryLabel = lv_label_create(m_screen);
    lv_label_set_text(m_batteryLabel, "100%");
    lv_obj_set_style_text_font(m_batteryLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m_batteryLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);
    lv_obj_set_pos(m_batteryLabel, DISPLAY_CENTER_X - 80, INFO_POS_Y);

    // Battery LED
    m_batteryLED = lv_obj_create(m_screen);
    lv_obj_set_size(m_batteryLED, INFO_LED_SIZE, INFO_LED_SIZE);
    lv_obj_set_style_radius(m_batteryLED, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_batteryLED, lv_color_hex(COLOR_LED_GOOD), 0);
    lv_obj_set_style_border_width(m_batteryLED, 0, 0);
    lv_obj_set_pos(m_batteryLED, DISPLAY_CENTER_X - 40, INFO_POS_Y + 2);

    // Radio sleep label (between battery and bridge)
    m_radioLabel = lv_label_create(m_screen);
    lv_label_set_text(m_radioLabel, "RDO");
    lv_obj_set_style_text_font(m_radioLabel, &lv_font_montserrat_12, 0);  // Same as other labels
    lv_obj_set_style_text_color(m_radioLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);
    lv_obj_set_pos(m_radioLabel, DISPLAY_CENTER_X - 38, INFO_POS_Y);

    // Radio sleep LED (orange when radio is sleeping)
    m_radioLED = lv_obj_create(m_screen);
    lv_obj_set_size(m_radioLED, INFO_LED_SIZE, INFO_LED_SIZE);
    lv_obj_set_style_radius(m_radioLED, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_radioLED, lv_color_hex(COLOR_LED_WARNING), 0);  // Orange for sleeping
    lv_obj_set_style_border_width(m_radioLED, 0, 0);
    lv_obj_set_pos(m_radioLED, DISPLAY_CENTER_X - 23, INFO_POS_Y + 2);
    lv_obj_add_flag(m_radioLED, LV_OBJ_FLAG_HIDDEN);  // Hidden by default (radio awake)

    // Bridge LED label
    m_bridgeLabel = lv_label_create(m_screen);
    lv_label_set_text(m_bridgeLabel, "BR");
    lv_obj_set_style_text_font(m_bridgeLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m_bridgeLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);
    lv_obj_set_pos(m_bridgeLabel, DISPLAY_CENTER_X - 20, INFO_POS_Y);

    // Bridge LED
    m_bridgeLED = lv_obj_create(m_screen);
    lv_obj_set_size(m_bridgeLED, INFO_LED_SIZE, INFO_LED_SIZE);
    lv_obj_set_style_radius(m_bridgeLED, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_bridgeLED, lv_color_hex(COLOR_LED_BAD), 0);
    lv_obj_set_style_border_width(m_bridgeLED, 0, 0);
    lv_obj_set_pos(m_bridgeLED, DISPLAY_CENTER_X + 10, INFO_POS_Y + 2);

    // React LED label
    m_reactLabel = lv_label_create(m_screen);
    lv_label_set_text(m_reactLabel, "APP");
    lv_obj_set_style_text_font(m_reactLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m_reactLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);
    lv_obj_set_pos(m_reactLabel, DISPLAY_CENTER_X + 25, INFO_POS_Y);

    // React LED
    m_reactLED = lv_obj_create(m_screen);
    lv_obj_set_size(m_reactLED, INFO_LED_SIZE, INFO_LED_SIZE);
    lv_obj_set_style_radius(m_reactLED, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_reactLED, lv_color_hex(COLOR_LED_BAD), 0);
    lv_obj_set_style_border_width(m_reactLED, 0, 0);
    lv_obj_set_pos(m_reactLED, DISPLAY_CENTER_X + 55, INFO_POS_Y + 2);

    // Remote ID label (to the right of APP status)
    m_remoteIdLabel = lv_label_create(m_screen);
    char idBuf[8];
    snprintf(idBuf, sizeof(idBuf), "ID %d", REMOTE_ID);
    lv_label_set_text(m_remoteIdLabel, idBuf);
    lv_obj_set_style_text_font(m_remoteIdLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m_remoteIdLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);
    lv_obj_set_pos(m_remoteIdLabel, DISPLAY_CENTER_X + 70, INFO_POS_Y);

    // Offline mode label (to the right of battery info, replaces bridge/app/ID info)
    m_offlineModeLabel = lv_label_create(m_screen);
    lv_label_set_text(m_offlineModeLabel, "OFFLINE MODE");
    lv_obj_set_style_text_font(m_offlineModeLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m_offlineModeLabel, lv_color_hex(COLOR_LED_WARNING), 0);  // Yellow/orange for offline
    lv_obj_set_pos(m_offlineModeLabel, DISPLAY_CENTER_X - 20, INFO_POS_Y);  // Position after battery LED
    lv_obj_add_flag(m_offlineModeLabel, LV_OBJ_FLAG_HIDDEN);  // Hidden by default (starts in bridge mode)

    DEBUG_UI_PRINTLN("Info section created");
}

void TimerUI::createSigmaIcon() {
    m_sigmaIcon = lv_label_create(m_screen);

    // Set sigma symbol (Σ in Unicode)
    lv_label_set_text(m_sigmaIcon, LV_SYMBOL_LIST);  // Using closest available symbol

    // Set font
    lv_obj_set_style_text_font(m_sigmaIcon, &lv_font_montserrat_16, 0);

    // Dark gray color (subtle)
    lv_obj_set_style_text_color(m_sigmaIcon, lv_color_hex(0x606060), 0);

    // Position at top center
    lv_obj_align(m_sigmaIcon, LV_ALIGN_TOP_MID, 0, SIGMA_POS_Y);

    DEBUG_UI_PRINTLN("Sigma icon created");
}

void TimerUI::createTimerDisplay() {
    // Create total time label (shows starting time above countdown)
    m_totalTimeLabel = lv_label_create(m_screen);
    lv_label_set_text(m_totalTimeLabel, "00:00");
    lv_obj_set_style_text_font(m_totalTimeLabel, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(m_totalTimeLabel, lv_color_hex(0x808080), 0);  // Gray color
    lv_obj_align(m_totalTimeLabel, LV_ALIGN_CENTER, 0, -80);  // Above timer

    // Create main timer label
    m_timerLabel = lv_label_create(m_screen);

    // Set initial text
    lv_label_set_text(m_timerLabel, "00:00");

    // WATCH-INSPIRED: Use custom 96pt font (200% larger) for dominant timer display
    lv_obj_set_style_text_font(m_timerLabel, &montserrat_96, 0);

    // White text
    lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);

    // Center position - moved down 30px (from -40 to -10)
    lv_obj_align(m_timerLabel, LV_ALIGN_CENTER, 0, -10);

    DEBUG_UI_PRINTLN("Timer display created (montserrat_96 - 200% larger!)");
    DEBUG_UI_PRINTLN("Total time label created (montserrat_40)");
}

void TimerUI::createStatusLabel() {
    m_statusLabel = lv_label_create(m_screen);

    // Set initial text
    lv_label_set_text(m_statusLabel, "STOPPED");

    // Larger font size (doubled from 20pt to 40pt)
    lv_obj_set_style_text_font(m_statusLabel, &lv_font_montserrat_40, 0);

    // White text
    lv_obj_set_style_text_color(m_statusLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), 0);

    // Position below timer display
    lv_obj_align(m_statusLabel, LV_ALIGN_CENTER, 0, 80);

    DEBUG_UI_PRINTLN("Status label created");
}

// Touch buttons removed - using physical buttons only

void TimerUI::createTimePicker() {
    // WATCH-INSPIRED: Full-screen overlay with clean design
    m_pickerContainer = lv_obj_create(m_screen);
    lv_obj_set_size(m_pickerContainer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_align(m_pickerContainer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(m_pickerContainer, lv_color_hex(COLOR_PICKER_BG), 0);
    lv_obj_set_style_border_width(m_pickerContainer, 0, 0);
    lv_obj_set_style_radius(m_pickerContainer, 0, 0);  // No rounded corners
    lv_obj_set_style_pad_all(m_pickerContainer, 0, 0);  // No padding

    // Label at top - "Set Time" (simpler than watch "Minutes"/"Seconds")
    m_pickerLabel = lv_label_create(m_pickerContainer);
    lv_label_set_text(m_pickerLabel, "Set Time");
    lv_obj_set_style_text_font(m_pickerLabel, &lv_font_montserrat_18, 0);  // Slightly smaller
    lv_obj_set_style_text_color(m_pickerLabel, lv_color_hex(0xAAAAAA), 0);  // Dimmed gray
    lv_obj_align(m_pickerLabel, LV_ALIGN_TOP_MID, 0, 30);

    // Minute roller (left side)
    m_pickerRollerMin = lv_roller_create(m_pickerContainer);
    lv_roller_set_options(m_pickerRollerMin,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
        "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
        "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
        "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
        "50\n51\n52\n53\n54\n55\n56\n57\n58\n59",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(m_pickerRollerMin, 5);
    lv_obj_set_width(m_pickerRollerMin, 100);
    lv_obj_set_pos(m_pickerRollerMin, DISPLAY_CENTER_X - 110, DISPLAY_CENTER_Y - 60);

    // Style for unselected rows (MAIN part)
    lv_obj_set_style_bg_color(m_pickerRollerMin, lv_color_hex(0x202020), LV_PART_MAIN);  // Dark gray background
    lv_obj_set_style_bg_opa(m_pickerRollerMin, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(m_pickerRollerMin, lv_color_hex(0x808080), LV_PART_MAIN);  // Gray text
    lv_obj_set_style_border_width(m_pickerRollerMin, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(m_pickerRollerMin, lv_color_hex(0x404040), LV_PART_MAIN);

    // Style for selected row (SELECTED part)
    lv_obj_set_style_bg_color(m_pickerRollerMin, lv_color_hex(COLOR_PICKER_SELECTED), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(m_pickerRollerMin, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(m_pickerRollerMin, lv_color_hex(0x000000), LV_PART_SELECTED);  // Black text on lavender

    // Minute label
    lv_obj_t* minLabel = lv_label_create(m_pickerContainer);
    lv_label_set_text(minLabel, "Min");
    lv_obj_set_style_text_font(minLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(minLabel, lv_color_hex(COLOR_PICKER_TEXT), 0);
    lv_obj_set_pos(minLabel, DISPLAY_CENTER_X - 85, DISPLAY_CENTER_Y + 70);

    // Second roller (right side)
    m_pickerRollerSec = lv_roller_create(m_pickerContainer);
    lv_roller_set_options(m_pickerRollerSec,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
        "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
        "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
        "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
        "50\n51\n52\n53\n54\n55\n56\n57\n58\n59",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(m_pickerRollerSec, 5);
    lv_obj_set_width(m_pickerRollerSec, 100);
    lv_obj_set_pos(m_pickerRollerSec, DISPLAY_CENTER_X + 10, DISPLAY_CENTER_Y - 60);

    // Style for unselected rows (MAIN part)
    lv_obj_set_style_bg_color(m_pickerRollerSec, lv_color_hex(0x202020), LV_PART_MAIN);  // Dark gray background
    lv_obj_set_style_bg_opa(m_pickerRollerSec, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(m_pickerRollerSec, lv_color_hex(0x808080), LV_PART_MAIN);  // Gray text
    lv_obj_set_style_border_width(m_pickerRollerSec, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(m_pickerRollerSec, lv_color_hex(0x404040), LV_PART_MAIN);

    // Style for selected row (SELECTED part)
    lv_obj_set_style_bg_color(m_pickerRollerSec, lv_color_hex(COLOR_PICKER_SELECTED), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(m_pickerRollerSec, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(m_pickerRollerSec, lv_color_hex(0x000000), LV_PART_SELECTED);  // Black text on lavender

    // Second label
    lv_obj_t* secLabel = lv_label_create(m_pickerContainer);
    lv_label_set_text(secLabel, "Sec");
    lv_obj_set_style_text_font(secLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(secLabel, lv_color_hex(COLOR_PICKER_TEXT), 0);
    lv_obj_set_pos(secLabel, DISPLAY_CENTER_X + 35, DISPLAY_CENTER_Y + 70);

    // WATCH-INSPIRED: Circular OK button with checkmark (bottom right like watch)
    m_pickerOkButton = lv_btn_create(m_pickerContainer);
    lv_obj_set_size(m_pickerOkButton, 70, 70);  // Circular
    lv_obj_set_style_radius(m_pickerOkButton, LV_RADIUS_CIRCLE, 0);  // Perfect circle
    lv_obj_set_style_bg_color(m_pickerOkButton, lv_color_hex(COLOR_BTN_PRIMARY), 0);  // Lavender
    lv_obj_set_style_border_width(m_pickerOkButton, 0, 0);
    lv_obj_set_style_shadow_width(m_pickerOkButton, 8, 0);  // Subtle shadow
    lv_obj_set_style_shadow_color(m_pickerOkButton, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(m_pickerOkButton, LV_OPA_40, 0);
    lv_obj_align(m_pickerOkButton, LV_ALIGN_BOTTOM_MID, 0, -40);  // Centered at bottom
    lv_obj_add_event_cb(m_pickerOkButton, onPickerOkButtonClicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t* labelOk = lv_label_create(m_pickerOkButton);
    lv_label_set_text(labelOk, LV_SYMBOL_OK);  // Checkmark
    lv_obj_set_style_text_font(labelOk, &lv_font_montserrat_20, 0);  // Larger checkmark
    lv_obj_set_style_text_color(labelOk, lv_color_hex(COLOR_BTN_TEXT), 0);  // Black on lavender
    lv_obj_center(labelOk);

    // Initially hidden
    lv_obj_add_flag(m_pickerContainer, LV_OBJ_FLAG_HIDDEN);

    DEBUG_UI_PRINTLN("Time picker created");
}

// Reset confirmation removed - direct reset on button press

// ============================================================================
// UI UPDATE HELPERS
// ============================================================================

void TimerUI::updateTimerDisplay(uint16_t seconds) {
    // DEBUG: Log call frequency (v2.9.6 diagnostic)
    static uint32_t callCounter = 0;
    static uint32_t lastReportTime = 0;
    callCounter++;
    uint32_t now = millis();
    if (now - lastReportTime >= 5000) {
        printf("[DEBUG_v2.9.6] updateTimerDisplay() called %lu times in last 5s\n", callCounter);
        callCounter = 0;
        lastReportTime = now;
    }

    if (m_timerLabel == nullptr) return;

    // Cache last seconds to prevent redundant label updates (v2.10.2)
    static uint16_t lastSeconds = 0xFFFF;
    if (seconds == lastSeconds) return;
    lastSeconds = seconds;

    uint16_t minutes = seconds / 60;
    uint16_t secs = seconds % 60;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, secs);
    lv_label_set_text(m_timerLabel, buf);
}

void TimerUI::updateTotalTimeLabel(uint16_t totalSeconds) {
    if (m_totalTimeLabel == nullptr) return;

    uint16_t minutes = totalSeconds / 60;
    uint16_t secs = totalSeconds % 60;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, secs);
    lv_label_set_text(m_totalTimeLabel, buf);
}

void TimerUI::updateProgressRing(uint16_t seconds, uint16_t maxSeconds) {
    // DEBUG: Log call frequency (v2.9.6 diagnostic)
    static uint32_t callCounter = 0;
    static uint32_t lastReportTime = 0;
    callCounter++;
    uint32_t now = millis();
    if (now - lastReportTime >= 5000) {
        printf("[DEBUG_v2.9.6] updateProgressRing() called %lu times in last 5s\n", callCounter);
        callCounter = 0;
        lastReportTime = now;
    }

    if (m_progressRing == nullptr) return;

    if (maxSeconds == 0) {
        lv_arc_set_value(m_progressRing, 0);
        return;
    }

    // Calculate percentage ELAPSED - white arc grows from 0% to 100%
    // 0% elapsed at start (30:00), 100% elapsed at end (00:00)
    int32_t elapsedPercentage = 100 - ((seconds * 100) / maxSeconds);

    // Cache last value to avoid redundant LVGL calls (v2.10.1)
    static int32_t lastElapsedPercentage = -1;
    if (elapsedPercentage != lastElapsedPercentage) {
        lv_arc_set_value(m_progressRing, elapsedPercentage);
        lastElapsedPercentage = elapsedPercentage;
    }

    // Update MAIN arc color (remaining time) based on remaining seconds
    // White INDICATOR chases this colored arc
    uint32_t ringColor;
    if (seconds <= 5) {
        ringColor = 0xFF0000;  // Red when 5 seconds or less
    } else if (seconds <= 30) {
        ringColor = 0xFFCC00;  // Yellow when 30 seconds or less
    } else {
        ringColor = 0x00FF00;  // Green when more than 30 seconds
    }

    // Cache last color to avoid redundant LVGL calls (v2.10.1)
    static uint32_t lastRingColor = 0xFFFFFFFF;
    if (ringColor != lastRingColor) {
        lv_obj_set_style_arc_color(m_progressRing, lv_color_hex(ringColor), LV_PART_MAIN);
        lastRingColor = ringColor;
    }
}

void TimerUI::updateStatusLabel(TimerUIState state) {
    if (m_statusLabel == nullptr) return;

    const char* statusText = "STOPPED";
    switch (state) {
        case UI_STATE_RUNNING:
            statusText = "RUNNING";
            break;
        case UI_STATE_PAUSED:
            statusText = "PAUSED";
            break;
        case UI_STATE_STOPPED:
        case UI_STATE_EXPIRED:
        default:
            statusText = "STOPPED";
            break;
    }

    lv_label_set_text(m_statusLabel, statusText);
}

void TimerUI::updateBackgroundColor(BackgroundState bgState) {
    if (m_screen == nullptr) return;

    // Get both colors
    uint32_t bgColor = getBackgroundColor(bgState);
    uint32_t textColor = getTextColor(bgState);
    uint32_t totalTimeColor = (bgState == BG_STATE_NORMAL || bgState == BG_STATE_EXPIRED_PULSATE) ? 0x808080 : 0x404040;

    // Cache colors to prevent redundant LVGL calls (v2.10.2)
    static uint32_t lastBgColor = 0xFFFFFFFF;
    static uint32_t lastTextColor = 0xFFFFFFFF;
    static uint32_t lastTotalTimeColor = 0xFFFFFFFF;

    // Update background color only if changed
    if (bgColor != lastBgColor) {
        lv_obj_set_style_bg_color(m_screen, lv_color_hex(bgColor), 0);
        lv_obj_set_style_bg_opa(m_screen, LV_OPA_COVER, 0);
        lastBgColor = bgColor;
    }

    // Update text colors only if changed
    if (textColor != lastTextColor) {
        if (m_timerLabel != nullptr) {
            lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(textColor), 0);
        }
        if (m_statusLabel != nullptr) {
            lv_obj_set_style_text_color(m_statusLabel, lv_color_hex(textColor), 0);
        }
        if (m_clockLabel != nullptr) {
            lv_obj_set_style_text_color(m_clockLabel, lv_color_hex(textColor), 0);
        }
        if (m_batteryLabel != nullptr) {
            lv_obj_set_style_text_color(m_batteryLabel, lv_color_hex(textColor), 0);
        }
        if (m_remoteIdLabel != nullptr) {
            lv_obj_set_style_text_color(m_remoteIdLabel, lv_color_hex(textColor), 0);
        }
        lastTextColor = textColor;
    }

    // Update total time label color only if changed
    if (totalTimeColor != lastTotalTimeColor && m_totalTimeLabel != nullptr) {
        lv_obj_set_style_text_color(m_totalTimeLabel, lv_color_hex(totalTimeColor), 0);
        lastTotalTimeColor = totalTimeColor;
    }
}

void TimerUI::updateTextColor(BackgroundState bgState) {
    // This function is now integrated into updateBackgroundColor()
    // Kept for compatibility, but does nothing
    // All color updates happen together in updateBackgroundColor()
}

// Button layout removed - using physical buttons only

void TimerUI::updateRealTimeClock(uint8_t hours, uint8_t minutes) {
    if (m_clockLabel == nullptr) return;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);
    lv_label_set_text(m_clockLabel, buf);
}

void TimerUI::updateInfoSection() {
    // DEBUG: Log how often this function is called (v2.9.6 diagnostic)
    static uint32_t callCounter = 0;
    static uint32_t lastReportTime = 0;
    callCounter++;
    uint32_t now = millis();
    if (now - lastReportTime >= 5000) {
        printf("[DEBUG_v2.9.6] updateInfoSection() called %lu times in last 5s\n", callCounter);
        callCounter = 0;
        lastReportTime = now;
    }

    // Update battery info (shown in both modes)
    if (m_batteryLabel != nullptr) {
        static uint8_t lastBatteryPercent = 255;  // Cache last battery % (v2.9.3 optimization)
        if (m_batteryPercent != lastBatteryPercent) {  // Only update label if battery changed
            lastBatteryPercent = m_batteryPercent;
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", m_batteryPercent);
            lv_label_set_text(m_batteryLabel, buf);
            lv_obj_clear_flag(m_batteryLabel, LV_OBJ_FLAG_HIDDEN);  // Always show
        }
    }

    if (m_batteryLED != nullptr) {
        uint32_t color;
        if (m_lowBattery) {
            color = COLOR_LED_BAD;  // Red
        } else if (m_batteryPercent > 50) {
            color = COLOR_LED_GOOD;  // Green
        } else {
            color = COLOR_LED_WARNING;  // Yellow
        }
        static uint32_t lastBatteryLEDColor = 0xFFFFFFFF;  // Cache last LED color (v2.9.3)
        if (color != lastBatteryLEDColor) {  // Only update color if changed
            lastBatteryLEDColor = color;
            lv_obj_set_style_bg_color(m_batteryLED, lv_color_hex(color), 0);
            lv_obj_clear_flag(m_batteryLED, LV_OBJ_FLAG_HIDDEN);  // Always show
        }
    }

    // Update radio sleep indicator (v2.7.0+)
    // Show orange LED when radio is sleeping for power optimization
    extern bool g_radioAsleep;  // Defined in main .ino file
    if (m_radioLED != nullptr) {
        static bool lastRadioAsleep = false;  // Cache last state (v2.9.5 optimization)
        if (g_radioAsleep != lastRadioAsleep) {  // Only update if state changed
            lastRadioAsleep = g_radioAsleep;
            if (g_radioAsleep) {
                lv_obj_clear_flag(m_radioLED, LV_OBJ_FLAG_HIDDEN);  // Show orange LED
            } else {
                lv_obj_add_flag(m_radioLED, LV_OBJ_FLAG_HIDDEN);     // Hide LED when awake
            }
        }
    }

    if (m_offlineMode) {
        // OFFLINE MODE: Hide bridge/app/radio/ID info, show "OFFLINE MODE" label
        if (m_bridgeLabel != nullptr) lv_obj_add_flag(m_bridgeLabel, LV_OBJ_FLAG_HIDDEN);
        if (m_bridgeLED != nullptr) lv_obj_add_flag(m_bridgeLED, LV_OBJ_FLAG_HIDDEN);
        if (m_radioLabel != nullptr) lv_obj_add_flag(m_radioLabel, LV_OBJ_FLAG_HIDDEN);
        if (m_radioLED != nullptr) lv_obj_add_flag(m_radioLED, LV_OBJ_FLAG_HIDDEN);
        if (m_reactLabel != nullptr) lv_obj_add_flag(m_reactLabel, LV_OBJ_FLAG_HIDDEN);
        if (m_reactLED != nullptr) lv_obj_add_flag(m_reactLED, LV_OBJ_FLAG_HIDDEN);
        if (m_remoteIdLabel != nullptr) lv_obj_add_flag(m_remoteIdLabel, LV_OBJ_FLAG_HIDDEN);

        // Show offline mode label
        if (m_offlineModeLabel != nullptr) lv_obj_clear_flag(m_offlineModeLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        // BRIDGE MODE: Show bridge/app/radio/ID info, hide "OFFLINE MODE" label
        if (m_bridgeLabel != nullptr) lv_obj_clear_flag(m_bridgeLabel, LV_OBJ_FLAG_HIDDEN);
        if (m_bridgeLED != nullptr) lv_obj_clear_flag(m_bridgeLED, LV_OBJ_FLAG_HIDDEN);
        if (m_radioLabel != nullptr) lv_obj_clear_flag(m_radioLabel, LV_OBJ_FLAG_HIDDEN);
        // Note: m_radioLED visibility controlled by g_radioAsleep state above
        if (m_reactLabel != nullptr) lv_obj_clear_flag(m_reactLabel, LV_OBJ_FLAG_HIDDEN);
        if (m_reactLED != nullptr) lv_obj_clear_flag(m_reactLED, LV_OBJ_FLAG_HIDDEN);
        if (m_remoteIdLabel != nullptr) lv_obj_clear_flag(m_remoteIdLabel, LV_OBJ_FLAG_HIDDEN);

        // Hide offline mode label
        if (m_offlineModeLabel != nullptr) lv_obj_add_flag(m_offlineModeLabel, LV_OBJ_FLAG_HIDDEN);

        // Update bridge status - show RSSI when connected, red LED when disconnected
        if (m_bridgeLabel != nullptr) {
            if (m_bridgeConnected) {
                // Connected - show RSSI value
                int8_t rssi = g_espnow.getLastRSSI();
                static int8_t lastRssi = 0;  // Cache last RSSI (v2.9.2 optimization)
                if (rssi != lastRssi) {  // Only update label if RSSI changed
                    lastRssi = rssi;
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%d", rssi);  // e.g., "-65"
                    lv_label_set_text(m_bridgeLabel, buf);
                }
            } else {
                // Disconnected - show "BR" label
                lv_label_set_text(m_bridgeLabel, "BR");
            }
        }

        // Update bridge LED - only show red when disconnected
        if (m_bridgeLED != nullptr) {
            if (m_bridgeConnected) {
                // Connected - hide LED (RSSI value shows connection strength)
                lv_obj_add_flag(m_bridgeLED, LV_OBJ_FLAG_HIDDEN);
            } else {
                // Disconnected - show red LED
                lv_obj_clear_flag(m_bridgeLED, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(m_bridgeLED, lv_color_hex(COLOR_LED_BAD), 0);
            }
        }

        // Update react LED
        if (m_reactLED != nullptr) {
            uint32_t color = m_reactConnected ? COLOR_LED_GOOD : COLOR_LED_BAD;
            static uint32_t lastReactLEDColor = 0xFFFFFFFF;  // Cache last react LED color (v2.9.3)
            if (color != lastReactLEDColor) {  // Only update color if changed
                lastReactLEDColor = color;
                lv_obj_set_style_bg_color(m_reactLED, lv_color_hex(color), 0);
            }
        }
    }
}

// Reset confirmation removed

// ============================================================================
// STATE HELPERS
// ============================================================================

TimerUIState TimerUI::determineUIState(uint16_t seconds, uint8_t flags) {
    // Check overlay states first
    if (m_pickerVisible) {
        return UI_STATE_TIME_PICKER;
    }
    // Reset confirmation removed

    // Normal timer states
    bool isRunning = (flags & FLAG_RUNNING) != 0;
    bool isExpired = (flags & FLAG_EXPIRED) != 0;

    if (isExpired || (seconds == 0 && !isRunning)) {
        return UI_STATE_EXPIRED;
    }

    if (isRunning) {
        return UI_STATE_RUNNING;
    }

    if (seconds > 0 && !isRunning) {
        return UI_STATE_PAUSED;
    }

    return UI_STATE_STOPPED;
}

BackgroundState TimerUI::determineBackgroundState(uint16_t seconds, uint8_t flags) {
    bool isRunning = (flags & FLAG_RUNNING) != 0;
    bool isExpired = (flags & FLAG_EXPIRED) != 0;

    if (isExpired || (seconds == 0 && isRunning)) {
        return BG_STATE_EXPIRED_PULSATE;
    }

    if (isRunning) {
        if (seconds <= 5) {
            return BG_STATE_CRITICAL;  // Red (5s or less)
        } else if (seconds <= 30) {
            return BG_STATE_WARNING;  // Yellow (30s or less)
        }
    }

    return BG_STATE_NORMAL;  // Black
}

uint32_t TimerUI::getTextColor(BackgroundState bgState) {
    switch (bgState) {
        case BG_STATE_WARNING:
        case BG_STATE_CRITICAL:
            return COLOR_TEXT_ON_YELLOW;  // Black on yellow/red
        case BG_STATE_EXPIRED_PULSATE:
        case BG_STATE_NORMAL:
        default:
            return COLOR_TEXT_ON_BLACK;  // White on black
    }
}

uint32_t TimerUI::getBackgroundColor(BackgroundState bgState) {
    switch (bgState) {
        case BG_STATE_WARNING:
            return COLOR_BG_WARNING;  // Yellow
        case BG_STATE_CRITICAL:
            return COLOR_BG_CRITICAL;  // Red
        case BG_STATE_EXPIRED_PULSATE:
            return COLOR_BG_CRITICAL;  // Red (will pulsate in animation)
        case BG_STATE_NORMAL:
        default:
            return COLOR_BG_NORMAL;  // Black
    }
}

// ============================================================================
// ANIMATION
// ============================================================================

void TimerUI::updatePulsatingAnimation() {
    static bool lastShowRed = false;  // Cache last state to avoid redundant updates

    // If not in pulsate mode, reset cache and return
    if (m_screen == nullptr || m_pulsateStartTime == 0) {
        lastShowRed = false;  // Reset cache when exiting pulsate mode
        return;
    }

    uint32_t elapsed = millis() - m_pulsateStartTime;

    // Simple 500ms flash: alternate between black and red every 500ms
    bool showRed = ((elapsed / 500) % 2) == 0;  // Toggle every 500ms

    // OPTIMIZATION: Only update if state changed
    if (showRed == lastShowRed) {
        return;  // No change, skip expensive LVGL updates
    }
    lastShowRed = showRed;

    uint32_t color = showRed ? COLOR_BG_CRITICAL : COLOR_BG_NORMAL;

    // Set full opacity (no fading, just instant switch)
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(m_screen, LV_OPA_COVER, 0);

    // Update text color to match background (contrast)
    uint32_t textColor = showRed ? COLOR_TEXT_ON_RED : COLOR_TEXT_ON_BLACK;
    if (m_timerLabel != nullptr) {
        lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(textColor), 0);
    }
    if (m_totalTimeLabel != nullptr) {
        // Total time label uses dimmed color
        uint32_t totalTimeColor = showRed ? 0x404040 : 0x808080;
        lv_obj_set_style_text_color(m_totalTimeLabel, lv_color_hex(totalTimeColor), 0);
    }
    if (m_statusLabel != nullptr) {
        lv_obj_set_style_text_color(m_statusLabel, lv_color_hex(textColor), 0);
    }
    if (m_clockLabel != nullptr) {
        lv_obj_set_style_text_color(m_clockLabel, lv_color_hex(textColor), 0);
    }
    if (m_batteryLabel != nullptr) {
        lv_obj_set_style_text_color(m_batteryLabel, lv_color_hex(textColor), 0);
    }
    if (m_remoteIdLabel != nullptr) {
        lv_obj_set_style_text_color(m_remoteIdLabel, lv_color_hex(textColor), 0);
    }
}

uint8_t TimerUI::calculatePulsateOpacity() {
    if (m_pulsateStartTime == 0) return 255;

    uint32_t elapsed = millis() - m_pulsateStartTime;
    uint32_t phase = elapsed % PULSATE_PERIOD_MS;

    float angle = (float)phase / PULSATE_PERIOD_MS * 2.0 * PI;
    float sinValue = sin(angle);
    float normalized = (sinValue + 1.0) / 2.0;

    return PULSATE_MIN_OPACITY + (uint8_t)((PULSATE_MAX_OPACITY - PULSATE_MIN_OPACITY) * normalized);
}

void TimerUI::updateProgressRingFlash() {
    static bool lastShowColor = false;  // Cache last state to avoid redundant updates

    // If not in flash mode, reset cache and return
    if (m_progressRing == nullptr || m_ringFlashStartTime == 0) {
        lastShowColor = false;  // Reset cache when exiting flash mode
        return;
    }

    uint32_t elapsed = millis() - m_ringFlashStartTime;

    // Flash every 1000ms: alternate between color and black
    bool showColor = ((elapsed / 1000) % 2) == 0;  // Toggle every 1000ms

    // OPTIMIZATION: Only update if state changed
    if (showColor == lastShowColor) {
        return;  // No change, skip expensive LVGL updates
    }
    lastShowColor = showColor;

    // Determine ring color based on remaining time
    uint32_t ringColor;
    if (showColor) {
        // Show the appropriate color based on remaining time
        if (m_currentSeconds <= 5) {
            ringColor = 0xFF0000;  // Red when 5 seconds or less
        } else if (m_currentSeconds <= 30) {
            ringColor = 0xFFCC00;  // Yellow when 30 seconds or less
        } else {
            ringColor = 0x00FF00;  // Green when more than 30 seconds
        }
    } else {
        // Show black (ring disappears)
        ringColor = 0x000000;
    }

    // Update MAIN arc color (background/remaining arc)
    lv_obj_set_style_arc_color(m_progressRing, lv_color_hex(ringColor), LV_PART_MAIN);
}

// ============================================================================
// MOTION DETECTION & DISPLAY AUTO-OFF
// ============================================================================
// REMOVED: Motion-based power management (v2.7.0 switched to time-based)
// ============================================================================

// void TimerUI::checkMotion() {
//     uint32_t now = millis();
//
//     // Check motion at specified interval (500ms)
//     if (now - m_lastMotionCheckTime < MOTION_CHECK_INTERVAL_MS) {
//         return;
//     }
//     m_lastMotionCheckTime = now;
//
//     // Read current accelerometer values from global Accel struct
//     float accelX = Accel.x;
//     float accelY = Accel.y;
//     float accelZ = Accel.z;
//
//     // Calculate motion magnitude (change in acceleration)
//     float dx = accelX - m_lastAccelX;
//     float dy = accelY - m_lastAccelY;
//     float dz = accelZ - m_lastAccelZ;
//     float motionMagnitude = sqrt(dx*dx + dy*dy + dz*dz);
//
//     // Update last values
//     m_lastAccelX = accelX;
//     m_lastAccelY = accelY;
//     m_lastAccelZ = accelZ;
//
//     // Check if motion exceeds threshold (major movement)
//     if (motionMagnitude > MOTION_THRESHOLD) {
//         m_lastMotionTime = now;
//
//         // If display was off, turn it back on
//         if (!m_displayOn) {
//             turnDisplayOn();
//         }
//     }
//
//     // Check if we should turn off display due to inactivity
//     // Only turn off if timer is stopped or paused (not running)
//     if (m_displayOn && !m_isRunning) {
//         uint32_t inactiveTime = now - m_lastMotionTime;
//         if (inactiveTime >= MOTION_INACTIVITY_TIMEOUT_MS) {
//             turnDisplayOff();
//         }
//     }
// }
//
// void TimerUI::turnDisplayOff() {
//     if (!m_displayOn) return;  // Already off
//
//     printf("[MOTION] Display OFF - no motion for %d seconds\n", MOTION_INACTIVITY_TIMEOUT_MS / 1000);
//
//     // Turn off backlight (set to 0%)
//     Set_Backlight(0);
//
//     m_displayOn = false;
// }
//
// void TimerUI::turnDisplayOn() {
//     if (m_displayOn) return;  // Already on
//
//     printf("[MOTION] Display ON - motion detected or wake event\n");
//
//     // Restore backlight to saved brightness level
//     uint8_t savedBrightness = m_preferences.getUChar("brightness", 100);
//     Set_Backlight(savedBrightness);
//
//     m_displayOn = true;
//     m_lastMotionTime = millis();  // Reset inactivity timer
// }

// ============================================================================
// AUTO-ROTATION
// ============================================================================

// REMOVED: Gyroscope-based auto-rotation (v2.7.0 - gyroscope removed for power savings)
// void TimerUI::checkOrientation() {
//     if (!m_autoRotationEnabled) return;  // Feature disabled
//
//     uint32_t now = millis();
//
//     // Read Z-axis accelerometer from global Accel struct (determines orientation)
//     float accelZ = Accel.z;
//
//     // Check if device is upside down (Z < -0.7g)
//     bool currentlyUpsideDown = (accelZ < ROTATION_Z_THRESHOLD);
//
//     // Detect orientation change
//     if (currentlyUpsideDown != m_isUpsideDown) {
//         // Orientation changed - start hold timer
//         m_isUpsideDown = currentlyUpsideDown;
//         m_upsideDownStartTime = now;
//     } else {
//         // Same orientation - check if held long enough
//         uint32_t holdTime = now - m_upsideDownStartTime;
//
//         if (holdTime >= ROTATION_HOLD_TIME_MS) {
//             // Held for 1 second - check if rotation needed
//             if (currentlyUpsideDown && !m_displayRotated) {
//                 // Upside down, not rotated yet → rotate 180°
//                 rotateDisplay(true);
//             } else if (!currentlyUpsideDown && m_displayRotated) {
//                 // Right-side up, currently rotated → rotate back to 0°
//                 rotateDisplay(false);
//             }
//         }
//     }
// }

// REMOVED: Auto-rotation function (gyroscope removed in v2.7.0)
// void TimerUI::rotateDisplay(bool upsideDown) {
//     if (m_displayRotated == upsideDown) return;  // Already in desired state

//     printf("[ROTATION] Rotating display %d degrees\n", upsideDown ? 180 : 0);

//     // Get LVGL display object
//     lv_disp_t* disp = lv_disp_get_default();
//     if (disp == nullptr) return;

//     // Rotate display
//     if (upsideDown) {
//         lv_disp_set_rotation(disp, LV_DISP_ROT_180);
//     } else {
//         lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);
//     }

//     m_displayRotated = upsideDown;
// }

// ============================================================================
// TIME PICKER
// ============================================================================

void TimerUI::showTimePicker() {
    if (m_pickerContainer == nullptr) return;

    // Set rollers to current preset value
    uint16_t minutes = m_presetSeconds / 60;
    uint16_t seconds = m_presetSeconds % 60;

    if (m_pickerRollerMin != nullptr) {
        lv_roller_set_selected(m_pickerRollerMin, minutes, LV_ANIM_OFF);
    }
    if (m_pickerRollerSec != nullptr) {
        lv_roller_set_selected(m_pickerRollerSec, seconds, LV_ANIM_OFF);
    }

    // Show picker
    lv_obj_clear_flag(m_pickerContainer, LV_OBJ_FLAG_HIDDEN);
    m_pickerVisible = true;
    m_uiState = UI_STATE_TIME_PICKER;

    DEBUG_UI_PRINTLN("Time picker shown");
}

void TimerUI::hideTimePicker() {
    if (m_pickerContainer == nullptr) return;

    lv_obj_add_flag(m_pickerContainer, LV_OBJ_FLAG_HIDDEN);
    m_pickerVisible = false;

    // Force UI state update (update() might not be called if timer isn't changing)
    m_uiState = determineUIState(m_currentSeconds, (m_isRunning ? FLAG_RUNNING : 0) | (m_currentSeconds == 0 ? FLAG_EXPIRED : 0));
    // No button layout update needed - using physical buttons only

    DEBUG_UI_PRINTLN("Time picker hidden");
}

void TimerUI::applyTimePickerValue() {
    if (m_pickerRollerMin == nullptr || m_pickerRollerSec == nullptr) return;

    // Get selected values
    uint16_t minutes = lv_roller_get_selected(m_pickerRollerMin);
    uint16_t seconds = lv_roller_get_selected(m_pickerRollerSec);
    uint16_t totalSeconds = minutes * 60 + seconds;

    m_presetSeconds = totalSeconds;

    printf("[UI] Time picker value selected: %d:%02d (%d seconds)\n",
           minutes, seconds, totalSeconds);

    // CRITICAL LIMITATION: Our 6-byte button packet cannot transmit the time value!
    // The React app will receive button ID but NOT the selected time.
    // Workaround options:
    // 1. React app maintains preset time and updates it via web interface
    // 2. Use preset increment buttons (+1m, +5m, etc.) instead of time picker
    // 3. Extend protocol to support time value packets

    // Send picker OK button (React app needs special handling for this)
    if (m_touchCallback != nullptr) {
        m_touchCallback(BTN_ID_TOUCH_PICKER_OK);  // ID 15
    }

    hideTimePicker();
}

// Reset confirmation and touch button handlers removed - using physical buttons only
// Time picker OK button handler kept for time picker functionality
void TimerUI::onPickerOkButtonClicked(lv_event_t* e) {
    DEBUG_UI_PRINTLN("Picker OK clicked");
    if (s_instance != nullptr) {
        s_instance->applyTimePickerValue();
    }
}

// =============================================================================
// PRESET SELECTION SCREEN
// =============================================================================

void TimerUI::createPresetsScreen() {
    // Create full-screen container
    m_presetsScreen = lv_obj_create(NULL);
    lv_obj_set_size(m_presetsScreen, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(m_presetsScreen, lv_color_hex(COLOR_PICKER_BG), LV_PART_MAIN);
    lv_obj_clear_flag(m_presetsScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    m_presetsTitle = lv_label_create(m_presetsScreen);
    lv_label_set_text(m_presetsTitle, "Presets");
    lv_obj_set_style_text_color(m_presetsTitle, lv_color_hex(COLOR_PICKER_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_presetsTitle, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(m_presetsTitle, LV_ALIGN_TOP_MID, 0, 20);

    // Last Reset button (prominent, orange, shown only when available)
    m_lastResetButton = lv_btn_create(m_presetsScreen);
    lv_obj_set_size(m_lastResetButton, 250, 55);
    lv_obj_align(m_lastResetButton, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(m_lastResetButton, lv_color_hex(COLOR_LED_WARNING), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_lastResetButton, lv_color_hex(0xFF8800), LV_STATE_PRESSED);
    lv_obj_add_event_cb(m_lastResetButton, onLastResetButtonClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(m_lastResetButton, LV_OBJ_FLAG_HIDDEN);  // Initially hidden

    lv_obj_t* lastResetLabel = lv_label_create(m_lastResetButton);
    lv_label_set_text(lastResetLabel, LV_SYMBOL_REFRESH " Last: --:--");
    lv_obj_set_style_text_color(lastResetLabel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_center(lastResetLabel);

    // Preset buttons in 3x2 grid
    const uint16_t presetTimes[6] = {
        PRESET_TIME_1MIN, PRESET_TIME_2MIN, PRESET_TIME_3MIN,
        PRESET_TIME_5MIN, PRESET_TIME_10MIN, PRESET_TIME_15MIN
    };
    const char* presetLabels[6] = {"1:00", "2:00", "3:00", "5:00", "10:00", "15:00"};

    int16_t btnW = 110;
    int16_t btnH = 80;
    int16_t startX = (DISPLAY_WIDTH - (3 * btnW + 2 * 20)) / 2;
    int16_t startY = 145;
    int16_t spacingX = 20;
    int16_t spacingY = 20;

    for (int i = 0; i < 6; i++) {
        int row = i / 3;
        int col = i % 3;
        int16_t x = startX + col * (btnW + spacingX);
        int16_t y = startY + row * (btnH + spacingY);

        m_presetButtons[i] = lv_btn_create(m_presetsScreen);
        lv_obj_set_pos(m_presetButtons[i], x, y);
        lv_obj_set_size(m_presetButtons[i], btnW, btnH);
        lv_obj_set_style_bg_color(m_presetButtons[i], lv_color_hex(COLOR_BTN_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_presetButtons[i], lv_color_hex(COLOR_BTN_SECONDARY), LV_STATE_PRESSED);
        lv_obj_add_event_cb(m_presetButtons[i], onPresetButtonClicked, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        lv_obj_t* label = lv_label_create(m_presetButtons[i]);
        lv_label_set_text(label, presetLabels[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_BTN_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_center(label);
    }

    // Back button
    m_presetsBackButton = lv_btn_create(m_presetsScreen);
    lv_obj_set_size(m_presetsBackButton, 120, 50);
    lv_obj_align(m_presetsBackButton, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(m_presetsBackButton, lv_color_hex(COLOR_BTN_SECONDARY), LV_PART_MAIN);
    lv_obj_add_event_cb(m_presetsBackButton, [](lv_event_t* e) {
        if (s_instance) s_instance->hidePresets();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* backLabel = lv_label_create(m_presetsBackButton);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(backLabel, lv_color_hex(COLOR_PICKER_TEXT), LV_PART_MAIN);
    lv_obj_center(backLabel);

    printf("[UI] Presets screen created\n");
}

void TimerUI::showPresets() {
    printf("[UI] Showing presets screen\n");

    // Update last reset button visibility and text
    if (m_hasLastResetValue && m_lastResetValue > 0) {
        lv_obj_clear_flag(m_lastResetButton, LV_OBJ_FLAG_HIDDEN);

        uint16_t mins = m_lastResetValue / 60;
        uint16_t secs = m_lastResetValue % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " Last: %02d:%02d", mins, secs);

        lv_obj_t* label = lv_obj_get_child(m_lastResetButton, 0);
        lv_label_set_text(label, buf);

        printf("[UI] Last reset button enabled: %d seconds\n", m_lastResetValue);
    } else {
        lv_obj_add_flag(m_lastResetButton, LV_OBJ_FLAG_HIDDEN);
        printf("[UI] Last reset button hidden (no value)\n");
    }

    // Load presets screen
    lv_scr_load(m_presetsScreen);
    m_uiState = UI_STATE_PRESETS;
}

void TimerUI::hidePresets() {
    printf("[UI] Hiding presets screen\n");
    lv_scr_load(m_screen);
    m_uiState = UI_STATE_STOPPED;  // Will be updated by next timer state packet
}

void TimerUI::onPresetSelected(uint16_t seconds) {
    printf("[UI] Preset selected: %d seconds\n", seconds);

    if (m_offlineMode) {
        // OFFLINE MODE: Set timer locally
        m_offlineSeconds = seconds;
        m_offlinePreset = seconds;  // Remember for reset
        m_offlineRunning = false;
        printf("[OFFLINE] Set timer to %d seconds\n", seconds);

        // Return to main screen FIRST (before updating UI)
        hidePresets();

        // Now update the display (after main screen is loaded)
        update(m_offlineSeconds, 0);
    } else {
        // BRIDGE MODE: Send SET_TIMER command to React app via ESP-NOW
        g_espnow.sendSetTimer(seconds);

        // Return to main screen
        hidePresets();
    }
}

void TimerUI::onPresetButtonClicked(lv_event_t* e) {
    uint32_t presetIndex = (uint32_t)lv_event_get_user_data(e);

    const uint16_t presetTimes[6] = {
        PRESET_TIME_1MIN, PRESET_TIME_2MIN, PRESET_TIME_3MIN,
        PRESET_TIME_5MIN, PRESET_TIME_10MIN, PRESET_TIME_15MIN
    };

    if (s_instance && presetIndex < 6) {
        s_instance->onPresetSelected(presetTimes[presetIndex]);
    }
}

void TimerUI::onLastResetButtonClicked(lv_event_t* e) {
    if (s_instance && s_instance->m_hasLastResetValue) {
        printf("[UI] Last reset selected: %d seconds (UNDO)\n", s_instance->m_lastResetValue);
        s_instance->onPresetSelected(s_instance->m_lastResetValue);
        // Optionally clear after use: s_instance->m_hasLastResetValue = false;
    }
}

// =============================================================================
// DEVICE SETTINGS SCREEN
// =============================================================================

void TimerUI::createSettingsScreen() {
    // Create full-screen container
    m_settingsScreen = lv_obj_create(NULL);
    lv_obj_set_size(m_settingsScreen, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(m_settingsScreen, lv_color_hex(COLOR_BG_NORMAL), LV_PART_MAIN);
    lv_obj_clear_flag(m_settingsScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    m_settingsTitle = lv_label_create(m_settingsScreen);
    lv_label_set_text(m_settingsTitle, "Settings");
    lv_obj_set_style_text_color(m_settingsTitle, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsTitle, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(m_settingsTitle, LV_ALIGN_TOP_MID, 0, 10);

    // Scrollable container for content
    m_settingsContainer = lv_obj_create(m_settingsScreen);
    lv_obj_set_size(m_settingsContainer, DISPLAY_WIDTH - 20, DISPLAY_HEIGHT - 90);
    lv_obj_align(m_settingsContainer, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_flex_flow(m_settingsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(m_settingsContainer, 3, 0);
    lv_obj_clear_flag(m_settingsContainer, LV_OBJ_FLAG_SCROLLABLE);  // Disable scroll for now
    lv_obj_set_style_bg_color(m_settingsContainer, lv_color_hex(COLOR_BG_NORMAL), LV_PART_MAIN);
    lv_obj_set_style_border_width(m_settingsContainer, 0, LV_PART_MAIN);

    // === Device Info Section ===
    lv_obj_t* infoHeader = lv_label_create(m_settingsContainer);
    lv_label_set_text(infoHeader, "=== Device Info ===");
    lv_obj_set_style_text_color(infoHeader, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(infoHeader, &lv_font_montserrat_14, LV_PART_MAIN);

    m_settingsBatteryLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsBatteryLabel, "Battery: ---%");
    lv_obj_set_style_text_color(m_settingsBatteryLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsBatteryLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsFirmwareLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsFirmwareLabel, "Firmware: " FIRMWARE_VERSION);
    lv_obj_set_style_text_color(m_settingsFirmwareLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsFirmwareLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsModeLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsModeLabel, m_offlineMode ? "Mode: Offline" : "Mode: Bridge");
    lv_obj_set_style_text_color(m_settingsModeLabel, lv_color_hex(m_offlineMode ? COLOR_LED_WARNING : COLOR_LED_GOOD), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsModeLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsRemoteIDLabel = lv_label_create(m_settingsContainer);
    char buf[32];
    snprintf(buf, sizeof(buf), "Remote ID: %d", REMOTE_ID);
    lv_label_set_text(m_settingsRemoteIDLabel, buf);
    lv_obj_set_style_text_color(m_settingsRemoteIDLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsRemoteIDLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsUptimeLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsUptimeLabel, "Uptime: 00:00:00");
    lv_obj_set_style_text_color(m_settingsUptimeLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsUptimeLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsConnectionLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsConnectionLabel, "Status: Disconnected");
    lv_obj_set_style_text_color(m_settingsConnectionLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsConnectionLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsBridgeMACLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsBridgeMACLabel, "Bridge: --:--:--:--:--:--");
    lv_obj_set_style_text_color(m_settingsBridgeMACLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsBridgeMACLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsChannelLabel = lv_label_create(m_settingsContainer);
    snprintf(buf, sizeof(buf), "ESP-NOW Ch: %d", ESPNOW_CHANNEL);
    lv_label_set_text(m_settingsChannelLabel, buf);
    lv_obj_set_style_text_color(m_settingsChannelLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsChannelLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    m_settingsRAMLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(m_settingsRAMLabel, "Free RAM: --- KB");
    lv_obj_set_style_text_color(m_settingsRAMLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsRAMLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    // === Brightness Control ===
    lv_obj_t* brightnessHeader = lv_label_create(m_settingsContainer);
    lv_label_set_text(brightnessHeader, "=== Brightness ===");
    lv_obj_set_style_text_color(brightnessHeader, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(brightnessHeader, &lv_font_montserrat_14, LV_PART_MAIN);

    m_settingsBrightnessSlider = lv_slider_create(m_settingsContainer);
    lv_obj_set_width(m_settingsBrightnessSlider, DISPLAY_WIDTH - 60);
    lv_slider_set_range(m_settingsBrightnessSlider, 10, 100);
    lv_slider_set_value(m_settingsBrightnessSlider, BACKLIGHT_ACTIVE, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_settingsBrightnessSlider, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_settingsBrightnessSlider, lv_color_hex(COLOR_BTN_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_settingsBrightnessSlider, lv_color_hex(COLOR_BTN_PRIMARY), LV_PART_KNOB);

    m_settingsBrightnessValue = lv_label_create(m_settingsContainer);
    snprintf(buf, sizeof(buf), "%d%%", BACKLIGHT_ACTIVE);
    lv_label_set_text(m_settingsBrightnessValue, buf);
    lv_obj_set_style_text_color(m_settingsBrightnessValue, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsBrightnessValue, &lv_font_montserrat_12, LV_PART_MAIN);

    // === Volume Control ===
    lv_obj_t* volumeHeader = lv_label_create(m_settingsContainer);
    lv_label_set_text(volumeHeader, "=== Volume ===");
    lv_obj_set_style_text_color(volumeHeader, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(volumeHeader, &lv_font_montserrat_14, LV_PART_MAIN);

    m_settingsVolumeSlider = lv_slider_create(m_settingsContainer);
    lv_obj_set_width(m_settingsVolumeSlider, DISPLAY_WIDTH - 60);
    lv_slider_set_range(m_settingsVolumeSlider, 0, 100);
    lv_slider_set_value(m_settingsVolumeSlider, m_systemVolume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_settingsVolumeSlider, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_settingsVolumeSlider, lv_color_hex(COLOR_BTN_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_settingsVolumeSlider, lv_color_hex(COLOR_BTN_PRIMARY), LV_PART_KNOB);

    m_settingsVolumeValue = lv_label_create(m_settingsContainer);
    snprintf(buf, sizeof(buf), "%d%%", m_systemVolume);
    lv_label_set_text(m_settingsVolumeValue, buf);
    lv_obj_set_style_text_color(m_settingsVolumeValue, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsVolumeValue, &lv_font_montserrat_12, LV_PART_MAIN);

    // === Auto-Rotation ===
    // REMOVED: Auto-rotation UI elements (gyroscope removed in v2.7.0)
    // lv_obj_t* autoRotateHeader = lv_label_create(m_settingsContainer);
    // lv_label_set_text(autoRotateHeader, "=== Auto-Rotation ===");
    // lv_obj_set_style_text_color(autoRotateHeader, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    // lv_obj_set_style_text_font(autoRotateHeader, &lv_font_montserrat_14, LV_PART_MAIN);

    // m_settingsAutoRotateValue = lv_label_create(m_settingsContainer);
    // lv_label_set_text(m_settingsAutoRotateValue, m_autoRotationEnabled ? "ON" : "OFF");
    // lv_obj_set_style_text_color(m_settingsAutoRotateValue, lv_color_hex(m_autoRotationEnabled ? COLOR_LED_GOOD : COLOR_LED_BAD), LV_PART_MAIN);
    // lv_obj_set_style_text_font(m_settingsAutoRotateValue, &lv_font_montserrat_12, LV_PART_MAIN);

    // === Mode Indicator & Instructions ===
    m_settingsModeIndicator = lv_label_create(m_settingsScreen);
    lv_label_set_text(m_settingsModeIndicator, "BTN1/3: BRIGHTNESS");
    lv_obj_set_style_text_color(m_settingsModeIndicator, lv_color_hex(COLOR_LED_WARNING), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsModeIndicator, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(m_settingsModeIndicator, LV_ALIGN_BOTTOM_MID, 0, -30);

    m_settingsInstructions = lv_label_create(m_settingsScreen);
    lv_label_set_text(m_settingsInstructions, "Swipe right to exit");
    lv_obj_set_style_text_color(m_settingsInstructions, lv_color_hex(COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_settingsInstructions, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(m_settingsInstructions, LV_ALIGN_BOTTOM_MID, 0, -10);

    printf("[UI] Settings screen created\n");
}

void TimerUI::showSettings() {
    printf("[UI] Showing settings screen\n");
    lv_scr_load(m_settingsScreen);
    m_uiState = UI_STATE_SETTINGS;
    updateSettingsScreen();
}

void TimerUI::hideSettings() {
    printf("[UI] Hiding settings screen\n");
    lv_scr_load(m_screen);
    m_uiState = UI_STATE_STOPPED;  // Will be updated by next timer state packet
}

void TimerUI::updateSettingsScreen() {
    if (!m_settingsScreen) return;

    char buf[64];

    // Update battery
    snprintf(buf, sizeof(buf), "Battery: %d%% (%.2fV)",
             m_batteryPercent, 3.7f + (m_batteryPercent / 100.0f) * 0.5f);  // Estimate voltage
    lv_label_set_text(m_settingsBatteryLabel, buf);

    // Update mode label
    lv_label_set_text(m_settingsModeLabel, m_offlineMode ? "Mode: Offline" : "Mode: Bridge");
    lv_obj_set_style_text_color(m_settingsModeLabel, lv_color_hex(m_offlineMode ? COLOR_LED_WARNING : COLOR_LED_GOOD), LV_PART_MAIN);

    // Update uptime
    uint32_t uptimeSec = millis() / 1000;
    uint32_t hours = uptimeSec / 3600;
    uint32_t minutes = (uptimeSec % 3600) / 60;
    uint32_t seconds = uptimeSec % 60;
    snprintf(buf, sizeof(buf), "Uptime: %02lu:%02lu:%02lu", hours, minutes, seconds);
    lv_label_set_text(m_settingsUptimeLabel, buf);

    // Update connection status
    if (m_bridgeConnected && m_reactConnected) {
        lv_label_set_text(m_settingsConnectionLabel, "Status: Connected");
    } else if (m_bridgeConnected) {
        lv_label_set_text(m_settingsConnectionLabel, "Status: Bridge Only");
    } else {
        lv_label_set_text(m_settingsConnectionLabel, "Status: Disconnected");
    }

    // Update bridge MAC (would need to be stored when bridge connects)
    lv_label_set_text(m_settingsBridgeMACLabel, "Bridge: XX:XX:XX:XX:XX:XX");

    // Update free RAM
    uint32_t freeRAM = esp_get_free_heap_size() / 1024;
    snprintf(buf, sizeof(buf), "Free RAM: %lu KB", freeRAM);
    lv_label_set_text(m_settingsRAMLabel, buf);

    // Update slider values
    snprintf(buf, sizeof(buf), "%d%%", lv_slider_get_value(m_settingsBrightnessSlider));
    lv_label_set_text(m_settingsBrightnessValue, buf);

    snprintf(buf, sizeof(buf), "%d%%", m_systemVolume);
    lv_label_set_text(m_settingsVolumeValue, buf);
    lv_slider_set_value(m_settingsVolumeSlider, m_systemVolume, LV_ANIM_OFF);

    // REMOVED: Update auto-rotation status (gyroscope removed in v2.7.0)
    // lv_label_set_text(m_settingsAutoRotateValue, m_autoRotationEnabled ? "ON" : "OFF");
    // lv_obj_set_style_text_color(m_settingsAutoRotateValue, lv_color_hex(m_autoRotationEnabled ? COLOR_LED_GOOD : COLOR_LED_BAD), LV_PART_MAIN);

    // Update mode indicator
    if (m_currentSettingMode == SETTING_MODE_BRIGHTNESS) {
        lv_label_set_text(m_settingsModeIndicator, "BTN1/3: BRIGHTNESS");
    } else if (m_currentSettingMode == SETTING_MODE_VOLUME) {
        lv_label_set_text(m_settingsModeIndicator, "BTN1/3: VOLUME");
    } else {
        lv_label_set_text(m_settingsModeIndicator, "BTN1/3: AUTO-ROTATE");
    }
}

void TimerUI::adjustSetting(int8_t direction) {
    if (m_currentSettingMode == SETTING_MODE_BRIGHTNESS) {
        // Adjust brightness
        int16_t current = lv_slider_get_value(m_settingsBrightnessSlider);
        int16_t newValue = current + (direction * 10);  // 10% steps

        if (newValue < 10) newValue = 10;
        if (newValue > 100) newValue = 100;

        lv_slider_set_value(m_settingsBrightnessSlider, newValue, LV_ANIM_ON);
        Set_Backlight(newValue);  // Apply immediately

        // Save to NVS for persistence across reboots
        m_preferences.putUChar("brightness", (uint8_t)newValue);
        printf("[UI Settings] Brightness adjusted to %d%% (saved to NVS)\n", newValue);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", newValue);
        lv_label_set_text(m_settingsBrightnessValue, buf);
    } else if (m_currentSettingMode == SETTING_MODE_VOLUME) {
        // Adjust volume
        int16_t newValue = m_systemVolume + (direction * 10);  // 10% steps

        if (newValue < 0) newValue = 0;
        if (newValue > 100) newValue = 100;

        m_systemVolume = (uint8_t)newValue;
        lv_slider_set_value(m_settingsVolumeSlider, newValue, LV_ANIM_ON);

        // Send to React app via ESP-NOW (only in bridge mode)
        if (!m_offlineMode) {
            g_espnow.sendSetVolume(m_systemVolume);
        }

        // Save to NVS for persistence across reboots
        m_preferences.putUChar("volume", m_systemVolume);
        printf("[UI Settings] Volume adjusted to %d%% (saved to NVS)\n", m_systemVolume);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", newValue);
        lv_label_set_text(m_settingsVolumeValue, buf);
    } // REMOVED: Auto-rotation adjustment (gyroscope removed in v2.7.0)
    // else {
    //     // Toggle auto-rotation (direction doesn't matter for boolean)
    //     m_autoRotationEnabled = !m_autoRotationEnabled;

    //     // Save to NVS for persistence across reboots
    //     m_preferences.putBool("autoRotate", m_autoRotationEnabled);
    //     printf("[UI Settings] Auto-rotation toggled to %s (saved to NVS)\n",
    //            m_autoRotationEnabled ? "ON" : "OFF");

    //     // Update display
    //     lv_label_set_text(m_settingsAutoRotateValue, m_autoRotationEnabled ? "ON" : "OFF");
    //     lv_obj_set_style_text_color(m_settingsAutoRotateValue,
    //                                 lv_color_hex(m_autoRotationEnabled ? COLOR_LED_GOOD : COLOR_LED_BAD),
    //                                 LV_PART_MAIN);
    // }
}

void TimerUI::toggleSettingMode() {
    // REMOVED: Auto-rotate mode (gyroscope removed in v2.7.0)
    // Cycle through brightness and volume only
    if (m_currentSettingMode == SETTING_MODE_BRIGHTNESS) {
        m_currentSettingMode = SETTING_MODE_VOLUME;
        lv_label_set_text(m_settingsModeIndicator, "BTN1/3: VOLUME");
    } else {
        m_currentSettingMode = SETTING_MODE_BRIGHTNESS;
        lv_label_set_text(m_settingsModeIndicator, "BTN1/3: BRIGHTNESS");
    }

    const char* modeNames[] = {"BRIGHTNESS", "VOLUME"};
    printf("[UI Settings] Mode switched to: %s\n", modeNames[m_currentSettingMode]);
}

// =============================================================================
// GESTURE HANDLERS
// =============================================================================

void TimerUI::handleTripleTap() {
    printf("[UI] Triple-tap detected!\n");

    // Only open settings if on main screen
    if (m_uiState == UI_STATE_STOPPED || m_uiState == UI_STATE_RUNNING ||
        m_uiState == UI_STATE_PAUSED || m_uiState == UI_STATE_EXPIRED) {
        showSettings();
    }
}

void TimerUI::handleSwipe(int16_t dx, int16_t dy) {
    printf("[UI] handleSwipe called: dx=%d, dy=%d, current state=%d\n", dx, dy, m_uiState);

    // Left-to-right swipe on settings screen = exit
    if (m_uiState == UI_STATE_SETTINGS) {
        printf("[UI] In settings screen, checking swipe conditions...\n");
        printf("[UI]   dx > SWIPE_THRESHOLD? %d > %d = %s\n", dx, SWIPE_THRESHOLD, (dx > SWIPE_THRESHOLD) ? "YES" : "NO");
        printf("[UI]   abs(dy) < SWIPE_THRESHOLD/2? %d < %d = %s\n", abs(dy), SWIPE_THRESHOLD / 2, (abs(dy) < SWIPE_THRESHOLD / 2) ? "YES" : "NO");

        if (dx > SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD / 2) {
            printf("[UI] *** Left-to-right swipe confirmed - exiting settings ***\n");
            hideSettings();
        } else {
            printf("[UI] Swipe conditions not met - staying in settings\n");
        }
    } else {
        printf("[UI] Not in settings screen (state=%d), ignoring swipe\n", m_uiState);
    }
}

// =============================================================================
// OFFLINE MODE TOGGLE
// =============================================================================

void TimerUI::toggleOfflineMode() {
    printf("[MODE] Toggling mode: %s → %s\n",
           m_offlineMode ? "Offline" : "Bridge",
           m_offlineMode ? "Bridge" : "Offline");

    // Toggle mode
    m_offlineMode = !m_offlineMode;

    // Reset timer state when switching modes
    if (m_offlineMode) {
        // Switched TO offline mode
        m_offlineSeconds = 0;
        m_offlinePreset = 120;  // Default 2 minutes
        m_offlineRunning = false;
        m_offlineLastTick = 0;
        printf("[MODE] Switched to OFFLINE MODE - timer reset to 0:00\n");
    } else {
        // Switched TO bridge mode
        printf("[MODE] Switched to BRIDGE MODE - timer reset to 0:00\n");
    }

    // Update UI immediately
    if (m_offlineMode) {
        update(m_offlineSeconds, m_offlineRunning ? FLAG_RUNNING : 0);
    } else {
        update(0, 0);  // Reset display in bridge mode
    }
    updateInfoSection();

    printf("[MODE] Mode switch complete\n");
}

// =============================================================================
// RESET CONFIRMATION OVERLAY (DISABLED - Reset now works immediately)
// =============================================================================

/*
void TimerUI::createResetConfirmOverlay() {
    // Semi-transparent overlay
    m_resetConfirmOverlay = lv_obj_create(m_screen);  // Created on main screen
    lv_obj_set_size(m_resetConfirmOverlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(m_resetConfirmOverlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_resetConfirmOverlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(m_resetConfirmOverlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(m_resetConfirmOverlay, LV_OBJ_FLAG_HIDDEN);  // Initially hidden
    lv_obj_clear_flag(m_resetConfirmOverlay, LV_OBJ_FLAG_SCROLLABLE);

    // Centered panel
    m_resetConfirmPanel = lv_obj_create(m_resetConfirmOverlay);
    lv_obj_set_size(m_resetConfirmPanel, 300, 200);
    lv_obj_center(m_resetConfirmPanel);
    lv_obj_set_style_bg_color(m_resetConfirmPanel, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_border_width(m_resetConfirmPanel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(m_resetConfirmPanel, lv_color_hex(COLOR_LED_WARNING), LV_PART_MAIN);
    lv_obj_clear_flag(m_resetConfirmPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    m_resetConfirmTitle = lv_label_create(m_resetConfirmPanel);
    lv_label_set_text(m_resetConfirmTitle, "Reset Timer?");
    lv_obj_set_style_text_color(m_resetConfirmTitle, lv_color_hex(COLOR_LED_WARNING), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_resetConfirmTitle, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(m_resetConfirmTitle, LV_ALIGN_TOP_MID, 0, 20);

    // Timer value being reset
    m_resetConfirmValueLabel = lv_label_create(m_resetConfirmPanel);
    lv_label_set_text(m_resetConfirmValueLabel, "00:00");
    lv_obj_set_style_text_color(m_resetConfirmValueLabel, lv_color_hex(COLOR_TEXT_ON_BLACK), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_resetConfirmValueLabel, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(m_resetConfirmValueLabel, LV_ALIGN_CENTER, 0, -10);

    // Progress bar for hold timing
    m_resetConfirmProgress = lv_bar_create(m_resetConfirmPanel);
    lv_obj_set_size(m_resetConfirmProgress, 250, 20);
    lv_obj_align(m_resetConfirmProgress, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(m_resetConfirmProgress, 0, 100);
    lv_bar_set_value(m_resetConfirmProgress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_resetConfirmProgress, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_resetConfirmProgress, lv_color_hex(COLOR_LED_WARNING), LV_PART_INDICATOR);

    // Instructions
    m_resetConfirmInstructions = lv_label_create(m_resetConfirmPanel);
    lv_label_set_text(m_resetConfirmInstructions, "Hold BTN1 for 1s or press BTN3");
    lv_obj_set_style_text_color(m_resetConfirmInstructions, lv_color_hex(COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_resetConfirmInstructions, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(m_resetConfirmInstructions, LV_ALIGN_BOTTOM_MID, 0, -10);

    printf("[UI] Reset confirmation overlay created\n");
}

void TimerUI::showResetConfirm(uint16_t currentValue) {
    printf("[UI] Showing reset confirmation: %d seconds\n", currentValue);

    m_resetConfirmVisible = true;
    m_resetConfirmStartTime = millis();
    m_resetConfirmValue = currentValue;

    // Update timer value display
    uint16_t mins = currentValue / 60;
    uint16_t secs = currentValue % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    lv_label_set_text(m_resetConfirmValueLabel, buf);

    // Reset progress bar
    lv_bar_set_value(m_resetConfirmProgress, 0, LV_ANIM_OFF);

    // Show overlay
    lv_obj_clear_flag(m_resetConfirmOverlay, LV_OBJ_FLAG_HIDDEN);

    // Change UI state
    m_uiState = UI_STATE_RESET_CONFIRM;
}

void TimerUI::hideResetConfirm() {
    printf("[UI] Hiding reset confirmation\n");

    m_resetConfirmVisible = false;
    lv_obj_add_flag(m_resetConfirmOverlay, LV_OBJ_FLAG_HIDDEN);

    // Return to previous state (will be updated by next timer packet)
    m_uiState = UI_STATE_STOPPED;
}

void TimerUI::updateResetConfirmProgress() {
    if (!m_resetConfirmVisible) return;

    // Check if Button 1 is still being held
    bool buttonStillPressed = g_buttons.isPressed(BTN_ID_PHYSICAL_1);

    // If button was released before completion, cancel reset
    if (!buttonStillPressed) {
        printf("[UI] Reset button released before hold complete - canceling reset\n");
        hideResetConfirm();
        return;
    }

    // Button is still pressed - update progress
    uint32_t elapsed = millis() - m_resetConfirmStartTime;
    uint32_t progress = (elapsed * 100) / RESET_HOLD_TIME_MS;

    if (progress > 100) progress = 100;

    lv_bar_set_value(m_resetConfirmProgress, progress, LV_ANIM_OFF);

    // If held for full duration, trigger reset
    if (elapsed >= RESET_HOLD_TIME_MS) {
        printf("[UI] Reset hold complete (button still pressed) - triggering reset\n");

        // Save last value for undo
        if (m_resetConfirmValue > 0) {
            m_lastResetValue = m_resetConfirmValue;
            m_hasLastResetValue = true;
            printf("[UI] Saved last reset value: %d seconds\n", m_lastResetValue);
        }

        // Send reset command
        if (m_touchCallback) {
            m_touchCallback(BTN_ID_PHYSICAL_1);  // Send reset button
        }

        // Hide confirmation
        hideResetConfirm();
    }
}
*/
