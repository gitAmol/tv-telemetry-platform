package com.telemetry;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.apache.flink.api.common.eventtime.WatermarkStrategy;
import org.apache.flink.api.common.functions.AggregateFunction;
import org.apache.flink.api.common.serialization.SimpleStringSchema;
import org.apache.flink.connector.jdbc.JdbcConnectionOptions;
import org.apache.flink.connector.jdbc.JdbcExecutionOptions;
import org.apache.flink.connector.jdbc.JdbcSink;
import org.apache.flink.connector.kafka.sink.KafkaRecordSerializationSchema;
import org.apache.flink.connector.kafka.sink.KafkaSink;
import org.apache.flink.connector.kafka.source.KafkaSource;
import org.apache.flink.connector.kafka.source.enumerator.initializer.OffsetsInitializer;
import org.apache.flink.streaming.api.datastream.DataStream;
import org.apache.flink.streaming.api.environment.StreamExecutionEnvironment;
import org.apache.flink.streaming.api.functions.windowing.ProcessWindowFunction;
import org.apache.flink.streaming.api.windowing.assigners.TumblingEventTimeWindows;
import org.apache.flink.streaming.api.windowing.time.Time;
import org.apache.flink.streaming.api.windowing.windows.TimeWindow;
import org.apache.flink.util.Collector;

import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Properties;

public class BtAnalyticsJob {

    // Connections slower than this are flagged as alerts
    private static final int SLOW_CONN_THRESHOLD_MS = 2000;

    // SQL to insert one window-stats row into TimescaleDB
    private static final String INSERT_STATS_SQL = 
        "INSERT INTO bt_window_stats" +
        "    (window_start, tv_id, event_count, avg_conn_ms, p99_conn_ms, unique_devices, avg_rssi)" +
        " VALUES (to_timestamp(? / 1000.0), ?, ?, ?, ?, ?, ?)" +
        " ON CONFLICT DO NOTHING";

    public static void main(String[] args) throws Exception {

        // --- Config (override via env vars in docker-compose) ---
        String kafkaBrokers  = getEnv("KAFKA_BROKERS",   "kafka:29092");
        String inputTopic    = getEnv("INPUT_TOPIC",     "bt-events");
        String alertTopic    = getEnv("ALERT_TOPIC",     "bt-alerts");
        String jdbcUrl       = getEnv("JDBC_URL",        "jdbc:postgresql://timescaledb:5432/telemetry");
        String jdbcUser      = getEnv("JDBC_USER",       "telemetry");
        String jdbcPassword  = getEnv("JDBC_PASSWORD",   "telemetry");

        // --- Flink environment ---
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();
        env.enableCheckpointing(60_000);  // checkpoint every 60s for fault tolerance

        // --- Kafka source ---
        Properties kafkaProps = new Properties();
        kafkaProps.setProperty("bootstrap.servers", kafkaBrokers);

        KafkaSource<String> kafkaSource = KafkaSource.<String>builder()
            .setBootstrapServers(kafkaBrokers)
            .setTopics(inputTopic)
            .setGroupId("bt-analytics-flink")
            .setStartingOffsets(OffsetsInitializer.latest())
            .setValueOnlyDeserializer(new SimpleStringSchema())
            .build();

        // --- Parse JSON -> BtEvent ---
        ObjectMapper mapper = new ObjectMapper();

        DataStream<BtEvent> events = env
            .fromSource(kafkaSource,
                WatermarkStrategy.<String>forBoundedOutOfOrderness(Duration.ofSeconds(10))
                    .withIdleness(Duration.ofMinutes(1)),
                "kafka-bt-events")
            .map(json -> mapper.readValue(json, BtEvent.class))
            .assignTimestampsAndWatermarks(
                WatermarkStrategy.<BtEvent>forBoundedOutOfOrderness(Duration.ofSeconds(10))
                    .withTimestampAssigner((e, t) -> e.timestampMs)
                    .withIdleness(Duration.ofMinutes(1))
            );

        // ---------------------------------------------------------------
        // Output 1: 5-minute tumbling window stats per TV -> TimescaleDB
        // ---------------------------------------------------------------
        DataStream<BtWindowStats> windowStats = events
            .keyBy(e -> e.tvId)
            .window(TumblingEventTimeWindows.of(Time.minutes(5)))
            .aggregate(new BtStatsAccumulator(), new BtWindowResultFunction());

        windowStats.addSink(JdbcSink.sink(
            INSERT_STATS_SQL,
            (stmt, s) -> {
                stmt.setLong  (1, s.windowStart);
                stmt.setString(2, s.tvId);
                stmt.setInt   (3, s.eventCount);
                stmt.setDouble(4, s.avgConnectionMs);
                stmt.setDouble(5, s.p99ConnectionMs);
                stmt.setInt   (6, s.uniqueDevices);
                stmt.setDouble(7, s.avgRssi);
            },
            JdbcExecutionOptions.builder()
                .withBatchSize(500)
                .withBatchIntervalMs(2000)
                .withMaxRetries(3)
                .build(),
            new JdbcConnectionOptions.JdbcConnectionOptionsBuilder()
                .withUrl(jdbcUrl)
                .withDriverName("org.postgresql.Driver")
                .withUsername(jdbcUser)
                .withPassword(jdbcPassword)
                .build()
        )).name("timescaledb-sink");

        // ---------------------------------------------------------------
        // Output 2: Slow connection alerts -> Kafka bt-alerts topic
        // ---------------------------------------------------------------
        KafkaSink<String> alertSink = KafkaSink.<String>builder()
            .setBootstrapServers(kafkaBrokers)
            .setRecordSerializer(KafkaRecordSerializationSchema.builder()
                .setTopic(alertTopic)
                .setValueSerializationSchema(new SimpleStringSchema())
                .build())
            .build();

        events
            .filter(e -> e.connectionMs > SLOW_CONN_THRESHOLD_MS)
            .map(e -> mapper.writeValueAsString(e))
            .sinkTo(alertSink)
            .name("kafka-alert-sink");

        env.execute("BtAnalyticsJob");
    }

    // -----------------------------------------------------------------------
    // Accumulator: collects raw values needed for avg, p99, unique count
    // -----------------------------------------------------------------------
    static class Accumulator {
        String tvId = "";
        List<Integer> connTimes = new ArrayList<>();
        List<Integer> rssiValues = new ArrayList<>();
        HashSet<String> macs = new HashSet<>();
    }

    static class BtStatsAccumulator
            implements AggregateFunction<BtEvent, Accumulator, Accumulator> {

        @Override public Accumulator createAccumulator() { return new Accumulator(); }

        @Override
        public Accumulator add(BtEvent e, Accumulator acc) {
            if (acc.tvId.isEmpty()) acc.tvId = e.tvId;
            acc.connTimes.add(e.connectionMs);
            acc.rssiValues.add(e.rssi);
            acc.macs.add(e.macAddress);
            return acc;
        }

        @Override public Accumulator getResult(Accumulator acc) { return acc; }

        @Override
        public Accumulator merge(Accumulator a, Accumulator b) {
            a.connTimes.addAll(b.connTimes);
            a.rssiValues.addAll(b.rssiValues);
            a.macs.addAll(b.macs);
            return a;
        }
    }

    static class BtWindowResultFunction
            extends ProcessWindowFunction<Accumulator, BtWindowStats, String, TimeWindow> {

        @Override
        public void process(String tvId, Context ctx, Iterable<Accumulator> elements,
                            Collector<BtWindowStats> out) {

            Accumulator acc = elements.iterator().next();
            BtWindowStats s = new BtWindowStats();
            s.tvId        = tvId;
            s.windowStart = ctx.window().getStart();
            s.windowEnd   = ctx.window().getEnd();
            s.eventCount  = acc.connTimes.size();
            s.uniqueDevices = acc.macs.size();

            if (s.eventCount == 0) return;

            // avg connection time
            s.avgConnectionMs = acc.connTimes.stream()
                .mapToInt(Integer::intValue).average().orElse(0);

            // p99 connection time
            List<Integer> sorted = new ArrayList<>(acc.connTimes);
            sorted.sort(Integer::compareTo);
            int p99idx = (int) Math.ceil(0.99 * sorted.size()) - 1;
            s.p99ConnectionMs = sorted.get(Math.max(0, p99idx));

            // avg RSSI
            s.avgRssi = acc.rssiValues.stream()
                .mapToInt(Integer::intValue).average().orElse(0);

            out.collect(s);
        }
    }

    private static String getEnv(String key, String defaultVal) {
        String v = System.getenv(key);
        return (v != null && !v.isBlank()) ? v : defaultVal;
    }
}
