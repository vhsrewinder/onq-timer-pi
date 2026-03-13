#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "Config.h"

/*
 * Queue-Master Remote Control - ESP-NOW Communication Driver
 *
 * This driver handles all ESP-NOW communication between the remote and bridge.
 * It implements the Queue-Master protocol with packet structures defined in the PRD.
 *
 * Protocol Overview:
 * - Remote sends: Button presses, heartbeats, discovery packets
 * - Bridge sends: Timer state updates, announcements, acknowledgments
 * - All packets are 6 bytes for efficiency
 * - Broadcast is used for discovery, then learned MAC for directed communication
 */

// ============================================================================
// PACKET STRUCTURES (All packets are exactly 6 bytes)
// ============================================================================

// Button Press Packet (Remote → Bridge)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_BUTTON (0x02)
    uint8_t remoteId;   // 1-20 (from Config.h)
    uint8_t buttonId;   // 1-3 (physical) or 10-13 (touch)
    uint8_t flags;      // FLAG_LOW_BATTERY if battery low
    uint8_t reserved;   // Reserved for future use
    uint8_t sequence;   // Incrementing packet counter
} ButtonPacket;

// Timer State Packet (Bridge → Remote)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_TIMER_STATE (0x01)
    uint8_t deviceId;   // 0 = bridge
    uint16_t payload;   // Time remaining in seconds (big-endian)
    uint8_t flags;      // FLAG_RUNNING | FLAG_EXPIRED | FLAG_CONNECTED
    uint8_t sequence;   // Incrementing packet counter
} TimerPacket;

// Heartbeat Packet (Remote → Bridge)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_HEARTBEAT (0x03)
    uint8_t remoteId;   // 1-20
    uint8_t batteryPct; // 0-100%
    uint8_t flags;      // FLAG_LOW_BATTERY if battery low
    uint8_t reserved;   // Reserved
    uint8_t sequence;   // Incrementing packet counter
} HeartbeatPacket;

// Acknowledgment Packet (Bridge → Remote)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_ACK (0x04)
    uint8_t deviceId;   // 0 = bridge
    uint8_t ackSeq;     // Sequence number being acknowledged
    uint8_t reserved[2];
    uint8_t sequence;   // Incrementing packet counter
} AckPacket;

// Discovery Packet (Remote → Bridge, broadcast)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_DISCOVER (0x06)
    uint8_t remoteId;   // 1-20
    uint8_t reserved[3];
    uint8_t sequence;   // Incrementing packet counter
} DiscoverPacket;

// Announce Packet (Bridge → Remote, response to discovery)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_ANNOUNCE (0x07)
    uint8_t deviceId;   // 0 = bridge
    uint8_t reserved[3];
    uint8_t sequence;   // Incrementing packet counter
} AnnouncePacket;

// Time Sync Packet (Bridge → Remote, current time)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_TIME_SYNC (0x05)
    uint8_t deviceId;   // 0 = bridge
    uint8_t hours;      // 0-23
    uint8_t minutes;    // 0-59
    uint8_t reserved;   // Reserved
    uint8_t sequence;   // Incrementing packet counter
} TimeSyncPacket;

// Set Timer Packet (Remote → Bridge, set timer value)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_SET_TIMER (0x08)
    uint8_t remoteId;   // 1-20
    uint16_t seconds;   // Timer value in seconds (big-endian)
    uint8_t reserved;   // Reserved
    uint8_t sequence;   // Incrementing packet counter
} SetTimerPacket;

// Set Volume Packet (Remote → Bridge, set system volume)
typedef struct __attribute__((packed)) {
    uint8_t msgType;    // MSG_SET_VOLUME (0x09)
    uint8_t remoteId;   // 1-20
    uint8_t volume;     // Volume 0-100%
    uint8_t reserved[2]; // Reserved
    uint8_t sequence;   // Incrementing packet counter
} SetVolumePacket;

// ============================================================================
// ESP-NOW DRIVER CLASS
// ============================================================================

class ESPNOWDriver {
public:
    // Initialization
    void init();

    // Deinitialization (for resource management when switching to BLE)
    void deinit();

    // Suspend/Resume (temporarily disable without full deinit)
    void suspend();
    void resume();

    // Main loop (call from FreeRTOS task or main loop)
    void loop();

    // Send functions
    void sendButton(uint8_t buttonId);
    void sendHeartbeat(uint8_t batteryPercent, bool lowBattery);
    void sendDiscover();
    void sendSetTimer(uint16_t seconds);  // NEW: Set timer to specific value (presets/undo)
    void sendSetVolume(uint8_t volume);   // NEW: Set system volume (0-100)

    // Getters for current state
    bool isConnected() const;
    uint16_t getTimerSeconds() const;
    uint8_t getTimerFlags() const;
    bool isTimerRunning() const;
    bool isTimerExpired() const;
    bool isBridgeConnected() const;  // Bridge connected to Pi

    // Connection info
    uint32_t getLastPacketTime() const;
    uint8_t getLastSequence() const;
    int8_t getLastRSSI() const;  // Get last received signal strength (dBm)

private:
    // ESP-NOW callbacks (static)
    static void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status);
    static void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len);

    // Packet handlers
    void handleTimerPacket(const TimerPacket* packet);
    void handleAckPacket(const AckPacket* packet);
    void handleAnnouncePacket(const AnnouncePacket* packet, const uint8_t* senderMac);
    void handleTimeSyncPacket(const TimeSyncPacket* packet);

    // Utility functions
    void sendPacket(const void* packet, size_t size);
    uint8_t getNextSequence();
    void updateConnectionState();

    // State variables
    bool m_initialized;               // True if ESP-NOW is initialized
    bool m_suspended;                 // True if temporarily suspended (for BLE)
    uint8_t m_bridgeMac[6];           // Bridge MAC address (broadcast initially)
    bool m_bridgeKnown;               // True once we've learned bridge MAC
    bool m_connected;                 // True if connected to bridge

    uint16_t m_timerSeconds;          // Current timer value in seconds
    uint8_t m_timerFlags;             // Timer state flags

    uint8_t m_sequence;               // Outgoing packet sequence number
    uint8_t m_lastRxSequence;         // Last received sequence number

    uint32_t m_lastPacketTime;        // millis() of last received packet
    uint32_t m_lastHeartbeatTime;     // millis() of last sent heartbeat
    uint32_t m_lastDiscoverTime;      // millis() of last sent discover

    int8_t m_lastRSSI;                // Last received signal strength (dBm)

    // Singleton instance (for static callbacks)
    static ESPNOWDriver* s_instance;
};

// Global instance
extern ESPNOWDriver g_espnow;
