#include "RssiFingerprintingModule.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <sstream>

RssiFingerprintingModule::RssiFingerprintingModule() {}

void RssiFingerprintingModule::addSample(const std::string& id, int rssi, double lat, double lon) {
    // Find or create fingerprint for this location
    for (auto& fp : database) {
        if (fp.latitude == lat && fp.longitude == lon) {
            fp.samples.push_back({id, rssi});
            return;
        }
    }
    Fingerprint fp;
    fp.latitude = lat;
    fp.longitude = lon;
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
        char id[32];
        int rssi;
        if (sscanf(line.c_str(), "%lf,%lf,%31[^,],%d", &lat, &lon, id, &rssi) == 4) {
            addSample(id, rssi, lat, lon);
        }
    }
}

void RssiFingerprintingModule::exportDatabase(const std::string& filename) {
    std::ofstream file(filename);
    for (const auto& fp : database) {
        for (const auto& s : fp.samples) {
            file << fp.latitude << "," << fp.longitude << "," << s.id << "," << s.rssi << "\n";
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

std::pair<double, double> RssiFingerprintingModule::localize(const std::vector<RssiSample>& scan, int k) {
    // KNN: find k closest fingerprints
    std::vector<std::pair<double, const Fingerprint*>> dists;
    for (const auto& fp : database) {
        double dist = rssiDistance(scan, fp.samples);
        dists.push_back({dist, &fp});
    }
    std::sort(dists.begin(), dists.end(), [](const std::pair<double, const Fingerprint*>& a, const std::pair<double, const Fingerprint*>& b) { return a.first < b.first; });
    double lat = 0, lon = 0;
    int count = 0;
    for (int i = 0; i < std::min(k, (int)dists.size()); ++i) {
        lat += dists[i].second->latitude;
        lon += dists[i].second->longitude;
        count++;
    }
    if (count == 0) return {0, 0};
    return {lat / count, lon / count};
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
    for (const auto& sample : currentScan) {
        addSample(sample.id, sample.rssi, currentLat, currentLon);
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
    addSample("ANCHOR:" + nodeId, 0, lat, lon);
}
