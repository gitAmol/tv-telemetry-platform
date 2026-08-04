#include "bt_monitor.hpp"
#include "logger.hpp"

namespace telemetry {

BtMonitor::BtMonitor(std::string tv_id) : tv_id_(std::move(tv_id)) {}

void BtMonitor::set_callback(EventCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(cb);
}

bool BtMonitor::start() {
    LOG_INFO("BtMonitor started for TV: ", tv_id_);
    // On real hardware:
    //   Linux/BlueZ: subscribe to org.bluez.Device1 PropertiesChanged via D-Bus
    //   webOS:       luna-send -n 1 luna://com.webos.service.bluetooth2/adapter/getStatus
    // Both platforms call inject_connect() when a device connects.
    return true;
}

void BtMonitor::stop() {
    LOG_INFO("BtMonitor stopped for TV: ", tv_id_);
}

void BtMonitor::inject_connect(const std::string& mac,
                                const std::string& name,
                                const std::string& type,
                                int32_t rssi,
                                uint32_t connection_ms) {
    BtEvent ev;
    ev.tv_id         = tv_id_;
    ev.mac_address   = mac;
    ev.device_name   = name;
    ev.device_type   = type;
    ev.rssi          = rssi;
    ev.connection_ms = connection_ms;
    ev.timestamp_ms  = now_ms();
    emit(std::move(ev));
}

void BtMonitor::emit(BtEvent event) {
    LOG_DEBUG("BT event: ", event.device_name, " (", event.device_type, ") ",
              event.connection_ms, "ms rssi=", event.rssi);
    std::lock_guard<std::mutex> lock(mutex_);
    if (callback_) callback_(event);
}

} // namespace telemetry
