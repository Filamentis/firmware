#if defined(ARCH_ESP32)

#include "BLEScanner.h"
#include "globals.h"       // For Log.println
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <vector> // Required for std::vector

// Define the generated protobuf header path based on typical Meshtastic structure
// This might need adjustment if the actual path is different.
// Assuming nanopb generates it into something like `mesh/generated/` relative to include paths
#include "meshtastic/fingerprint.pb.h"

// For debugging
#if defined(HAS_SCREEN)
#include "Display.h"
extern Display* Oled; // Use extern if Oled is globally defined elsewhere
#endif

// Global BLE Scan pointer (as per ESP-IDF examples)
static BLEScan* pBLEScan;

namespace meshtastic {

// Callback class for handling scan results
class BLEScanner::MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
 public:
  MyAdvertisedDeviceCallbacks(meshtastic_BleScanBatch* batch) : _batch(batch) {
    // Clear previous results from the batch before a new scan
    // This assumes the batch is cleared/reset by the caller of performScan or here.
    // For now, let's clear it here.
    // _batch->results_count = 0; // This would be for raw C arrays with nanopb
    // For C++ vectors or similar, ensure it's cleared.
    // If latest_results is directly used, it needs clearing.
    // Let's assume latest_results is cleared in performScan before starting.
  }

  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (_batch->results_count >= MESHTASTIC_BLE_SCAN_BATCH_RESULTS_MAX_COUNT) {
        Log.printf("BLE Scan: Maximum results reached (%d), discarding new findings.\n", MESHTASTIC_BLE_SCAN_BATCH_RESULTS_MAX_COUNT);
        return;
    }

    Log.printf("Advertised Device: %s, RSSI: %d\n", advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI());

    meshtastic_BleScanResult* result = &_batch->results[_batch->results_count++];
    
    // Get MAC address
    BLEAddress addr = advertisedDevice.getAddress();
    memcpy(result->mac_address, addr.getNative(), sizeof(result->mac_address)); // Assuming mac_address is bytes[6]

    result->rssi = advertisedDevice.getRSSI();

    // Print to serial for debugging
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             result->mac_address[0], result->mac_address[1], result->mac_address[2],
             result->mac_address[3], result->mac_address[4], result->mac_address[5]);
    Log.printf("BLEScanner: Discovered %s, RSSI: %d\n", macStr, result->rssi);
  }

 private:
  meshtastic_BleScanBatch* _batch;
};


BLEScanner::BLEScanner() : _isScanning(false) {
  // Initialize the callback handler
  _advertisedDeviceCallbacks = new MyAdvertisedDeviceCallbacks(&latest_results);
}

BLEScanner::~BLEScanner() {
  delete _advertisedDeviceCallbacks;
  // Consider if BLE deinitialization is needed here, but it might be global.
  // BLEDevice::deinit(false); // `false` to not release BLE controller memory for faster re-init
}

void BLEScanner::performScan(int durationSeconds) {
  Log.println("BLEScanner: Starting BLE Scan...");
  _isScanning = true;

  // Clear previous results
  // For nanopb, if 'results' is a static array, we just reset the count.
  // If it were dynamic, we'd clear and free.
  latest_results.results_count = 0; 
  latest_results.timestamp = 0; // Will be set at the end of the scan

  // Initialize BLE if it hasn't been already.
  // Note: BLEDevice::init can be called multiple times; it will only init once.
  // An empty device name is used as we are only scanning.
  if (!BLEDevice::getInitialized()) {
    Log.println("BLEScanner: Initializing BLE...");
    BLEDevice::init(""); 
  }
  
  pBLEScan = BLEDevice::getScan(); // Get the global BLE Scan object
  pBLEScan->setAdvertisedDeviceCallbacks(_advertisedDeviceCallbacks, true); // Pass true to be notified of duplicates during the scan
  pBLEScan->setActiveScan(true); // Active scan uses more power, but gets scan response data
  pBLEScan->setInterval(100);    // Set scan interval (N * 0.625 ms)
  pBLEScan->setWindow(99);       // Set scan window (N * 0.625 ms), should be less than or equal to interval

  Log.printf("BLEScanner: Scan starting for %d seconds.\n", durationSeconds);

  #if defined(HAS_SCREEN) && defined(Oled) // Check Oled pointer too
  if (Oled) {
    Oled->setLine(4, "BLE Scanning...", true); // Assuming line 4 is available
  }
  #endif

  // Start the scan
  // This is a blocking call, which is fine for this initial implementation
  // For a real application, we might run this in a separate task.
  pBLEScan->start(durationSeconds, false); // false = not continuous scanning

  latest_results.timestamp = now(); // Record timestamp after scan completion

  Log.printf("BLEScanner: Scan finished. Found %d devices.\n", latest_results.results_count);

  #if defined(HAS_SCREEN) && defined(Oled)
  if (Oled) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Scan done: %d found", latest_results.results_count);
    Oled->setLine(4, buf, true);
  }
  #endif

  // Clean up scan resources
  pBLEScan->clearResults(); // Frees memory from BLEScanResults
  // Do not call pBLEScan->stop() here if start() was called with a duration and false for continuous, as it stops automatically.
  
  _isScanning = false;
}

// Simple test function
void testBLEScan() {
  static BLEScanner scanner; // Create a static instance to reuse
  Log.println("Executing testBLEScan()...");
  scanner.performScan(10); // Scan for 10 seconds

  Log.println("Test BLE Scan Results:");
  char macStr[18];
  for (int i = 0; i < scanner.latest_results.results_count; ++i) {
    meshtastic_BleScanResult* res = &scanner.latest_results.results[i];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             res->mac_address[0], res->mac_address[1], res->mac_address[2],
             res->mac_address[3], res->mac_address[4], res->mac_address[5]);
    Log.printf("  MAC: %s, RSSI: %d\n", macStr, res->rssi);
  }
}

} // namespace meshtastic

#endif // ARCH_ESP32
