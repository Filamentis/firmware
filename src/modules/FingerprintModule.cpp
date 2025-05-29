#include "FingerprintModule.h"
#include "GPS.h" // For accessing gps->p
#include "NodeDB.h" // For now()
#include "meshUtils.h" // For LOG_DEBUG, etc.
#include "globals.h" // For access to the main 'radio' object if needed for LoRa signals later
#include "FSCommon.h" // For fsInit and file operations
#include "SafeFile.h" // For SafeFile
#include "NodeDB.h"   // For nodeDB and meshtastic_NodeInfoLite
#include "MeshTypes.h" // For NODE_NAME_LEN (used by User.long_name)
#include "meshUtils.h" // For bytesToHexString
#include <pb_encode.h> // For pb_encode
#include <pb_decode.h> // For pb_decode
#include <pb_common.h> // For PB_ostream_from_buffer, PB_istream_from_buffer
#include <vector>      // For std::vector
#include <algorithm>   // For std::min

#if defined(ARCH_ESP32)
#include "ble/BLEScanner.h" // ESP32 specific
// BLEBeaconManager.h is included via FingerprintModule.h
#endif

FingerprintModule::FingerprintModule() : Module()
{
    // Constructor
#if defined(ARCH_ESP32)
    // Initialization of ble_scanner_ and ble_beacon_manager_ is deferred to setup()
    last_anchor_status_ = false;
#endif
    // last_collection_time_ = 0;
    // collection_interval_ms_ = 60000; // Example: 1 minute
    // current_event_ is default constructed
}

void FingerprintModule::onLoRaPacketReceived(const meshtastic_MeshPacket* packet)
{
    if (!packet || packet->from == 0 || packet->from == nodeDB->getNodeNum()) return; // Ignore packets from no-one or self

    if (recent_lora_signals_.size() >= 20) { // Cap recent signals before processing (arbitrary limit for now)
        LOG_WARN("Fingerprint: recent_lora_signals_ buffer full (20). Discarding new LoRa signal data from 0x%08x.", packet->from);
        return;
    }

    InternalLoRaSignalData lora_signal;
    lora_signal.node_id = packet->from;
    lora_signal.rssi = packet->rx_rssi; // Note: This was missing in InternalLoRaSignalData struct def, added in Phase 1
    lora_signal.snr = packet->rx_snr;
    
    // Get node name
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(packet->from);
    if (node && node->has_user) {
        if (node->user.long_name[0] != '\0') {
            strncpy(lora_signal.node_name, node->user.long_name, MAX_NODE_NAME_LEN -1);
        } else if (node->user.short_name[0] != '\0') {
            strncpy(lora_signal.node_name, node->user.short_name, MAX_NODE_NAME_LEN -1);
        } else {
            snprintf(lora_signal.node_name, MAX_NODE_NAME_LEN, "!%08lx", (unsigned long)packet->from);
        }
    } else {
        snprintf(lora_signal.node_name, MAX_NODE_NAME_LEN, "!%08lx", (unsigned long)packet->from);
    }
    lora_signal.node_name[MAX_NODE_NAME_LEN - 1] = '\0'; // Ensure null termination

    recent_lora_signals_.push_back(lora_signal);

    LOG_DEBUG("Fingerprint: LoRa signal from %s (0x%08x) (RSSI: %d, SNR: %.2f) stored. Count: %d",
              lora_signal.node_name, packet->from, packet->rx_rssi, packet->rx_snr, recent_lora_signals_.size());
}

void FingerprintModule::setup()
{
    LOG_INFO("FingerprintModule::setup()");
#if defined(ARCH_ESP32)
    if (!ble_scanner_) { 
        ble_scanner_ = std::make_unique<meshtastic::BLEScanner>();
        LOG_INFO("BLEScanner initialized for FingerprintModule.");
    }
    if (!ble_beacon_manager_) {
        ble_beacon_manager_ = std::make_unique<meshtastic::BLEBeaconManager>();
        LOG_INFO("BLEBeaconManager initialized for FingerprintModule.");
    }

    // Check initial anchor status and start/stop advertising
    // Ensure config is loaded by this point. NodeDB which loads config is initialized before setupModules.
    if (config.has_fingerprint_config && config.fingerprint_config.is_anchor_node) {
        LOG_INFO("Device is configured as an anchor node. Starting BLE beacon advertising.");
        if (ble_beacon_manager_ && ble_beacon_manager_->startAdvertising()) {
            last_anchor_status_ = true;
        } else {
            LOG_ERROR("Failed to start anchor beacon advertising on setup.");
            last_anchor_status_ = false; // Ensure it's false if start failed
        }
    } else {
        LOG_INFO("Device is not an anchor node initially, or no fingerprint_config. Ensuring advertising is off.");
        if (ble_beacon_manager_ && ble_beacon_manager_->isAdvertising()){ // Stop only if it was somehow on
             ble_beacon_manager_->stopAdvertising();
        }
        last_anchor_status_ = false;
    }
#else
    LOG_INFO("BLE scanning and beaconing not supported on this platform for FingerprintModule.");
#endif

    // For testing, let's call collectFingerprintData once in setup
    // In the future, this would be triggered by a timer in loop() or a command
    // collectFingerprintData();
}

void FingerprintModule::loop()
{
    // Example of timed collection - this should be refined
    // if (millis() - last_collection_time_ > collection_interval_ms_) {
    //     collectFingerprintData();
    //     last_collection_time_ = millis();
    // }

#if defined(ARCH_ESP32)
    if (ble_beacon_manager_) { // Ensure manager is initialized
        bool current_anchor_status = (config.has_fingerprint_config && config.fingerprint_config.is_anchor_node);
        if (current_anchor_status != last_anchor_status_) {
            if (current_anchor_status) {
                LOG_INFO("Anchor mode enabled. Starting BLE beacon advertising.");
                if(!ble_beacon_manager_->startAdvertising()){
                    LOG_ERROR("Failed to start anchor beacon advertising on config change.");
                    // If start fails, we should reflect that current_anchor_status effectively couldn't be applied for advertising.
                    // However, last_anchor_status_ will be updated to current_anchor_status to prevent repeated attempts each loop.
                    // A more robust system might retry or set an error state.
                }
            } else {
                LOG_INFO("Anchor mode disabled. Stopping BLE beacon advertising.");
                ble_beacon_manager_->stopAdvertising();
            }
            last_anchor_status_ = current_anchor_status;
        }
    }
#endif
}

void FingerprintModule::collectFingerprintData()
{
    LOG_INFO("FingerprintModule::collectFingerprintData() called");

    // Clear previous record
    fingerprint_record_ = meshtastic_FingerprintRecord_init_default;

    // 1. Timestamp
    fingerprint_record_.timestamp = now();
    fingerprint_record_.has_timestamp = true;

    // 2. Location
    if (gps && gps->hasLock() && gps->p.has_latitude_i && gps->p.has_longitude_i) {
        fingerprint_record_.location = gps->p; // Copy the current position
        fingerprint_record_.has_location = true;
        LOG_DEBUG("Fingerprint: Location acquired: lat=%d, lon=%d, alt=%d", 
                  fingerprint_record_.location.latitude_i, 
                  fingerprint_record_.location.longitude_i,
                  fingerprint_record_.location.altitude);
    } else {
        LOG_DEBUG("Fingerprint: No valid GPS lock for location data.");
        fingerprint_record_.has_location = false;
    }

    // 3. BLE Data Collection
#if defined(ARCH_ESP32)
    if (ble_scanner_) {
        LOG_DEBUG("Fingerprint: Starting BLE scan...");
        // Perform a scan for a few seconds (e.g., 5 seconds)
        // Note: performScan is synchronous for now as per BLEScanner.h
        ble_scanner_->performScan(5); 
        
        // Clear previous beacons
        fingerprint_record_.ble_beacons_count = 0; 

        if (ble_scanner_->latest_results.results_count > 0) {
            LOG_DEBUG("Fingerprint: BLE scan found %d devices.", ble_scanner_->latest_results.results_count);
            int beacons_to_copy = std::min((int)ble_scanner_->latest_results.results_count, (int)MESHTASTIC_FINGERPRINTRECORD_BLE_BEACONS_MAX_COUNT);
            
            for (int i = 0; i < beacons_to_copy; ++i) {
                fingerprint_record_.ble_beacons[i] = ble_scanner_->latest_results.results[i];
                fingerprint_record_.ble_beacons_count++;
            }
        } else {
            LOG_DEBUG("Fingerprint: BLE scan found no devices.");
        }
        fingerprint_record_.has_ble_beacons = fingerprint_record_.ble_beacons_count > 0;
    } else {
        LOG_WARN("Fingerprint: BLEScanner not initialized on ESP32.");
        fingerprint_record_.has_ble_beacons = false;
    }
#else
    fingerprint_record_.has_ble_beacons = false; // No BLE scanning on other platforms
#endif

    // 4. LoRa Data Collection (Placeholder)
    LOG_DEBUG("Fingerprint: LoRa signal collection is a placeholder.");
    fingerprint_record_.lora_signals_count = 0; // No LoRa signals collected yet
    // 4. LoRa Data Collection (from recently received packets)
    fingerprint_record_.lora_signals_count = 0; // Reset count for the new record
    if (!recent_lora_signals_.empty()) {
        LOG_DEBUG("Fingerprint: Copying %d recent LoRa signals to record.", recent_lora_signals_.size());
        for (const auto& signal : recent_lora_signals_) {
            if (fingerprint_record_.lora_signals_count < MESHTASTIC_FINGERPRINTRECORD_LORA_SIGNALS_MAX_COUNT) {
                fingerprint_record_.lora_signals[fingerprint_record_.lora_signals_count++] = signal;
            } else {
                LOG_WARN("Fingerprint: Max LoRa signals reached for current record.");
                break;
            }
        }
        recent_lora_signals_.clear(); // Clear the temporary list after copying
    }
    fingerprint_record_.has_lora_signals = (fingerprint_record_.lora_signals_count > 0);


    // Log the assembled record (basic logging for now)
    LOG_INFO("Fingerprint Record Assembled: Timestamp=%u, HasLocation=%d, BLE_Count=%d, LoRa_Count=%d",
              fingerprint_record_.timestamp,
              fingerprint_record_.has_location,
              fingerprint_record_.ble_beacons_count,
              fingerprint_record_.lora_signals_count);
    if(fingerprint_record_.has_location) {
        LOG_DEBUG("Location: Lat=%d, Lon=%d, Alt=%d", fingerprint_record_.location.latitude_i, fingerprint_record_.location.longitude_i, fingerprint_record_.location.altitude);
    }
    for(int i=0; i < fingerprint_record_.ble_beacons_count; ++i) {
        char macStr[18];
        bytesToHexString(fingerprint_record_.ble_beacons[i].mac_address.bytes, fingerprint_record_.ble_beacons[i].mac_address.size, macStr, sizeof(macStr));
        LOG_DEBUG("  BLE Beacon %d: MAC=%s, RSSI=%d", i, macStr, fingerprint_record_.ble_beacons[i].rssi);
    }
    for(int i=0; i < fingerprint_record_.lora_signals_count; ++i) {
        LOG_DEBUG("  LoRa Signal %d: NodeID=0x%08x, RSSI=%d, SNR=%.2f", 
                  i, 
                  fingerprint_record_.lora_signals[i].anchor_node_id,
                  fingerprint_record_.lora_signals[i].rssi,
                  fingerprint_record_.lora_signals[i].snr);
    }
    // TODO: Send this record over the mesh or store it.
    if (saveRecordToDB(fingerprint_record_)) {
        LOG_INFO("Fingerprint record saved to DB.");
    } else {
        LOG_ERROR("Failed to save fingerprint record to DB.");
    }
}

bool FingerprintModule::saveRecordToDB(const meshtastic_FingerprintRecord& record)
{
    // Assuming fsInit() is called globally at boot. If not, it might be needed here with a check.
    // For example: if (!FSCom.isMounted()) { fsInit(); }

    uint8_t buffer[meshtastic_FingerprintRecord_size]; // Max possible size
    pb_ostream_t stream = PB_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, meshtastic_FingerprintRecord_fields, &record)) {
        LOG_ERROR("Fingerprint: Failed to encode record: %s", PB_GET_ERROR(&stream));
        return false;
    }

    size_t record_len = stream.bytes_written;
    if (record_len == 0) {
        LOG_ERROR("Fingerprint: Encoded record length is 0. Not saving.");
        return false;
    }
    if (record_len > UINT16_MAX) {
        LOG_ERROR("Fingerprint: Encoded record length (%d) exceeds UINT16_MAX. Not saving.", record_len);
        return false;
    }
    uint16_t record_len_header = (uint16_t)record_len;

    LOG_DEBUG("Fingerprint: Attempting to save record of size %d (header: %d bytes)", record_len, sizeof(record_len_header));

    SafeFile dbFile(FINGERPRINT_DB_FILENAME, "ab"); // Append binary mode
    if (!dbFile) {
        LOG_ERROR("Fingerprint: Failed to open database file %s for append.", FINGERPRINT_DB_FILENAME);
        return false;
    }

    // Write the length header
    if (dbFile.write((uint8_t*)&record_len_header, sizeof(record_len_header)) != sizeof(record_len_header)) {
        LOG_ERROR("Fingerprint: Failed to write record length header to DB.");
        dbFile.close(); // Attempt to close
        return false;
    }

    // Write the encoded record
    if (dbFile.write(buffer, record_len) != record_len) {
        LOG_ERROR("Fingerprint: Failed to write encoded record to DB.");
        dbFile.close(); // Attempt to close
        return false;
    }

    if (!dbFile.close()) { // SafeFile::close() returns true on success
        LOG_ERROR("Fingerprint: Failed to close database file properly.");
        // Data might have been written, so not returning false immediately unless write failed.
    }
    
    LOG_INFO("Fingerprint: Record (len %d) successfully appended to %s", record_len, FINGERPRINT_DB_FILENAME);
    return true;
}

bool FingerprintModule::readRecordsFromDB(std::function<void(meshtastic_FingerprintRecord& record)> processRecordCallback)
{
    LOG_INFO("Fingerprint: Reading records from %s", FINGERPRINT_DB_FILENAME);
    SafeFile dbFile(FINGERPRINT_DB_FILENAME, "rb"); // Read binary mode

    if (!dbFile) {
        LOG_ERROR("Fingerprint: Failed to open database file %s for read.", FINGERPRINT_DB_FILENAME);
        return false;
    }

    while (true) {
        uint16_t record_len_header = 0;
        size_t bytes_read = dbFile.read((uint8_t*)&record_len_header, sizeof(record_len_header));

        if (bytes_read == 0 && dbFile.eof()) {
            LOG_INFO("Fingerprint: Reached end of DB file.");
            break; // Clean EOF
        }

        if (bytes_read < sizeof(record_len_header)) {
            if (bytes_read > 0) { // Partial read of header - indicates corruption or incomplete write
                LOG_ERROR("Fingerprint: Failed to read full record length header. Read %d bytes. DB potentially corrupt.", bytes_read);
            } else { // bytes_read == 0 but not EOF, or some other error
                 LOG_ERROR("Fingerprint: Error reading record length header from DB or unexpected EOF.");
            }
            dbFile.close();
            return false; 
        }
        
        size_t record_len = record_len_header;

        if (record_len == 0) { // Should not happen if written correctly
            LOG_WARN("Fingerprint: Encountered record with length 0. Skipping.");
            continue; 
        }
        if (record_len > meshtastic_FingerprintRecord_size) {
            LOG_ERROR("Fingerprint: Record length %d from DB exceeds max possible size %d. DB potentially corrupt.", record_len, meshtastic_FingerprintRecord_size);
            dbFile.close();
            return false; 
        }

        std::vector<uint8_t> record_buffer(record_len);
        bytes_read = dbFile.read(record_buffer.data(), record_len);

        if (bytes_read < record_len) {
            LOG_ERROR("Fingerprint: Failed to read full record (expected %d, got %d). DB potentially corrupt.", record_len, bytes_read);
            dbFile.close();
            return false;
        }

        meshtastic_FingerprintRecord record = meshtastic_FingerprintRecord_init_default;
        pb_istream_t stream = pb_istream_from_buffer(record_buffer.data(), record_len);

        if (!pb_decode(&stream, meshtastic_FingerprintRecord_fields, &record)) {
            LOG_ERROR("Fingerprint: Failed to decode record from DB: %s", PB_GET_ERROR(&stream));
            // Depending on desired robustness, could try to skip and continue, or fail hard.
            // For now, fail hard on decode error.
            dbFile.close();
            return false; 
        }

        processRecordCallback(record);
    }

    dbFile.close();
    return true;
}

void FingerprintModule::debugPrintAllRecords() {
    LOG_INFO("Fingerprint: --- Debug Print All Records ---");
    readRecordsFromDB([](meshtastic_FingerprintRecord& record) {
        LOG_INFO("  Record Timestamp: %u", record.timestamp);
        if (record.has_location) {
            LOG_INFO("    Location: Lat=%d, Lon=%d, Alt=%d", 
                     record.location.latitude_i, 
                     record.location.longitude_i, 
                     record.location.altitude);
        } else {
            LOG_INFO("    Location: Not available");
        }
        LOG_INFO("    BLE Beacons Count: %d", record.ble_beacons_count);
        for (int i = 0; i < record.ble_beacons_count; ++i) {
            char macStr[18]; // XX:XX:XX:XX:XX:XX\0
            bytesToHexString(record.ble_beacons[i].mac_address.bytes, record.ble_beacons[i].mac_address.size, macStr, sizeof(macStr));
            LOG_INFO("      BLE %d: MAC=%s, RSSI=%d", i, macStr, record.ble_beacons[i].rssi);
        }
        LOG_INFO("    LoRa Signals Count: %d", record.lora_signals_count);
        for (int i = 0; i < record.lora_signals_count; ++i) {
            LOG_INFO("      LoRa %d: NodeID=0x%08x, RSSI=%d, SNR=%.2f", 
                     i, 
                     record.lora_signals[i].anchor_node_id, 
                     record.lora_signals[i].rssi, 
                     record.lora_signals[i].snr);
        }
    });
    LOG_INFO("Fingerprint: --- End Debug Print All Records ---");
}
