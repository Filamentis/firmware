#include "RssiFingerprintingModule.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <sstream>

// The global pointer is declared extern in the header.
// Its definition (and instantiation) should be in Modules.cpp.

RssiFingerprintingModule::RssiFingerprintingModule() {
    // Set the global pointer to this instance
    // This assumes that only one instance of this module will be created.
    ::rssiFingerprintingModule = this;
}

void RssiFingerprintingModule::addSample(const std::string& id, int rssi, double lat, double lon, const std::string& name_str) {
    // Find or create fingerprint for this location
    for (auto& fp : database) {
        if (fp.latitude == lat && fp.longitude == lon) {
            // TODO: What if name_str is different for the same lat/lon?
            // For now, we assume name is consistent or take the first one.
            // If fp.name is empty, we can set it.
            if (fp.name.empty() && !name_str.empty()) {
                fp.name = name_str;
            }
            fp.samples.push_back({id, rssi});
            return;
        }
    }
    Fingerprint fp;
    fp.latitude = lat;
    fp.longitude = lon;
    fp.name = name_str; // Store the name
    fp.samples.push_back({id, rssi});
    database.push_back(fp);
}

void RssiFingerprintingModule::importDatabase(const std::string& filename) {
    // Simple CSV import: lat,lon,id,rssi
    std::ifstream file(filename);
    if (!file.is_open()) return;
    database.clear();
    std::string line;
    while (std::getline(file, line)) {
        double lat, lon;
        char name_cstr[32]; // Buffer for name
        char id_cstr[32];   // Buffer for id
        int rssi;
        // Use %31[^,] to read up to 31 characters for name and id to prevent buffer overflow
        if (sscanf(line.c_str(), "%lf,%lf,%31[^,],%31[^,],%d", &lat, &lon, name_cstr, id_cstr, &rssi) == 5) {
            addSample(id_cstr, rssi, lat, lon, name_cstr);
        }
    }
}

void RssiFingerprintingModule::exportDatabase(const std::string& filename) {
    std::ofstream file(filename);
    for (const auto& fp : database) {
        for (const auto& s : fp.samples) {
            // Export format: lat,lon,name,id,rssi
            file << fp.latitude << "," << fp.longitude << "," << fp.name << "," << s.id << "," << s.rssi << "\n";
        }
    }
}

void RssiFingerprintingModule::clearDatabase() {
    database.clear();
}

// Euclidean distance between two RSSI vectors
static double rssiDistance(const std::vector<RssiSample>& a, const std::vector<RssiSample>& b) {
    double sum = 0;
    for (const auto& sa : a) {
        auto it = std::find_if(b.begin(), b.end(), [&](const RssiSample& sb) { return sb.id == sa.id; });
        int rssi_b = (it != b.end()) ? it->rssi : -100; // Assume -100 if not found
        sum += (sa.rssi - rssi_b) * (sa.rssi - rssi_b);
    }
    return sqrt(sum);
}

std::tuple<double, double, std::string> RssiFingerprintingModule::localize(const std::vector<RssiSample>& scan, int k) {
    // KNN: find k closest fingerprints
    if (database.empty()) {
        return {0, 0, ""};
    }

    std::vector<std::pair<double, const Fingerprint*>> dists;
    for (const auto& fp : database) {
        double dist = rssiDistance(scan, fp.samples);
        dists.push_back({dist, &fp});
    }
    std::sort(dists.begin(), dists.end(), [](const std::pair<double, const Fingerprint*>& a, const std::pair<double, const Fingerprint*>& b) { return a.first < b.first; });

    double lat_sum = 0, lon_sum = 0;
    int count = 0;
    // Use a map to vote for the most common name among the k nearest neighbors
    std::map<std::string, int> name_votes;

    for (int i = 0; i < std::min(k, (int)dists.size()); ++i) {
        lat_sum += dists[i].second->latitude;
        lon_sum += dists[i].second->longitude;
        if (!dists[i].second->name.empty()) {
            name_votes[dists[i].second->name]++;
        }
        count++;
    }

    if (count == 0) return {0, 0, ""};

    std::string estimated_name = "";
    int max_votes = 0;
    for (const auto& vote : name_votes) {
        if (vote.second > max_votes) {
            max_votes = vote.second;
            estimated_name = vote.first;
        }
    }
    // If no names found or all names were empty, keep estimated_name as ""
    // Otherwise, estimated_name will hold the most voted name.

    return {lat_sum / count, lon_sum / count, estimated_name};
}

// Add a BLE scan result (id = MAC address)
void RssiFingerprintingModule::addBleSample(const std::string& bleId, int rssi) {
    currentScan.push_back({bleId, rssi});
}

// Add a LoRa scan result (id = node ID)
void RssiFingerprintingModule::addLoraSample(const std::string& loraId, int rssi) {
    currentScan.push_back({loraId, rssi});
}

// Set current GPS position (for anchor or data collection)
void RssiFingerprintingModule::setCurrentGps(double lat, double lon) {
    currentLat = lat;
    currentLon = lon;
}

// Set anchor mode and configure via WiFi AP
void RssiFingerprintingModule::configureAnchorViaWiFiAp(double lat, double lon) {
    anchorMode = true;
    currentLat = lat;
    currentLon = lon;
    // TODO: Add WiFi AP configuration logic here
}

// Collect data: triggers BLE/LoRa scan and GPS read, then stores fingerprint
void RssiFingerprintingModule::collectData() {
    // In a real implementation, trigger BLE/LoRa scan and GPS read here.
    // For now, assume currentScan and currentLat/currentLon are set externally.
    if (currentScan.empty()) return;

    // Assuming collectData is for a specific named location,
    // we would need a way to pass the name here.
    // For now, let's assume a default name or empty if not specified.
    // This part might need further design depending on how names are collected.
    std::string currentName = "CollectedLocation"; // Placeholder

    for (const auto& sample : currentScan) {
        addSample(sample.id, sample.rssi, currentLat, currentLon, currentName);
    }
    currentScan.clear();
}

// Serialize anchor info (node ID, coordinates) for mesh broadcast
std::string RssiFingerprintingModule::serializeAnchorInfo() const {
    // Example: "ANCHOR,<node_id>,<lat>,<lon>"
    std::ostringstream oss;
    // TODO: Replace "myNodeId" with actual node ID from your system
    std::string myNodeId = "myNodeId";
    oss << "ANCHOR," << myNodeId << "," << currentLat << "," << currentLon;
    return oss.str();
}

// Process received anchor info from mesh
void RssiFingerprintingModule::processAnchorInfo(const std::string& msg) {
    // Example expected: "ANCHOR,<node_id>,<lat>,<lon>"
    if (msg.find("ANCHOR,") != 0) return;
    size_t p1 = msg.find(',', 7);
    size_t p2 = msg.find(',', p1 + 1);
    size_t p3 = msg.find(',', p2 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return;
    std::string nodeId = msg.substr(7, p1 - 7);
    double lat = std::stod(msg.substr(p1 + 1, p2 - p1 - 1));
    double lon = std::stod(msg.substr(p2 + 1));
    // Store anchor as a fingerprint with a special sample (e.g., id = "ANCHOR:<nodeId>", rssi = 0)
    // Anchors don't have names in their messages, so use a default or empty name.
    addSample("ANCHOR:" + nodeId, 0, lat, lon, "Anchor"); // Using "Anchor" as default name
}

const std::vector<RssiSample>& RssiFingerprintingModule::getCurrentScanResults() {
    return currentScan;
}

void RssiFingerprintingModule::triggerNewScan() {
    currentScan.clear(); // Clear previous scan results

    // Placeholder for actual scan logic.
    // In a real implementation, this would trigger hardware scans (BLE, LoRa)
    // and the results would be added to currentScan, possibly asynchronously.
    // For now, adding dummy samples:
    addBleSample("dummy_ble_SOS", -75); // Simulate a BLE device found
    addLoraSample("dummy_lora_SOS", -85);  // Simulate a LoRa node heard
    // To make it slightly more dynamic, could add another one:
    // addBleSample("dummy_ble_SOS_2", -60);
}
