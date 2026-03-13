#include "Button_Driver.h"
#include "Display_SPD2010.h"  // For Set_Backlight()
#include "Timer_UI.h"          // For physical button integration
#include <esp_sleep.h>
#include <driver/rtc_io.h>    // For RTC GPIO hold during deep sleep

// Global instance
ButtonDriver g_buttons;

// Static instance pointer for ISR access
ButtonDriver* ButtonDriver::s_instance = nullptr;

// ============================================================================
// INITIALIZATION
// ============================================================================

void ButtonDriver::init() {
    DEBUG_BTN_PRINTLN("Initializing interrupt-driven button driver...");

    // Set static instance for ISR access
    s_instance = this;

    // Initialize callbacks
    m_shortPressCallback = nullptr;
    m_longPressCallback = nullptr;

    // Configure buttons with interrupt support
    m_buttons[0] = {BUTTON_1_PIN, BTN_ID_PHYSICAL_1, BTN_STATE_IDLE, 0, 0, 0, false, false};
    m_buttons[1] = {BUTTON_2_PIN, BTN_ID_PHYSICAL_2, BTN_STATE_IDLE, 0, 0, 0, false, false};
    m_buttons[2] = {BUTTON_3_PIN, BTN_ID_PHYSICAL_3, BTN_STATE_IDLE, 0, 0, 0, false, false};

    // Configure GPIO pins (active low with internal pullup)
    for (int i = 0; i < 3; i++) {
        pinMode(m_buttons[i].pin, INPUT_PULLUP);
        DEBUG_BTN_PRINT("Button ");
        DEBUG_BTN_PRINT(m_buttons[i].buttonId);
        DEBUG_BTN_PRINT(" on GPIO ");
        DEBUG_BTN_PRINTLN(m_buttons[i].pin);
    }

    // Attach interrupts (CHANGE mode detects both press and release)
    attachInterrupt(digitalPinToInterrupt(BUTTON_1_PIN), button1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_2_PIN), button2ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_3_PIN), button3ISR, CHANGE);

    DEBUG_BTN_PRINTLN("Button interrupts attached");
    DEBUG_BTN_PRINTLN("Button driver initialized");
}

// ============================================================================
// INTERRUPT SERVICE ROUTINES (ISR)
// ============================================================================

void IRAM_ATTR ButtonDriver::button1ISR() {
    handleISR(0);
}

void IRAM_ATTR ButtonDriver::button2ISR() {
    handleISR(1);
}

void IRAM_ATTR ButtonDriver::button3ISR() {
    handleISR(2);
}

void IRAM_ATTR ButtonDriver::handleISR(uint8_t buttonIndex) {
    if (s_instance == nullptr || buttonIndex >= 3) {
        return;
    }

    Button& btn = s_instance->m_buttons[buttonIndex];

    // Record interrupt time and set flag
    btn.lastInterruptTime = millis();
    btn.interruptFlag = true;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void ButtonDriver::loop() {
    // Process each button
    for (int i = 0; i < 3; i++) {
        processButton(m_buttons[i]);
    }
}

// ============================================================================
// CALLBACK SETTERS
// ============================================================================

void ButtonDriver::setShortPressCallback(ButtonCallback callback) {
    m_shortPressCallback = callback;
}

void ButtonDriver::setLongPressCallback(ButtonCallback callback) {
    m_longPressCallback = callback;
}

// ============================================================================
// BUTTON STATE QUERY
// ============================================================================

bool ButtonDriver::isPressed(uint8_t buttonId) const {
    for (int i = 0; i < 3; i++) {
        if (m_buttons[i].buttonId == buttonId) {
            return m_buttons[i].state == BTN_STATE_PRESSED ||
                   m_buttons[i].state == BTN_STATE_LONG_WAIT;
        }
    }
    return false;
}

// ============================================================================
// BUTTON PROCESSING (Interrupt-Driven)
// ============================================================================

void ButtonDriver::processButton(Button& btn) {
    uint32_t now = millis();

    // Check if interrupt occurred
    if (btn.interruptFlag) {
        btn.interruptFlag = false;  // Clear flag

        // Read current button state
        bool reading = digitalRead(btn.pin);  // LOW = pressed (active low)

        DEBUG_BTN_PRINT("Button ");
        DEBUG_BTN_PRINT(btn.buttonId);
        DEBUG_BTN_PRINT(" interrupt: ");
        DEBUG_BTN_PRINTLN(reading == LOW ? "PRESSED" : "RELEASED");

        // Handle button press
        if (reading == LOW && btn.state == BTN_STATE_IDLE) {
            // Button pressed, start debounce
            btn.state = BTN_STATE_DEBOUNCE;
            btn.debounceTime = now;

            DEBUG_BTN_PRINT("Button ");
            DEBUG_BTN_PRINT(btn.buttonId);
            DEBUG_BTN_PRINTLN(" press detected, debouncing...");
        }
        // Handle button release
        else if (reading == HIGH) {
            if (btn.state == BTN_STATE_DEBOUNCE) {
                // Released during debounce - ignore
                btn.state = BTN_STATE_IDLE;

                DEBUG_BTN_PRINT("Button ");
                DEBUG_BTN_PRINT(btn.buttonId);
                DEBUG_BTN_PRINTLN(" released during debounce (ignored)");
            }
            else if (btn.state == BTN_STATE_PRESSED) {
                // Released after confirmed press - fire short press
                btn.state = BTN_STATE_IDLE;
                handleShortPress(btn.buttonId);

                DEBUG_BTN_PRINT("Button ");
                DEBUG_BTN_PRINT(btn.buttonId);
                DEBUG_BTN_PRINTLN(" short press");
            }
            else if (btn.state == BTN_STATE_LONG_WAIT) {
                // Released after long press
                btn.state = BTN_STATE_IDLE;

                DEBUG_BTN_PRINT("Button ");
                DEBUG_BTN_PRINT(btn.buttonId);
                DEBUG_BTN_PRINTLN(" released after long press");
            }
        }
    }

    // Process debounce timeout
    if (btn.state == BTN_STATE_DEBOUNCE) {
        if (now - btn.debounceTime >= BUTTON_DEBOUNCE_MS) {
            // Check if still pressed
            bool reading = digitalRead(btn.pin);
            if (reading == LOW) {
                // Debounce complete, button is confirmed pressed
                btn.state = BTN_STATE_PRESSED;
                btn.pressTime = now;
                btn.longPressFired = false;

                DEBUG_BTN_PRINT("Button ");
                DEBUG_BTN_PRINT(btn.buttonId);
                DEBUG_BTN_PRINTLN(" confirmed pressed");
            } else {
                // Released during debounce
                btn.state = BTN_STATE_IDLE;

                DEBUG_BTN_PRINT("Button ");
                DEBUG_BTN_PRINT(btn.buttonId);
                DEBUG_BTN_PRINTLN(" released during debounce");
            }
        }
    }

    // Process long press detection
    if (btn.state == BTN_STATE_PRESSED && !btn.longPressFired) {
        if (now - btn.pressTime >= BUTTON_LONG_PRESS_MS) {
            // Long press threshold reached
            btn.longPressFired = true;
            btn.state = BTN_STATE_LONG_WAIT;
            handleLongPress(btn.buttonId);

            DEBUG_BTN_PRINT("Button ");
            DEBUG_BTN_PRINT(btn.buttonId);
            DEBUG_BTN_PRINTLN(" long press");
        }
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void ButtonDriver::handleShortPress(uint8_t buttonId) {
    // NEW: Physical buttons now have context-aware behavior handled by Timer_UI
    // Timer_UI will decide whether to update local UI state or send to React app

    // RATE LIMITING: Prevent rapid button presses (debouncing enhancement)
    // Each button has its own rate limit timer to prevent UI thread flooding
    static uint32_t lastPressTime[3] = {0, 0, 0};  // Track last press for each button
    static const uint32_t MIN_PRESS_INTERVAL_MS = 200;  // Minimum 200ms between presses

    uint8_t buttonIndex = buttonId - 1;  // Convert button ID to array index (1->0, 2->1, 3->2)
    if (buttonIndex < 3) {
        uint32_t now = millis();
        uint32_t timeSinceLastPress = now - lastPressTime[buttonIndex];

        if (timeSinceLastPress < MIN_PRESS_INTERVAL_MS) {
            printf("[BUTTON] Rate limit: Button %d pressed too soon (%lu ms ago), ignoring\n",
                   buttonId, timeSinceLastPress);
            return;  // Ignore this press
        }

        lastPressTime[buttonIndex] = now;  // Update last press time
    }

    printf("[BUTTON] Physical button %d pressed\n", buttonId);

    // Call Timer_UI physical button handlers (context-aware logic)
    switch (buttonId) {
        case BTN_ID_PHYSICAL_1:
            g_timerUI.onPhysicalButton1();
            break;
        case BTN_ID_PHYSICAL_2:
            g_timerUI.onPhysicalButton2();
            break;
        case BTN_ID_PHYSICAL_3:
            g_timerUI.onPhysicalButton3();
            break;
        default:
            // For any other button IDs, use legacy callback
            if (m_shortPressCallback != nullptr) {
                m_shortPressCallback(buttonId);
            }
            break;
    }
}

void ButtonDriver::handleLongPress(uint8_t buttonId) {
    // Special handling for Button 1 long press = deep sleep
    if (buttonId == BTN_ID_PHYSICAL_1) {
        DEBUG_BTN_PRINTLN("Button 1 long press - entering deep sleep...");
        enterDeepSleep();
        // Note: This function does not return
    }
    // Special handling for Button 2 long press = TOGGLE settings menu
    else if (buttonId == BTN_ID_PHYSICAL_2) {
        if (g_timerUI.getCurrentUIState() == UI_STATE_SETTINGS ||
            g_timerUI.getCurrentUIState() == UI_STATE_BLE_PAIRING) {
            printf("[BUTTON] Button 2 long press - CLOSING settings/BLE\n");
            g_timerUI.hideSettings();  // Will also close BLE if open
        } else {
            printf("[BUTTON] Button 2 long press - OPENING settings menu\n");
            g_timerUI.showSettings();
        }
    }
    // Special handling for Button 3 long press = toggle offline mode
    else if (buttonId == BTN_ID_PHYSICAL_3) {
        printf("[BUTTON] Button 3 long press - toggling offline mode\n");
        g_timerUI.toggleOfflineMode();
    }

    // Call callback if set (for other buttons)
    if (m_longPressCallback != nullptr) {
        m_longPressCallback(buttonId);
    }
}

void ButtonDriver::enterDeepSleep() {
    printf("\n\n==========================================\n");
    printf("DEEP SLEEP ENTRY SEQUENCE STARTING\n");
    printf("==========================================\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("\n\n==========================================");
    Serial.println("DEEP SLEEP ENTRY SEQUENCE STARTING");
    Serial.println("==========================================");
    Serial.flush();  // CRITICAL: Flush serial buffer
#endif
    delay(50);

    printf("[1/7] Preparing for deep sleep...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[1/7] Preparing for deep sleep...");
    Serial.flush();
#endif
    delay(50);

    // Turn off backlight
    printf("[2/7] Turning off backlight...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[2/7] Turning off backlight...");
    Serial.flush();
#endif
    Set_Backlight(BACKLIGHT_OFF);
    delay(100);

    // CRITICAL FIX: Wait for ALL buttons to be released before sleeping
    printf("[3/7] Waiting for button release...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[3/7] Waiting for button release...");
    Serial.flush();
#endif

    uint32_t waitStart = millis();
    bool allReleased = false;
    int checkCount = 0;

    while (!allReleased && (millis() - waitStart < 8000)) {  // 8 second timeout (increased)
        allReleased = (digitalRead(BUTTON_1_PIN) == HIGH) &&
                      (digitalRead(BUTTON_2_PIN) == HIGH) &&
                      (digitalRead(BUTTON_3_PIN) == HIGH);

        if (!allReleased) {
            checkCount++;
            if (checkCount % 10 == 0) {  // Print every 100ms
                printf("  Still waiting... (btn1=%d, btn2=%d, btn3=%d)\n",
                       digitalRead(BUTTON_1_PIN), digitalRead(BUTTON_2_PIN), digitalRead(BUTTON_3_PIN));
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
                Serial.flush();
#endif
            }
            delay(10);  // Wait 10ms and check again
        }
    }

    if (!allReleased) {
        printf("[ERROR] Button release timeout after 8 seconds!\n");
        printf("[ERROR] Aborting deep sleep, turning screen back on...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
        Serial.println("[ERROR] Button release timeout!");
        Serial.println("[ERROR] Aborting deep sleep!");
        Serial.flush();
#endif
        Set_Backlight(BACKLIGHT_ACTIVE);  // Turn screen back on
        return;  // Don't enter sleep if buttons stuck
    }

    printf("[3/7] All buttons released (took %lu ms)\n", millis() - waitStart);
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[3/7] All buttons released!");
    Serial.flush();
#endif

    // CRITICAL: Add extra settling time after button release
    // Hardware buttons can bounce or have slow pull-up rise times
    printf("[3/7] Waiting 300ms for button state to stabilize...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[3/7] Stabilizing...");
    Serial.flush();
#endif
    delay(300);  // Let button state fully settle

    // Verify buttons are STILL released after settling
    bool btn1State = digitalRead(BUTTON_1_PIN);
    bool btn2State = digitalRead(BUTTON_2_PIN);
    bool btn3State = digitalRead(BUTTON_3_PIN);

    printf("[3/7] Final verification: btn1=%d, btn2=%d, btn3=%d\n", btn1State, btn2State, btn3State);
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.flush();
#endif

    if (btn1State == LOW || btn2State == LOW || btn3State == LOW) {
        printf("[ERROR] Button(s) still pressed after settling period!\n");
        printf("[ERROR] Cannot safely enter deep sleep. Aborting.\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
        Serial.println("[ERROR] Button still pressed!");
        Serial.flush();
#endif
        Set_Backlight(BACKLIGHT_ACTIVE);
        return;
    }

    printf("[3/7] Button release confirmed and stable.\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.flush();
#endif
    delay(50);

    // CRITICAL: Keep power latched during deep sleep
    printf("[4/7] Configuring power latch (GPIO 7)...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[4/7] Configuring power latch...");
    Serial.flush();
#endif

    rtc_gpio_init((gpio_num_t)7);  // Initialize as RTC GPIO
    rtc_gpio_set_direction((gpio_num_t)7, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level((gpio_num_t)7, 1);  // Keep HIGH during deep sleep
    rtc_gpio_hold_en((gpio_num_t)7);  // Hold the state

    printf("[4/7] Power latch held via RTC GPIO (GPIO 7 = HIGH)\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[4/7] Power latch configured!");
    Serial.flush();
#endif
    delay(50);

    // Configure wake sources
    printf("[5/7] Configuring wake sources...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[5/7] Configuring wake sources...");
    Serial.flush();
#endif

    // CRITICAL: Configure button GPIOs as RTC GPIOs with pull-ups
    // This ensures they maintain HIGH state during deep sleep and don't float
    printf("[5/7] Configuring RTC GPIO pull-ups for buttons...\n");

    // Button 1 (GPIO 13)
    rtc_gpio_init((gpio_num_t)BUTTON_1_PIN);
    rtc_gpio_set_direction((gpio_num_t)BUTTON_1_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BUTTON_1_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)BUTTON_1_PIN);
    rtc_gpio_hold_en((gpio_num_t)BUTTON_1_PIN);
    printf("  Button 1 (GPIO %d): RTC pull-up enabled and held\n", BUTTON_1_PIN);

    // Button 2 (GPIO 12)
    rtc_gpio_init((gpio_num_t)BUTTON_2_PIN);
    rtc_gpio_set_direction((gpio_num_t)BUTTON_2_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BUTTON_2_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)BUTTON_2_PIN);
    rtc_gpio_hold_en((gpio_num_t)BUTTON_2_PIN);
    printf("  Button 2 (GPIO %d): RTC pull-up enabled and held\n", BUTTON_2_PIN);

    // Button 3 (GPIO 1)
    rtc_gpio_init((gpio_num_t)BUTTON_3_PIN);
    rtc_gpio_set_direction((gpio_num_t)BUTTON_3_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BUTTON_3_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)BUTTON_3_PIN);
    rtc_gpio_hold_en((gpio_num_t)BUTTON_3_PIN);
    printf("  Button 3 (GPIO %d): RTC pull-up enabled and held\n", BUTTON_3_PIN);

    printf("[5/7] All button GPIOs configured with RTC pull-ups\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.flush();
#endif
    delay(50);

    // Use ONLY EXT1 (not EXT0) for better noise immunity
    uint64_t wakeup_pin_mask = (1ULL << BUTTON_1_PIN) | (1ULL << BUTTON_2_PIN) | (1ULL << BUTTON_3_PIN);
    esp_sleep_enable_ext1_wakeup(wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    printf("[5/7] Wake sources: EXT1 on GPIO %d, %d, %d (wake on ANY LOW)\n",
           BUTTON_1_PIN, BUTTON_2_PIN, BUTTON_3_PIN);
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[5/7] Wake sources configured!");
    Serial.flush();
#endif
    delay(50);

    printf("[6/7] Final button state check before sleep:\n");
    printf("  Button 1 (GPIO %d): %s\n", BUTTON_1_PIN, digitalRead(BUTTON_1_PIN) == HIGH ? "HIGH (released)" : "LOW (pressed)");
    printf("  Button 2 (GPIO %d): %s\n", BUTTON_2_PIN, digitalRead(BUTTON_2_PIN) == HIGH ? "HIGH (released)" : "LOW (pressed)");
    printf("  Button 3 (GPIO %d): %s\n", BUTTON_3_PIN, digitalRead(BUTTON_3_PIN) == HIGH ? "HIGH (released)" : "LOW (pressed)");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[6/7] Final button check done!");
    Serial.flush();
#endif
    delay(100);

    printf("\n[7/7] *** ENTERING DEEP SLEEP NOW ***\n");
    printf("Press any button to wake the device.\n");
    printf("==========================================\n\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("\n[7/7] *** ENTERING DEEP SLEEP NOW ***");
    Serial.println("Press any button to wake.");
    Serial.println("==========================================\n");

    // CRITICAL: Ensure ALL serial data is transmitted before sleep
    Serial.flush();
#endif
    delay(200);  // Extra delay to ensure serial TX completes

    // CRITICAL: One final delay to let GPIO states fully stabilize
    // This prevents false wake from electrical noise or button bounce
    printf("[7/7] Waiting 500ms for final GPIO stabilization...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.println("[7/7] Final stabilization delay...");
    Serial.flush();
#endif
    delay(500);

    printf("[7/7] GPIO stable. Entering sleep in 3... 2... 1...\n");
#if COMMUNICATION_MODE != COMM_MODE_USB_SERIAL
    Serial.flush();
#endif
    delay(100);

    // Enter deep sleep
    esp_deep_sleep_start();

    // This function does not return
}
