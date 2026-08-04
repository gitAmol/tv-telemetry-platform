#include "local_buffer.hpp"
#include "logger.hpp"
#include <stdexcept>

namespace telemetry {

LocalBuffer::LocalBuffer(const std::string& db_path) : db_path_(db_path) {}

LocalBuffer::~LocalBuffer() { close(); }

bool LocalBuffer::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        LOG_ERROR("LocalBuffer open failed: ", sqlite3_errmsg(db_));
        return false;
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec(R"(
        CREATE TABLE IF NOT EXISTS bt_buffer (
            rowid        INTEGER PRIMARY KEY AUTOINCREMENT,
            tv_id        TEXT    NOT NULL,
            device_name  TEXT,
            device_type  TEXT,
            mac_address  TEXT,
            connection_ms INTEGER,
            rssi         INTEGER,
            timestamp_ms INTEGER NOT NULL,
            sent         INTEGER NOT NULL DEFAULT 0
        )
    )");
    exec("CREATE INDEX IF NOT EXISTS idx_sent ON bt_buffer(sent, rowid)");
    LOG_INFO("LocalBuffer opened: ", db_path_);
    return true;
}

void LocalBuffer::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool LocalBuffer::push(const BtEvent& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = R"(
        INSERT INTO bt_buffer
            (tv_id, device_name, device_type, mac_address, connection_ms, rssi, timestamp_ms)
        VALUES (?,?,?,?,?,?,?)
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text (stmt, 1, e.tv_id.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, e.device_name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, e.device_type.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 4, e.mac_address.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 5, static_cast<int>(e.connection_ms));
    sqlite3_bind_int  (stmt, 6, e.rssi);
    sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(e.timestamp_ms));

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<BtEvent> LocalBuffer::peek(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BtEvent> out;

    const char* sql = R"(
        SELECT tv_id, device_name, device_type, mac_address,
               connection_ms, rssi, timestamp_ms
        FROM bt_buffer WHERE sent = 0 ORDER BY rowid LIMIT ?
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BtEvent e;
        auto col = [&](int i) -> std::string {
            auto* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            return t ? t : "";
        };
        e.tv_id         = col(0);
        e.device_name   = col(1);
        e.device_type   = col(2);
        e.mac_address   = col(3);
        e.connection_ms = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        e.rssi          = sqlite3_column_int(stmt, 5);
        e.timestamp_ms  = static_cast<uint64_t>(sqlite3_column_int64(stmt, 6));
        out.push_back(e);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool LocalBuffer::mark_sent(int64_t up_to_rowid) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "UPDATE bt_buffer SET sent=1 WHERE rowid <= ? AND sent=0";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, up_to_rowid);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

int64_t LocalBuffer::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM bt_buffer WHERE sent=0",
                           -1, &stmt, nullptr) != SQLITE_OK) return -1;
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool LocalBuffer::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        LOG_ERROR("LocalBuffer SQL error: ", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

} // namespace telemetry
