package com.telemetry;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

// Mirrors the C++ BtEvent struct.
// Jackson deserializes the JSON produced by the C++ agent.
@JsonIgnoreProperties(ignoreUnknown = true)
public class BtEvent {

    @JsonProperty("tv_id")
    public String tvId;

    @JsonProperty("device_name")
    public String deviceName;

    @JsonProperty("device_type")
    public String deviceType;

    @JsonProperty("mac_address")
    public String macAddress;

    @JsonProperty("connection_ms")
    public int connectionMs;

    @JsonProperty("rssi")
    public int rssi;

    @JsonProperty("timestamp_ms")
    public long timestampMs;

    @Override
    public String toString() {
        return String.format("BtEvent{tv=%s, device=%s, type=%s, conn=%dms, rssi=%d}",
                tvId, deviceName, deviceType, connectionMs, rssi);
    }
}
