#pragma once
#include "bt_event.hpp"
#include <string>
#include <atomic>
#include <mutex>

#ifdef ENABLE_KAFKA
#include <librdkafka/rdkafkacpp.h>
#endif

namespace telemetry {

// Produces BtEvents to Kafka topic "bt-events".
// Partitioned by tv_id so all events from one TV go to the same partition.
// Batching: linger_ms + batch_size reduce per-message overhead at 20M-device scale.
class KafkaProducer {
public:
    struct Config {
        std::string brokers      = "localhost:9092";
        std::string topic        = "bt-events";
        int         linger_ms    = 5;       // batch window
        int         batch_bytes  = 65536;   // 64 KB batch
        int         retries      = 3;
    };

    explicit KafkaProducer(const Config& cfg);
    ~KafkaProducer();

    bool connect();
    void disconnect();

    // Returns false only on unrecoverable error; retries handled internally
    bool send(const BtEvent& event);

    bool is_connected() const { return connected_.load(); }

    struct Stats {
        uint64_t sent    = 0;
        uint64_t failed  = 0;
        uint64_t retried = 0;
    };
    Stats stats() const;

private:
    bool send_raw(const std::string& key, const std::string& payload);

    Config cfg_;
    std::atomic<bool> connected_{false};
    mutable std::mutex stats_mutex_;
    Stats stats_;

#ifdef ENABLE_KAFKA
    std::unique_ptr<RdKafka::Producer> producer_;
#endif
};

} // namespace telemetry
