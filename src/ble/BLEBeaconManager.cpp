#include "BLEBeaconManager.h"
#include "meshUtils.h" // For LOG_DEBUG, LOG_ERROR etc.

#if defined(ARCH_ESP32)

namespace meshtastic {

BLEBeaconManager::BLEBeaconManager() : pAdvertising_(nullptr), advertising_active_(false) {
    // Constructor: Initialize members.
    // BLEDevice::init may be called here or deferred to startAdvertising.
    // Deferring to startAdvertising is often safer to ensure BLE stack is up only when needed
    // and to handle cases where it might already be initialized by another part of the system.
}

bool BLEBeaconManager::startAdvertising() {
    LOG_DEBUG("Attempting to start BLE anchor beacon advertising.");

    if (advertising_active_) {
        LOG_DEBUG("Advertising is already active.");
        return true;
    }

    // Initialize BLE if it hasn't been initialized yet.
    // Pass an empty device name; the name in the advertisement will be set separately if needed.
    // Note: If other parts of Meshtastic (like the client BLE service) initialize BLEDevice,
    // this call might not be strictly necessary or could conflict if not handled carefully.
    // A robust solution might involve a shared BLE initialization manager or reference counting.
    // For now, we call init if it's not already initialized.
    if (!BLEDevice::getInitialized()) {
        LOG_DEBUG("Initializing BLEDevice for advertising.");
        BLEDevice::init(""); // Initialize with an empty device name for now
    } else {
        LOG_DEBUG("BLEDevice already initialized.");
    }
    
    pAdvertising_ = BLEDevice::getAdvertising();
    if (!pAdvertising_) {
        LOG_ERROR("Failed to get BLEAdvertising object.");
        return false;
    }

    BLEAdvertisementData advertisementData = BLEAdvertisementData();

    // Set appearance (optional)
    advertisementData.setAppearance(0x0000); // Generic, or choose a specific one

    // Set the service UUID for our anchor beacon
    // The BLEUUID constructor can take a const char*
    try {
        BLEUUID serviceUUID(FINGERPRINT_ANCHOR_BEACON_SERVICE_UUID.c_str());
        advertisementData.addServiceUUID(serviceUUID);
        pAdvertising_->setAdvertisementData(advertisementData);
        
        // For iBeacon (alternative, if needed, but we're using service UUID):
        // BLEBeacon oBeacon = BLEBeacon();
        // oBeacon.setManufacturerId(0x4C00); // Apple, Inc.
        // oBeacon.setProximityUUID(BLEUUID(FINGERPRINT_ANCHOR_BEACON_SERVICE_UUID.c_str()));
        // oBeacon.setMajor(0);
        // oBeacon.setMinor(0);
        // BLEAdvertisementData MBeaconData = BLEAdvertisementData();
        // MBeaconData.setData(oBeacon.getData());
        // pAdvertising->setAdvertisementData(MBeaconData);
        // delete MBeaconData;
        // delete oBeacon;

        // Set advertising parameters (optional, defaults are usually fine for simple beacons)
        // pAdvertising_->setMinPreferred(0x06);  // Min interval based on ESP-IDF specs
        // pAdvertising_->setMaxPreferred(0x12);  // Max interval based on ESP-IDF specs

        pAdvertising_->start();
        advertising_active_ = true;
        LOG_INFO("BLE anchor beacon advertising started with Service UUID: %s", FINGERPRINT_ANCHOR_BEACON_SERVICE_UUID.c_str());
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception while setting up BLE advertising: %s", e.what());
        // In case of an exception with BLEUUID (e.g. invalid format, though unlikely for a valid const string)
        if (pAdvertising_ && advertising_active_) { // Try to stop if it somehow started
             pAdvertising_->stop();
             advertising_active_ = false;
        }
        return false;
    } catch (...) {
        LOG_ERROR("Unknown exception while setting up BLE advertising.");
         if (pAdvertising_ && advertising_active_) { // Try to stop if it somehow started
             pAdvertising_->stop();
             advertising_active_ = false;
        }
        return false;
    }
}

void BLEBeaconManager::stopAdvertising() {
    LOG_DEBUG("Attempting to stop BLE anchor beacon advertising.");
    if (advertising_active_ && pAdvertising_) {
        pAdvertising_->stop();
        advertising_active_ = false;
        LOG_INFO("BLE anchor beacon advertising stopped.");
    } else {
        LOG_DEBUG("BLE advertising was not active or pAdvertising is null.");
    }
    // Consider BLEDevice::deinit() carefully. If Meshtastic has a central BLE service
    // for app connections, de-initializing here would break that.
    // Generally, deinit should only be called if this module is the *only* user of BLE.
    // For now, we do not call deinit().
}

bool BLEBeaconManager::isAdvertising() {
    return advertising_active_;
}

} // namespace meshtastic

#endif // ARCH_ESP32
