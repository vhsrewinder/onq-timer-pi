#include "USBSerial_Driver.h"

// Global instance
USBSerialDriver g_usbSerial;

// ============================================================================
// INITIALIZATION
// ============================================================================

void USBSerialDriver::init() {
    m_rxPos = 0;
    m_timerSeconds = 0;
    m_timerFlags = 0;
    m_connected = false;
    m_wasRunning = false;
    m_lastRxTime = 0;

    // Serial is already initialized in setup() — we just use it for JSON protocol
    printf("[USBSerial] USB Serial driver initialized (remoteId=%d)\n", REMOTE_ID);

    // Send initial heartbeat so relay knows we're here
    sendHeartbeat(100, false);
}

void USBSerialDriver::deinit() {
    // No-op for USB serial
}

void USBSerialDriver::suspend() {
    // No-op for USB serial
}

void USBSerialDriver::resume() {
    // No-op for USB serial
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void USBSerialDriver::loop() {
    // Read available serial data into line buffer
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n') {
            // Null-terminate and process the line
            m_rxBuf[m_rxPos] = '\0';
            if (m_rxPos > 0) {
                _handleLine(m_rxBuf);
            }
            m_rxPos = 0;
        } else if (c != '\r') {
            // Append to buffer (skip \r)
            if (m_rxPos < sizeof(m_rxBuf) - 1) {
                m_rxBuf[m_rxPos++] = c;
            } else {
                // Buffer overflow — discard line
                printf("[USBSerial] RX buffer overflow, discarding line\n");
                m_rxPos = 0;
            }
        }
    }

    // Check connection timeout
    if (m_connected && m_lastRxTime > 0) {
        uint32_t elapsed = millis() - m_lastRxTime;
        if (elapsed > USB_SERIAL_CONNECTION_TIMEOUT_MS) {
            m_connected = false;
            m_timerFlags &= ~FLAG_CONNECTED;
            printf("[USBSerial] Connection timeout (%lums since last data)\n", elapsed);
        }
    }
}

// ============================================================================
// SEND FUNCTIONS
// ============================================================================

void USBSerialDriver::sendButton(uint8_t buttonId) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"button-press\",\"remoteId\":%d,\"buttonId\":%d,\"timestamp\":%lu}",
        REMOTE_ID, buttonId, millis());
    _sendJson(buf);
    printf("[USBSerial] Sent button: %d\n", buttonId);
}

void USBSerialDriver::sendHeartbeat(uint8_t batteryPercent, bool lowBattery) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"remote-status\",\"remoteId\":%d,\"battery\":%d,\"rssi\":0,\"connected\":%s}",
        REMOTE_ID, batteryPercent, m_connected ? "true" : "false");
    _sendJson(buf);
}

void USBSerialDriver::sendDiscover() {
    // No-op for USB serial — wired connection doesn't need discovery
}

void USBSerialDriver::sendSetTimer(uint16_t seconds) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"set-time\",\"remoteId\":%d,\"seconds\":%d}",
        REMOTE_ID, seconds);
    _sendJson(buf);
    printf("[USBSerial] Sent set-time: %ds\n", seconds);
}

void USBSerialDriver::sendSetVolume(uint8_t volume) {
    char buf[64];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"set-volume\",\"remoteId\":%d,\"volume\":%d}",
        REMOTE_ID, volume);
    _sendJson(buf);
}

// ============================================================================
// GETTERS
// ============================================================================

bool USBSerialDriver::isConnected() const {
    return m_connected;
}

uint16_t USBSerialDriver::getTimerSeconds() const {
    return m_timerSeconds;
}

uint8_t USBSerialDriver::getTimerFlags() const {
    return m_timerFlags;
}

bool USBSerialDriver::isTimerRunning() const {
    return (m_timerFlags & FLAG_RUNNING) != 0;
}

bool USBSerialDriver::isTimerExpired() const {
    return (m_timerFlags & FLAG_EXPIRED) != 0;
}

bool USBSerialDriver::isBridgeConnected() const {
    return (m_timerFlags & FLAG_CONNECTED) != 0;
}

uint32_t USBSerialDriver::getLastPacketTime() const {
    return m_lastRxTime;
}

uint8_t USBSerialDriver::getLastSequence() const {
    return 0;  // No sequence numbers in JSON protocol
}

int8_t USBSerialDriver::getLastRSSI() const {
    return 0;  // No RSSI for wired connection
}

// ============================================================================
// LINE PARSING
// ============================================================================

void USBSerialDriver::_handleLine(const char* line) {
    // Find message type
    const char* typeStart = strstr(line, "\"type\":\"");
    if (!typeStart) {
        printf("[USBSerial] No type field in: %s\n", line);
        return;
    }

    m_lastRxTime = millis();

    // Check message type
    if (strstr(typeStart, "\"timer-update\"") || strstr(typeStart, "\"timer-sync\"") || strstr(typeStart, "\"timer-state\"")) {
        _handleTimerUpdate(line);
    } else if (strstr(typeStart, "\"time-sync\"")) {
        _handleTimeSync(line);
    } else {
        printf("[USBSerial] Unknown type in: %s\n", line);
    }
}

void USBSerialDriver::_handleTimerUpdate(const char* json) {
    int time = 0;
    bool isRunning = false;

    if (!_getIntField(json, "time", &time)) {
        printf("[USBSerial] Missing 'time' field\n");
        return;
    }

    _getBoolField(json, "isRunning", &isRunning);

    // Check if we also got flags directly (from timer-state type)
    int flags = -1;
    if (_getIntField(json, "flags", &flags)) {
        // Direct flags from relay — use as-is
        m_timerFlags = (uint8_t)flags;
    } else {
        // Derive flags from isRunning boolean
        m_timerFlags = FLAG_CONNECTED;  // always set when receiving data

        if (isRunning) {
            m_timerFlags |= FLAG_RUNNING;
            m_wasRunning = true;
        } else if (time == 0 && m_wasRunning) {
            m_timerFlags |= FLAG_EXPIRED;
            m_wasRunning = false;
        }
    }

    m_timerSeconds = (uint16_t)time;

    // Mark as connected
    if (!m_connected) {
        m_connected = true;
        printf("[USBSerial] Connected to relay\n");
    }

    printf("[USBSerial] Timer: %ds, flags: 0x%02X\n", m_timerSeconds, m_timerFlags);
}

void USBSerialDriver::_handleTimeSync(const char* json) {
    int hours = 0, minutes = 0;
    _getIntField(json, "hours", &hours);
    _getIntField(json, "minutes", &minutes);
    printf("[USBSerial] Time sync: %02d:%02d\n", hours, minutes);
    // Timer UI can use this for RTC display if needed
}

// ============================================================================
// HELPERS
// ============================================================================

void USBSerialDriver::_sendJson(const char* json) {
    Serial.println(json);  // println adds \n
}

bool USBSerialDriver::_getIntField(const char* json, const char* field, int* out) {
    // Search for "field": or "field" :
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);

    const char* pos = strstr(json, pattern);
    if (!pos) return false;

    // Skip past the pattern
    pos += strlen(pattern);

    // Skip whitespace
    while (*pos == ' ') pos++;

    // Parse integer (handles negative)
    *out = atoi(pos);
    return true;
}

bool USBSerialDriver::_getBoolField(const char* json, const char* field, bool* out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);

    const char* pos = strstr(json, pattern);
    if (!pos) return false;

    pos += strlen(pattern);
    while (*pos == ' ') pos++;

    *out = (strncmp(pos, "true", 4) == 0);
    return true;
}
