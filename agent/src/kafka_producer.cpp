#include "kafka_producer.hpp"
#include "logger.hpp"

namespace telemetry {

KafkaProducer::KafkaProducer(const Config& cfg) : cfg_(cfg) {}

KafkaProducer::~KafkaProducer() { disconnect(); }

bool KafkaProducer::connect() {
#ifdef ENABLE_KAFKA
    std::string err;
    auto* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    conf->set("bootstrap.servers",  cfg_.brokers,                    err);
    conf->set("linger.ms",          std::to_string(cfg_.linger_ms),  err);
    conf->set("batch.size",         std::to_string(cfg_.batch_bytes), err);
    conf->set("retries",            std::to_string(cfg_.retries),     err);
    conf->set("compression.type",   "snappy",                         err);
    // Idempotent producer: safe for retries at scale
    conf->set("enable.idempotence", "true",                           err);

    producer_.reset(RdKafka::Producer::create(conf, err));
    delete conf;

    if (!producer_) {
        LOG_ERROR("Kafka connect failed: ", err);
        return false;
    }
    connected_.store(true);
    LOG_INFO("Kafka connected: ", cfg_.brokers);
    return true;
#else
    connected_.store(true);
    LOG_WARN("Kafka disabled at compile time - events will be logged only");
    return true;
#endif
}

void KafkaProducer::disconnect() {
    if (!connected_.load()) return;
#ifdef ENABLE_KAFKA
    if (producer_) {
        producer_->flush(5000);
        producer_.reset();
    }
#endif
    connected_.store(false);
}

bool KafkaProducer::send(const BtEvent& event) {
    return send_raw(event.tv_id, event.to_json().dump());
}

bool KafkaProducer::send_raw(const std::string& key, const std::string& payload) {
#ifdef ENABLE_KAFKA
    if (!producer_) return false;

    auto err = producer_->produce(
        cfg_.topic,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(payload.data()), payload.size(),
        key.data(), key.size(),
        0, nullptr
    );
    producer_->poll(0);

    if (err != RdKafka::ERR_NO_ERROR) {
        LOG_ERROR("Kafka produce error: ", RdKafka::err2str(err));
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.failed++;
        return false;
    }
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.sent++;
    return true;
#else
    LOG_DEBUG("[kafka-stub] ", cfg_.topic, " key=", key, " payload=", payload);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.sent++;
    return true;
#endif
}

KafkaProducer::Stats KafkaProducer::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace telemetry
