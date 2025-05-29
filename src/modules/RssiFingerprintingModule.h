#pragma once
#include <vector>
#include <string>
#include <map>
#include "gps/GPS.h"

struct RssiSample {
    std::string id; // BLE MAC or LoRa node ID
    int rssi;
};

struct Fingerprint {
    double latitude;
    double longitude;
    std::string name; // Added name field
    std::vector<RssiSample> samples;
};

class RssiFingerprintingModule {
public:
    RssiFingerprintingModule();
    // Add a BLE scan result (id = MAC address)
    void addBleSample(const std::string& bleId, int rssi);
    // Add a LoRa scan result (id = node ID)
    void addLoraSample(const std::string& loraId, int rssi);
    // Set current GPS position (for anchor or data collection)
    void setCurrentGps(double lat, double lon);
    // Set anchor mode and configure via WiFi AP
    void configureAnchorViaWiFiAp(double lat, double lon);
    // Add a full sample (for import or direct injection)
    void addSample(const std::string& id, int rssi, double lat, double lon, const std::string& name_str);
    // Import/export fingerprint database (CSV)
    void importDatabase(const std::string& filename);
    void exportDatabase(const std::string& filename);
    void clearDatabase();
    // KNN localization: returns estimated (lat, lon, name)
    std::tuple<double, double, std::string> localize(const std::vector<RssiSample>& scan, int k = 3);
    // Collect data: triggers BLE/LoRa scan and GPS read, then stores fingerprint
    void collectData();
    // Serialize anchor info (node ID, coordinates) for mesh broadcast
    std::string serializeAnchorInfo() const;
    // Process received anchor info from mesh
    void processAnchorInfo(const std::string& msg);

    // New methods for SOS Module
    const std::vector<RssiSample>& getCurrentScanResults();
    void triggerNewScan();
private:
    std::vector<Fingerprint> database;
    std::vector<RssiSample> currentScan; // Holds latest BLE/LoRa scan
    double currentLat = 0.0, currentLon = 0.0;
    bool anchorMode = false;
};

extern RssiFingerprintingModule *rssiFingerprintingModule; // Global instance
