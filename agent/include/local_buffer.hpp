#pragma once
#include "bt_event.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace telemetry {

// SQLite-backed offline buffer.
// When Kafka is unreachable, events are written here.
// On reconnect, drain() replays them in order and marks them sent.
class LocalBuffer {
public:
    explicit LocalBuffer(const std::string& db_path);
    ~LocalBuffer();

    bool open();
    void close();

    bool push(const BtEvent& event);

    // Returns up to `limit` unsent events
    std::vector<BtEvent> peek(int limit = 200) const;

    // Mark a batch as successfully sent (by rowid range)
    bool mark_sent(int64_t up_to_rowid);

    // Total unsent count
    int64_t pending_count() const;

private:
    bool exec(const char* sql);

    std::string db_path_;
    sqlite3*    db_{nullptr};
    mutable std::mutex mutex_;
};

} // namespace telemetry
