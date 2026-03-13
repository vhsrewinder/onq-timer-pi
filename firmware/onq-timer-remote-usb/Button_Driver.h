#pragma once

#include <Arduino.h>
#include "Config.h"

/*
 * Queue-Master Remote Control - Physical Button Driver
 *
 * This driver handles the 3 external physical buttons with debouncing
 * and long-press detection using interrupt-driven detection.
 *
 * Button Configuration:
 * - Button 1 (GPIO13): Reset + Long press (2s) = Deep Sleep (Power Off)
 * - Button 2 (GPIO1): Presets Screen + Long press (2s) = Settings Menu
 * - Button 3 (GPIO12): Start/Pause Toggle + Long press (2s) = Toggle Offline Mode
 *
 * Power Management:
 * - Long press Button 1 (2s) → Device enters deep sleep (appears powered off)
 * - Press ANY button → Device wakes from deep sleep (powers back on)
 * - Power latch held during sleep (no need for inaccessible PWR button)
 *
 * All buttons are active-low (connect to GND when pressed)
 *
 * Features:
 * - Interrupt-driven detection (immediate response)
 * - Software debouncing (50ms)
 * - Long press detection (2000ms threshold)
 * - Callback-based event system
 * - Deep sleep trigger on Button 1 long press
 */

// ============================================================================
// BUTTON STATES
// ============================================================================

enum ButtonState {
    BTN_STATE_IDLE,         // Button not pressed
    BTN_STATE_DEBOUNCE,     // Button pressed, waiting for debounce
    BTN_STATE_PRESSED,      // Button confirmed pressed
    BTN_STATE_LONG_WAIT,    // Waiting for long press threshold
};

// ============================================================================
// BUTTON STRUCTURE
// ============================================================================

typedef struct {
    uint8_t pin;                    // GPIO pin number
    uint8_t buttonId;               // Button ID (1-3)
    ButtonState state;              // Current state
    uint32_t pressTime;             // millis() when button was first pressed
    uint32_t debounceTime;          // millis() when debounce started
    volatile uint32_t lastInterruptTime;  // ISR: Last interrupt timestamp
    volatile bool interruptFlag;    // ISR: Interrupt occurred
    bool longPressFired;            // True if long press event already fired
} Button;

// ============================================================================
// CALLBACK TYPES
// ============================================================================

typedef void (*ButtonCallback)(uint8_t buttonId);

// ============================================================================
// BUTTON DRIVER CLASS
// ============================================================================

class ButtonDriver {
public:
    // Initialization
    void init();

    // Main loop (call frequently to process interrupt events)
    void loop();

    // Set callbacks
    void setShortPressCallback(ButtonCallback callback);
    void setLongPressCallback(ButtonCallback callback);

    // Get button state
    bool isPressed(uint8_t buttonId) const;

    // ISR handlers (must be public for attachInterrupt)
    static void IRAM_ATTR button1ISR();
    static void IRAM_ATTR button2ISR();
    static void IRAM_ATTR button3ISR();

private:
    // Button processing
    void processButton(Button& btn);
    void handleShortPress(uint8_t buttonId);
    void handleLongPress(uint8_t buttonId);
    void enterDeepSleep();

    // ISR helper
    static void IRAM_ATTR handleISR(uint8_t buttonIndex);

    // Button instances
    Button m_buttons[3];

    // Callbacks
    ButtonCallback m_shortPressCallback;
    ButtonCallback m_longPressCallback;

    // Static instance pointer for ISR access
    static ButtonDriver* s_instance;
};

// Global instance
extern ButtonDriver g_buttons;
