/*sleep
 * Queue-Master Remote Control Firmware
 * For Waveshare ESP32-S3-Touch-LCD-1.46B
 *
 * This firmware transforms the Waveshare demo into a wireless remote control
 * for the Queue-Master speaker timer application.
 *
 * Features:
 * - ESP-NOW wireless communication with bridge
 * - Real-time timer display on round LCD
 * - 3 external physical buttons (GPIO 9, 3, 14)
 * - 4 touch buttons on screen (Start, Pause, Reset, Add Time)
 * - Battery monitoring and low battery warning
 * - Power management (auto-dim, deep sleep)
 * - Connection status indicator
 *
 * See QUEUE_MASTER_PRD.md for complete specification.
 */

// Core hardware drivers (from Waveshare demo)
#include "Display_SPD2010.h"
#include "Touch_SPD2010.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Audio_PCM5101.h"
#include "SD_Card.h"
// #include "Gyro_QMI8658.h"  // REMOVED: Gyro disabled for power savings (~5-10mA)

// ESP32 filesystem
#include "FS.h"
#include "SPIFFS.h"
#include <Preferences.h>  // For NVS crash diagnostics

// Queue-Master custom drivers
#include "Config.h"
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
  #include "USBSerial_Driver.h"
#else
  #include "ESPNOW_Driver.h"
#endif
#include "Button_Driver.h"
#include "Timer_UI.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Power management state
uint32_t g_lastActivityTime = 0;
bool g_isDimmed = false;
bool g_isScreenOff = false;
uint32_t g_lastBatteryUpdate = 0;
uint32_t g_lastHeartbeat = 0;

// Radio sleep/wake management (v2.7.0+)
uint32_t g_lastRadioActivity = 0;
bool g_radioAsleep = false;
uint32_t g_lastRadioWake = 0;

// Audio alert state
bool g_mp3FilesCopied = false;
bool g_tenSecondAlertPlayed = false;

// LVGL watchdog - detects if main loop freezes
volatile uint32_t g_loopCounter = 0;
uint32_t g_lastLoopCount = 0;
uint32_t g_loopFreezeDetectedTime = 0;
bool g_timesUpAlertPlayed = false;

// Crash diagnostics
Preferences g_crashDiagnostics;

// ============================================================================
// AUDIO ALERT FUNCTIONS
// ============================================================================

// Copy MP3 files from SPIFFS to SD card (first boot only)
void copyMP3FilesToSD() {
  printf("Checking if MP3 files need to be copied to SD card...\n");

  // Check if files already exist on SD card
  if (SD_MMC.exists("/10-seconds.mp3") && SD_MMC.exists("/times-up.mp3")) {
    printf("MP3 files already exist on SD card, skipping copy.\n");
    return;
  }

  printf("Copying MP3 files from SPIFFS to SD card...\n");

  // Copy 10-seconds.mp3
  if (SPIFFS.exists("/10-seconds.mp3")) {
    printf("Found /10-seconds.mp3 in SPIFFS\n");

    // CRITICAL FIX: Open files separately to ensure proper cleanup on error
    File srcFile = SPIFFS.open("/10-seconds.mp3", "r");
    if (!srcFile) {
      printf("✗ Failed to open source file: /10-seconds.mp3\n");
    } else {
      File dstFile = SD_MMC.open("/10-seconds.mp3", "w");
      if (!dstFile) {
        printf("✗ Failed to open destination file: /10-seconds.mp3\n");
        srcFile.close();  // CRITICAL: Close source before returning
      } else {
        // Both files open - proceed with copy
        uint8_t buffer[512];
        int totalBytes = 0;
        while (srcFile.available()) {
          int bytesRead = srcFile.read(buffer, sizeof(buffer));
          dstFile.write(buffer, bytesRead);
          totalBytes += bytesRead;
        }
        srcFile.close();
        dstFile.close();
        printf("✓ Copied 10-seconds.mp3 (%d bytes)\n", totalBytes);
      }
    }
  } else {
    printf("✗ /10-seconds.mp3 NOT FOUND in SPIFFS!\n");
  }

  // Copy times-up.mp3
  if (SPIFFS.exists("/times-up.mp3")) {
    printf("Found /times-up.mp3 in SPIFFS\n");

    // CRITICAL FIX: Open files separately to ensure proper cleanup on error
    File srcFile = SPIFFS.open("/times-up.mp3", "r");
    if (!srcFile) {
      printf("✗ Failed to open source file: /times-up.mp3\n");
    } else {
      File dstFile = SD_MMC.open("/times-up.mp3", "w");
      if (!dstFile) {
        printf("✗ Failed to open destination file: /times-up.mp3\n");
        srcFile.close();  // CRITICAL: Close source before returning
      } else {
        // Both files open - proceed with copy
        uint8_t buffer[512];
        int totalBytes = 0;
        while (srcFile.available()) {
          int bytesRead = srcFile.read(buffer, sizeof(buffer));
          dstFile.write(buffer, bytesRead);
          totalBytes += bytesRead;
        }
        srcFile.close();
        dstFile.close();
        printf("✓ Copied times-up.mp3 (%d bytes)\n", totalBytes);
      }
    }
  } else {
    printf("✗ /times-up.mp3 NOT FOUND in SPIFFS!\n");
  }

  printf("MP3 copy complete!\n");
}

// Play an audio alert (non-blocking)
void playAlert(const char* filename) {
  printf("=== PLAY ALERT CALLED ===\n");
  printf("Playing alert: %s\n", filename);

  // Check if SD card has the file
  if (SD_MMC.exists(filename)) {
    printf("✓ File exists on SD card: %s\n", filename);
  } else {
    printf("✗ FILE NOT FOUND on SD card: %s\n", filename);
    return;
  }

  // Stop any currently playing audio
  if (audio.isRunning()) {
    printf("Stopping currently playing audio...\n");
    audio.stopSong();
  }

  // Play the MP3 file from SD card
  printf("Calling audio.connecttoFS()...\n");
  bool ret = audio.connecttoFS(SD_MMC, filename);
  if (ret) {
    printf("✓ Alert playback started successfully!\n");
  } else {
    printf("✗ Alert playback FAILED!\n");
  }

  // Check if audio is actually running (removed blocking delay for button responsiveness)
  if (audio.isRunning()) {
    printf("✓ Audio is RUNNING\n");
  } else {
    printf("✗ Audio is NOT RUNNING!\n");
  }

  // Get audio file info
  uint32_t duration = audio.getAudioFileDuration();
  printf("Audio duration: %d seconds\n", duration);

  printf("=== PLAY ALERT DONE ===\n");
}

// ============================================================================
// POWER MANAGEMENT FUNCTIONS (v2.7.0+)
// ============================================================================

// Update brightness based on idle time
void updateBrightnessManagement() {
  uint32_t idleTime = millis() - g_lastActivityTime;

  // State machine for brightness management
  if (idleTime >= IDLE_SLEEP_TIMEOUT_MS) {
    // Screen off after 140 seconds
    if (!g_isScreenOff) {
      printf("[PowerMgmt] Screen OFF (idle %ds)\n", idleTime / 1000);
      Set_Backlight(BRIGHTNESS_OFF);
      g_isScreenOff = true;
      g_isDimmed = true;  // Also mark as dimmed
    }
  } else if (idleTime >= IDLE_DIM_TIMEOUT_MS) {
    // Dim to 10% after 20 seconds
    if (!g_isDimmed) {
      printf("[PowerMgmt] Dimming to %d%% (idle %ds)\n", BRIGHTNESS_DIMMED, idleTime / 1000);
      Set_Backlight(BRIGHTNESS_DIMMED);
      g_isDimmed = true;
      g_isScreenOff = false;
    }
  } else {
    // Active - restore to default brightness
    if (g_isDimmed || g_isScreenOff) {
      printf("[PowerMgmt] Restoring to %d%% brightness\n", BRIGHTNESS_ACTIVE);
      Set_Backlight(BRIGHTNESS_ACTIVE);
      g_isDimmed = false;
      g_isScreenOff = false;
    }
  }
}

// Update radio sleep/wake cycles for power optimization
void updateRadioSleepWake() {
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
  // No radio in USB serial mode — nothing to do
#else
  uint32_t now = millis();

  // SCENARIO 1: OFFLINE MODE - Turn radio completely off for maximum power savings
  if (g_timerUI.isOfflineMode()) {
    if (!g_radioAsleep) {
      printf("[RadioMgmt] Offline mode - radio OFF (maximum power savings)\n");
      // In offline mode, we don't need radio at all
      // ESP-NOW can't fully turn off WiFi, but we mark it as asleep for UI
      g_radioAsleep = true;
      g_lastRadioWake = now;
    }
    return;  // Skip all other logic in offline mode
  }

  // SCENARIO 2 & 3: BRIDGE MODE - Smart sleep/wake based on connection state
  uint32_t timeSinceRadioActivity = now - g_lastRadioActivity;
  bool bridgeConnected = g_espnow.isConnected();

  if (timeSinceRadioActivity < RADIO_ACTIVE_TIMEOUT_MS) {
    // Keep radio awake for 5s after button press (both connected and disconnected)
    if (g_radioAsleep) {
      printf("[RadioMgmt] Waking radio (button activity)\n");
      esp_wifi_set_ps(WIFI_POWER_MODE);
      g_radioAsleep = false;
    }
  } else {
    // After 5s idle, enter sleep/wake cycles
    if (!g_radioAsleep) {
      if (bridgeConnected) {
        printf("[RadioMgmt] Connected - entering sleep/wake cycles (wake every %ds for sync)\n",
               RADIO_WAKE_INTERVAL_MS / 1000);
      } else {
        printf("[RadioMgmt] Disconnected - entering sleep/wake cycles (wake every %ds to check for bridge)\n",
               RADIO_WAKE_INTERVAL_DISCONNECTED_MS / 1000);
      }
      g_radioAsleep = true;
      g_lastRadioWake = now;
    }

    // Periodic wake cycles - use different intervals for connected vs disconnected
    uint32_t wakeInterval = bridgeConnected ? RADIO_WAKE_INTERVAL_MS : RADIO_WAKE_INTERVAL_DISCONNECTED_MS;
    uint32_t timeSinceWake = now - g_lastRadioWake;
    if (timeSinceWake >= wakeInterval) {
      // Brief wake for sync (connected) or reconnection attempt (disconnected)
      g_lastRadioWake = now;
      // Heartbeat/discover in main loop will handle periodic communication
    }
  }
#endif // COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
}

// Reset activity timers on button press
void onButtonActivity() {
  uint32_t now = millis();
  g_lastActivityTime = now;
  g_lastRadioActivity = now;

  // If screen was off, restore brightness immediately
  if (g_isScreenOff) {
    printf("[PowerMgmt] Button press - restoring display\n");
    Set_Backlight(BRIGHTNESS_ACTIVE);
    g_isScreenOff = false;
    g_isDimmed = false;
  }
}

// ============================================================================
// CALLBACK FUNCTIONS
// ============================================================================

// Called when a physical button is pressed (short press)
void onPhysicalButtonPress(uint8_t buttonId) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.print("Physical button pressed: ");
  Serial.println(buttonId);
#endif
  printf("Physical button pressed: %d\n", buttonId);

  // Reset activity timers and restore display if needed
  onButtonActivity();

  // Send button press via communication driver
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
  g_usbSerial.sendButton(buttonId);
#else
  g_espnow.sendButton(buttonId);
#endif
}

// Called when a touch button is clicked
void onTouchButtonClick(uint8_t buttonId) {
  printf("*** TOUCH BUTTON CLICKED: %d ***\n", buttonId);

  // Reset activity timers and restore display if needed
  onButtonActivity();

  // Test audio button (local only, not sent to bridge)
  if (buttonId == BTN_ID_TOUCH_TEST_AUDIO) {
    printf("*** TEST AUDIO BUTTON - Playing 10-second alert ***\n");
    playAlert("/10-seconds.mp3");
    return;  // Don't send to bridge
  }

  // Send button press via communication driver
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
  g_usbSerial.sendButton(buttonId);
#else
  g_espnow.sendButton(buttonId);
#endif
}

// ============================================================================
// DRIVER LOOP (FreeRTOS Task)
// ============================================================================

void Driver_Loop(void *parameter)
{
  uint32_t batteryUpdateCounter = 0;
  uint32_t memoryCheckCounter = 0;  // Check memory periodically
  uint32_t performanceCheckCounter = 0;  // Check performance metrics

  while(1)
  {
    // Memory and performance diagnostics (every 10 seconds)
    if (memoryCheckCounter++ >= 1000) {  // 1000 loops * 10ms = 10s
      memoryCheckCounter = 0;

      // Memory diagnostics
      printf("\n========== DIAGNOSTICS ==========\n");
      printf("[MEMORY] Free heap: %d bytes, Largest free block: %d bytes\n",
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      printf("[MEMORY] PSRAM free: %d bytes\n", ESP.getFreePsram());

      // Task stack diagnostics
      UBaseType_t stackWatermark = uxTaskGetStackHighWaterMark(NULL);
      printf("[TASK] Driver_Loop stack remaining: %d bytes\n", stackWatermark * 4);

      // LVGL memory info
      lv_mem_monitor_t mon;
      lv_mem_monitor(&mon);
      printf("[LVGL] Memory: Used=%d%%, Frag=%d%%, Biggest free block=%d bytes\n",
             mon.used_pct, mon.frag_pct, (int)mon.free_biggest_size);

      // Save diagnostic snapshot to NVS (survives reboot)
      static bool diagPrefsOpened = false;
      if (!diagPrefsOpened) {
        g_crashDiagnostics.begin("crash-diag", false);  // Read-write mode
        diagPrefsOpened = true;
      }

      // Save current state (overwrites previous)
      g_crashDiagnostics.putUInt("freeHeap", ESP.getFreeHeap());
      g_crashDiagnostics.putUInt("freePSRAM", ESP.getFreePsram());
      g_crashDiagnostics.putUInt("lvglUsed", mon.used_pct);
      g_crashDiagnostics.putUInt("lvglFrag", mon.frag_pct);
      g_crashDiagnostics.putUInt("bigBlock", (uint32_t)mon.free_biggest_size);
      g_crashDiagnostics.putUInt("uptime", millis() / 1000);  // Seconds
      g_crashDiagnostics.putUInt("loopCount", g_loopCounter);

      // LVGL watchdog - check if main loop is still running
      uint32_t currentLoopCount = g_loopCounter;
      if (currentLoopCount == g_lastLoopCount) {
        // Loop counter hasn't changed in 10 seconds - LVGL is frozen!
        if (g_loopFreezeDetectedTime == 0) {
          g_loopFreezeDetectedTime = millis();
          printf("\n!!! CRITICAL: LVGL MAIN LOOP FROZEN !!!\n");
          printf("!!! Loop counter stuck at: %lu !!!\n", currentLoopCount);
          printf("!!! Display rendering has stopped !!!\n\n");
        } else {
          uint32_t frozenDuration = (millis() - g_loopFreezeDetectedTime) / 1000;
          printf("!!! LVGL still frozen for %lu seconds !!!\n", frozenDuration);

          // AUTO-RECOVERY: Force restart after 30 seconds frozen
          if (frozenDuration >= 30) {
            printf("\n");
            printf("╔═══════════════════════════════════════════╗\n");
            printf("║  CRITICAL: LVGL FROZEN FOR 30 SECONDS    ║\n");
            printf("║  FORCING ESP32 RESTART FOR RECOVERY      ║\n");
            printf("╚═══════════════════════════════════════════╝\n");
            printf("\n");
            delay(1000);  // Let serial output finish
            ESP.restart();  // Reboot device
          }
        }
      } else {
        // Loop is running normally
        if (g_loopFreezeDetectedTime != 0) {
          printf("[WATCHDOG] LVGL loop recovered after freeze\n");
          g_loopFreezeDetectedTime = 0;
        }
        g_lastLoopCount = currentLoopCount;
        printf("[WATCHDOG] LVGL loop healthy (count: %lu)\n", currentLoopCount);
      }

      printf("=================================\n\n");
    }

    // Update battery reading
    BAT_Get_Volts();
    batteryUpdateCounter++;

    // Update battery display every 100 loops (10 seconds at 100ms interval)
    if (batteryUpdateCounter >= 100) {
      uint8_t batteryPercent = BAT_Get_Percentage();
      bool lowBattery = BAT_Is_Low();

      g_timerUI.setBattery(batteryPercent, lowBattery);
      batteryUpdateCounter = 0;
    }

    // Process physical buttons
    g_buttons.loop();

    // REMOVED: Gyro disabled for power savings (~5-10mA)
    // QMI8658_Loop();

    // Process communication driver
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
    g_usbSerial.loop();

    // Update UI connection status
    bool bridgeConnected = g_usbSerial.isConnected();
    bool reactConnected = (g_usbSerial.getTimerFlags() & FLAG_CONNECTED) != 0;
    g_timerUI.setConnectionStatus(bridgeConnected, reactConnected);

    // Update timer display if values changed (ONLY in bridge mode)
    static uint16_t lastTimerSeconds = 0;
    static uint8_t lastTimerFlags = 0;
    uint16_t currentSeconds = g_usbSerial.getTimerSeconds();
    uint8_t currentFlags = g_usbSerial.getTimerFlags();
#else
    g_espnow.loop();

    // Update UI connection status (dual: bridge ESP-NOW + React app)
    bool bridgeConnected = g_espnow.isConnected();  // ESP-NOW packets received
    bool reactConnected = (g_espnow.getTimerFlags() & FLAG_CONNECTED) != 0;  // Bridge-to-React status
    g_timerUI.setConnectionStatus(bridgeConnected, reactConnected);

    // Update timer display if values changed (ONLY in bridge mode)
    static uint16_t lastTimerSeconds = 0;
    static uint8_t lastTimerFlags = 0;
    uint16_t currentSeconds = g_espnow.getTimerSeconds();
    uint8_t currentFlags = g_espnow.getTimerFlags();
#endif

    // OFFLINE MODE: Skip bridge timer updates (offline mode updates happen in tick())
    if (!g_timerUI.isOfflineMode() && (currentSeconds != lastTimerSeconds || currentFlags != lastTimerFlags)) {
      g_timerUI.update(currentSeconds, currentFlags);
      lastTimerSeconds = currentSeconds;
      lastTimerFlags = currentFlags;

      // Reset idle timer on timer state change (for future power management features)
      g_lastActivityTime = millis();

      // Audio alerts
      bool isRunning = (currentFlags & FLAG_RUNNING) != 0;

      // Debug output
      printf("Timer: %ds, Running: %d, 10sPlayed: %d, 0sPlayed: %d\n",
             currentSeconds, isRunning, g_tenSecondAlertPlayed, g_timesUpAlertPlayed);

      // SKIP AUDIO IN OFFLINE MODE
      if (g_timerUI.isOfflineMode()) {
        // Reset alert flags (don't play audio)
        if (currentSeconds > 10) {
          g_tenSecondAlertPlayed = false;
          g_timesUpAlertPlayed = false;
        }
        // Skip audio playback
        goto skip_audio;
      }

      // 10-second warning (play once when timer reaches 10s while running)
      if (isRunning && currentSeconds == 10 && !g_tenSecondAlertPlayed) {
        printf("*** TRIGGERING 10-SECOND ALERT ***\n");
        playAlert("/10-seconds.mp3");
        g_tenSecondAlertPlayed = true;
      }

      // Times-up alert (play once when timer reaches 0s while running)
      if (isRunning && currentSeconds == 0 && !g_timesUpAlertPlayed) {
        printf("*** TRIGGERING TIMES-UP ALERT ***\n");
        playAlert("/times-up.mp3");
        g_timesUpAlertPlayed = true;
      }

      // Reset alert flags when timer goes above 10 seconds
      if (currentSeconds > 10) {
        g_tenSecondAlertPlayed = false;
        g_timesUpAlertPlayed = false;
      }

      skip_audio:  // Label for offline mode to skip audio playback
    }

    // Send heartbeat periodically
    uint32_t now = millis();
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
    if (now - g_lastHeartbeat >= USB_SERIAL_HEARTBEAT_INTERVAL_MS) {
      uint8_t batteryPercent = BAT_Get_Percentage();
      bool lowBattery = BAT_Is_Low();
      g_usbSerial.sendHeartbeat(batteryPercent, lowBattery);
      g_lastHeartbeat = now;
    }
#else
    // OVERFLOW-SAFE: Unsigned subtraction handles millis() wraparound correctly
    if (now - g_lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
      uint8_t batteryPercent = BAT_Get_Percentage();
      bool lowBattery = BAT_Is_Low();
      g_espnow.sendHeartbeat(batteryPercent, lowBattery);
      g_lastHeartbeat = now;
    }
#endif

    // Power button monitoring (shutdown detection)
    PWR_Loop();

    // Power management
    powerManagementUpdate();

    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms update interval for responsive buttons
  }
}

// ============================================================================
// POWER MANAGEMENT
// ============================================================================

void powerManagementUpdate() {
  // Auto-dim DISABLED - screen stays at constant brightness
  // Auto-sleep DISABLED - device stays on while timer is running

  // To re-enable auto-dim: uncomment the code below
  // uint32_t now = millis();
  // uint32_t idleTime = now - g_lastActivityTime;
  // if (!g_isDimmed && idleTime >= IDLE_DIM_TIMEOUT_MS) {
  //   Serial.println("Auto-dimming backlight...");
  //   Set_Backlight(BACKLIGHT_DIMMED);
  //   g_isDimmed = true;
  // }
}

// DEPRECATED: Old deep sleep function - replaced by ButtonDriver::enterDeepSleep()
// The new implementation includes button release detection, RTC GPIO pull-ups,
// and comprehensive debug logging. See Button_Driver.cpp for the active version.
/*
void enterDeepSleep() {
  // Turn off backlight
  Set_Backlight(BACKLIGHT_OFF);

  // Small delay
  delay(100);

  // Configure Button 1 (GPIO 13) as wake source
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_1_PIN, 0);

  Serial.println("Entering deep sleep...");
  delay(100);

  // Enter deep sleep
  esp_deep_sleep_start();

  // This function does not return
}
*/

// ============================================================================
// INITIALIZATION
// ============================================================================

void Driver_Init()
{
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("Initializing drivers...");
#endif
  printf("Initializing drivers...\n");

  // Core hardware initialization
  PWR_Init();  // CRITICAL: Initialize power control FIRST to latch power on
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Backlight_Init();
  Set_Backlight(BACKLIGHT_ACTIVE);
  BAT_Init();

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("Core hardware initialized");
#endif
  printf("Core hardware initialized\n");
}

void setup()
{
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(2000);

  // USB serial mode: Serial is reserved for JSON protocol, use printf only
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("\n\n\n\n====================================");
  Serial.println("Queue-Master Remote Control");
  Serial.println("Firmware: " FIRMWARE_VERSION);
  Serial.println("Power Mode: " POWER_MODE_NAME);
  Serial.println("====================================\n");
#endif
  printf("\n\n\n\n====================================\n");
  printf("Queue-Master Remote Control\n");
  printf("Firmware: %s\n", FIRMWARE_VERSION);
  printf("Comm Mode: %s\n",
#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
    "USB_SERIAL"
#else
    POWER_MODE_NAME
#endif
  );
  printf("====================================\n\n");

  // Check for crash diagnostics from previous session
  Preferences crashDiag;
  crashDiag.begin("crash-diag", true);  // Read-only mode

  if (crashDiag.isKey("uptime")) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║  PREVIOUS SESSION DIAGNOSTICS (Before Crash)  ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("  Uptime:           %lu seconds\n", crashDiag.getUInt("uptime", 0));
    Serial.printf("  Loop Count:       %lu\n", crashDiag.getUInt("loopCount", 0));
    Serial.printf("  Free Heap:        %lu bytes\n", crashDiag.getUInt("freeHeap", 0));
    Serial.printf("  Free PSRAM:       %lu bytes\n", crashDiag.getUInt("freePSRAM", 0));
    Serial.printf("  LVGL Memory Used: %lu%%\n", crashDiag.getUInt("lvglUsed", 0));
    Serial.printf("  LVGL Frag:        %lu%%\n", crashDiag.getUInt("lvglFrag", 0));
    Serial.printf("  LVGL Biggest Blk: %lu bytes\n", crashDiag.getUInt("bigBlock", 0));
    Serial.println("════════════════════════════════════════════════\n");
#endif

    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║  PREVIOUS SESSION DIAGNOSTICS (Before Crash)  ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    printf("  Uptime:           %lu seconds\n", crashDiag.getUInt("uptime", 0));
    printf("  Loop Count:       %lu\n", crashDiag.getUInt("loopCount", 0));
    printf("  Free Heap:        %lu bytes\n", crashDiag.getUInt("freeHeap", 0));
    printf("  Free PSRAM:       %lu bytes\n", crashDiag.getUInt("freePSRAM", 0));
    printf("  LVGL Memory Used: %lu%%\n", crashDiag.getUInt("lvglUsed", 0));
    printf("  LVGL Frag:        %lu%%\n", crashDiag.getUInt("lvglFrag", 0));
    printf("  LVGL Biggest Blk: %lu bytes\n", crashDiag.getUInt("bigBlock", 0));
    printf("════════════════════════════════════════════════\n\n");

    // Check for memory exhaustion
    if (crashDiag.getUInt("lvglUsed", 0) > 90) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
      Serial.println("⚠️  WARNING: LVGL memory was >90% before crash!");
      Serial.println("    Likely cause: LVGL heap exhaustion\n");
#endif
      printf("⚠️  WARNING: LVGL memory was >90%% before crash!\n");
      printf("    Likely cause: LVGL heap exhaustion\n\n");
    }
    if (crashDiag.getUInt("lvglFrag", 0) > 60) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
      Serial.println("⚠️  WARNING: LVGL fragmentation was >60% before crash!");
      Serial.println("    Likely cause: Memory fragmentation\n");
#endif
      printf("⚠️  WARNING: LVGL fragmentation was >60%% before crash!\n");
      printf("    Likely cause: Memory fragmentation\n\n");
    }
  } else {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("No crash diagnostics found (clean boot)\n");
#endif
    printf("No crash diagnostics found (clean boot)\n\n");
  }

  crashDiag.end();

  // Check wake reason for deep sleep debugging
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
      Serial.println("*** Woke from deep sleep via EXT0 (Button 1) ***");
#endif
      printf("*** Woke from deep sleep via EXT0 (Button 1) ***\n");
      break;

    case ESP_SLEEP_WAKEUP_EXT1: {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
      Serial.println("*** Woke from deep sleep via EXT1 (Any button) ***");
#endif
      printf("*** Woke from deep sleep via EXT1 (Any button) ***\n");

      // Check which button(s) caused the wake
      uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_pin_mask & (1ULL << BUTTON_1_PIN)) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
        Serial.printf("  -> Button 1 (GPIO %d) pressed\n", BUTTON_1_PIN);
#endif
        printf("  -> Button 1 (GPIO %d) pressed\n", BUTTON_1_PIN);
      }
      if (wakeup_pin_mask & (1ULL << BUTTON_2_PIN)) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
        Serial.printf("  -> Button 2 (GPIO %d) pressed\n", BUTTON_2_PIN);
#endif
        printf("  -> Button 2 (GPIO %d) pressed\n", BUTTON_2_PIN);
      }
      if (wakeup_pin_mask & (1ULL << BUTTON_3_PIN)) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
        Serial.printf("  -> Button 3 (GPIO %d) pressed\n", BUTTON_3_PIN);
#endif
        printf("  -> Button 3 (GPIO %d) pressed\n", BUTTON_3_PIN);
      }
      break;
    }

    case ESP_SLEEP_WAKEUP_TIMER:
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
      Serial.println("*** Woke from deep sleep via timer ***");
#endif
      printf("*** Woke from deep sleep via timer ***\n");
      break;

    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
      Serial.println("*** Normal power-on (not from deep sleep) ***");
#endif
      printf("*** Normal power-on (not from deep sleep) ***\n");
      break;
  }
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println();
#endif

  // Initialize core drivers
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[1/7] Initializing core drivers...");
#endif
  printf("[1/7] Initializing core drivers...\n");
  Driver_Init();
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[1/7] Core drivers OK");
#endif
  printf("[1/7] Core drivers OK\n");

  // Initialize display and LVGL
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[2/7] Initializing LCD...");
#endif
  printf("[2/7] Initializing LCD...\n");
  LCD_Init();
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[2/7] LCD OK");
#endif
  printf("[2/7] LCD OK\n");

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[3/7] Initializing LVGL...");
#endif
  printf("[3/7] Initializing LVGL...\n");
  Lvgl_Init();
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[3/7] LVGL OK");
#endif
  printf("[3/7] LVGL OK\n");

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[4/7] Display and LVGL initialized");
#endif
  printf("[4/7] Display and LVGL initialized\n");

  // Initialize Queue-Master components
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[5/7] Initializing buttons...");
#endif
  printf("[5/7] Initializing buttons...\n");
  g_buttons.init();
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[5/7] Buttons OK");
#endif
  printf("[5/7] Buttons OK\n");

#if COMMUNICATION_MODE == COMM_MODE_USB_SERIAL
  printf("[6/7] Initializing USB Serial driver...\n");
  g_usbSerial.init();
  printf("[6/7] USB Serial OK\n");
#else
  Serial.println("[6/7] Initializing ESP-NOW...");
  printf("[6/7] Initializing ESP-NOW...\n");
  Serial.println("[6/7] This is the critical step - watch for errors...");
  printf("[6/7] This is the critical step - watch for errors...\n");
  g_espnow.init();
  Serial.println("[6/7] ESP-NOW OK");
  printf("[6/7] ESP-NOW OK\n");
#endif

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[7/7] Initializing Timer UI...");
#endif
  printf("[7/7] Initializing Timer UI...\n");
  g_timerUI.init();
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[7/7] Timer UI OK");
#endif
  printf("[7/7] Timer UI OK\n");

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("\n*** All components initialized ***\n");
#endif
  printf("\n*** All components initialized ***\n\n");

  // Initialize audio system
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[8/10] Initializing SPIFFS...");
#endif
  printf("[8/10] Initializing SPIFFS...\n");
  if (!SPIFFS.begin(true)) {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[8/10] SPIFFS mount failed!");
#endif
    printf("[8/10] SPIFFS mount failed!\n");
  } else {
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[8/10] SPIFFS OK");
#endif
    printf("[8/10] SPIFFS OK\n");
  }

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[9/10] Initializing SD card...");
#endif
  printf("[9/10] Initializing SD card...\n");
  SD_Init();
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[9/10] SD card OK");
#endif
  printf("[9/10] SD card OK\n");

  // Copy MP3 files from SPIFFS to SD on first boot
  if (!g_mp3FilesCopied) {
    copyMP3FilesToSD();
    g_mp3FilesCopied = true;
  }

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[10/10] Initializing audio...");
#endif
  printf("[10/10] Initializing audio...\n");

  // Enable speaker amplifier (try all unused EXIO pins)
  printf("Enabling speaker amplifier pins...\n");
  Set_EXIO(EXIO_PIN3, High);  // Try PIN3
  Set_EXIO(EXIO_PIN5, High);  // Try PIN5
  Set_EXIO(EXIO_PIN6, High);  // Try PIN6
  Set_EXIO(EXIO_PIN7, High);  // Try PIN7
  printf("Speaker amp pins enabled\n");

  Audio_Init();
  Volume_adjustment(10);  // Volume level 10 (0-21 range)
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("[10/10] Audio OK");
#endif
  printf("[10/10] Audio OK\n");

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("\n*** All systems ready! ***\n");
#endif
  printf("\n*** All systems ready! ***\n\n");

  // Set up callbacks
  g_buttons.setShortPressCallback(onPhysicalButtonPress);
  g_timerUI.setTouchCallback(onTouchButtonClick);

  // Initialize power management state
  g_lastActivityTime = millis();
  g_isDimmed = false;
  g_lastBatteryUpdate = 0;
  g_lastHeartbeat = millis();

  // Initialize audio alert state
  g_mp3FilesCopied = false;
  g_tenSecondAlertPlayed = false;
  g_timesUpAlertPlayed = false;

  // Create background task for drivers
  xTaskCreatePinnedToCore(
    Driver_Loop,
    "Queue-Master Drivers",
    8192,                // Stack size (increased from 4096 to prevent overflow)
    NULL,
    3,                   // Priority
    NULL,
    0                    // Core 0
  );

  // Initialize power management timers (v2.7.0+)
  uint32_t now = millis();
  g_lastActivityTime = now;
  g_lastRadioActivity = now;
  g_lastRadioWake = now;
  printf("[PowerMgmt] Activity timers initialized\n");

#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
  Serial.println("\nInitialization complete!");
  Serial.println("====================================\n");
#endif
  printf("\nInitialization complete!\n");
  printf("====================================\n\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  static uint32_t renderTimeCheckCounter = 0;
  static uint32_t maxRenderTime = 0;
  static uint32_t totalRenderTime = 0;

  // Increment loop counter for watchdog monitoring
  g_loopCounter++;

  // LED heartbeat - blink every second to show device is alive
  // This helps diagnose if main loop freezes vs just LVGL freezing
  static uint32_t ledBlinkCounter = 0;
  static bool ledState = false;
  if (ledBlinkCounter++ >= 60) {  // 60 loops @ 16ms = ~1 second
    ledBlinkCounter = 0;
    ledState = !ledState;
    // Note: If you have an LED connected to a GPIO pin, uncomment and configure:
    // digitalWrite(LED_PIN, ledState);
    // For now, this just maintains the counter for future use
  }

  // Measure LVGL render time
  uint32_t startTime = micros();

  // LVGL must run in main loop
  Lvgl_Loop();

  // Process touch input and gestures (triple-tap, swipe)
  Touch_Loop();

  // Update UI animations (connection indicator blink, etc.)
  g_timerUI.tick();

  uint32_t renderTime = micros() - startTime;

  // Track max render time
  if (renderTime > maxRenderTime) {
    maxRenderTime = renderTime;
  }
  totalRenderTime += renderTime;
  renderTimeCheckCounter++;

  // Report render stats every 300 loops (~5 seconds at 16ms delay)
  if (renderTimeCheckCounter >= 300) {
    uint32_t avgRenderTime = totalRenderTime / renderTimeCheckCounter;
    printf("[RENDER] Avg: %d us, Max: %d us (last 5s)\n", avgRenderTime, maxRenderTime);

    // Warn if render time is too high
    if (maxRenderTime > 10000) {  // 10ms = starting to cause issues
      printf("[RENDER] WARNING: Max render time exceeds 10ms! UI may lag.\n");
    }

    // Reset stats
    renderTimeCheckCounter = 0;
    maxRenderTime = 0;
    totalRenderTime = 0;
  }

  // Note: Audio processing handled by timer interrupt in Audio_Init()
  // No need to call Audio_Loop() here

  // Power management - brightness and radio sleep/wake (v2.7.0+)
  updateBrightnessManagement();
  updateRadioSleepWake();

  // OPTIMIZATION: Reduced from 5ms to 33ms (30 FPS instead of 200 FPS)
  // Timer display doesn't need high FPS - 30 FPS is smooth and saves battery (v2.8.0)
  vTaskDelay(pdMS_TO_TICKS(33));  // 33ms delay (30 FPS) - balances smoothness and battery life
}
