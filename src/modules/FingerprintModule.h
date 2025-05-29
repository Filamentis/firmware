#pragma once

#include "Module.h"
#include "meshtastic/mesh.pb.h"      // For meshtastic_Position
#include "meshtastic/fingerprint.pb.h" // For meshtastic_BleScanResult (potentially)
#include <vector>      // For std::vector
#include <memory>      // For std::unique_ptr
#include <functional>  // For std::function
#include <string>      // For std::string (if chosen for node_name)

#if defined(ARCH_ESP32)
#include "ble/BLEBeaconManager.h" // For BLEBeaconManager
#endif

// Database filename constant for CSV
#define FINGERPRINT_CSV_FILENAME "/prefs/fp_log.csv" // Changed from fp.dat to reflect CSV format

// Max length for node name storage in InternalLoRaSignalData
static const int MAX_NODE_NAME_LEN = 41; // Accommodates 40 chars + null terminator (long_name is max_size: 40)

// Forward declaration
namespace meshtastic {
class BLEScanner;
}

// Internal C++ data structures for fingerprinting
struct InternalLoRaSignalData {
    uint32_t node_id;
    float snr;
    char node_name[MAX_NODE_NAME_LEN]; 
    int32_t rssi; // Added RSSI here as it's essential for LoRa signal characterization

    InternalLoRaSignalData() : node_id(0), snr(0.0f), rssi(0) {
        memset(node_name, 0, sizeof(node_name));
    }
};

struct InternalBleBeaconData {
    char mac_address_str[18]; // "XX:XX:XX:XX:XX:XX\0"
    int32_t rssi;

    InternalBleBeaconData() : rssi(0) {
        memset(mac_address_str, 0, sizeof(mac_address_str));
    }
};

struct CurrentFingerprintEvent {
    uint32_t timestamp;
    meshtastic_Position location; // From mesh.pb.h
    bool has_location;
    std::vector<InternalLoRaSignalData> lora_signals;
    std::vector<InternalBleBeaconData> ble_beacons;

    CurrentFingerprintEvent() : timestamp(0), has_location(false) {
        location = meshtastic_Position_init_default;
    }
};


class FingerprintModule : public Module
{
  public:
    FingerprintModule();
    virtual ~FingerprintModule() {}

    void setup() override;
    void loop() override;

    // Called by the Router when a LoRa packet is received
    void onLoRaPacketReceived(const meshtastic_MeshPacket* packet);

    // Reads all records from the DB and calls the callback for each.
    // This method will be removed/modified as we are switching to CSV.
    // bool readRecordsFromDB(std::function<void(meshtastic_FingerprintRecord& record)> processRecordCallback); 

    // Utility function to print all records to the log for debugging
    // This method will be removed/modified.
    // void debugPrintAllRecords(); 

    // Populates the provided 'database_message' with records from the DB.
    // This method will be removed/modified.
    // bool getFingerprintDatabase(meshtastic_FingerprintDatabase& database_message);

  private:
    void collectFingerprintData();
    // bool saveRecordToDB(const meshtastic_FingerprintRecord& record); // Will be replaced by saveEventToCsv
    bool saveEventToCsv(const CurrentFingerprintEvent& event_data);

    CurrentFingerprintEvent current_event_;
    std::vector<InternalLoRaSignalData> recent_lora_signals_; // Temp storage for LoRa signals

#if defined(ARCH_ESP32) // BLE scanning primarily for ESP32 for now
    std::unique_ptr<meshtastic::BLEScanner> ble_scanner_;
    std::unique_ptr<meshtastic::BLEBeaconManager> ble_beacon_manager_;
    bool last_anchor_status_; // To track changes in anchor mode
#endif

    // Timer or other mechanism to trigger data collection
    // uint32_t last_collection_time_;
    // uint32_t collection_interval_ms_;
};
