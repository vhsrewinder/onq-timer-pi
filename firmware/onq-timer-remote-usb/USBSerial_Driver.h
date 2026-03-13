#pragma once

#include <Arduino.h>
#include "Config.h"

/*
 * Queue-Master Remote Control - USB Serial Communication Driver
 *
 * Drop-in replacement for ESPNOW_Driver when COMMUNICATION_MODE == COMM_MODE_USB_SERIAL.
 * Communicates with a Relay Pi via USB CDC serial using newline-delimited JSON.
 *
 * Protocol (Stopwatch → Relay Pi):
 *   {"type":"button-press","remoteId":1,"buttonId":10,"timestamp":12345}
 *   {"type":"set-time","remoteId":1,"seconds":300}
 *   {"type":"remote-status","remoteId":1,"battery":85,"rssi":0,"connected":true}
 *
 * Protocol (Relay Pi → Stopwatch):
 *   {"type":"timer-update","time":120,"isRunning":true}
 *   {"type":"timer-sync","time":120,"isRunning":true,"serverTimestamp":1710423654892}
 *   {"type":"time-sync","hours":14,"minutes":30}
 */

// USB serial heartbeat interval (slower than ESP-NOW since wired is reliable)
#define USB_SERIAL_HEARTBEAT_INTERVAL_MS 10000

// Connection timeout (no data from relay in this window = disconnected)
#define USB_SERIAL_CONNECTION_TIMEOUT_MS 15000

class USBSerialDriver {
public:
    // Initialization
    void init();

    // Deinitialization (no-op for USB, included for interface parity)
    void deinit();

    // Suspend/Resume (no-op for USB)
    void suspend();
    void resume();

    // Main loop — call from FreeRTOS task or main loop
    void loop();

    // Send functions (same interface as ESPNOWDriver)
    void sendButton(uint8_t buttonId);
    void sendHeartbeat(uint8_t batteryPercent, bool lowBattery);
    void sendDiscover();  // No-op for USB (no discovery needed)
    void sendSetTimer(uint16_t seconds);
    void sendSetVolume(uint8_t volume);

    // Getters (same interface as ESPNOWDriver)
    bool isConnected() const;
    uint16_t getTimerSeconds() const;
    uint8_t getTimerFlags() const;
    bool isTimerRunning() const;
    bool isTimerExpired() const;
    bool isBridgeConnected() const;

    // Connection info
    uint32_t getLastPacketTime() const;
    uint8_t getLastSequence() const;  // Always 0 for USB
    int8_t getLastRSSI() const;       // Always 0 for USB (wired)

private:
    // Line buffer for incoming serial data
    char m_rxBuf[512];
    uint16_t m_rxPos;

    // Timer state (received from relay)
    uint16_t m_timerSeconds;
    uint8_t m_timerFlags;
    bool m_connected;
    bool m_wasRunning;  // track previous running state for FLAG_EXPIRED

    uint32_t m_lastRxTime;

    // Parse handlers
    void _handleLine(const char* line);
    void _handleTimerUpdate(const char* json);
    void _handleTimeSync(const char* json);

    // JSON output helpers
    void _sendJson(const char* json);

    // Simple JSON field extraction (no ArduinoJson dependency)
    static bool _getIntField(const char* json, const char* field, int* out);
    static bool _getBoolField(const char* json, const char* field, bool* out);
};

// Global instance
extern USBSerialDriver g_usbSerial;
