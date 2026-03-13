#include "ESPNOW_Driver.h"
#include "Timer_UI.h"  // For updating real-time clock

// Global instance
ESPNOWDriver g_espnow;

// Static instance pointer for callbacks
ESPNOWDriver* ESPNOWDriver::s_instance = nullptr;

// ============================================================================
// INITIALIZATION
// ============================================================================

void ESPNOWDriver::init() {
    // Set singleton instance
    s_instance = this;

    // Initialize state
    // Load bridge MAC from config (may be broadcast FF:FF:FF:FF:FF:FF for auto-discovery)
    uint8_t configuredMac[6] = BRIDGE_MAC_ADDR;
    memcpy(m_bridgeMac, configuredMac, 6);

    // Check if bridge MAC is configured (not broadcast)
    bool isBroadcast = true;
    for (int i = 0; i < 6; i++) {
        if (m_bridgeMac[i] != 0xFF) {
            isBroadcast = false;
            break;
        }
    }
    m_bridgeKnown = !isBroadcast;  // We know the bridge if MAC is configured
    m_connected = false;
    m_timerSeconds = 0;
    m_timerFlags = 0;
    m_sequence = 0;
    m_lastRxSequence = 0;
    m_lastPacketTime = 0;
    m_lastHeartbeatTime = 0;
    m_lastDiscoverTime = 0;
    m_lastRSSI = -127;  // Initialize to minimum value

    // Set WiFi mode to STA (required for ESP-NOW)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Don't connect to any AP

    // POWER OPTIMIZATION: WiFi power save mode (configurable in Config.h)
    // WIFI_PS_MIN_MODEM: Light sleep between packets (~60-70mA saved)
    // WIFI_PS_NONE: Always on for max performance (range testing)
    esp_wifi_set_ps(WIFI_POWER_MODE);
    printf("[ESPNOW] WiFi power save mode: %s (%s)\n",
           WIFI_POWER_MODE == WIFI_PS_NONE ? "NONE" : "MIN_MODEM",
           POWER_MODE_NAME);

    // Wait for WiFi to be ready (MAC address available)
    delay(100);

    // Print MAC address for debugging
    printf("[ESPNOW] ESP-NOW Remote MAC: %s\n", WiFi.macAddress().c_str());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        DEBUG_ESPNOW_PRINTLN("ERROR: ESP-NOW init failed");
        return;
    }

    DEBUG_ESPNOW_PRINTLN("ESP-NOW initialized");

#if ESPNOW_ENABLE_ENCRYPTION
    // Set Primary Master Key (PMK) for encryption
    uint8_t pmk[16] = ESPNOW_PMK;
    if (esp_now_set_pmk(pmk) != ESP_OK) {
        DEBUG_ESPNOW_PRINTLN("ERROR: Failed to set PMK");
        return;
    }
    DEBUG_ESPNOW_PRINTLN("ESP-NOW encryption enabled (AES-128-CCM)");
#else
    DEBUG_ESPNOW_PRINTLN("ESP-NOW encryption disabled");
#endif

    // Set WiFi channel for ESP-NOW (CRITICAL: must match bridge!)
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    printf("[ESPNOW] WiFi channel set to %d\n", ESPNOW_CHANNEL);

    // CRITICAL: Set WiFi country for regulatory compliance and maximum TX power
    // Must be set AFTER esp_now_init() and BEFORE WiFi.setTxPower()
    wifi_country_t country = {
        .cc = "US",           // Country code (USA)
        .schan = 1,           // Start channel
        .nchan = 11,          // Number of channels (1-11 legal in USA)
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };
    esp_wifi_set_country(&country);
    printf("[ESPNOW] WiFi country set to USA (channels 1-11)\n");

    // POWER OPTIMIZATION: TX power (configurable in Config.h)
    // 11dBm: Battery saver (~50-100ft range, saves ~30-50mA)
    // 19.5dBm: Max range for long distance or interference
    // NOTE: Must be set AFTER esp_now_init() or it gets reset to default
    WiFi.setTxPower(WIFI_TX_POWER);
    const char* txPowerStr = (WIFI_TX_POWER == WIFI_POWER_19_5dBm) ? "19.5dBm (MAX)" :
                             (WIFI_TX_POWER == WIFI_POWER_11dBm) ? "11dBm" : "OTHER";
    printf("[ESPNOW] TX Power: %s (%s mode)\n", txPowerStr, POWER_MODE_NAME);

    // Verification: Check that TX power was actually applied
    int8_t powerDbm = 0;
    esp_wifi_get_max_tx_power(&powerDbm);
    printf("[ESPNOW] ========================================\n");
    printf("[ESPNOW] WiFi Configuration Verification:\n");
    printf("[ESPNOW]   Channel: %d\n", ESPNOW_CHANNEL);
    printf("[ESPNOW]   TX Power: %d (raw) = %.1f dBm\n", powerDbm, powerDbm / 4.0);
    printf("[ESPNOW]   Expected: 78 (raw) = 19.5 dBm\n");
    if (powerDbm < 78) {
        printf("[ESPNOW]   ⚠️  WARNING: TX power is LOWER than expected!\n");
        printf("[ESPNOW]   ⚠️  This will severely limit range!\n");
    } else {
        printf("[ESPNOW]   ✓ TX power verified - full power enabled\n");
    }
    printf("[ESPNOW] ========================================\n");

    // Enable ESP32 Long-Range mode for maximum distance
    // This sacrifices data rate for range and interference resistance
    // NOTE: This failed in v2.3.1 due to TX power bug, trying again with power fixed
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);  // ESP32 proprietary long-range only
    printf("[ESPNOW] Long-range protocol enabled (LR mode)\n");

    // CRITICAL: Set ESP-NOW PHY rate to 250K for MAXIMUM range
    // This is the missing piece! LR mode needs explicit rate setting.
    // 250K = max range (220-480m), 512K = balanced, 1M = default
    esp_err_t rate_result = esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_LORA_250K);
    if (rate_result == ESP_OK) {
        printf("[ESPNOW] ✓ PHY rate successfully set to 250 Kbps\n");
    } else {
        printf("[ESPNOW] ⚠️  WARNING: PHY rate config FAILED (error: %d)\n", rate_result);
        printf("[ESPNOW] ⚠️  Running at default 1 Mbps - range will be limited!\n");
    }

    // Force 20MHz bandwidth (narrower = better range)
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    printf("[ESPNOW] Bandwidth set to 20MHz\n");

    // Register callbacks
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // Add peer (bridge or broadcast)
    // CRITICAL: ALWAYS start with UNENCRYPTED peer for DISCOVER phase
    // This allows bridge to receive DISCOVER and add us as encrypted peer
    // After ANNOUNCE, we can switch to encrypted (but for simplicity, keep unencrypted initial peer)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, m_bridgeMac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;  // Start unencrypted for DISCOVER phase

    if (m_bridgeKnown) {
        printf("[ESPNOW] Adding configured bridge MAC (unencrypted for discovery): ");
        for (int i = 0; i < 6; i++) {
            printf("%02X", m_bridgeMac[i]);
            if (i < 5) printf(":");
        }
        printf("\n");
    } else {
        DEBUG_ESPNOW_PRINTLN("Using broadcast for discovery (unencrypted)");
    }

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        DEBUG_ESPNOW_PRINTLN("ERROR: Failed to add peer");
        return;
    }

    // Always send DISCOVER on startup (even when bridge MAC is known)
    // This ensures bridge adds us as an encrypted peer for receiving our packets
    DEBUG_ESPNOW_PRINTLN("ESP-NOW ready, sending discovery...");
    sendDiscover();

    m_initialized = true;
    m_suspended = false;
}

// ============================================================================
// DEINITIALIZATION (for switching to BLE)
// ============================================================================

void ESPNOWDriver::deinit() {
    if (!m_initialized) {
        printf("[ESPNOW] Already deinitialized\n");
        return;
    }

    printf("[ESPNOW] Deinitializing for BLE mode...\n");

    // Unregister callbacks
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();

    // Deinitialize ESP-NOW
    esp_now_deinit();

    // CRITICAL: Keep WiFi on but disconnect from AP
    // BLE needs WiFi radio to be initialized (they share the same 2.4GHz radio)
    // Just disconnect and set to STA mode without full shutdown
    WiFi.disconnect(true);  // Disconnect from AP
    delay(100);  // Give WiFi time to disconnect

    m_initialized = false;
    m_suspended = false;
    m_connected = false;

    printf("[ESPNOW] Deinitialized - ESP-NOW stopped (WiFi radio kept on for BLE)\n");
}

// ============================================================================
// SUSPEND/RESUME (lighter weight - keeps WiFi on but stops transmitting)
// ============================================================================

void ESPNOWDriver::suspend() {
    if (!m_initialized || m_suspended) {
        return;
    }

    printf("[ESPNOW] Suspending for BLE operation...\n");
    m_suspended = true;
    m_connected = false;  // Mark as disconnected
}

void ESPNOWDriver::resume() {
    if (!m_initialized || !m_suspended) {
        return;
    }

    printf("[ESPNOW] Resuming from BLE operation...\n");
    m_suspended = false;

    // Send discovery to reconnect
    sendDiscover();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void ESPNOWDriver::loop() {
    // Skip if not initialized or suspended
    if (!m_initialized || m_suspended) {
        return;
    }
    uint32_t now = millis();

    // Update connection state based on last packet time
    updateConnectionState();

    // Send heartbeat periodically (if connected)
    if (m_connected && (now - m_lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)) {
        // Heartbeat will be sent by external code calling sendHeartbeat()
        // This is just tracking the interval
        m_lastHeartbeatTime = now;
    }

    // Send discovery if not connected (every 2 seconds)
    // OR send occasional discovery even when connected (every 30 seconds) as keepalive
    // This ensures bridge has us as peer even if it missed initial DISCOVER
    uint32_t discoverInterval = m_connected ? 30000 : DISCOVER_INTERVAL_MS;  // 30s when connected, 2s when not
    if (now - m_lastDiscoverTime >= discoverInterval) {
        sendDiscover();
        m_lastDiscoverTime = now;
    }
}

// ============================================================================
// SEND FUNCTIONS
// ============================================================================

void ESPNOWDriver::sendButton(uint8_t buttonId) {
    ButtonPacket packet;
    packet.msgType = MSG_BUTTON;
    packet.remoteId = REMOTE_ID;
    packet.buttonId = buttonId;
    packet.flags = 0;  // Will be set by caller if low battery
    packet.reserved = 0;
    packet.sequence = getNextSequence();

    printf("[ESPNOW] Sending button: %d\n", buttonId);

    sendPacket(&packet, sizeof(packet));
}

void ESPNOWDriver::sendHeartbeat(uint8_t batteryPercent, bool lowBattery) {
    HeartbeatPacket packet;
    packet.msgType = MSG_HEARTBEAT;
    packet.remoteId = REMOTE_ID;
    packet.batteryPct = batteryPercent;
    packet.flags = lowBattery ? FLAG_LOW_BATTERY : 0;
    packet.reserved = 0;
    packet.sequence = getNextSequence();

    printf("[ESPNOW] Sending heartbeat, battery: %d%%\n", batteryPercent);

    sendPacket(&packet, sizeof(packet));
    m_lastHeartbeatTime = millis();
}

void ESPNOWDriver::sendDiscover() {
    DiscoverPacket packet;
    packet.msgType = MSG_DISCOVER;
    packet.remoteId = REMOTE_ID;
    memset(packet.reserved, 0, sizeof(packet.reserved));
    packet.sequence = getNextSequence();

    // DISCOVER is always sent to broadcast (unencrypted due to ESP-NOW limitation)
    // After bridge responds with ANNOUNCE, we switch to encrypted communication
    printf("[ESPNOW] Sending DISCOVER (unencrypted broadcast) to find bridge, remoteId=%d, seq=%d\n", REMOTE_ID, packet.sequence);

    sendPacket(&packet, sizeof(packet));
    m_lastDiscoverTime = millis();
}

void ESPNOWDriver::sendSetTimer(uint16_t seconds) {
    // Validate timer range (prevent integer overflow and invalid values)
    if (seconds > 3600) {
        printf("[ESPNOW] WARNING: Timer value %d exceeds 1 hour, clamping to 3600\n", seconds);
        seconds = 3600;  // Clamp to maximum (1 hour)
    }

    SetTimerPacket packet;
    packet.msgType = MSG_SET_TIMER;
    packet.remoteId = REMOTE_ID;
    // Convert to big-endian for network byte order
    packet.seconds = (seconds << 8) | (seconds >> 8);
    packet.reserved = 0;
    packet.sequence = getNextSequence();

    printf("[ESPNOW] Sending SET_TIMER: %d seconds\n", seconds);

    sendPacket(&packet, sizeof(packet));
}

void ESPNOWDriver::sendSetVolume(uint8_t volume) {
    SetVolumePacket packet;
    packet.msgType = MSG_SET_VOLUME;
    packet.remoteId = REMOTE_ID;
    packet.volume = volume;
    memset(packet.reserved, 0, sizeof(packet.reserved));
    packet.sequence = getNextSequence();

    printf("[ESPNOW] Sending SET_VOLUME: %d%%\n", volume);

    sendPacket(&packet, sizeof(packet));
}

// ============================================================================
// GETTERS
// ============================================================================

bool ESPNOWDriver::isConnected() const {
    return m_connected;
}

uint16_t ESPNOWDriver::getTimerSeconds() const {
    return m_timerSeconds;
}

uint8_t ESPNOWDriver::getTimerFlags() const {
    return m_timerFlags;
}

bool ESPNOWDriver::isTimerRunning() const {
    return (m_timerFlags & FLAG_RUNNING) != 0;
}

bool ESPNOWDriver::isTimerExpired() const {
    return (m_timerFlags & FLAG_EXPIRED) != 0;
}

bool ESPNOWDriver::isBridgeConnected() const {
    return (m_timerFlags & FLAG_CONNECTED) != 0;
}

uint32_t ESPNOWDriver::getLastPacketTime() const {
    return m_lastPacketTime;
}

uint8_t ESPNOWDriver::getLastSequence() const {
    return m_lastRxSequence;
}

int8_t ESPNOWDriver::getLastRSSI() const {
    return m_lastRSSI;
}

// ============================================================================
// ESP-NOW CALLBACKS (STATIC)
// ============================================================================

void ESPNOWDriver::onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        DEBUG_ESPNOW_PRINTLN("Packet sent successfully");
    } else {
        DEBUG_ESPNOW_PRINTLN("ERROR: Packet send failed");
    }
}

void ESPNOWDriver::onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
    // CRITICAL: This callback runs in WiFi interrupt context!
    // Keep it as fast as possible - NO printf, NO Serial, NO delay, NO blocking calls!
    // ONLY update state variables and defer processing to loop()

    if (s_instance == nullptr || recv_info == nullptr || data == nullptr || data_len < 1) {
        return;
    }

    // Extract MAC address from recv_info
    const uint8_t *mac_addr = recv_info->src_addr;

    // Update last packet time and RSSI (fast operations only)
    s_instance->m_lastPacketTime = millis();
    s_instance->m_lastRSSI = recv_info->rx_ctrl->rssi;

    // Parse message type
    uint8_t msgType = data[0];

    // REMOVED: printf() calls - these cause watchdog resets and crashes in interrupt context!
    // Logging moved to loop() for debugging

    // Route to appropriate handler (handlers should also be fast and interrupt-safe!)
    switch (msgType) {
        case MSG_TIMER_STATE:
            if (data_len == sizeof(TimerPacket)) {
                s_instance->handleTimerPacket((const TimerPacket*)data);
            }
            break;

        case MSG_ACK:
            if (data_len == sizeof(AckPacket)) {
                s_instance->handleAckPacket((const AckPacket*)data);
            }
            break;

        case MSG_ANNOUNCE:
            if (data_len == sizeof(AnnouncePacket)) {
                s_instance->handleAnnouncePacket((const AnnouncePacket*)data, mac_addr);
            }
            break;

        case MSG_TIME_SYNC:
            if (data_len == sizeof(TimeSyncPacket)) {
                s_instance->handleTimeSyncPacket((const TimeSyncPacket*)data);
            }
            break;

        default:
            // Unknown message type - silently ignore in interrupt context
            break;
    }
}

// ============================================================================
// PACKET HANDLERS
// ============================================================================

void ESPNOWDriver::handleTimerPacket(const TimerPacket* packet) {
    // CRITICAL: Called from interrupt context - keep fast, NO printf!

    // OFFLINE MODE: Ignore timer updates when in offline mode
    if (g_timerUI.isOfflineMode()) {
        return;  // Silently ignore in interrupt context
    }

    // SECURITY: Validate sequence number to prevent replay attacks
    if (m_lastRxSequence != 0) {  // Skip validation for first packet
        // Calculate difference with wraparound handling
        uint8_t seqDiff = packet->sequence - m_lastRxSequence;

        // Reject if sequence goes backwards or stays same (replay)
        // Allow forward jumps up to 10 (packet loss tolerance)
        if (seqDiff == 0 || seqDiff > 10) {
            return;  // Reject silently in interrupt context
        }
    }

    // Extract timer value (big-endian uint16_t)
    uint16_t seconds = (packet->payload >> 8) | ((packet->payload & 0xFF) << 8);

    // VALIDATION: Sanity check timer value (max 1 hour)
    if (seconds > 3600) {
        return;  // Reject silently in interrupt context
    }

    // VALIDATION: Check flags are within expected range
    if ((packet->flags & 0xF0) != 0) {  // Upper 4 bits should be 0
        // Silently ignore unexpected flags in interrupt context
        // Don't reject - just warn, as new flags may be added in future
    }

    m_timerSeconds = seconds;
    m_timerFlags = packet->flags;
    m_lastRxSequence = packet->sequence;

    // Mark as connected (even from broadcast) to stop spamming DISCOVER
    // This is safe because timer packets mean bridge is alive and we can receive
    // The bridge will add us as peer when we send our first button/heartbeat
    if (!m_connected) {
        m_connected = true;
    }
}

void ESPNOWDriver::handleAckPacket(const AckPacket* packet) {
    // CRITICAL: Called from interrupt context - keep fast, NO printf!

    m_lastRxSequence = packet->sequence;

    // Mark as connected - ACK is unicast, proves bridge has us as peer
    if (!m_connected) {
        m_connected = true;
    }
}

void ESPNOWDriver::handleAnnouncePacket(const AnnouncePacket* packet, const uint8_t* senderMac) {
    m_lastRxSequence = packet->sequence;

    DEBUG_ESPNOW_PRINTLN("Bridge announced!");

#if BRIDGE_MAC_VALIDATION_ENABLED
    // SECURITY: Validate bridge MAC address to prevent MitM attacks
    const uint8_t expectedBridgeMac[] = BRIDGE_MAC_ADDRESS;
    if (memcmp(senderMac, expectedBridgeMac, 6) != 0) {
        return;  // Silently reject in interrupt context
    }
#endif

    // Learn bridge MAC address if not already known (broadcast discovery)
    if (!m_bridgeKnown) {
        // Remove broadcast peer
        esp_now_del_peer(m_bridgeMac);

        // Save bridge MAC
        memcpy(m_bridgeMac, senderMac, 6);
        m_bridgeKnown = true;
    }

    // CRITICAL FIX: Always upgrade to encrypted peer after ANNOUNCE (even if MAC was known)
    // This ensures two-way encrypted communication works
#if ESPNOW_ENABLE_ENCRYPTION
    // Check if peer exists
    bool peerExists = esp_now_is_peer_exist(m_bridgeMac);

    if (peerExists) {
        // Try to modify existing peer first (safer than delete+add)
        esp_now_peer_info_t peerInfo = {};
        if (esp_now_get_peer(m_bridgeMac, &peerInfo) == ESP_OK) {
            // Check if already encrypted
            if (!peerInfo.encrypt) {
                // Delete and re-add as encrypted
                printf("[ESPNOW] Upgrading peer from unencrypted to encrypted\n");
                esp_now_del_peer(m_bridgeMac);

                // Re-add as encrypted
                memset(&peerInfo, 0, sizeof(peerInfo));
                memcpy(peerInfo.peer_addr, m_bridgeMac, 6);
                peerInfo.channel = ESPNOW_CHANNEL;
                peerInfo.encrypt = true;

                if (esp_now_add_peer(&peerInfo) == ESP_OK) {
                    printf("[ESPNOW] Bridge upgraded to encrypted peer\n");
                } else {
                    printf("[ESPNOW] ERROR: Failed to upgrade bridge peer\n");
                    return;  // Don't mark connected if upgrade failed
                }
            } else {
                printf("[ESPNOW] Bridge already encrypted peer\n");
            }
        }
    } else {
        // Add bridge as encrypted peer (first time)
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, m_bridgeMac, 6);
        peerInfo.channel = ESPNOW_CHANNEL;
        peerInfo.encrypt = true;

        if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            printf("[ESPNOW] Bridge added as encrypted peer\n");
        } else {
            printf("[ESPNOW] ERROR: Failed to add encrypted bridge peer\n");
            return;  // Don't mark connected if add failed
        }
    }
#else
    // Unencrypted mode - just ensure peer exists
    if (!esp_now_is_peer_exist(m_bridgeMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, m_bridgeMac, 6);
        peerInfo.channel = ESPNOW_CHANNEL;
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            DEBUG_ESPNOW_PRINTLN("Bridge added as peer");
        } else {
            DEBUG_ESPNOW_PRINTLN("ERROR: Failed to add bridge peer");
            return;
        }
    }
#endif

    // Mark as connected
    if (!m_connected) {
        m_connected = true;
    }
}

void ESPNOWDriver::handleTimeSyncPacket(const TimeSyncPacket* packet) {
    // CRITICAL: Called from interrupt context - keep fast, NO printf!

    m_lastRxSequence = packet->sequence;

    // Update Timer UI with current time for clock display
    g_timerUI.setCurrentTime(packet->hours, packet->minutes);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void ESPNOWDriver::sendPacket(const void* packet, size_t size) {
    // Send to bridge (or broadcast if not known yet)
    esp_err_t result = esp_now_send(m_bridgeMac, (uint8_t*)packet, size);

    // Note: Don't log errors here if called from interrupt context
    // Error handling deferred to calling function if needed
    (void)result;  // Suppress unused variable warning
}

uint8_t ESPNOWDriver::getNextSequence() {
    return m_sequence++;
}

void ESPNOWDriver::updateConnectionState() {
    // Check if we've timed out
    if (m_connected && m_lastPacketTime > 0) {
        uint32_t timeSinceLastPacket = millis() - m_lastPacketTime;

        if (timeSinceLastPacket > CONNECTION_TIMEOUT_MS) {
            m_connected = false;
            DEBUG_ESPNOW_PRINTLN("Connection timeout, disconnected");
        }
    }
}
