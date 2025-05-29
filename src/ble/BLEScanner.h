#pragma once

#if defined(ARCH_ESP32)

#include "meshtastic/fingerprint.pb.h" // For BleScanBatch

// Forward declaration
class BLEAdvertisedDevice;

namespace meshtastic {

class BLEScanner {
 public:
  BLEScanner();
  ~BLEScanner();

  /**
   * @brief Perform a BLE scan for a specified duration.
   *
   * @param durationSeconds The duration of the scan in seconds.
   */
  void performScan(int durationSeconds);

  /**
   * @brief The results of the latest scan.
   */
  meshtastic_BleScanBatch latest_results;

 private:
  // Callback class for handling scan results
  class MyAdvertisedDeviceCallbacks;
  MyAdvertisedDeviceCallbacks* _advertisedDeviceCallbacks;

  bool _isScanning;
  // Add any necessary private members, e.g., for managing BLE state
};

} // namespace meshtastic

#endif // ARCH_ESP32
