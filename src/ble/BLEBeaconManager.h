#pragma once

#include <string> // For std::string

// Define a namespace if appropriate, e.g., meshtastic::ble
namespace meshtastic { // Or your project's namespace

// Service UUID for Meshtastic Fingerprinting Anchor Beacons
const std::string FINGERPRINT_ANCHOR_BEACON_SERVICE_UUID = "f07a8360-02db-47d7-9a08-23b73845069d";

#if defined(ARCH_ESP32)
#include <BLEDevice.h>
#include <BLEServer.h> // Though not strictly for server, often included with BLEDevice
#include <BLEAdvertising.h>
#include <BLEUUID.h>

class BLEBeaconManager {
public:
    BLEBeaconManager();
    bool startAdvertising(); // Takes the UUID from the constant
    void stopAdvertising();
    bool isAdvertising(); 
private:
    BLEAdvertising *pAdvertising_; // ESP32 BLE Advertising object
    bool advertising_active_;
};
#endif // ARCH_ESP32

} // namespace meshtastic
