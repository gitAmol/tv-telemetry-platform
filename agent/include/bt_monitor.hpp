#pragma once
#include "bt_event.hpp"
#include <functional>
#include <string>
#include <map>
#include <mutex>
#include <queue>

namespace telemetry {

// Monitors Bluetooth state changes on the TV.
// On real hardware this hooks into BlueZ (Linux) or the webOS luna-service.
// In simulation mode it generates realistic synthetic events.
class BtMonitor {
public:
    using EventCallback = std::function<void(const BtEvent&)>;

    explicit BtMonitor(std::string tv_id);

    void set_callback(EventCallback cb);

    // Start/stop background monitoring thread
    bool start();
    void stop();

    // Inject a synthetic event (used by simulation and tests)
    void inject_connect(const std::string& mac,
                        const std::string& name,
                        const std::string& type,
                        int32_t rssi,
                        uint32_t connection_ms);

private:
    void emit(BtEvent event);

    std::string    tv_id_;
    EventCallback  callback_;
    std::mutex     mutex_;
};

} // namespace telemetry
