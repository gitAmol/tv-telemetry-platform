package com.telemetry;

// Output of the 5-minute tumbling window aggregation.
// One row per (tv_id, window) written to TimescaleDB bt_window_stats.
public class BtWindowStats {

    public String tvId;
    public long   windowStart;   // epoch ms
    public long   windowEnd;     // epoch ms
    public int    eventCount;
    public double avgConnectionMs;
    public double p99ConnectionMs;
    public int    uniqueDevices;
    public double avgRssi;

    @Override
    public String toString() {
        return String.format(
            "BtWindowStats{tv=%s, events=%d, avgConn=%.1fms, p99=%.1fms, devices=%d}",
            tvId, eventCount, avgConnectionMs, p99ConnectionMs, uniqueDevices);
    }
}
