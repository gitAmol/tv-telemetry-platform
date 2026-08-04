-- TimescaleDB schema for TV Bluetooth Telemetry
-- Run once on first startup (handled by docker-compose healthcheck)

CREATE EXTENSION IF NOT EXISTS timescaledb;

-- -----------------------------------------------------------------------
-- Raw events (written by C++ agent via Kafka -> Flink -> here)
-- Retained 30 days, then auto-dropped by retention policy
-- -----------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS bt_events (
    time          TIMESTAMPTZ  NOT NULL,
    tv_id         TEXT         NOT NULL,
    device_name   TEXT,
    device_type   TEXT,
    mac_address   TEXT,
    connection_ms INTEGER,
    rssi          SMALLINT
);

SELECT create_hypertable('bt_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE);

CREATE INDEX IF NOT EXISTS idx_bt_events_tv_time
    ON bt_events (tv_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_bt_events_device_type
    ON bt_events (device_type, time DESC);

-- Auto-drop chunks older than 30 days
SELECT add_retention_policy('bt_events', INTERVAL '30 days', if_not_exists => TRUE);

-- -----------------------------------------------------------------------
-- Window stats (written by Flink every 5 minutes per TV)
-- Retained 1 year - used for dashboards and trend analysis
-- -----------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS bt_window_stats (
    window_start   TIMESTAMPTZ  NOT NULL,
    tv_id          TEXT         NOT NULL,
    event_count    INTEGER,
    avg_conn_ms    DOUBLE PRECISION,
    p99_conn_ms    DOUBLE PRECISION,
    unique_devices INTEGER,
    avg_rssi       DOUBLE PRECISION,
    PRIMARY KEY (window_start, tv_id)
);

SELECT create_hypertable('bt_window_stats', 'window_start',
    chunk_time_interval => INTERVAL '1 week',
    if_not_exists => TRUE);

CREATE INDEX IF NOT EXISTS idx_bt_stats_tv_time
    ON bt_window_stats (tv_id, window_start DESC);

SELECT add_retention_policy('bt_window_stats', INTERVAL '1 year', if_not_exists => TRUE);

-- -----------------------------------------------------------------------
-- Continuous aggregate: hourly fleet-wide summary (for Grafana overview)
-- -----------------------------------------------------------------------
CREATE MATERIALIZED VIEW IF NOT EXISTS bt_hourly_fleet
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 hour', window_start)  AS hour,
    COUNT(DISTINCT tv_id)                AS active_tvs,
    AVG(avg_conn_ms)                     AS fleet_avg_conn_ms,
    MAX(p99_conn_ms)                     AS fleet_p99_conn_ms,
    SUM(event_count)                     AS total_events
FROM bt_window_stats
GROUP BY hour
WITH NO DATA;

SELECT add_continuous_aggregate_policy('bt_hourly_fleet',
    start_offset => INTERVAL '3 hours',
    end_offset   => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour',
    if_not_exists => TRUE);
