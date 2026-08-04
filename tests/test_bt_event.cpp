#include <gtest/gtest.h>
#include "bt_event.hpp"

using namespace telemetry;

TEST(BtEvent, JsonRoundTrip) {
    BtEvent e;
    e.tv_id         = "TV-00001";
    e.device_name   = "Sony WH-1000XM5";
    e.device_type   = "headphones";
    e.mac_address   = "AA:BB:CC:DD:EE:FF";
    e.connection_ms = 342;
    e.rssi          = -55;
    e.timestamp_ms  = 1700000000000ULL;

    auto j       = e.to_json();
    auto decoded = BtEvent::from_json(j);

    EXPECT_EQ(decoded.tv_id,         e.tv_id);
    EXPECT_EQ(decoded.device_name,   e.device_name);
    EXPECT_EQ(decoded.device_type,   e.device_type);
    EXPECT_EQ(decoded.mac_address,   e.mac_address);
    EXPECT_EQ(decoded.connection_ms, e.connection_ms);
    EXPECT_EQ(decoded.rssi,          e.rssi);
    EXPECT_EQ(decoded.timestamp_ms,  e.timestamp_ms);
}

TEST(BtEvent, MissingFieldsDefaultSafely) {
    auto e = BtEvent::from_json({});
    EXPECT_EQ(e.tv_id,         "");
    EXPECT_EQ(e.device_type,   "other");
    EXPECT_EQ(e.connection_ms, 0u);
    EXPECT_EQ(e.rssi,          0);
}

TEST(BtEvent, NowMsIsReasonable) {
    uint64_t t = now_ms();
    // After year 2020 in epoch ms
    EXPECT_GT(t, 1577836800000ULL);
}
