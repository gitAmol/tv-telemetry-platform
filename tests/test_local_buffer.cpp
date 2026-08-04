#include <gtest/gtest.h>
#include "local_buffer.hpp"
#include "bt_event.hpp"
#include <cstdio>

using namespace telemetry;

class LocalBufferTest : public ::testing::Test {
protected:
    const std::string db_path = "./test_buffer_tmp.db";

    void SetUp() override {
        buf = std::make_unique<LocalBuffer>(db_path);
        ASSERT_TRUE(buf->open());
    }
    void TearDown() override {
        buf->close();
        std::remove(db_path.c_str());
    }

    BtEvent make_event(const std::string& tv, uint32_t conn_ms) {
        BtEvent e;
        e.tv_id         = tv;
        e.device_name   = "Test Device";
        e.device_type   = "headphones";
        e.mac_address   = "AA:BB:CC:DD:EE:FF";
        e.connection_ms = conn_ms;
        e.rssi          = -55;
        e.timestamp_ms  = now_ms();
        return e;
    }

    std::unique_ptr<LocalBuffer> buf;
};

TEST_F(LocalBufferTest, PushAndPeek) {
    EXPECT_TRUE(buf->push(make_event("TV-01", 100)));
    EXPECT_TRUE(buf->push(make_event("TV-01", 200)));

    auto events = buf->peek(10);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].connection_ms, 100u);
    EXPECT_EQ(events[1].connection_ms, 200u);
}

TEST_F(LocalBufferTest, PendingCount) {
    EXPECT_EQ(buf->pending_count(), 0);
    buf->push(make_event("TV-01", 100));
    buf->push(make_event("TV-01", 200));
    EXPECT_EQ(buf->pending_count(), 2);
}

TEST_F(LocalBufferTest, PeekRespectsLimit) {
    for (int i = 0; i < 10; i++) buf->push(make_event("TV-01", i * 10));
    auto events = buf->peek(3);
    EXPECT_EQ(events.size(), 3u);
}

TEST_F(LocalBufferTest, EmptyPeekReturnsEmpty) {
    EXPECT_TRUE(buf->peek(10).empty());
}

TEST_F(LocalBufferTest, RoundTripPreservesFields) {
    BtEvent e = make_event("TV-99", 777);
    e.device_name = "Sony WH-1000XM5";
    e.device_type = "headphones";
    e.mac_address = "11:22:33:44:55:66";
    e.rssi        = -72;
    buf->push(e);

    auto events = buf->peek(1);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].tv_id,         "TV-99");
    EXPECT_EQ(events[0].device_name,   "Sony WH-1000XM5");
    EXPECT_EQ(events[0].device_type,   "headphones");
    EXPECT_EQ(events[0].mac_address,   "11:22:33:44:55:66");
    EXPECT_EQ(events[0].connection_ms, 777u);
    EXPECT_EQ(events[0].rssi,          -72);
}
