# TV Bluetooth Telemetry — Scalable to 20M Devices

Collects Bluetooth connection events from Smart TVs and streams them to AWS/on-prem
for real-time analytics. Handles 20 million concurrent TV devices.

## What data is collected

Every time a Bluetooth device connects to a TV, one event is produced:

| Field          | Type    | Example                  |
|----------------|---------|--------------------------|
| tv_id          | string  | TV-00000001              |
| device_name    | string  | Sony WH-1000XM5          |
| device_type    | string  | headphones               |
| mac_address    | string  | AA:BB:CC:DD:EE:FF        |
| connection_ms  | uint32  | 342                      |
| rssi           | int32   | -55                      |
| timestamp_ms   | uint64  | 1700000000000            |

## Architecture

```
20M TVs  (C++ agent)
    │  JSON over Kafka/TLS
    ▼
Kafka  ──  topic: bt-events  (200 partitions, keyed by tv_id)
    │
    ▼
Apache Flink  (Java)
    ├── 5-min tumbling window per TV
    │       avg/p99 connection_ms, unique devices, avg RSSI
    │       └──▶  TimescaleDB  bt_window_stats
    │
    └── filter connection_ms > 2000ms
            └──▶  Kafka  bt-alerts
                    └──▶  (SNS / PagerDuty / webhook)

TimescaleDB
    ├── bt_events          (raw, 30-day retention)
    ├── bt_window_stats    (aggregated, 1-year retention)
    └── bt_hourly_fleet    (continuous aggregate, for Grafana)

Grafana  (port 3000)
    └── Fleet avg/p99 connection time, active TVs, device type breakdown
```

## Project Structure

```
tv-telemetry-analytics/
├── agent/                        C++ agent (runs on each TV)
│   ├── include/
│   │   ├── bt_event.hpp          single data model
│   │   ├── bt_monitor.hpp        bluetooth monitoring
│   │   ├── kafka_producer.hpp    batched Kafka produce
│   │   ├── local_buffer.hpp      SQLite offline queue
│   │   └── logger.hpp            thread-safe logger
│   ├── src/
│   │   ├── main.cpp
│   │   ├── bt_monitor.cpp
│   │   ├── kafka_producer.cpp
│   │   └── local_buffer.cpp
│   ├── CMakeLists.txt
│   └── Dockerfile.agent
│
├── flink-job/                    Java Flink job (runs on server)
│   ├── pom.xml
│   └── src/main/java/com/telemetry/
│       ├── BtEvent.java
│       ├── BtWindowStats.java
│       └── BtAnalyticsJob.java
│
├── infra/                        Local development stack
│   ├── docker-compose.yml        Kafka + Flink + TimescaleDB + Grafana
│   ├── init-db.sql               TimescaleDB schema
│   └── grafana-provisioning/     auto-loaded dashboards + datasource
│
├── k8s/                          Kubernetes manifests (EKS deployment)
│   ├── namespace.yaml            telemetry namespace
│   ├── storage-class.yaml        gp3 StorageClass for EBS
│   ├── kafka-strimzi.yaml        Strimzi Kafka cluster (KRaft mode)
│   ├── timescaledb.yaml          TimescaleDB StatefulSet
│   ├── flink.yaml                Flink JobManager + TaskManager
│   └── tv-agent.yaml             TV agent deployment (simulators)
│
├── terraform/                    Infrastructure as Code
│   └── eks/                      EKS cluster configuration
│       └── main.tf               VPC, EKS, node groups, IAM
│
├── scripts/
│   └── quick-start.ps1           PowerShell quick start script
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_bt_event.cpp
│   ├── test_bt_monitor.cpp
│   └── test_local_buffer.cpp
│
├── LEARNING_GUIDE.md             Comprehensive system design tutorial
├── AWS_EKS_COMPLETE_GUIDE.md     EKS deployment & scalability guide
└── README.md                     This file
```

## Scalability at 20M Devices

| Concern              | Solution                                                    |
|----------------------|-------------------------------------------------------------|
| 20M producers        | Kafka 200 partitions on bt-events, partitioned by tv_id     |
| High write throughput| linger_ms=5 + batch.size=64KB in C++ agent                  |
| Offline TVs          | SQLite local buffer → drain on Kafka reconnect              |
| Stream processing    | Flink TaskManagers scale horizontally (`--scale flink-tm=N`)|
| Storage              | TimescaleDB hypertable, 1-day chunks, auto-compression      |
| Query performance    | Index on (tv_id, time DESC), continuous aggregates          |
| Fault tolerance      | Flink checkpointing every 60s, idempotent Kafka producer    |

Throughput estimate: 20M TVs × 1 event/min = ~333K events/sec
Kafka: 200 partitions × ~1,700 events/sec/partition — well within limits.

## Build & Run

### 1. Start the full stack

```bash
cd infra
docker-compose up -d
# Scale Flink TaskManagers for more throughput:
docker-compose up -d --scale flink-tm=4
```

### 2. Build and submit the Flink job

```bash
cd flink-job
mvn clean package -q
# Submit to running Flink cluster:
curl -X POST http://localhost:8081/jars/upload \
  -H "Expect:" -F "jarfile=@target/bt-analytics-1.0.0.jar"
```

### 3. Build the C++ agent (Linux / WSL)

```bash
cd agent
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_KAFKA=ON .
make -j$(nproc)
./tv-bt-agent TV-00001 localhost:9092 ./buffer.db
```

### 4. Build and run tests

```bash
cd agent
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_KAFKA=OFF -DBUILD_TESTS=ON .
make -j$(nproc)
ctest --output-on-failure
```

### 5. Open dashboards

- Grafana:   http://localhost:3000  (admin / admin)
- Flink UI:  http://localhost:8081

## Deploy to AWS EKS

### 1. Create EKS cluster with Terraform

```bash
cd terraform/eks
terraform init
terraform apply

# Configure kubectl
aws eks update-kubeconfig --region us-east-1 --name tv-telemetry
```

### 2. Install Strimzi Kafka operator

```bash
kubectl create namespace kafka
kubectl apply -f 'https://strimzi.io/install/latest?namespace=kafka' -n kafka
```

### 3. Deploy the stack

```bash
kubectl apply -f k8s/namespace.yaml
kubectl apply -f k8s/storage-class.yaml
kubectl apply -f k8s/kafka-strimzi.yaml
kubectl apply -f k8s/timescaledb.yaml
kubectl apply -f k8s/flink.yaml
kubectl apply -f k8s/tv-agent.yaml
```

### 4. Verify deployment

```bash
kubectl get pods -n telemetry
kubectl get kafka -n telemetry
```

See `AWS_EKS_COMPLETE_GUIDE.md` for detailed deployment instructions and troubleshooting.

## Deploying on Real TVs

1. Cross-compile the agent for the TV's CPU (ARM/MIPS):
   ```bash
   cmake -DCMAKE_TOOLCHAIN_FILE=<your-toolchain>.cmake \
         -DCMAKE_BUILD_TYPE=Release -DENABLE_KAFKA=ON .
   ```
2. Remove the `run_simulation()` call in `main.cpp`
3. Implement the BlueZ D-Bus or webOS luna-service hook in `bt_monitor.cpp`
4. Deploy binary + config via OTA update system

## Documentation

| Document | Description |
|----------|-------------|
| `README.md` | Project overview and quick start |
| `LEARNING_GUIDE.md` | System design tutorial (C++, Flink, Kafka, K8s) |
| `AWS_EKS_COMPLETE_GUIDE.md` | EKS deployment, scalability, and architecture |
