#include <gtest/gtest.h>
#include "bt_monitor.hpp"

using namespace telemetry;

TEST(BtMonitor, CallbackReceivesEvent) {
    BtMonitor monitor("TV-TEST-01");
    monitor.start();

    BtEvent received{};
    monitor.set_callback([&](const BtEvent& e) { received = e; });

    monitor.inject_connect("AA:BB:CC:DD:EE:FF", "Test Headphones", "headphones", -60, 250);

    EXPECT_EQ(received.tv_id,         "TV-TEST-01");
    EXPECT_EQ(received.device_name,   "Test Headphones");
    EXPECT_EQ(received.device_type,   "headphones");
    EXPECT_EQ(received.mac_address,   "AA:BB:CC:DD:EE:FF");
    EXPECT_EQ(received.connection_ms, 250u);
    EXPECT_EQ(received.rssi,          -60);
    EXPECT_GT(received.timestamp_ms,  0u);

    monitor.stop();
}

TEST(BtMonitor, MultipleEventsDeliveredInOrder) {
    BtMonitor monitor("TV-TEST-02");
    monitor.start();

    std::vector<uint32_t> conn_times;
    monitor.set_callback([&](const BtEvent& e) {
        conn_times.push_back(e.connection_ms);
    });

    monitor.inject_connect("AA:00:00:00:00:01", "Dev1", "speaker",   -50, 100);
    monitor.inject_connect("AA:00:00:00:00:02", "Dev2", "keyboard",  -70, 200);
    monitor.inject_connect("AA:00:00:00:00:03", "Dev3", "headphones",-45, 300);

    ASSERT_EQ(conn_times.size(), 3u);
    EXPECT_EQ(conn_times[0], 100u);
    EXPECT_EQ(conn_times[1], 200u);
    EXPECT_EQ(conn_times[2], 300u);

    monitor.stop();
}

TEST(BtMonitor, NoCallbackDoesNotCrash) {
    BtMonitor monitor("TV-TEST-03");
    monitor.start();
    // No callback set - should not crash
    monitor.inject_connect("AA:BB:CC:DD:EE:FF", "Dev", "other", -80, 500);
    monitor.stop();
}
