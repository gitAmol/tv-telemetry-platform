#pragma once
#include <string>
#include <cstdint>
#include <chrono>
#include <nlohmann/json.hpp>

namespace telemetry {

using json = nlohmann::json;

struct BtEvent {
    std::string tv_id;
    std::string device_name;
    std::string device_type;   // headphones | speaker | keyboard | remote | other
    std::string mac_address;   // AA:BB:CC:DD:EE:FF
    uint32_t    connection_ms; // CONNECTING -> CONNECTED latency
    int32_t     rssi;          // signal strength dBm
    uint64_t    timestamp_ms;  // epoch milliseconds

    json to_json() const {
        return {
            {"tv_id",         tv_id},
            {"device_name",   device_name},
            {"device_type",   device_type},
            {"mac_address",   mac_address},
            {"connection_ms", connection_ms},
            {"rssi",          rssi},
            {"timestamp_ms",  timestamp_ms}
        };
    }

    static BtEvent from_json(const json& j) {
        return {
            j.value("tv_id",         ""),
            j.value("device_name",   ""),
            j.value("device_type",   "other"),
            j.value("mac_address",   ""),
            j.value("connection_ms", 0u),
            j.value("rssi",          0),
            j.value("timestamp_ms",  0ull)
        };
    }
};

inline uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace telemetry
