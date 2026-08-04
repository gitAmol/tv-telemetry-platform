#include "bt_monitor.hpp"
#include "kafka_producer.hpp"
#include "local_buffer.hpp"
#include "logger.hpp"

#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <random>

using namespace telemetry;
using namespace std::chrono_literals;

static std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

// ---------------------------------------------------------------------------
// Simulation: generates realistic BT connect events so the agent runs
// without real hardware. Remove this block when deploying on actual TVs.
// ---------------------------------------------------------------------------
void run_simulation(BtMonitor& monitor) {
    static const std::vector<std::tuple<std::string,std::string,std::string>> devices = {
        {"AA:BB:CC:11:22:33", "Sony WH-1000XM5",    "headphones"},
        {"AA:BB:CC:44:55:66", "JBL Flip 6",          "speaker"},
        {"AA:BB:CC:77:88:99", "Logitech K380",        "keyboard"},
        {"AA:BB:CC:AA:BB:CC", "Samsung Remote",       "remote"},
        {"AA:BB:CC:DD:EE:FF", "Apple AirPods Pro",    "headphones"},
    };

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dev_pick(0, static_cast<int>(devices.size()) - 1);
    std::uniform_int_distribution<int> conn_ms(80, 3500);  // realistic range
    std::uniform_int_distribution<int> rssi_dist(-90, -40);
    std::uniform_int_distribution<int> interval_s(2, 8);

    while (g_running.load()) {
        auto [mac, name, type] = devices[dev_pick(rng)];
        monitor.inject_connect(mac, name, type, rssi_dist(rng), conn_ms(rng));
        std::this_thread::sleep_for(std::chrono::seconds(interval_s(rng)));
    }
}
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // --- Config from env / args ---
    std::string tv_id   = (argc > 1) ? argv[1] : "TV-00001";
    std::string brokers = (argc > 2) ? argv[2] : "localhost:9092";
    std::string db_path = (argc > 3) ? argv[3] : "./buffer.db";

    Logger::instance().set_level(LogLevel::INFO);
    LOG_INFO("=== TV Bluetooth Telemetry Agent ===");
    LOG_INFO("TV ID:   ", tv_id);
    LOG_INFO("Brokers: ", brokers);
    LOG_INFO("Buffer:  ", db_path);

    // --- Components ---
    LocalBuffer buffer(db_path);
    if (!buffer.open()) return 1;

    KafkaProducer::Config kafka_cfg;
    kafka_cfg.brokers = brokers;
    KafkaProducer producer(kafka_cfg);
    producer.connect();  // non-fatal; offline mode uses buffer

    BtMonitor monitor(tv_id);

    // On every BT connection event: try Kafka, fall back to buffer
    monitor.set_callback([&](const BtEvent& ev) {
        if (producer.is_connected() && producer.send(ev)) return;
        LOG_WARN("Kafka unavailable - buffering event locally");
        buffer.push(ev);
    });

    monitor.start();

    // Simulation thread (remove on real hardware)
    std::thread sim(run_simulation, std::ref(monitor));

    // Drain loop: replay buffered events when Kafka comes back
    while (g_running.load()) {
        std::this_thread::sleep_for(5s);

        int64_t pending = buffer.pending_count();
        if (pending <= 0 || !producer.is_connected()) continue;

        LOG_INFO("Draining ", pending, " buffered events...");
        auto batch = buffer.peek(200);
        if (batch.empty()) continue;

        bool all_ok = true;
        for (const auto& ev : batch) {
            if (!producer.send(ev)) { all_ok = false; break; }
        }
        if (all_ok) {
            // mark up to the last rowid in the batch as sent
            // (peek returns in rowid order; we use timestamp as proxy here)
            buffer.mark_sent(static_cast<int64_t>(batch.back().timestamp_ms));
            LOG_INFO("Drained ", batch.size(), " events");
        }
    }

    // --- Shutdown ---
    sim.join();
    monitor.stop();
    producer.disconnect();
    buffer.close();

    auto s = producer.stats();
    LOG_INFO("Shutdown complete. sent=", s.sent, " failed=", s.failed);
    return 0;
}
