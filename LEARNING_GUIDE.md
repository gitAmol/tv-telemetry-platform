# TV Telemetry Analytics — System Design from Scratch to Cloud
## Scale: 20 Million Smart TVs | Deployment: Kubernetes on AWS
## Development Environment: Windows 11 + VS Code

---

## How to Use This Guide

This is a **hands-on system design tutorial** built for development in **VS Code on Windows 11**.
Each phase builds on the previous one:

1. **Phases 0–2**: System design fundamentals — you learn WHY before HOW
2. **Phases 3–5**: Build the system locally with real code
3. **Phases 6–8**: Containerize and deploy to Kubernetes on AWS
4. **Phases 9–10**: Load test with 20M simulated devices, observe, and tune

**Every phase has:**
- Theory (system design concepts)
- Practical coding exercises (all in VS Code)
- Verification steps (how to confirm it works)

**Development Stack (Windows 11 + VS Code):**
- **Editor**: VS Code with C/C++, Java Extension Pack, Docker, Kubernetes extensions
- **Containers**: Docker Desktop for Windows (WSL2 backend)
- **C++ Build**: CMake + MSVC or MinGW (via VS Code CMake Tools)
- **Java Build**: JDK 11 + Maven (native Windows install)
- **Terminal**: VS Code integrated terminal (PowerShell primary, Git Bash for POSIX)
- **Kubernetes**: Docker Desktop K8s or Minikube on Windows
- **Cloud CLI**: AWS CLI v2 for Windows, Terraform, kubectl, Helm

---

## PHASE 0 — System Design Fundamentals (Week 1)

Before writing code, you must understand the design principles that make a system
handle 20 million devices. This phase teaches the vocabulary and mental models.

### 0.1 The Four Pillars of Scalable System Design

```
┌─────────────────────────────────────────────────────────────────┐
│                    SCALABILITY PILLARS                           │
├─────────────────┬──────────────────┬──────────────┬─────────────┤
│  1. DECOUPLE    │  2. PARTITION    │  3. BUFFER   │  4. OBSERVE │
│                 │                  │              │             │
│  Producers must │  Split data by   │  Absorb      │  You cannot │
│  not know about │  a key so each   │  bursts in   │  fix what   │
│  consumers.     │  shard handles   │  a queue so  │  you cannot │
│  Change one     │  a bounded       │  downstream  │  measure.   │
│  without the    │  subset.         │  processes   │             │
│  other.         │                  │  at its own  │             │
│                 │                  │  pace.       │             │
└─────────────────┴──────────────────┴──────────────┴─────────────┘
```

**How they apply to this project:**
| Pillar | This System's Application |
|--------|--------------------------|
| Decouple | TV agent → Kafka → Flink. The TV doesn't know Flink exists. |
| Partition | Kafka 200 partitions keyed by `tv_id`. Each shard handles ~100K TVs. |
| Buffer | SQLite on TV (offline), Kafka between TV and Flink (backpressure). |
| Observe | Prometheus + Grafana on every component; CloudWatch on AWS. |

### 0.2 Back-of-Envelope Estimation (Critical Interview Skill)

Before designing anything, estimate the load:

```
Given:
  - 20,000,000 TVs
  - Each TV sends 1 Bluetooth event per minute (average)
  - Each event is ~300 bytes JSON

Calculate:
  Events per second:  20M / 60 = ~333,333 events/sec
  Bandwidth:          333,333 × 300 bytes = ~100 MB/sec = ~800 Mbps
  Daily storage:      333,333 × 300 × 86,400 = ~8.6 TB/day (raw)

Peak factor (prime time 7–10 PM):
  3× average = ~1,000,000 events/sec = ~300 MB/sec

These numbers drive every design decision:
  - Kafka partitions: 300 MB/s ÷ 1 MB/s per partition = 300 partitions
  - Kinesis shards:   300 MB/s ÷ 1 MB/s per shard = 300 shards
  - Network:          Needs 1 Gbps NIC minimum on ingestion layer
```

**Exercise 0.2**: Change the assumptions — what if TVs send 1 event/sec during
active Bluetooth usage (10% of fleet active)? Recalculate.

### 0.3 CAP Theorem — Practical Application

```
       Consistency
          /\
         /  \
        /    \
       / PICK \
      /  TWO   \
     /          \
    /____________\
Availability   Partition Tolerance
```

**For this system:**
- We choose **AP** (Available + Partition Tolerant) for event ingestion
  - If Kafka is partitioned, TVs buffer locally (available to write)
  - Events may arrive out of order (not strictly consistent)
- We choose **CP** (Consistent + Partition Tolerant) for aggregated stats
  - Flink guarantees exactly-once processing via checkpoints
  - Dashboard may be stale during a partition, but never wrong

### 0.4 Data Flow Patterns

```
Pattern 1: FIRE AND FORGET (TV → Kafka)
  TV sends event. Kafka acks. TV forgets. Simple, fast.
  Trade-off: If Kafka loses the event, it's gone.

Pattern 2: STORE AND FORWARD (TV → SQLite → Kafka)
  TV writes to local SQLite first. Background thread drains to Kafka.
  Trade-off: Slower, but survives TV reboot and network outage.

Pattern 3: EVENT SOURCING (Kafka → Flink → DB)
  The Kafka log IS the source of truth. DB is a derived view.
  Trade-off: Can replay and recompute, but Kafka retention costs money.

This system uses Pattern 2 (TV side) + Pattern 3 (server side).
```

### 0.5 Prerequisites — Setting Up Your Windows 11 + VS Code Environment

#### Your Current Environment (Already Installed)

Based on your system, you already have these tools ready:

| Tool | Version | Status |
|------|---------|--------|
| Docker Desktop | 29.6.2 | ✅ Ready |
| Java JDK 11 | 11.0.32.9 (Eclipse Adoptium) | ✅ Ready |
| Maven | 3.9.16 | ✅ Ready |
| Python | 3.13.7 | ✅ Ready |
| Node.js | 22.19.0 | ✅ Ready |
| AWS CLI | 2.34.42 | ✅ Ready |
| kubectl | 1.36.1 | ✅ Ready |
| Terraform | 1.9.8 | ✅ Ready |
| Helm | 3.16.3 | ✅ Ready |
| eksctl | 0.194.0 | ✅ Ready |
| Git | (installed) | ✅ Ready |

**Important**: Java 11 is required (not Java 8) because the Flink job uses Java 11 features.
Set JAVA_HOME permanently:
```powershell
setx JAVA_HOME "C:\Program Files\Eclipse Adoptium\jdk-11.0.32.9-hotspot"
# Restart terminal after running this
```

#### Step 1: Install Missing Tools

**Option A: Using Chocolatey (if it works)**
```powershell
# Run PowerShell as Administrator
choco install maven terraform kubernetes-helm eksctl -y
```

**Option B: Manual Installation (Recommended - more reliable)**

If Chocolatey has permission issues, install manually:

1. **Maven**: Download from https://maven.apache.org/download.cgi
   - Extract to `C:\maven`
   - Add `C:\maven\bin` to PATH

2. **Terraform**: Download from https://developer.hashicorp.com/terraform/downloads
   - Extract to `C:\terraform`
   - Add `C:\terraform` to PATH

3. **Helm**: Download from https://github.com/helm/helm/releases
   - Extract to `C:\helm`
   - Add `C:\helm` to PATH

4. **eksctl**: Download from https://github.com/weaveworks/eksctl/releases
   - Extract to `C:\eksctl`
   - Add `C:\eksctl` to PATH

**After installation, close and reopen VS Code terminal to refresh PATH.**

#### Step 2: Verify All Tools

```powershell
# Run this in VS Code terminal (PowerShell) to verify everything works:
docker --version          # Docker version 29.6.2
java -version             # java version "1.8.0_202"
mvn --version             # Apache Maven 3.x.x (after install)
python --version          # Python 3.13.7
node --version            # v22.19.0
aws --version             # aws-cli/2.34.42
kubectl version --client  # Client Version: v1.36.1
terraform --version       # Terraform v1.x.x (after install)
helm version --short      # v3.x.x (after install)
cmake --version           # cmake version 3.x.x (after install)
```

#### Step 3: Install VS Code Extensions

Open VS Code → Extensions panel (`Ctrl+Shift+X`) → Install these:

| Extension | Publisher | Purpose |
|-----------|-----------|---------|
| C/C++ | Microsoft | IntelliSense, debugging for the agent |
| CMake Tools | Microsoft | Build system integration |
| Java Extension Pack | Microsoft | Java language support for Flink job |
| Maven for Java | Microsoft | Maven build integration |
| Docker | Microsoft | Dockerfile syntax, container management |
| Kubernetes | Microsoft | K8s cluster explorer, manifest editing |
| YAML | Red Hat | YAML schema validation (K8s, docker-compose) |
| PostgreSQL | Chris Kolkman | Query TimescaleDB directly from VS Code |
| REST Client | Humao | Test Flink REST API from `.http` files |
| GitLens | GitKraken | Git history, blame |

**Quick install via command line:**
```powershell
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
code --install-extension vscjava.vscode-java-pack
code --install-extension ms-azuretools.vscode-docker
code --install-extension ms-kubernetes-tools.vscode-kubernetes-tools
code --install-extension redhat.vscode-yaml
code --install-extension ckolkman.vscode-postgres
code --install-extension humao.rest-client
code --install-extension eamodio.gitlens
```

#### Step 4: Configure Docker Desktop

1. Open Docker Desktop → Settings (gear icon)
2. **General**: Enable "Use WSL 2 based engine" ✓
3. **Kubernetes**: Check "Enable Kubernetes" ✓ (gives you a local K8s cluster)
4. **Resources**: Allocate at least 8 GB RAM, 4 CPUs
5. Click "Apply & Restart"
6. Wait for both Docker and Kubernetes to show green status

**Verify Kubernetes is running:**
```powershell
kubectl cluster-info
# Should show: Kubernetes control plane is running at https://kubernetes.docker.internal:6443
```

#### Step 5: C++ Build — Use Docker (No Local Compiler Needed!)

**Good news:** Since you have Docker, you don't need to install C++ compilers locally.
The C++ agent builds inside a Docker container with all dependencies pre-installed.

```powershell
# Build the C++ agent using Docker (from project root)
cd e:\tv-telemetry-analytics\agent
docker build -t tv-bt-agent:latest -f Dockerfile.agent .

# Run the agent in simulation mode
docker run --rm -e TV_ID=TV-00001 tv-bt-agent:latest
```

**This approach:**
- ✅ No need to install Visual Studio Build Tools (5+ GB)
- ✅ No need to install vcpkg or manage C++ dependencies
- ✅ Builds identically on any machine with Docker
- ✅ Same container runs in Kubernetes later

**Optional: Local C++ Development (if you want IntelliSense)**

If you want to edit C++ code with full IntelliSense in VS Code:

```powershell
# Option A: Visual Studio Build Tools (Recommended)
choco install visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" -y

# Option B: MinGW-w64 (lighter weight)
choco install mingw -y
```

Then install vcpkg for C++ dependencies:
```powershell
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install sqlite3:x64-windows nlohmann-json:x64-windows librdkafka:x64-windows
[System.Environment]::SetEnvironmentVariable("CMAKE_TOOLCHAIN_FILE", "C:\vcpkg\scripts\buildsystems\vcpkg.cmake", "User")
```

#### Step 6: VS Code Workspace Settings

Create `.vscode/settings.json` in the project root:
```json
{
  "java.configuration.updateBuildConfiguration": "automatic",
  "terminal.integrated.defaultProfile.windows": "PowerShell",
  "files.associations": {
    "*.hpp": "cpp",
    "*.cpp": "cpp",
    "docker-compose.yml": "dockercompose"
  },
  "docker.showStartPage": false,
  "kubernetes.namespace": "telemetry"
}
```

#### Step 7: Quick Verification — Everything Works

Run these commands in VS Code terminal to confirm your environment is ready:

```powershell
# 1. Docker is running
docker run --rm hello-world

# 2. Kubernetes is running (Docker Desktop K8s)
kubectl get nodes
# Expected: docker-desktop   Ready   control-plane   ...

# 3. AWS CLI is configured
aws sts get-caller-identity
# Expected: Shows your AWS account ID (or configure with: aws configure)

# 4. Python works
python --version

# 5. Java works
java -version
```

**If all commands succeed, you're ready to start Phase 1!**

---

## PHASE 1 — Understand the Architecture (Week 2)

### 1.1 System Overview — What We Are Building

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         TV TELEMETRY ANALYTICS                                │
│                                                                               │
│  EDGE (20M TVs)          INGESTION            PROCESSING          STORAGE    │
│  ┌──────────┐            ┌───────┐            ┌───────┐          ┌───────┐  │
│  │C++ Agent │──MQTT/TLS──│ Kafka │────────────│ Flink │──────────│TimescaleDB│
│  │on each TV│            │Cluster│            │Cluster│          │(per-TV   │
│  └──────────┘            └───────┘            └───────┘          │ stats)   │
│       │                       │                    │              └───────┘  │
│       │                       │                    │              ┌───────┐  │
│  ┌──────────┐                 │                    └──────────────│  S3   │  │
│  │  SQLite  │                 │                                   │(raw    │  │
│  │  Buffer  │                 │              ┌───────┐            │archive)│  │
│  └──────────┘                 └──────────────│Alerts │            └───────┘  │
│                                              │(SNS)  │                       │
│                                              └───────┘           ┌───────┐  │
│                                                                  │Grafana│  │
│                                                                  │(viz)  │  │
│                                                                  └───────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Why Each Component Exists

| Component | Role | Why Not Skip It? |
|-----------|------|------------------|
| C++ Agent | Lightweight telemetry collector on TV | C++ is mandatory for constrained embedded devices (256MB RAM) |
| SQLite Buffer | Offline queue on TV | TVs lose network; events must survive power loss |
| Kafka | Distributed event bus | Decouples 20M producers from consumers; absorbs bursts |
| Flink | Stream processor | Stateful per-TV windows; exactly-once; event-time processing |
| TimescaleDB | Time-series store | Time-range queries on aggregated stats; auto-compression |
| S3 | Raw event archive | Cheap long-term storage; enables reprocessing |
| Grafana | Visualization | Operational dashboards for fleet health |
| Prometheus | Metrics collection | Observes all K8s pods; alerts on SLA breach |

### 1.3 The Event Model — Keep It Simple

```cpp
// One struct. Seven fields. Serves the entire system.
struct BtEvent {
    std::string tv_id;         // "TV-00000001"
    std::string device_name;   // "Sony WH-1000XM5"
    std::string device_type;   // "headphones"
    std::string mac_address;   // "AA:BB:CC:DD:EE:FF"
    uint32_t    connection_ms; // 342 (time to connect)
    int32_t     rssi;          // -55 (signal strength)
    uint64_t    timestamp_ms;  // 1700000000000 (epoch ms)
};
```

**Design Decision**: One event type, not many. Why?
- One Kafka topic, one Flink job, one DB schema to maintain
- Every additional event type multiplies operational complexity at EVERY layer
- YAGNI — add new types when the requirement arrives, not speculatively

### 1.4 Project Directory Structure (in VS Code Explorer)

```
tv-telemetry-analytics/          ← Open this folder in VS Code
├── .vscode/                     # VS Code workspace settings
│   ├── settings.json            # CMake, Java, terminal config
│   ├── launch.json              # Debug configurations
│   └── tasks.json               # Build tasks (Ctrl+Shift+B)
│
├── agent/                       # C++ agent (runs on each TV)
│   ├── include/                 # Header files
│   ├── src/                     # Implementation
│   ├── build/                   # CMake build output (gitignored)
│   ├── CMakeLists.txt           # Build system
│   └── Dockerfile.agent         # Container for testing
│
├── flink-job/                   # Java Flink streaming job
│   ├── src/main/java/           # Flink application
│   ├── target/                  # Maven build output (gitignored)
│   └── pom.xml                  # Maven dependencies
│
├── infra/                       # Infrastructure
│   ├── docker-compose.yml       # Local development stack
│   ├── init-db.sql              # TimescaleDB schema
│   └── grafana-provisioning/    # Dashboard configs
│
├── k8s/                         # Kubernetes manifests
│   ├── namespace.yaml
│   ├── kafka/                   # Strimzi Kafka operator
│   ├── flink/                   # Flink on K8s
│   ├── timescaledb/             # StatefulSet
│   ├── monitoring/              # Prometheus + Grafana
│   └── load-test/               # 20M device simulator
│
├── terraform/                   # AWS infrastructure
│   ├── eks/                     # EKS cluster
│   ├── networking/              # VPC, subnets
│   └── storage/                 # S3, EBS
│
├── load-test/                   # Traffic simulator
│   ├── Dockerfile
│   ├── simulator.py             # Generates 20M device traffic
│   └── k8s-job.yaml            # Runs as K8s Job
│
└── tests/                       # Unit tests
    ├── CMakeLists.txt
    └── test_*.cpp
```

---

## PHASE 2 — Build the C++ Agent (Week 3)

### 2.1 Design Principles for Edge Agents

```
Rule 1: SHIP RAW DATA, DON'T PROCESS
  - Aggregation logic changes → requires OTA firmware update to 20M TVs
  - Raw events enable reprocessing with new algorithms
  - Processing belongs on cheap cloud servers, not expensive TV hardware

Rule 2: NEVER BLOCK THE MAIN THREAD
  - TV must remain responsive to user input
  - All I/O (network, disk) happens on background threads

Rule 3: SURVIVE EVERYTHING
  - Network down? Buffer to SQLite.
  - Power loss? SQLite WAL mode = atomic writes.
  - Kafka slow? Backpressure via bounded queue, never drop events.

Rule 4: MINIMAL RESOURCE FOOTPRINT
  - < 5MB RAM usage
  - < 1% CPU (background thread wakes every few seconds)
  - < 50MB disk for SQLite buffer
```

### 2.2 Building the Agent in VS Code (Windows)

**Using CMake Tools Extension:**

1. Open the project folder in VS Code
2. Press `Ctrl+Shift+P` → "CMake: Select a Kit" → Choose your compiler:
   - "Visual Studio Build Tools 2022 - amd64" (MSVC)
   - OR "GCC x.x.x (MinGW-w64)" if using MSYS2
3. Press `Ctrl+Shift+P` → "CMake: Configure"
4. Press `Ctrl+Shift+P` → "CMake: Build" (or press `F7`)

**Manual build via VS Code terminal (PowerShell):**

```powershell
# Navigate to agent directory
cd agent

# Create build directory
New-Item -ItemType Directory -Force -Path build
cd build

# Configure with CMake (using vcpkg for dependencies)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_KAFKA=OFF -DBUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build . --config Debug

# Run tests
ctest --output-on-failure -C Debug
```

**Alternative: Build via Git Bash terminal in VS Code:**

```bash
# If you prefer POSIX-style commands, switch to Git Bash terminal
cd agent
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_KAFKA=OFF -DBUILD_TESTS=ON \
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build . --config Debug
ctest --output-on-failure -C Debug
```

### 2.3 VS Code Debug Configuration for Agent

Create `.vscode/launch.json`:
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug C++ Agent",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/agent/build/Debug/tv-bt-agent.exe",
      "args": ["TV-00001", "localhost:9092", "./test-buffer.db"],
      "cwd": "${workspaceFolder}/agent/build",
      "stopAtEntry": false,
      "environment": [],
      "console": "integratedTerminal"
    },
    {
      "name": "Debug C++ Tests",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/agent/build/Debug/agent_tests.exe",
      "cwd": "${workspaceFolder}/agent/build",
      "stopAtEntry": false,
      "console": "integratedTerminal"
    }
  ]
}
```

Press `F5` to start debugging. Set breakpoints by clicking the gutter in any `.cpp` file.

### 2.4 VS Code Build Tasks

Create `.vscode/tasks.json`:
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "CMake: Build Agent",
      "type": "shell",
      "command": "cmake --build agent/build --config Debug",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": ["$gcc", "$msCompile"]
    },
    {
      "label": "CMake: Run Tests",
      "type": "shell",
      "command": "ctest --output-on-failure -C Debug",
      "options": { "cwd": "${workspaceFolder}/agent/build" },
      "group": "test",
      "problemMatcher": []
    },
    {
      "label": "Maven: Build Flink Job",
      "type": "shell",
      "command": "mvn clean package -DskipTests",
      "options": { "cwd": "${workspaceFolder}/flink-job" },
      "group": "build",
      "problemMatcher": ["$javac"]
    },
    {
      "label": "Docker: Start Stack",
      "type": "shell",
      "command": "docker compose up -d",
      "options": { "cwd": "${workspaceFolder}/infra" },
      "problemMatcher": []
    },
    {
      "label": "Docker: Stop Stack",
      "type": "shell",
      "command": "docker compose down",
      "options": { "cwd": "${workspaceFolder}/infra" },
      "problemMatcher": []
    }
  ]
}
```

Use `Ctrl+Shift+B` to run the default build, or `Ctrl+Shift+P` → "Tasks: Run Task" for others.

### 2.5 The Code (Review in VS Code)

Open these files in VS Code's editor and study them:

- `agent/include/bt_event.hpp` — The event data model
- `agent/include/local_buffer.hpp` — SQLite offline queue
- `agent/include/kafka_producer.hpp` — Kafka batched producer
- `agent/include/bt_monitor.hpp` — Bluetooth monitoring
- `agent/src/main.cpp` — Wiring everything together

**Key tip**: Use `Ctrl+Click` on any symbol to jump to its definition. Use `Ctrl+Shift+O` to browse symbols in a file.

### 2.6 Code Deep Dive — Line-by-Line Walkthrough

Let's understand exactly how the C++ agent works by examining the key files.

#### 2.6.1 The Event Model (`bt_event.hpp`)

```cpp
// WHY a struct and not a class?
// - All fields are public data, no invariants to protect
// - Simpler serialization (no getters/setters)
// - This is a "Plain Old Data" (POD) type — intentional design choice

struct BtEvent {
    std::string tv_id;         // Partition key for Kafka (ensures ordering per TV)
    std::string device_name;   // Human-readable, for debugging/dashboards
    std::string device_type;   // Categorical: headphones|speaker|remote|gamepad
    std::string mac_address;   // Unique device identifier (anonymized in prod)
    uint32_t    connection_ms; // THE KEY METRIC — how long BT pairing took
    int32_t     rssi;          // Signal strength: -30 (strong) to -90 (weak)
    uint64_t    timestamp_ms;  // Event time (not arrival time!) — critical for Flink
};
```

**Why `timestamp_ms` is uint64_t, not `time_t`:**
- `time_t` is seconds — we need millisecond precision for accurate windowing
- uint64_t holds milliseconds since epoch until year 584 million — sufficient!
- Matches JavaScript `Date.now()` and Java `System.currentTimeMillis()`

#### 2.6.2 Thread Safety Pattern (`bt_monitor.hpp`)

```cpp
class BtMonitor {
private:
    std::atomic<bool> running_{false};  // WHY atomic?
    std::thread monitor_thread_;
    
public:
    void start() {
        running_.store(true);  // Thread-safe write
        monitor_thread_ = std::thread([this]() {
            while (running_.load()) {  // Thread-safe read
                // ... monitor Bluetooth
            }
        });
    }
    
    void stop() {
        running_.store(false);  // Signal thread to exit
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();  // Wait for clean shutdown
        }
    }
};
```

**Why `std::atomic<bool>` instead of plain `bool`?**

```
PROBLEM WITHOUT ATOMIC:
  Thread A (main):     running_ = false;   // Write
  Thread B (monitor):  while(running_) {}  // Read
  
  Without atomic:
  1. Compiler may cache running_ in a register (never sees update)
  2. CPU may reorder writes (running_=false happens "after" other code)
  3. CPU caches may not sync between cores
  
  Result: Thread B loops forever, program hangs on shutdown.

WITH ATOMIC:
  - Memory barrier ensures visibility across cores
  - Compiler cannot optimize away the load
  - Guaranteed to see the update within nanoseconds
```

#### 2.6.3 SQLite Buffer Pattern (`local_buffer.cpp`)

```cpp
void LocalBuffer::push(const BtEvent& event) {
    // WHY prepare statement once, bind many times?
    // - Parsing SQL is expensive (~100μs)
    // - Binding values is cheap (~1μs)
    // - 100x faster for repeated inserts
    
    sqlite3_bind_text(insert_stmt_, 1, event.tv_id.c_str(), -1, SQLITE_TRANSIENT);
    // SQLITE_TRANSIENT = "copy this string, I might free it"
    // SQLITE_STATIC = "this string lives forever, don't copy"
    // We use TRANSIENT because event goes out of scope
    
    sqlite3_step(insert_stmt_);   // Execute
    sqlite3_reset(insert_stmt_);  // Reuse for next insert
}
```

**Common Pitfall: Forgetting `sqlite3_reset()`**
```cpp
// WRONG — second insert silently fails!
push(event1);  // Works
push(event2);  // Returns SQLITE_MISUSE, event lost!

// RIGHT — reset after each step
push(event1);  // Works
sqlite3_reset(stmt);
push(event2);  // Works
```

#### 2.6.4 Kafka Producer Pattern (`kafka_producer.cpp`)

```cpp
void KafkaProducer::send(const BtEvent& event) {
    std::string json = serialize(event);
    
    // WHY key = tv_id?
    // Kafka partitions by hash(key) % num_partitions
    // Same tv_id → same partition → ordered delivery per TV
    // Different tv_ids → spread across partitions → parallelism
    
    rd_kafka_producev(
        producer_,
        RD_KAFKA_V_TOPIC("bt-events"),
        RD_KAFKA_V_KEY(event.tv_id.c_str(), event.tv_id.size()),
        RD_KAFKA_V_VALUE(json.c_str(), json.size()),
        RD_KAFKA_V_END
    );
    
    // WHY poll(0) after produce?
    // librdkafka batches internally. poll() triggers:
    // 1. Delivery reports (success/failure callbacks)
    // 2. Actual network send if batch is ready
    // Without poll(), events queue up but never send!
    rd_kafka_poll(producer_, 0);
}
```

### 2.7 Exercises with Expected Output

#### Exercise 2.7.1: Verify SQLite Buffering

```powershell
# 1. Run agent without Kafka (events buffer to SQLite)
cd agent\build\Debug
.\tv-bt-agent.exe TV-TEST-001 localhost:9092 .\test.db
# Let it run for 10 seconds, then Ctrl+C

# 2. Inspect the SQLite buffer
sqlite3 test.db "SELECT COUNT(*) FROM events;"
# Expected: ~10 (one event per second in simulation mode)

sqlite3 test.db "SELECT * FROM events LIMIT 3;"
# Expected output:
# 1|TV-TEST-001|Sony WH-1000XM5|headphones|AA:BB:CC:11:22:33|342|-55|1700000000000
# 2|TV-TEST-001|JBL Flip 6|speaker|AA:BB:CC:11:22:34|512|-62|1700000001000
# 3|TV-TEST-001|Samsung Remote|remote|AA:BB:CC:11:22:35|128|-45|1700000002000
```

#### Exercise 2.7.2: Verify Event JSON Format

```powershell
# Add debug logging to see JSON output
# In kafka_producer.cpp, add: LOG_DEBUG("JSON: " + json);
# Rebuild and run

# Expected JSON format:
# {"tv_id":"TV-TEST-001","device_name":"Sony WH-1000XM5","device_type":"headphones",
#  "mac_address":"AA:BB:CC:11:22:33","connection_ms":342,"rssi":-55,
#  "timestamp_ms":1700000000000}
```

#### Exercise 2.7.3: Stress Test the Buffer

```powershell
# Modify simulation to generate 1000 events/sec for 60 seconds
# Then check:
sqlite3 test.db "SELECT COUNT(*) FROM events;"
# Expected: ~60,000 events

sqlite3 test.db "SELECT SUM(LENGTH(json_data)) FROM events;"
# Expected: ~18 MB (300 bytes × 60K events)

# Verify no data corruption:
sqlite3 test.db "PRAGMA integrity_check;"
# Expected: ok
```

### 2.8 Common Pitfalls and How to Avoid Them

```
┌─────────────────────────────────────────────────────────────────────────┐
│  PITFALL #1: Memory Leak in Event Loop                                   │
├─────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                  │
│    while (running_) {                                                   │
│        BtEvent* event = new BtEvent();  // Allocated on heap            │
│        // ... use event ...                                             │
│        queue.push(event);  // Pointer stored, but who deletes it?       │
│    }                                                                    │
│                                                                         │
│  RIGHT:                                                                  │
│    while (running_) {                                                   │
│        BtEvent event;  // Stack allocated, auto-destroyed               │
│        // ... use event ...                                             │
│        queue.push(event);  // Copied into queue                         │
│    }                                                                    │
│                                                                         │
│  OR (if you need heap):                                                 │
│    while (running_) {                                                   │
│        auto event = std::make_unique<BtEvent>();                        │
│        queue.push(std::move(event));  // Ownership transferred          │
│    }                                                                    │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│  PITFALL #2: Blocking the Main Thread                                    │
├─────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                  │
│    void onBluetoothEvent(BtEvent event) {                               │
│        kafka_producer.send(event);  // Blocks if Kafka is slow!         │
│        // TV UI freezes during network issues                           │
│    }                                                                    │
│                                                                         │
│  RIGHT:                                                                  │
│    void onBluetoothEvent(BtEvent event) {                               │
│        event_queue.push(event);  // Non-blocking, ~1μs                  │
│        // Background thread drains queue to Kafka                       │
│    }                                                                    │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│  PITFALL #3: Lost Events on Shutdown                                     │
├─────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                  │
│    void shutdown() {                                                    │
│        running_ = false;                                                │
│        exit(0);  // Events in queue are lost!                           │
│    }                                                                    │
│                                                                         │
│  RIGHT:                                                                  │
│    void shutdown() {                                                    │
│        running_ = false;                                                │
│        monitor_thread_.join();      // Wait for monitor to stop         │
│        producer_thread_.join();     // Wait for queue to drain          │
│        local_buffer_.flush();       // Persist any remaining events     │
│        kafka_producer_.flush(5000); // Wait up to 5s for Kafka acks     │
│    }                                                                    │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│  PITFALL #4: Timestamp from Wrong Clock                                  │
├─────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                  │
│    event.timestamp_ms = time(nullptr) * 1000;  // System clock          │
│    // Problem: NTP sync can jump time backward!                         │
│    // Event at 10:00:00 followed by event at 09:59:58 = chaos           │
│                                                                         │
│  RIGHT:                                                                  │
│    event.timestamp_ms = std::chrono::duration_cast<milliseconds>(       │
│        system_clock::now().time_since_epoch()                           │
│    ).count();                                                           │
│    // Still system clock, but at least consistent within process        │
│                                                                         │
│  BEST (for ordering):                                                   │
│    static std::atomic<uint64_t> seq{0};                                 │
│    event.sequence = seq.fetch_add(1);  // Monotonic sequence number     │
│    event.timestamp_ms = ...;           // For human readability         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.9 Debugging Strategies

#### Strategy 1: Add Structured Logging

```cpp
// In logger.hpp, add log levels:
enum class LogLevel { DEBUG, INFO, WARN, ERROR };

// Use structured fields for easy grep/parsing:
LOG_INFO("event_produced", 
    "tv_id", event.tv_id,
    "connection_ms", event.connection_ms,
    "queue_depth", queue.size());

// Output: [INFO] event_produced tv_id=TV-00001 connection_ms=342 queue_depth=5
```

#### Strategy 2: VS Code Debugger Breakpoints

```
Set breakpoints at these key locations:

1. bt_monitor.cpp:onDeviceConnected()  — See raw BT events
2. local_buffer.cpp:push()             — Verify SQLite writes
3. kafka_producer.cpp:send()           — Check JSON serialization
4. kafka_producer.cpp:deliveryCallback — Confirm Kafka acks

Use Conditional Breakpoints (right-click breakpoint → Edit):
  Condition: event.connection_ms > 2000
  This pauses only on slow connections — great for debugging alerts.
```

#### Strategy 3: SQLite as Debug Log

```sql
-- The SQLite buffer IS your debug log for offline analysis!

-- Find slowest connections:
SELECT * FROM events ORDER BY connection_ms DESC LIMIT 10;

-- Find events in a time range:
SELECT * FROM events 
WHERE timestamp_ms BETWEEN 1700000000000 AND 1700000060000;

-- Count events per device type:
SELECT device_type, COUNT(*) FROM events GROUP BY device_type;

-- Export to CSV for analysis:
.mode csv
.output events.csv
SELECT * FROM events;
.output stdout
```

### 2.10 Verification

After building, run the agent in simulation mode:

```powershell
# From VS Code terminal (PowerShell)
cd agent\build\Debug
.\tv-bt-agent.exe TV-00001 localhost:9092 .\test-buffer.db
```

You should see log output showing events being generated and buffered
(since Kafka isn't running yet, events go to SQLite).

---

## PHASE 3 — Stream Processing with Apache Flink (Week 4)

### 3.1 Why Flink? — The Stream Processing Decision

| Option | Model | State | Latency | Verdict |
|--------|-------|-------|---------|---------|
| AWS Lambda | Stateless, event-trigger | None (use DynamoDB) | ~100ms cold start | ❌ Stateless = expensive state lookups per event |
| Spark Streaming | Micro-batch | RDD-based | 1–30 sec | ❌ Not true streaming; high latency |
| Apache Flink | True streaming | Built-in keyed state | <100ms | ✅ Stateful, exactly-once, event-time |
| Custom C++ | Hand-rolled | Manual | Low | ❌ You'd rebuild Flink poorly |

**Flink wins because:**
1. **Stateful per-key processing** — accumulates stats per TV across events
2. **Exactly-once** — checkpoint + Kafka offset = no double-counting
3. **Event-time windows** — handles out-of-order events from offline TVs
4. **Managed on AWS (KDA) or Kubernetes** — no server management

### 3.2 Building the Flink Job in VS Code

The Java Extension Pack provides full IntelliSense for Maven projects.

**Build via VS Code terminal:**
```powershell
cd flink-job
mvn clean package -DskipTests
```

**Or use the Maven side panel**: Click the Maven icon in the Activity Bar → `flink-job` → Lifecycle → `package`.

The JAR is output to `flink-job/target/bt-analytics-1.0.0.jar`.

### 3.3 Understanding Windowing (System Design Concept)

```
EVENT TIMELINE FOR TV-00001:
  ──────────────────────────────────────────────────────────────────▶ time
  │  ev1  ev2  ev3  │  ev4  ev5  │  ev6  ev7  ev8  ev9  │
  └────── W1 ───────┘└──── W2 ───┘└──────── W3 ─────────┘
  0:00             5:00         10:00                   15:00

TUMBLING WINDOW (what we use):
  - Each event belongs to exactly ONE window
  - Window fires at end time → emits one aggregated row
  - W1 output: {tv_id: "TV-00001", avg_conn_ms: 250, events: 3, ...}

WHY NOT SLIDING?
  Sliding(5min, slide=1min) → each event counted in 5 windows
  = 5× more output rows, 5× more DB writes, confusing "total" metrics

WHY NOT SESSION?
  Session(gap=5min) → window boundaries are unpredictable
  = variable output rate, hard to capacity-plan downstream
```

### 3.4 Watermarks — Handling Late Data

```
PROBLEM: TV-00001 went offline at 10:00, came back at 10:08.
         It sends 8 minutes of buffered events with OLD timestamps.

WITHOUT WATERMARKS:
  - Flink sees event with timestamp 10:02 arrive at 10:08
  - The [10:00–10:05] window already closed and fired
  - Event is DROPPED — wrong aggregation forever

WITH WATERMARKS (bounded out-of-orderness = 10 seconds):
  - Flink allows events up to 10 seconds late
  - But 8 minutes late? Still dropped.

SOLUTION FOR THIS SYSTEM:
  - Watermark bound = 10 seconds (handles minor network jitter)
  - Events > 10 seconds late → written to a SIDE OUTPUT
  - Side output events go directly to bt_events raw table
  - They are NOT aggregated (acceptable trade-off)
  - For massive late data (hours), trigger a reprocessing job from S3 archive
```

### 3.5 Flink Code Deep Dive — Line-by-Line Walkthrough

#### 3.5.1 The Main Job (`BtAnalyticsJob.java`)

```java
public class BtAnalyticsJob {
    public static void main(String[] args) throws Exception {
        // WHY StreamExecutionEnvironment?
        // This is Flink's entry point — like main() but for distributed processing
        // It builds a DAG (Directed Acyclic Graph) of operators
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();
        
        // CRITICAL: Enable checkpointing for exactly-once semantics
        // Without this: Flink restart = lost state = wrong aggregations
        env.enableCheckpointing(60_000);  // Every 60 seconds
        env.getCheckpointConfig().setCheckpointingMode(CheckpointingMode.EXACTLY_ONCE);
        
        // WHY event time, not processing time?
        // Processing time: "when Flink sees the event" — affected by network delays
        // Event time: "when the event actually happened" — from timestamp_ms field
        // For offline TVs sending buffered events, event time is essential
        env.setStreamTimeCharacteristic(TimeCharacteristic.EventTime);
        
        // Kafka source — reads from bt-events topic
        KafkaSource<BtEvent> source = KafkaSource.<BtEvent>builder()
            .setBootstrapServers("kafka:9092")
            .setTopics("bt-events")
            .setGroupId("bt-analytics-flink")
            .setStartingOffsets(OffsetsInitializer.committedOffsets(OffsetResetStrategy.EARLIEST))
            .setValueOnlyDeserializer(new BtEventDeserializer())
            .build();
        
        DataStream<BtEvent> events = env.fromSource(
            source,
            // Watermark strategy: "events can be up to 10 seconds late"
            WatermarkStrategy.<BtEvent>forBoundedOutOfOrderness(Duration.ofSeconds(10))
                .withTimestampAssigner((event, ts) -> event.getTimestampMs()),
            "Kafka Source"
        );
        
        // KEY BY tv_id — all events for same TV go to same operator instance
        // This enables per-TV state (running averages, counts, etc.)
        DataStream<BtWindowStats> stats = events
            .keyBy(BtEvent::getTvId)
            .window(TumblingEventTimeWindows.of(Time.minutes(5)))
            .aggregate(new BtStatsAggregator());
        
        // Write aggregated stats to TimescaleDB
        stats.addSink(new TimescaleDBSink());
        
        // IMPORTANT: Nothing runs until execute() is called!
        // Everything above just builds the DAG
        env.execute("BT Analytics Job");
    }
}
```

**Key Insight: Flink's Lazy Execution**
```
env.fromSource(...)     // Doesn't read anything yet
   .keyBy(...)          // Doesn't partition yet  
   .window(...)         // Doesn't window yet
   .aggregate(...)      // Doesn't aggregate yet
   .addSink(...)        // Doesn't write yet

env.execute(...)        // NOW everything runs!

This is like building a recipe vs. cooking.
The DAG is the recipe; execute() starts cooking.
```

#### 3.5.2 The Aggregator (`BtStatsAggregator.java`)

```java
public class BtStatsAggregator 
    implements AggregateFunction<BtEvent, StatsAccumulator, BtWindowStats> {
    
    // Called once when window is created for a new key
    @Override
    public StatsAccumulator createAccumulator() {
        return new StatsAccumulator();
    }
    
    // Called for EVERY event in the window — must be fast!
    @Override
    public StatsAccumulator add(BtEvent event, StatsAccumulator acc) {
        acc.count++;
        acc.sumConnectionMs += event.getConnectionMs();
        acc.sumRssi += event.getRssi();
        
        // Track min/max for p0/p100
        acc.minConnectionMs = Math.min(acc.minConnectionMs, event.getConnectionMs());
        acc.maxConnectionMs = Math.max(acc.maxConnectionMs, event.getConnectionMs());
        
        // For p99, we'd need a sketch (T-Digest) — simplified here
        acc.connectionMsList.add(event.getConnectionMs());
        
        // Track unique devices via Set
        acc.uniqueDevices.add(event.getMacAddress());
        
        return acc;
    }
    
    // Called when window closes — emit the final result
    @Override
    public BtWindowStats getResult(StatsAccumulator acc) {
        Collections.sort(acc.connectionMsList);
        int p99Index = (int) (acc.connectionMsList.size() * 0.99);
        
        return new BtWindowStats(
            acc.tvId,
            acc.windowStart,
            acc.windowEnd,
            acc.count,
            acc.sumConnectionMs / acc.count,  // avg
            acc.connectionMsList.get(p99Index), // p99
            acc.sumRssi / acc.count,           // avg RSSI
            acc.uniqueDevices.size()           // unique device count
        );
    }
    
    // Called when merging windows (session windows) — not used for tumbling
    @Override
    public StatsAccumulator merge(StatsAccumulator a, StatsAccumulator b) {
        // ... combine two accumulators
    }
}
```

**Why AggregateFunction instead of reduce()?**
```
reduce(): Input type == Output type
  BtEvent + BtEvent → BtEvent  // Awkward, need to abuse fields

AggregateFunction: Input → Accumulator → Output (all different types)
  BtEvent → StatsAccumulator → BtWindowStats  // Clean separation

Also: AggregateFunction is incremental (O(1) per event)
      vs. WindowFunction which buffers all events (O(n) memory)
```

### 3.6 Exercises with Expected Output

#### Exercise 3.6.1: Verify Flink Job Submission

```powershell
# 1. Build the JAR
cd flink-job
mvn clean package -DskipTests

# 2. Check JAR was created
dir target\bt-analytics-*.jar
# Expected: bt-analytics-1.0.0.jar (~15 MB with dependencies)

# 3. Submit to local Flink (docker-compose must be running)
$jarId = (Invoke-RestMethod -Method Post -Uri "http://localhost:8081/jars/upload" `
  -Form @{ jarfile = Get-Item "target\bt-analytics-1.0.0.jar" }).filename.Split("/")[-1]
Write-Host "Uploaded JAR: $jarId"

# 4. Start the job
Invoke-RestMethod -Method Post -Uri "http://localhost:8081/jars/$jarId/run"
# Expected: {"jobid":"abc123..."}

# 5. Verify job is running
Invoke-RestMethod http://localhost:8081/jobs | ConvertTo-Json
# Expected: "state": "RUNNING"
```

#### Exercise 3.6.2: Trace an Event Through the Pipeline

```powershell
# 1. Produce a test event to Kafka
$testEvent = '{"tv_id":"TV-TRACE-001","device_name":"Test Device","device_type":"headphones","mac_address":"AA:BB:CC:DD:EE:FF","connection_ms":500,"rssi":-60,"timestamp_ms":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() + '}'

docker exec -i kafka kafka-console-producer `
  --bootstrap-server localhost:29092 `
  --topic bt-events `
  --property "parse.key=true" `
  --property "key.separator=:" <<< "TV-TRACE-001:$testEvent"

# 2. Wait 5+ minutes for window to close, then check TimescaleDB
docker exec timescaledb psql -U telemetry -d telemetry -c `
  "SELECT * FROM bt_window_stats WHERE tv_id = 'TV-TRACE-001';"

# Expected output:
#  tv_id       | window_start        | window_end          | event_count | avg_connection_ms | ...
# -------------+---------------------+---------------------+-------------+-------------------+
#  TV-TRACE-001| 2024-01-15 10:00:00 | 2024-01-15 10:05:00 |           1 |               500 |
```

#### Exercise 3.6.3: Test Late Event Handling

```powershell
# 1. Send an event with timestamp 15 minutes in the past
$oldTimestamp = [DateTimeOffset]::Now.AddMinutes(-15).ToUnixTimeMilliseconds()
$lateEvent = '{"tv_id":"TV-LATE-001","device_name":"Late Device","device_type":"speaker","mac_address":"11:22:33:44:55:66","connection_ms":999,"rssi":-70,"timestamp_ms":' + $oldTimestamp + '}'

docker exec -i kafka kafka-console-producer `
  --bootstrap-server localhost:29092 --topic bt-events <<< $lateEvent

# 2. This event is TOO LATE (watermark allows only 10 seconds)
# Check Flink metrics for dropped events:
Invoke-RestMethod "http://localhost:8081/jobs/{jobId}/metrics?get=numLateRecordsDropped"
# Expected: numLateRecordsDropped increased by 1

# 3. Check side output (if configured) or raw events table
docker exec timescaledb psql -U telemetry -d telemetry -c `
  "SELECT * FROM bt_events_late WHERE tv_id = 'TV-LATE-001';"
```

### 3.7 Common Pitfalls and How to Avoid Them

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  PITFALL #1: Checkpointing Disabled = Data Loss on Restart                  │
├─────────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                     │
│    // No checkpointing configured                                           │
│    env.execute("My Job");                                                   │
│    // Flink crashes → restarts → re-reads Kafka from beginning              │
│    // All window state is LOST, aggregations restart from zero              │
│                                                                             │
│  RIGHT:                                                                     │
│    env.enableCheckpointing(60_000);                                         │
│    env.getCheckpointConfig().setCheckpointStorage("s3://bucket/checkpoints");│
│    // Flink crashes → restarts → resumes from last checkpoint               │
│    // Window state restored, Kafka offsets restored, exactly-once!          │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│  PITFALL #2: Processing Time Windows with Buffered Events                   │
├─────────────────────────────────────────────────────────────────────────────┤
│  SCENARIO:                                                                  │
│    TV offline 10:00-10:30, sends 30 min of buffered events at 10:31         │
│                                                                             │
│  WITH PROCESSING TIME:                                                      │
│    All 30 min of events land in the [10:30-10:35] window                    │
│    Result: One window has 30x normal events, others have zero               │
│    Dashboard shows: "10:30 had 30,000 events, 10:00-10:25 had zero"         │
│    This is WRONG — events actually happened throughout 10:00-10:30          │
│                                                                             │
│  WITH EVENT TIME:                                                           │
│    Events are assigned to windows based on their timestamp_ms               │
│    Result: Events correctly distributed across [10:00], [10:05], ... [10:25]│
│    Dashboard shows accurate historical data                                 │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│  PITFALL #3: Watermark Too Tight = Dropped Events                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                     │
│    WatermarkStrategy.forBoundedOutOfOrderness(Duration.ofMillis(100))       │
│    // Only 100ms tolerance — any network jitter drops events!               │
│                                                                             │
│  WRONG (opposite extreme):                                                  │
│    WatermarkStrategy.forBoundedOutOfOrderness(Duration.ofHours(1))          │
│    // Windows don't close until 1 hour after last event                     │
│    // Dashboard is always 1 hour behind — useless for real-time             │
│                                                                             │
│  RIGHT:                                                                     │
│    WatermarkStrategy.forBoundedOutOfOrderness(Duration.ofSeconds(10))       │
│    // 10 seconds handles network jitter                                     │
│    // Windows close 10 seconds after window end — acceptable delay          │
│    // Events >10s late go to side output for separate handling              │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│  PITFALL #4: State Too Large = Checkpoint Timeout                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  WRONG:                                                                     │
│    // Storing all events in accumulator for "exact" p99                     │
│    acc.allEvents.add(event);  // 20M TVs × 100 events × 300 bytes = 600 GB! │
│                                                                             │
│  RIGHT:                                                                     │
│    // Use approximate algorithms                                            │
│    acc.tdigest.add(event.getConnectionMs());  // T-Digest: ~10 KB per key   │
│    // 20M TVs × 10 KB = 200 GB — still large but manageable                 │
│                                                                             │
│  BETTER:                                                                    │
│    // Pre-aggregate at the agent, send percentiles not raw values           │
│    // Or: Sample 1% of events for percentile calculation                    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.8 Debugging Strategies for Flink

#### Strategy 1: Flink Web UI Deep Dive

```
Open http://localhost:8081 and explore:

1. Job Graph Tab:
   - Visual DAG of your job
   - Click each operator to see parallelism, records in/out
   - RED operator = backpressure (bottleneck)

2. Checkpoints Tab:
   - "Completed" = healthy
   - "In Progress" for >30s = state too large
   - "Failed" = investigate logs immediately

3. Metrics Tab:
   - numRecordsIn/Out per operator
   - currentInputWatermark — if stuck, events aren't flowing
   - numLateRecordsDropped — watermark too tight?

4. Task Managers Tab:
   - Memory usage per TM
   - If near limit, increase or add more TMs
```

#### Strategy 2: Add Logging to Operators

```java
// In your AggregateFunction:
@Override
public StatsAccumulator add(BtEvent event, StatsAccumulator acc) {
    if (event.getConnectionMs() > 2000) {
        LOG.warn("Slow connection: tv_id={}, connection_ms={}", 
            event.getTvId(), event.getConnectionMs());
    }
    // ... rest of aggregation
}

// View logs:
docker logs -f flink-taskmanager | grep "Slow connection"
```

#### Strategy 3: Test with Controlled Input

```java
// In a unit test, use a TestHarness:
@Test
public void testWindowAggregation() throws Exception {
    OneInputStreamOperatorTestHarness<BtEvent, BtWindowStats> harness = 
        new OneInputStreamOperatorTestHarness<>(new WindowOperator(...));
    
    harness.open();
    
    // Send events with controlled timestamps
    harness.processElement(new BtEvent("TV-1", 100, 1000L), 1000L);
    harness.processElement(new BtEvent("TV-1", 200, 2000L), 2000L);
    
    // Advance watermark to trigger window
    harness.processWatermark(300_000L);  // 5 minutes
    
    // Verify output
    List<BtWindowStats> output = harness.extractOutputValues();
    assertEquals(1, output.size());
    assertEquals(150, output.get(0).getAvgConnectionMs());  // (100+200)/2
}
```

### 3.9 Thought-Provoking Questions

```
Q1: Why do we key by tv_id and not by device_type?
    Think about: What questions does the business want to answer?
    - "How is TV-00001 performing?" → key by tv_id ✓
    - "How are all headphones performing?" → key by device_type
    Answer: We can always re-aggregate by device_type in SQL,
            but we can't disaggregate if we only stored device_type stats.

Q2: What happens if one TV sends 1000x more events than others?
    Think about: Flink parallelism and key distribution
    Answer: That TV's events all go to ONE TaskManager (same key = same partition).
            This creates a "hot key" — one TM is overloaded while others idle.
    Solution: Add a random suffix to the key for high-volume TVs,
              then re-aggregate in a second window.

Q3: Why 5-minute windows and not 1-minute or 1-hour?
    Think about: Trade-offs
    - 1-minute: 5x more DB writes, 5x more rows, but fresher data
    - 1-hour: 12x fewer writes, but dashboard is stale for up to 1 hour
    - 5-minute: Balance — fresh enough for ops, not too many writes
    Answer: It depends on SLA. If you need <1 min alerting, use 1-min windows.

Q4: What if Kafka has 200 partitions but Flink has only 4 TaskManagers?
    Think about: Parallelism mismatch
    Answer: Each TM handles 50 partitions. This is fine!
            Flink parallelism ≠ Kafka partitions.
            But: If you have 200 TMs and 50 partitions, 150 TMs are idle.
```

---

## PHASE 4 — Local Development Stack (Week 5)

### 4.1 Data Flow Visualization

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    END-TO-END DATA FLOW (Local Stack)                        │
└─────────────────────────────────────────────────────────────────────────────┘

  TV Agent (Docker)                 Kafka                      Flink
  ┌─────────────┐              ┌─────────────┐            ┌─────────────┐
  │ BtMonitor   │              │ bt-events   │            │ Source      │
  │ generates   │──── JSON ───▶│ topic       │───────────▶│ (Kafka      │
  │ BtEvent     │   (300 bytes)│ partition 0 │            │  Consumer)  │
  └─────────────┘              │ partition 1 │            └──────┬──────┘
        │                      │ ...         │                   │
        │ if Kafka down        └─────────────┘                   │
        ▼                                                        ▼
  ┌─────────────┐                                         ┌─────────────┐
  │ SQLite      │                                         │ keyBy(tv_id)│
  │ Buffer      │                                         │ window(5min)│
  │ (offline)   │                                         │ aggregate() │
  └─────────────┘                                         └──────┬──────┘
                                                                 │
                                                                 ▼
                                                          ┌─────────────┐
  Grafana                      TimescaleDB                │ BtWindowStats│
  ┌─────────────┐              ┌─────────────┐            │ {tv_id,     │
  │ Dashboard   │◀── SELECT ───│bt_window_   │◀── INSERT ─│  avg_conn,  │
  │ (port 3000) │              │stats table  │            │  p99, ...}  │
  └─────────────┘              └─────────────┘            └─────────────┘

  TIMING:
  ─────────────────────────────────────────────────────────────────────────▶
  0s          1s              5m              5m+10s           5m+15s
  Event       Event in        Window          Window           Row visible
  generated   Kafka           closes          written to DB    in Grafana
```

### 4.2 Docker Compose — Start the Stack

Make sure Docker Desktop is running (check the system tray icon).

**Start the full stack from VS Code terminal:**

```powershell
cd infra
docker compose up -d
```

This starts:
- `kafka` — event bus (KRaft mode, no Zookeeper)
- `kafka-init` — creates topics (bt-events: 200 partitions, bt-alerts: 10 partitions)
- `flink-jm` — Flink Job Manager (port 8081)
- `flink-tm` — Flink Task Managers
- `timescaledb` — time-series database (port 5432)
- `grafana` — dashboards (port 3000, admin/admin)
- `tv-agent` — simulated device

**Important Network Details:**
- Docker network name: `infra_telemetry`
- Internal Kafka address: `kafka:29092` (for containers)
- External Kafka address: `localhost:9092` (for host machine)

**Scale Flink workers:**
```powershell
docker compose up -d --scale flink-tm=4
```

**Verify Kafka topics were created:**
```powershell
docker exec kafka kafka-topics --bootstrap-server localhost:29092 --list
# Expected: bt-events, bt-alerts
```

**If topics don't exist, create them manually:**
```powershell
docker exec kafka kafka-topics --bootstrap-server localhost:29092 --create --topic bt-events --partitions 200 --replication-factor 1
docker exec kafka kafka-topics --bootstrap-server localhost:29092 --create --topic bt-alerts --partitions 10 --replication-factor 1
```

### 4.2 Verify the Data Pipeline End-to-End

Open a **new VS Code terminal** (`Ctrl+Shift+``) and run:

```powershell
# 1. Check Kafka topics exist
docker exec kafka kafka-topics --bootstrap-server localhost:29092 --list
# Expected: bt-events, bt-alerts

# 2. Watch raw events flowing into Kafka (Ctrl+C to stop)
docker exec kafka kafka-console-consumer `
  --bootstrap-server localhost:29092 `
  --topic bt-events --max-messages 5

# 3. Check TimescaleDB has window stats
docker exec timescaledb psql -U telemetry -d telemetry -c `
  "SELECT * FROM bt_window_stats ORDER BY window_start DESC LIMIT 5;"

# 4. Check Flink is processing
Invoke-RestMethod http://localhost:8081/jobs | ConvertTo-Json

# 5. Open Grafana dashboard
Start-Process "http://localhost:3000"
# Login: admin / admin
```

### 4.3 Using the VS Code Docker Extension

Instead of terminal commands, you can manage containers visually:

1. Click the Docker icon in the Activity Bar (left sidebar)
2. See running containers under "Containers"
3. Right-click a container → "View Logs", "Attach Shell", "Stop"
4. Right-click `docker-compose.yml` → "Compose Up" / "Compose Down"

### 4.4 Using the REST Client Extension for Flink API

Create a file `infra/flink-api.http` in VS Code:

```http
### List all Flink jobs
GET http://localhost:8081/jobs

### Get job details (replace JOB_ID)
GET http://localhost:8081/jobs/{{jobId}}

### Upload Flink JAR
POST http://localhost:8081/jars/upload
Content-Type: multipart/form-data; boundary=boundary

--boundary
Content-Disposition: form-data; name="jarfile"; filename="bt-analytics-1.0.0.jar"
Content-Type: application/java-archive

< ../flink-job/target/bt-analytics-1.0.0.jar
--boundary--
```

Click "Send Request" above each `###` block to execute.

### 4.5 Submit the Flink Job

**Step 1: Build the Flink JAR**
```powershell
cd flink-job
mvn clean package -DskipTests
```

**Step 2: Fix Flink checkpoint directory permissions**
```powershell
# This is required before running the job!
docker exec flink-jm bash -c "mkdir -p /tmp/checkpoints && chmod 777 /tmp/checkpoints"
```

**Step 3: Upload and run the JAR**

*Note: PowerShell's `curl` is an alias for `Invoke-WebRequest`. Use `curl.exe` for real curl:*

```powershell
# Upload the JAR
curl.exe -X POST http://localhost:8081/jars/upload -H "Expect:" -F "jarfile=@target/bt-analytics-1.0.0.jar"
# Returns: {"filename":"...<jar-id>_bt-analytics-1.0.0.jar","status":"success"}

# Copy the JAR ID from the response, then run:
curl.exe -X POST "http://localhost:8081/jars/<jar-id>_bt-analytics-1.0.0.jar/run"
# Returns: {"jobid":"..."}
```

**Alternative using PowerShell (older versions):**
```powershell
# If Invoke-RestMethod -Form doesn't work (older PowerShell), use curl.exe as shown above
```

**Verify job is running:**
```powershell
curl.exe http://localhost:8081/jobs
# Or open http://localhost:8081 in browser
```

### 4.6 Scale Locally — Simulate More TVs

```powershell
# Run 10 simulated TV agents via Docker
1..10 | ForEach-Object {
    docker run -d --name "tv-sim-$_" --network infra_telemetry `
      -e "TV_ID=TV-SIM-$('{0:D3}' -f $_)" `
      -e "BROKERS=kafka:29092" `
      -e "DB_PATH=/data/buffer.db" `
      tv-telemetry-agent
}

# Watch Kafka throughput
docker exec kafka kafka-consumer-groups `
  --bootstrap-server localhost:29092 `
  --group bt-analytics-flink --describe
```

### 4.7 Debugging Checklist — When Things Go Wrong

```
┌───────────────────────────────────────────────────────────────────┐
│  SYMPTOM: No data in Grafana dashboard                            │
├───────────────────────────────────────────────────────────────────┤
│  Step 1: Is the agent producing events?                           │
│    docker logs tv-agent --tail 20                                 │
│    ✓ See "event_produced" logs? → Agent OK, check Kafka           │
│    ✗ No logs? → Agent crashed, check docker ps                    │
│                                                                   │
│  Step 2: Are events reaching Kafka?                               │
│    docker exec kafka kafka-console-consumer \                     │
│      --bootstrap-server localhost:29092 --topic bt-events \       │
│      --max-messages 3                                             │
│    ✓ See JSON events? → Kafka OK, check Flink                     │
│    ✗ Timeout? → Kafka not receiving, check agent broker config    │
│                                                                   │
│  Step 3: Is Flink consuming?                                      │
│    curl.exe http://localhost:8081/jobs                            │
│    ✓ Job "RUNNING"? → Check Flink metrics                        │
│    ✗ Job "FAILED"? → Check Flink logs: docker logs flink-jm       │
│    ✗ No jobs? → Submit the JAR (see Phase 4.5)                    │
│                                                                   │
│  Step 4: Is Flink writing to TimescaleDB?                         │
│    docker exec timescaledb psql -U telemetry -d telemetry \       │
│      -c "SELECT COUNT(*) FROM bt_window_stats;"                   │
│    ✓ Count > 0? → Data exists, check Grafana query                │
│    ✗ Count = 0? → Wait 5+ min for window to close, or check sink  │
│                                                                   │
│  Step 5: Is Grafana querying correctly?                           │
│    Open Grafana → Explore → Run: SELECT * FROM bt_window_stats    │
│    ✓ Data shows? → Dashboard panel query is wrong                 │
│    ✗ No data? → Check datasource connection                       │
│                                                                   │
│  Step 6: Grafana shows empty but data exists?                     │
│    Check timestamps! Simulated data may have FUTURE timestamps.   │
│    → Change Grafana time range to match data timestamps           │
│    → Or refresh continuous aggregates (see below)                 │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  SYMPTOM: Flink job keeps restarting                              │
├───────────────────────────────────────────────────────────────────┤
│  Check 1: Flink TaskManager logs                                  │
│    docker logs flink-tm --tail 50                                 │
│    Look for: OutOfMemoryError, NullPointerException, etc.         │
│                                                                   │
│  Check 2: Kafka connectivity                                      │
│    Is Kafka broker reachable from Flink container?                │
│    docker exec flink-tm nc -zv kafka 9092                         │
│                                                                   │
│  Check 3: TimescaleDB connectivity                                │
│    docker exec flink-tm nc -zv timescaledb 5432                   │
│                                                                   │
│  Common fixes:                                                    │
│    - Increase TaskManager memory in docker-compose.yml            │
│    - Check network: all containers on same Docker network?        │
│    - Verify JAR has all dependencies (fat JAR)                    │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  SYMPTOM: Flink job fails with checkpoint directory error         │
├───────────────────────────────────────────────────────────────────┤
│  Error: "Failed to create directory for shared state"             │
│                                                                   │
│  Fix: Create checkpoint directory with proper permissions:        │
│    docker exec flink-jm bash -c \                                 │
│      "mkdir -p /tmp/checkpoints && chmod 777 /tmp/checkpoints"    │
│                                                                   │
│  Then re-submit the job.                                          │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  SYMPTOM: Grafana dashboard empty but bt_window_stats has data    │
├───────────────────────────────────────────────────────────────────┤
│  Cause: Dashboard queries bt_hourly_fleet (continuous aggregate)  │
│         which needs manual refresh for non-realtime data.         │
│                                                                   │
│  Fix 1: Refresh the continuous aggregate:                         │
│    docker exec timescaledb psql -U telemetry -d telemetry -c \    │
│      "CALL refresh_continuous_aggregate('bt_hourly_fleet',        │
│       '2026-08-01', '2026-08-03');"                               │
│                                                                   │
│  Fix 2: Adjust Grafana time range to match data timestamps:       │
│    - Check actual timestamps in data:                             │
│      docker exec timescaledb psql -U telemetry -d telemetry -c \  │
│        "SELECT DISTINCT DATE(window_start) FROM bt_window_stats;" │
│    - Set Grafana time picker to that date range                   │
│                                                                   │
│  Note: Simulated data may have future timestamps (e.g., 2026)     │
└───────────────────────────────────────────────────────────────────┘
```

### 4.8 Teardown

```powershell
# Stop everything
cd infra
docker compose down -v    # -v removes volumes (clean slate)

# Remove simulator containers
docker rm -f $(docker ps -a --filter "name=tv-sim" -q)
```

---

## PHASE 5 — Kubernetes Fundamentals (Week 6)

### 5.1 Why Kubernetes for This System?

```
WITHOUT KUBERNETES:
  - "I need 4 more Flink workers" → SSH to server, install Java, configure, start
  - "Kafka broker died" → manual detection, manual restart, check data
  - "Scale up at 7 PM prime time" → cron job? hope it works?
  - "Deploy new Flink job" → stop old, upload JAR, start new, pray

WITH KUBERNETES:
  - "I need 4 more Flink workers" → change replicas: 4 → 8, apply
  - "Kafka broker died" → K8s restarts it automatically in <30 seconds
  - "Scale up at 7 PM" → HorizontalPodAutoscaler watches CPU/throughput
  - "Deploy new Flink job" → rolling update, automatic rollback on failure
```

### 5.2 Kubernetes on Windows: Your Options

| Option | Setup | Resources Needed | Best For |
|--------|-------|------------------|----------|
| Docker Desktop K8s | Settings → Enable Kubernetes | 8 GB RAM | Quick single-node testing |
| Minikube | `winget install minikube` | 8–16 GB RAM | Multi-node simulation |
| Kind (K8s in Docker) | `winget install kind` | 6 GB RAM | CI-like environments |

**Recommended: Docker Desktop Kubernetes** (already installed in Phase 0)

```powershell
# Verify Kubernetes is running (Docker Desktop)
kubectl cluster-info
kubectl get nodes
# NAME             STATUS   ROLES           AGE   VERSION
# docker-desktop   Ready    control-plane   10m   v1.28.2
```

### 5.3 Using the VS Code Kubernetes Extension

1. Click the Kubernetes icon in the Activity Bar
2. See your cluster, namespaces, pods, services
3. Right-click any resource → "Describe", "Logs", "Delete"
4. Hover over YAML files → auto-complete K8s schemas

### 5.4 Namespace and Resource Quotas

Create `k8s/namespace.yaml` (VS Code → New File):

```yaml
apiVersion: v1
kind: Namespace
metadata:
  name: telemetry
  labels:
    app.kubernetes.io/part-of: tv-telemetry
---
apiVersion: v1
kind: ResourceQuota
metadata:
  name: telemetry-quota
  namespace: telemetry
spec:
  hard:
    requests.cpu: "32"
    requests.memory: 64Gi
    limits.cpu: "64"
    limits.memory: 128Gi
    persistentvolumeclaims: "20"
```

Apply it:
```powershell
kubectl apply -f k8s\namespace.yaml
kubectl get ns telemetry
```

### 5.5 Key Kubernetes Concepts

```
┌─────────────────────────────────────────────────────┐
│  CLUSTER                                            │
│  ┌────────────────┐  ┌────────────────┐             │
│  │  NODE (VM/EC2) │  │  NODE (VM/EC2) │  ...        │
│  │  ┌──────────┐  │  │  ┌──────────┐  │             │
│  │  │ POD      │  │  │  │ POD      │  │             │
│  │  │┌────────┐│  │  │  │┌────────┐│  │             │
│  │  ││Container││  │  │  ││Container││  │             │
│  │  │└────────┘│  │  │  │└────────┘│  │             │
│  │  └──────────┘  │  │  └──────────┘  │             │
│  └────────────────┘  └────────────────┘             │
└─────────────────────────────────────────────────────┘

KEY CONCEPTS:
  Pod         = smallest deployable unit (1+ containers)
  Deployment  = "run N copies of this Pod, restart if any die"
  StatefulSet = like Deployment but with stable network IDs + persistent storage
  Service     = stable DNS name pointing to a set of Pods
  ConfigMap   = configuration injected into Pods as env vars or files
  PVC         = persistent disk that survives Pod restart
  HPA         = auto-scale Pods based on CPU/memory/custom metrics
  Namespace   = logical isolation (dev/staging/prod in same cluster)
```

### 5.6 Hands-On Exercises

#### Exercise 5.6.1: Deploy Your First Pod

```powershell
# Create a simple nginx pod
kubectl run my-nginx --image=nginx --port=80 -n default

# Verify it's running
kubectl get pods -n default
# Expected output:
# NAME       READY   STATUS    RESTARTS   AGE
# my-nginx   1/1     Running   0          30s

# Access the pod
kubectl port-forward pod/my-nginx 8080:80
# Open http://localhost:8080 in browser - see nginx welcome page

# Clean up
kubectl delete pod my-nginx
```

#### Exercise 5.6.2: Create a Deployment with Scaling

```powershell
# Create deployment (save as web-deploy.yaml and run: kubectl apply -f web-deploy.yaml)
# Or run inline with kubectl create deployment web-app --image=nginx:alpine --replicas=2

kubectl create deployment web-app --image=nginx:alpine --replicas=2

# Watch pods come up
kubectl get pods -l app=web-app --watch
# Expected: 2 pods running

# Scale up
kubectl scale deployment web-app --replicas=5
kubectl get pods -l app=web-app
# Expected: 5 pods running

# Scale down
kubectl scale deployment web-app --replicas=1

# Clean up
kubectl delete deployment web-app
```

#### Exercise 5.6.3: ConfigMaps and Secrets

```powershell
# Create a ConfigMap
kubectl create configmap app-config --from-literal=KAFKA_BROKERS=kafka:9092 --from-literal=LOG_LEVEL=INFO

# View it
kubectl get configmap app-config -o yaml

# Create a Secret
kubectl create secret generic db-creds --from-literal=username=telemetry --from-literal=password=secret123

# View it (values are base64 encoded)
kubectl get secret db-creds -o yaml

# Clean up
kubectl delete configmap app-config
kubectl delete secret db-creds
```

#### Exercise 5.6.4: Services and DNS

```powershell
# Create a deployment and expose it
kubectl create deployment hello --image=nginxdemos/hello --replicas=3
kubectl expose deployment hello --port=80 --target-port=80

# Check the service
kubectl get svc hello
# Expected: ClusterIP service with internal IP

# Port-forward to access locally
kubectl port-forward svc/hello 8080:80
# Open http://localhost:8080 - see server info

# Clean up
kubectl delete deployment hello
kubectl delete svc hello
```

---

## PHASE 6 — Deploy to Kubernetes (Week 7)

### 6.1 Kafka on Kubernetes with Strimzi Operator

```powershell
# Install Strimzi operator
kubectl create namespace kafka
kubectl apply -f 'https://strimzi.io/install/latest?namespace=kafka' -n kafka

# Wait for operator to be ready
kubectl wait --for=condition=Ready pod -l name=strimzi-cluster-operator -n kafka --timeout=120s
```

Then apply the Kafka cluster manifest:
```powershell
kubectl apply -f k8s\kafka\kafka-cluster.yaml
# Wait ~3 minutes for Kafka cluster to come up
kubectl wait kafka/bt-telemetry --for=condition=Ready -n telemetry --timeout=300s
```

### 6.2 Deploy the Full Stack to K8s

```powershell
# 1. Namespace
kubectl apply -f k8s\namespace.yaml

# 2. Kafka (wait for ready)
kubectl apply -f k8s\kafka\kafka-cluster.yaml
kubectl wait kafka/bt-telemetry --for=condition=Ready -n telemetry --timeout=300s

# 3. TimescaleDB (wait for ready)
kubectl apply -f k8s\timescaledb\
kubectl wait pod/timescaledb-0 --for=condition=Ready -n telemetry --timeout=120s

# 4. Flink cluster
kubectl apply -f k8s\flink\

# 5. Submit Flink job
kubectl cp flink-job\target\bt-analytics-1.0.0.jar `
  telemetry/flink-jobmanager-xxx:/opt/flink/jobs/

kubectl exec -n telemetry deploy/flink-jobmanager -- `
  /opt/flink/bin/flink run /opt/flink/jobs/bt-analytics-1.0.0.jar

# 6. Monitoring
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm repo update
helm install monitoring prometheus-community/kube-prometheus-stack `
  --namespace monitoring --create-namespace `
  --set grafana.adminPassword=admin

# 7. Verify all pods
kubectl get pods -n telemetry
kubectl get pods -n monitoring
```

### 6.3 Port-Forward for Local Access

```powershell
# Access Grafana (open http://localhost:3000)
kubectl port-forward -n monitoring svc/monitoring-grafana 3000:80

# Access Flink UI (open http://localhost:8081)
kubectl port-forward -n telemetry svc/flink-jobmanager 8081:8081

# Access TimescaleDB (connect via VS Code PostgreSQL extension)
kubectl port-forward -n telemetry svc/timescaledb 5432:5432
```

Open each URL in your browser: `Start-Process "http://localhost:3000"`

### 6.4 Kubernetes Debugging Guide

```
┌─────────────────────────────────────────────────────────────────────────┐
│  DEBUGGING DECISION TREE: "Why isn't my pod working?"                   │
└─────────────────────────────────────────────────────────────────────────┘

  kubectl get pods -n telemetry
           │
           ▼
  ┌─────────────────┐
  │ Pod STATUS?     │
  └────────┬────────┘
           │
     ┌─────┴─────┬──────────────┬──────────────┬─────────────┐
     ▼           ▼              ▼              ▼             ▼
  Pending    ContainerCreating  CrashLoopBack  ImagePullBack  Running
     │           │              │              │             │
     ▼           ▼              ▼              ▼             ▼
  No node     Mounting         App crash      Wrong image   Check logs
  has room    volume/secret    on startup     name/tag      if unhealthy
```

#### Status: Pending
```powershell
# Why is the pod stuck in Pending?
kubectl describe pod <pod-name> -n telemetry | Select-String -Pattern "Events" -Context 0,10

# Common causes and fixes:
# "Insufficient cpu"     → Node is full. Scale up nodes or reduce requests.
# "Insufficient memory"  → Same as above.
# "No nodes match"       → Check nodeSelector/affinity. Wrong label?
# "PVC not bound"        → PersistentVolumeClaim waiting for storage.

# Check node resources:
kubectl describe nodes | Select-String -Pattern "Allocated resources" -Context 0,6
```

#### Status: CrashLoopBackOff
```powershell
# App is crashing repeatedly. Get the logs:
kubectl logs <pod-name> -n telemetry --previous
# --previous shows logs from the LAST crash (current container has no logs yet)

# Common causes:
# "Connection refused"   → Dependency not ready. Check if Kafka/DB is up.
# "OOMKilled"            → Out of memory. Increase limits.
# "Error: config not found" → ConfigMap/Secret not mounted correctly.

# Check why it was killed:
kubectl describe pod <pod-name> -n telemetry | Select-String -Pattern "Last State" -Context 0,5
```

#### Status: ImagePullBackOff
```powershell
# Can't pull the container image:
kubectl describe pod <pod-name> -n telemetry | Select-String -Pattern "Failed" -Context 0,3

# Common causes:
# "unauthorized"         → Need imagePullSecrets for private registry.
# "not found"            → Typo in image name or tag doesn't exist.
# "timeout"              → Network issue or registry is down.

# Fix for ECR:
kubectl create secret docker-registry ecr-secret `
  --docker-server=<account>.dkr.ecr.<region>.amazonaws.com `
  --docker-username=AWS `
  --docker-password=$(aws ecr get-login-password) `
  -n telemetry
```

#### Status: Running but Not Working
```powershell
# Pod is running but app isn't behaving correctly:

# 1. Check logs (live stream):
kubectl logs -f <pod-name> -n telemetry

# 2. Exec into the pod to debug:
kubectl exec -it <pod-name> -n telemetry -- /bin/sh
# Now you're inside the container. Check:
#   - env | grep KAFKA  (are env vars set?)
#   - nc -zv kafka 9092 (can it reach Kafka?)
#   - cat /app/config   (is config file correct?)

# 3. Check if service is routing correctly:
kubectl get endpoints <service-name> -n telemetry
# If ENDPOINTS is empty, service selector doesn't match pod labels.

# 4. Test service DNS:
kubectl run debug --rm -it --image=busybox -n telemetry -- nslookup kafka
```

#### Quick Reference Commands
```powershell
# All pods with restart counts:
kubectl get pods -n telemetry -o wide

# Events (most recent issues):
kubectl get events -n telemetry --sort-by='.lastTimestamp' | Select-Object -Last 20

# Resource usage:
kubectl top pods -n telemetry
kubectl top nodes

# Describe everything about a pod:
kubectl describe pod <pod-name> -n telemetry

# Get YAML of running pod (see actual config):
kubectl get pod <pod-name> -n telemetry -o yaml

# Delete and let it recreate (quick restart):
kubectl delete pod <pod-name> -n telemetry
```

### 6.5 VS Code Kubernetes Workflow

The Kubernetes extension lets you:
- **Apply manifests**: Right-click a YAML file → "Apply"
- **View logs**: Kubernetes panel → Pods → Right-click → "Logs"
- **Exec into pod**: Right-click pod → "Terminal"
- **Describe resources**: Right-click → "Describe" (shows events, conditions)
- **Delete resources**: Right-click → "Delete"

---

## PHASE 7 — AWS EKS Deployment (Week 8)

### 7.1 AWS Architecture with EKS

```
┌─────────────────────────────────────────────────────────────────────┐
│  AWS REGION: us-east-1                                               │
│                                                                       │
│  ┌─── VPC (10.0.0.0/16) ────────────────────────────────────────┐   │
│  │                                                                │   │
│  │  ┌── Private Subnets (3 AZs) ─────────────────────────────┐  │   │
│  │  │                                                          │  │   │
│  │  │  ┌─────────── EKS Cluster ────────────────────────────┐ │  │   │
│  │  │  │                                                      │ │  │   │
│  │  │  │  Node Group: kafka (i3.xlarge × 3)                  │ │  │   │
│  │  │  │  Node Group: compute (c6i.2xlarge × 4–32)           │ │  │   │
│  │  │  │  Node Group: data (r6i.xlarge × 2)                  │ │  │   │
│  │  │  │  Node Group: general (m6i.large × 2)                │ │  │   │
│  │  │  │                                                      │ │  │   │
│  │  │  └──────────────────────────────────────────────────────┘ │  │   │
│  │  └──────────────────────────────────────────────────────────┘  │   │
│  │                                                                │   │
│  │  ┌── Public Subnets ──┐                                       │   │
│  │  │  ALB (Grafana UI)  │                                       │   │
│  │  │  NLB (Kafka ingress for TVs)                               │   │
│  │  └────────────────────┘                                       │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                                                                       │
│  S3: tv-telemetry-raw-events (Glacier lifecycle)                     │
│  S3: tv-telemetry-checkpoints (Flink state)                          │
│  ECR: container images                                                │
└─────────────────────────────────────────────────────────────────────┘
```

### 7.2 Configure AWS CLI

```powershell
# Configure credentials (you need an AWS account)
aws configure
# AWS Access Key ID: <your-key>
# AWS Secret Access Key: <your-secret>
# Default region: us-east-1
# Default output format: json

# Verify
aws sts get-caller-identity
```

### 7.3 Terraform — Create EKS Cluster

**Edit Terraform files in VS Code** (install HashiCorp Terraform extension for syntax highlighting):

```powershell
cd terraform\eks
terraform init
terraform plan      # Review what will be created
terraform apply     # ~15 minutes to create EKS cluster
```

After creation:
```powershell
# Configure kubectl to use the new EKS cluster
aws eks update-kubeconfig --region us-east-1 --name tv-telemetry

# Verify
kubectl get nodes
```

### 7.4 Deploy to EKS

Same K8s manifests as Phase 6, now targeting the real AWS cluster:

```powershell
# Apply all manifests (same commands as Phase 6)
kubectl apply -f k8s\ --recursive

# Verify deployment
kubectl get pods -n telemetry
kubectl get svc -n telemetry
```

### 7.5 AWS Cost Estimation for 20M TVs

```
┌───────────────────────────────────────────────────────────────────┐
│  MONTHLY COST BREAKDOWN (us-east-1, On-Demand Pricing)              │
└───────────────────────────────────────────────────────────────────┘

COMPONENT                  SPEC                           COST/MONTH
───────────────────────────────────────────────────────────────────
EKS Control Plane          1 cluster                      $73

Kafka Nodes (EC2)          3× i3.xlarge (NVMe for logs)   $1,123
                           (4 vCPU, 30.5 GB, 950 GB NVMe)

Flink Nodes (EC2)          8× c6i.2xlarge (compute)       $1,958
                           (8 vCPU, 16 GB each)
                           Auto-scales 4-16 based on load

TimescaleDB (RDS)          db.r6g.xlarge (4 vCPU, 32 GB)  $438
                           + 2 TB gp3 storage             $320
                           Multi-AZ for HA                $438
                                                          ------
                                                          $1,196

S3 Raw Archive             8.6 TB/day × 30 days = 258 TB  $5,934
                           (Standard tier, first month)
                           After Glacier transition:      $1,032

Data Transfer              ~100 MB/s inbound (free)       $0
                           Internal (same AZ): free       $0
                           Cross-AZ: ~$500                $500

Load Balancer (NLB)        For Kafka ingress              $200

CloudWatch                 Logs + Metrics                 $300

───────────────────────────────────────────────────────────────────
TOTAL (On-Demand)                                         ~$11,300/mo
TOTAL (1-yr Reserved, ~40% off)                           ~$6,800/mo
TOTAL (3-yr Reserved + Glacier)                           ~$4,500/mo
```

#### Cost Optimization Strategies

```
┌───────────────────────────────────────────────────────────────────┐
│  STRATEGY 1: Use Spot Instances for Flink Workers                   │
├───────────────────────────────────────────────────────────────────┤
│  Flink is stateless (state in checkpoints). Spot interruption      │
│  just triggers a restart from last checkpoint.                     │
│                                                                    │
│  On-Demand: 8× c6i.2xlarge = $1,958/mo                             │
│  Spot:      8× c6i.2xlarge = ~$590/mo (70% savings!)               │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  STRATEGY 2: S3 Lifecycle Policies                                  │
├───────────────────────────────────────────────────────────────────┤
│  Day 0-7:    S3 Standard ($0.023/GB)   - Recent data, fast access  │
│  Day 8-30:   S3 Standard-IA ($0.0125/GB) - Infrequent access       │
│  Day 31-90:  S3 Glacier IR ($0.004/GB) - Rare access, mins retrieval│
│  Day 91+:    S3 Glacier Deep ($0.00099/GB) - Archive, hours retrieval│
│                                                                    │
│  258 TB in Standard:     $5,934/mo                                 │
│  258 TB in Glacier Deep: $256/mo (95% savings!)                    │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  STRATEGY 3: Use MSK Serverless Instead of Self-Managed Kafka       │
├───────────────────────────────────────────────────────────────────┤
│  Self-managed Kafka: $1,123/mo (3× i3.xlarge) + ops overhead       │
│  MSK Provisioned:    ~$1,500/mo but zero ops                       │
│  MSK Serverless:     Pay per GB (~$800/mo at our scale)            │
│                                                                    │
│  Trade-off: MSK Serverless has 200 MB/s limit per cluster.         │
│  At 100 MB/s average, we're fine. At peak 300 MB/s, need provisioned.│
└───────────────────────────────────────────────────────────────────┘
```

#### Cost Calculator Exercise

```
Exercise: Calculate cost for YOUR scale

Inputs:
  - Number of TVs: __________ (e.g., 1,000,000)
  - Events per TV per minute: __________ (e.g., 1)
  - Event size in bytes: __________ (e.g., 300)
  - Retention days: __________ (e.g., 30)

Calculations:
  Events/sec = TVs ÷ 60 = __________
  MB/sec = Events/sec × bytes ÷ 1,000,000 = __________
  GB/day = MB/sec × 86,400 ÷ 1,000 = __________
  GB/month = GB/day × 30 = __________

Use AWS Pricing Calculator: https://calculator.aws
```

### 7.6 Clean Up AWS Resources (Important — Avoid Costs!)

```powershell
# When done experimenting, destroy everything
cd terraform\eks
terraform destroy    # ~10 minutes
```

---

## PHASE 8 — Load Testing: Simulate 20M Devices (Week 9)

### 8.1 The Load Test Strategy

```
LOAD TEST ARCHITECTURE:
                                    ┌──────────────┐
  K8s Job: 100 pods                 │              │
  Each pod simulates 200,000 TVs    │  Kafka       │
  = 20,000,000 virtual devices      │  Cluster     │
                                    │              │
  ┌─────────┐  ┌─────────┐         └──────────────┘
  │ Pod 1   │  │ Pod 2   │  ...          ▲
  │ 200K TVs│  │ 200K TVs│              │
  └────┬────┘  └────┬────┘              │
       │            │                    │
       └────────────┴────────────────────┘
              Kafka produce

TRAFFIC PROFILE (realistic prime-time):
  00:00 – 06:00:  10% of 20M active  =  2M TVs  =   33K events/sec
  06:00 – 18:00:  30% of 20M active  =  6M TVs  =  100K events/sec
  18:00 – 22:00:  80% of 20M active  = 16M TVs  =  266K events/sec  ← PEAK
  22:00 – 00:00:  50% of 20M active  = 10M TVs  =  166K events/sec
```

### 8.2 Build and Run Load Test Locally (Single Pod)

First, test the load simulator on your machine:

```powershell
# Install Python dependency
pip install confluent-kafka

# Run locally (small scale test — make sure docker-compose stack is up)
cd load-test
$env:KAFKA_BROKERS = "localhost:9092"
$env:TVS_PER_POD = "1000"
$env:TARGET_EPS = "100"
$env:DURATION_SECS = "60"
python simulator.py
```

### 8.3 Build Docker Image for Load Tester

```powershell
cd load-test
docker build -t tv-load-test:latest .

# Test it against your local docker-compose Kafka
docker run --rm --network infra_telemetry `
  -e KAFKA_BROKERS=kafka:29092 `
  -e TVS_PER_POD=1000 `
  -e TARGET_EPS=100 `
  -e DURATION_SECS=30 `
  tv-load-test:latest
```

### 8.4 Graduated Load Test Plan

```
Step 1:    1,000 TVs  (1 pod,  TVS_PER_POD=1000)     → verify pipeline works
Step 2:   10,000 TVs  (1 pod,  TVS_PER_POD=10000)    → verify no errors
Step 3:  100,000 TVs  (1 pod,  TVS_PER_POD=100000)   → verify single-pod limits
Step 4: 1,000,000 TVs (5 pods, TVS_PER_POD=200000)   → verify multi-pod scale
Step 5: 5,000,000 TVs (25 pods)                       → verify Flink autoscaling
Step 6: 20,000,000 TVs (100 pods)                     → FULL SCALE (EKS only)
```

At each step, verify:
- [ ] No events dropped (compare Kafka produce count vs Flink consume count)
- [ ] Flink windows close on time (5-min windows emit within 5m10s)
- [ ] TimescaleDB has correct row counts
- [ ] No OOMKilled pods
- [ ] Alerts fire for connection_ms > 2000

### 8.5 Run Full Scale on EKS

```powershell
# Push image to ECR
aws ecr get-login-password --region us-east-1 | docker login --username AWS --password-stdin $env:ECR_REGISTRY
docker tag tv-load-test:latest "$env:ECR_REGISTRY/tv-load-test:latest"
docker push "$env:ECR_REGISTRY/tv-load-test:latest"

# Deploy the load test job
kubectl apply -f load-test\k8s-job.yaml

# Watch progress
kubectl get pods -n telemetry -l app=load-test --watch
```

### 8.6 What to Watch During Load Test

| Metric | Healthy | Problem |
|--------|---------|---------|
| Kafka bytes-in/sec | ~100 MB/s | > partition throughput limit |
| Kafka consumer lag | < 10,000 records | Growing = Flink can't keep up |
| Flink checkpoint duration | < 10 seconds | > 30s = state too large |
| Flink backpressure | 0% | > 50% = need more TaskManagers |
| TimescaleDB write latency | < 100ms | > 500ms = need connection pool tuning |
| Pod memory usage | < 80% limit | OOMKilled = increase limits |
| Pod CPU usage | < 70% | > 90% = scale up |

### 8.7 Expected Outputs at Each Scale

```
┌───────────────────────────────────────────────────────────────────┐
│  STEP 1: 1,000 TVs (Sanity Check)                                   │
├───────────────────────────────────────────────────────────────────┤
│  Events/sec:     ~17 (1000 TVs ÷ 60 sec)                            │
│  Kafka lag:      0 (Flink easily keeps up)                         │
│  CPU usage:      <5% on all containers                             │
│  Memory:         <500 MB total                                     │
│  DB rows/5min:   1,000 (one per TV per window)                     │
│                                                                    │
│  If this fails: Something is fundamentally broken. Check logs.     │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  STEP 4: 1,000,000 TVs (First Real Scale)                           │
├───────────────────────────────────────────────────────────────────┤
│  Events/sec:     ~16,667                                            │
│  Kafka lag:      <1,000 (should stay flat)                         │
│  Flink TMs:      2-4 needed                                        │
│  CPU usage:      30-50% on Flink TMs                               │
│  Memory:         2-4 GB for Flink state                            │
│  DB rows/5min:   1,000,000                                         │
│  DB write rate:  ~3,300 rows/sec                                   │
│                                                                    │
│  Expected issues at this scale:                                    │
│    - Single Flink TM may show backpressure → add more TMs          │
│    - TimescaleDB may slow down → enable batch inserts              │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│  STEP 6: 20,000,000 TVs (FULL SCALE - EKS Only)                     │
├───────────────────────────────────────────────────────────────────┤
│  Events/sec:     ~333,333                                           │
│  Kafka:          200 partitions, 3 brokers (i3.xlarge)             │
│  Flink TMs:      16-32 pods (c6i.2xlarge nodes)                    │
│  Checkpoint:     <30 seconds (with S3 state backend)               │
│  Memory:         50-100 GB total Flink state                       │
│  DB rows/5min:   20,000,000                                        │
│  DB write rate:  ~66,667 rows/sec (needs batching!)                │
│                                                                    │
│  Expected issues at this scale:                                    │
│    - Checkpoint duration grows → use incremental checkpoints       │
│    - Hot keys (popular TVs) → add key salting                      │
│    - DB connection exhaustion → use connection pooling (PgBouncer) │
│    - Network saturation → enable Kafka compression                 │
└───────────────────────────────────────────────────────────────────┘
```

---

## PHASE 9 — Observability & Production Readiness (Week 10)

### 9.1 The Three Pillars of Observability

```
┌──────────────────────────────────────────────────────────┐
│                 OBSERVABILITY                              │
├──────────────────┬──────────────────┬────────────────────┤
│   METRICS        │   LOGS           │   TRACES           │
│   (Prometheus)   │   (Loki/ELK)     │   (Jaeger)         │
│                  │                  │                    │
│ "How fast is     │ "What happened   │ "Where did this    │
│  the system      │  at 10:03?"      │  event go through  │
│  right now?"     │                  │  the pipeline?"    │
│                  │                  │                    │
│ Counters, gauges │ Structured JSON  │ Span per service   │
│ histograms       │ with trace IDs   │ hop                │
└──────────────────┴──────────────────┴────────────────────┘
```

### 9.2 Monitoring from VS Code

**View Grafana dashboards:**
```powershell
# Port-forward Grafana
kubectl port-forward -n monitoring svc/monitoring-grafana 3000:80
Start-Process "http://localhost:3000"
```

**Query TimescaleDB directly from VS Code:**
1. Install the PostgreSQL extension
2. Add connection: host=localhost, port=5432, user=telemetry, db=telemetry
3. Run queries like:
```sql
SELECT tv_id, avg_connection_ms, event_count
FROM bt_window_stats
WHERE window_start > NOW() - INTERVAL '1 hour'
ORDER BY avg_connection_ms DESC
LIMIT 20;
```

### 9.3 Key Alerts Configuration

```yaml
# Critical alerts — page the on-call engineer:
- alert: KafkaConsumerLagCritical
  expr: kafka_consumergroup_lag_sum{group="bt-analytics-flink"} > 1000000
  for: 5m
  labels: { severity: critical }
  annotations:
    summary: "Flink is 1M+ records behind Kafka"

- alert: FlinkCheckpointFailing
  expr: flink_jobmanager_job_numberOfFailedCheckpoints > 0
  for: 2m
  labels: { severity: critical }
  annotations:
    summary: "Flink checkpoints failing — risk of data loss on restart"

- alert: FlinkBackpressureHigh
  expr: flink_taskmanager_job_task_backPressuredTimeMsPerSecond > 500
  for: 3m
  labels: { severity: warning }
  annotations:
    summary: "Flink backpressure > 50% — scale up TaskManagers"
```

### 9.4 Production Readiness Checklist

```
RELIABILITY:
  ✓ Flink exactly-once checkpointing enabled (60s interval)
  ✓ Kafka replication factor = 3, min.insync.replicas = 2
  ✓ TimescaleDB automated backups (daily + WAL shipping)
  ✓ Pod disruption budgets (max 1 Kafka broker down at a time)
  ✓ Resource limits on all pods (prevent noisy neighbor)

SCALABILITY:
  ✓ Flink TaskManagers auto-scale via HPA (4 → 32 pods)
  ✓ Kafka partitions = 200 (can handle 200 MB/s write)
  ✓ EKS Cluster Autoscaler for node groups
  ✓ TimescaleDB chunking (1 week per chunk, auto-compression)

SECURITY:
  ✓ Kafka TLS encryption in transit
  ✓ K8s secrets for all credentials (not hardcoded)
  ✓ Network policies: pods can only reach what they need
  ✓ IRSA (IAM Roles for Service Accounts) for S3 access

OPERABILITY:
  ✓ All config in version control (GitOps)
  ✓ Rolling deploys with automatic rollback
  ✓ Runbooks for common incidents
  ✓ On-call rotation with PagerDuty integration
```

---

## PHASE 10 — System Design Decisions (Reference)

### DD-1: Why Kafka (Not Direct DB Write)

| Aspect | Direct DB Write | Kafka in the Middle |
|--------|----------------|---------------------|
| Backpressure | DB slow → all TVs block | Kafka absorbs burst, Flink drains at DB speed |
| Fan-out | Add consumer = change TV code | Add consumer = new reader on same topic |
| Replay | Data gone after write | Replay 24h from Kafka retention |
| Connection overhead | 20M DB connections = impossible | 20M → Kafka (designed for it) → 1 Flink job → DB |

### DD-2: Why Flink (Not Lambda or Spark)

| Requirement | Lambda | Spark Streaming | Flink |
|------------|--------|-----------------|-------|
| Stateful per-TV window | ❌ Stateless | ⚠️ Micro-batch state | ✅ Built-in keyed state |
| Exactly-once | ❌ Needs idempotency | ⚠️ Complex | ✅ Checkpoint + offset commit |
| Event-time processing | ❌ Arrival time only | ⚠️ Limited | ✅ Watermarks |
| Latency | ~100ms cold start | 1–30s micro-batch | <100ms per event |

### DD-3: Why Kubernetes (Not ECS or Bare EC2)

| Concern | Bare EC2 | ECS | EKS (Kubernetes) |
|---------|---------|-----|-------------------|
| Auto-healing | Manual | Per-task | Per-pod, auto-restart |
| Scaling | Scripts/ASG | Service auto-scale | HPA + Cluster Autoscaler |
| Kafka operator | DIY | DIY | Strimzi (production-grade) |
| Portability | AWS-only | AWS-only | Any cloud / on-prem |

### DD-4: Why 200 Kafka Partitions

```
Math:
  Peak throughput = 300 MB/s (20M TVs, 80% active, 300 bytes/event)
  With lz4 compression (3:1): 100 MB/s on wire
  Partitions needed = 100 ÷ 1 MB/s per partition = 100 minimum
  We pick 200 for headroom, even parallelism, and future growth.

IMPORTANT: Kafka partition count can only INCREASE, never decrease.
```

### DD-5: Why TimescaleDB (Not Plain PostgreSQL)

At 20M TVs × 12 windows/hour × 24 hours = **5.76 billion rows/day**.
Plain PostgreSQL degrades past ~100M rows per table.

TimescaleDB adds: hypertable chunking, chunk compression (10–20×),
continuous aggregates, and automatic retention policies.

### DD-6: Why TimescaleDB (Not OLAP Databases)

#### Database Types Comparison

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  DATABASE TYPES FOR ANALYTICS                                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  OLTP (PostgreSQL, MySQL)     → Transactions, user data, orders             │
│  Time-Series (TimescaleDB)    → IoT, metrics, monitoring, real-time         │
│  OLAP (ClickHouse, Redshift)  → Historical analytics, BI, data warehouse    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### Row-Based vs Column-Based Storage

```
ROW-BASED (TimescaleDB/PostgreSQL):
┌─────────────────────────────────────────────────────────────┐
│ Row 1: │ tv_id │ timestamp │ conn_ms │ rssi │ devices │    │
│ Row 2: │ tv_id │ timestamp │ conn_ms │ rssi │ devices │    │
│ Row 3: │ tv_id │ timestamp │ conn_ms │ rssi │ devices │    │
└─────────────────────────────────────────────────────────────┘
→ Fast to INSERT one row (all fields together)
→ Fast to SELECT one row by ID
→ Slower for "SUM all conn_ms" (must read entire rows)

COLUMN-BASED (OLAP - ClickHouse, Redshift):
┌──────────┐ ┌───────────┐ ┌─────────┐ ┌──────┐ ┌─────────┐
│ tv_id    │ │ timestamp │ │ conn_ms │ │ rssi │ │ devices │
│ TV-00001 │ │ 10:00:00  │ │ 342     │ │ -55  │ │ 5       │
│ TV-00002 │ │ 10:00:01  │ │ 512     │ │ -62  │ │ 3       │
│ TV-00003 │ │ 10:00:02  │ │ 128     │ │ -45  │ │ 7       │
└──────────┘ └───────────┘ └─────────┘ └──────┘ └─────────┘
→ Slower to INSERT (must update multiple column files)
→ FAST for "SUM all conn_ms" (reads only one column)
→ Great compression (similar values stored together)
```

#### When to Use What

| Database Type | Best For | Examples | Our Use Case |
|---------------|----------|----------|---------------|
| **TimescaleDB** | Real-time time-series, high-frequency writes | IoT sensors, metrics | ✅ 333K inserts/sec, real-time dashboards |
| **OLAP** | Historical analytics, batch queries | Yearly reports, BI | ❌ Batch-oriented, higher latency |
| **OLTP** | Transactions, user data | E-commerce, banking | ❌ Not optimized for time-series |

#### Why We Chose TimescaleDB

| Requirement | TimescaleDB | OLAP (ClickHouse) |
|-------------|-------------|-------------------|
| 333K inserts/sec | ✅ Designed for this | ⚠️ Prefers batch loads |
| Real-time queries | ✅ Sub-second latency | ⚠️ Seconds latency |
| "Last 5 minutes" queries | ✅ Optimized | ⚠️ Overkill |
| PostgreSQL compatibility | ✅ Full SQL, joins | ⚠️ Limited SQL dialect |
| Operational simplicity | ✅ Just PostgreSQL | ⚠️ Separate system |

#### Hybrid Architecture (Production)

In large production systems, you often use BOTH:

```
┌─────────────────────────────────────────────────────────────────┐
│  HYBRID DATA ARCHITECTURE                                        │
│                                                                  │
│  TV Agents → Kafka → Flink → TimescaleDB (HOT: 30 days)        │
│                                    │                             │
│                                    │ nightly ETL                 │
│                                    ▼                             │
│                              S3 (Parquet files)                 │
│                                    │                             │
│                                    ▼                             │
│                              OLAP (Redshift/ClickHouse)         │
│                              (COLD: years of history)           │
│                                    │                             │
│                                    ▼                             │
│                              BI Tools (Tableau, Looker)         │
│                                                                  │
│  HOT PATH: Real-time dashboards, alerting, last 30 days        │
│  COLD PATH: Historical analysis, business intelligence          │
└─────────────────────────────────────────────────────────────────┘
```

---

### DD-7: Kafka Partition Planning (The 1 MB/s Rule)

#### Why 1 MB/s Per Partition?

This is a **planning guideline**, not a hard limit:

```
Actual partition limits:
  - Single partition MAX: 10-50 MB/s (depends on disk, network)
  - Recommended per partition: 1-2 MB/s (for balanced load)
  - Our planning number: 1 MB/s (conservative)

Why conservative?
  1. Balanced partitions: Avoid hot spots
  2. Consumer parallelism: 1 consumer per partition
  3. Rebalancing headroom: Handle broker failures
  4. Safe math: Easy capacity planning
```

#### Partition Calculation

```
Our system:
  Peak throughput = 300 MB/s (20M TVs × 80% active × 300 bytes)
  With lz4 compression (3:1) = 100 MB/s on wire
  
Calculation:
  Partitions needed = 100 MB/s ÷ 1 MB/s = 100 minimum
  We chose: 200 partitions (2x headroom for growth)

IMPORTANT: Partition count can only INCREASE, never decrease!
```

#### Component Throughput Comparison

| Component | Throughput Capacity | Bottleneck Risk |
|-----------|---------------------|------------------|
| Kafka | 10-50 MB/s per broker | Low (scales horizontally) |
| Flink | 1-5M events/sec per TaskManager | Low (very fast) |
| TimescaleDB | 100K-500K inserts/sec | Medium (needs batching) |
| Network | Depends on instance type | Medium (budget instances limited) |

---

## Summary — What You Have Built

```
EDGE                INGESTION              PROCESSING            STORAGE
────────────       ─────────────         ──────────────        ──────────
C++ Agent     →    Kafka (Strimzi)   →   Flink (K8s)     →    TimescaleDB
20M TVs            200 partitions         4–32 TaskManagers     Hypertable
SQLite buffer      lz4 compression        5-min windows         S3 archive
                   3× replication         Exactly-once          30d raw / 1y agg

OBSERVABILITY           DEPLOYMENT              LOAD TEST
─────────────          ──────────────          ──────────────
Prometheus + Grafana   EKS (3 AZs)            100 K8s pods
AlertManager           Terraform IaC          200K TVs per pod
Custom dashboards      GitHub Actions CI/CD   333K events/sec
Flink metrics          Rolling deploys        Graduated 1K→20M
```

---

## Quick Reference — VS Code Shortcuts for This Project

| Action | Shortcut |
|--------|----------|
| Build C++ Agent | `Ctrl+Shift+B` (default build task) |
| Run Tests | `Ctrl+Shift+P` → "Tasks: Run Task" → "CMake: Run Tests" |
| Debug Agent | `F5` |
| Open Terminal | `` Ctrl+` `` |
| New Terminal | `` Ctrl+Shift+` `` |
| Switch Terminal | `Ctrl+Shift+]` / `Ctrl+Shift+[` |
| Search Files | `Ctrl+P` |
| Search Symbols | `Ctrl+Shift+O` (file) or `Ctrl+T` (workspace) |
| Go to Definition | `F12` or `Ctrl+Click` |
| Format Document | `Shift+Alt+F` |
| Toggle Sidebar | `Ctrl+B` |
| Command Palette | `Ctrl+Shift+P` |

---

## Learning Path — Recommended Order

| Week | Phase | Key Skill Learned | VS Code Focus |
|------|-------|-------------------|---------------|
| 1 | Phase 0 | System design thinking, env setup | Install extensions + tools |
| 2 | Phase 1 | Architecture understanding | Navigate project structure |
| 3 | Phase 2 | C++ embedded agent development | CMake Tools, debugging |
| 4 | Phase 3 | Stream processing with Flink | Java Extension Pack, Maven |
| 5 | Phase 4 | Docker Compose, local integration | Docker extension, curl.exe |
| 6 | Phase 5 | Kubernetes fundamentals | Kubernetes extension |
| 7 | Phase 6 | K8s deployment (Kafka, Flink, DB) | kubectl via terminal |
| 8 | Phase 7 | AWS EKS, Terraform IaC | Terraform extension, AWS CLI |
| 9 | Phase 8 | Load testing at scale | Python, monitoring dashboards |
| 10 | Phase 9 | Observability, production readiness | Grafana, PostgreSQL extension |

---

## Lessons Learned from Hands-On Implementation

### What Worked Well

1. **Docker-based C++ build**: Building the agent inside Docker avoids Windows C++ toolchain complexity
2. **KRaft mode Kafka**: No Zookeeper dependency simplifies the stack
3. **curl.exe for API calls**: Bypasses PowerShell's curl alias issues
4. **Manual tool installation**: More reliable than Chocolatey when permissions are restricted
5. **Local scaling with Docker**: Running multiple TV agents via `docker run` is simple and effective
6. **Flink consumer group behavior**: Shows "no active members" in Kafka consumer-groups but still works (Flink manages its own offsets)

### Common Pitfalls Encountered

1. **Java version mismatch**: Code used Java 15+ text blocks (`"""`) but JDK 11 was installed
   - Fix: Convert text blocks to string concatenation

2. **PowerShell curl confusion**: `curl` in PowerShell is an alias for `Invoke-WebRequest`
   - Fix: Use `curl.exe` explicitly

3. **Flink checkpoint permissions**: Job fails with "Failed to create directory for shared state"
   - Fix: `docker exec flink-jm bash -c "mkdir -p /tmp/checkpoints && chmod 777 /tmp/checkpoints"`

4. **Grafana empty dashboard**: Data exists but dashboard shows nothing
   - Cause: Simulated data has future timestamps (2026)
   - Fix: Adjust Grafana time range OR refresh continuous aggregates

5. **Docker network naming**: Network is `infra_telemetry`, not `infra_default`
   - Check with: `docker network ls`

6. **Kafka internal vs external ports**: 
   - Internal (container-to-container): `kafka:29092`
   - External (host machine): `localhost:9092`

7. **Container logs empty**: TV agent containers may show empty logs but still work
   - Verify with: `docker exec <container> ps aux` to confirm process is running
   - Check Kafka directly: `docker exec kafka kafka-console-consumer --bootstrap-server localhost:29092 --topic bt-events --max-messages 5`

### Quick Verification Commands

```powershell
# Check all containers are running
docker ps --format "table {{.Names}}\t{{.Status}}"

# Verify Kafka is receiving events
docker exec kafka kafka-console-consumer --bootstrap-server localhost:29092 --topic bt-events --max-messages 3

# Check Flink job status
curl.exe http://localhost:8081/jobs

# Check TimescaleDB has data
docker exec timescaledb psql -U telemetry -d telemetry -c "SELECT COUNT(*) FROM bt_window_stats;"

# Check continuous aggregate
docker exec timescaledb psql -U telemetry -d telemetry -c "SELECT * FROM bt_hourly_fleet LIMIT 5;"

# Refresh continuous aggregate (if needed)
docker exec timescaledb psql -U telemetry -d telemetry -c "CALL refresh_continuous_aggregate('bt_hourly_fleet', '2026-08-01', '2026-08-03');"

# Check all TVs in the system
docker exec timescaledb psql -U telemetry -d telemetry -c "SELECT tv_id, COUNT(*) as windows FROM bt_window_stats GROUP BY tv_id ORDER BY tv_id;"

# Check detailed stats per TV
docker exec timescaledb psql -U telemetry -d telemetry -c "SELECT tv_id, event_count, ROUND(avg_conn_ms::numeric, 1) as avg_ms, unique_devices FROM bt_window_stats ORDER BY window_start DESC LIMIT 12;"
```

### Scaling Multiple TV Agents Locally

```powershell
# Start multiple TV simulators (run from project root)
1..5 | ForEach-Object {
    docker run -d --name "tv-sim-$_" --network infra_telemetry `
      -v "tv-sim-$($_)-data:/data" `
      tv-bt-agent:latest TV-SIM-$('{0:D3}' -f $_) kafka:29092 /data/buffer.db
}

# Verify all are running
docker ps --filter "name=tv-sim" --format "table {{.Names}}\t{{.Status}}"

# Check events from all TVs in Kafka
docker exec kafka kafka-console-consumer --bootstrap-server localhost:29092 --topic bt-events --max-messages 10

# Stop and remove all simulators
docker stop $(docker ps -q --filter "name=tv-sim") 2>$null
docker rm $(docker ps -aq --filter "name=tv-sim") 2>$null
```

---

## Troubleshooting on Windows

| Problem | Solution |
|---------|----------|
| Docker Desktop won't start | Enable WSL2: `wsl --install` (reboot). Enable Hyper-V in BIOS. |
| CMake can't find SQLite | Ensure vcpkg installed it: `vcpkg list sqlite3` |
| Port already in use | `netstat -ano \| findstr :9092` → `taskkill /PID <pid> /F` |
| Kubernetes unreachable | Restart Docker Desktop. Check "Enable Kubernetes" in settings. |
| `kubectl` uses wrong cluster | `kubectl config get-contexts` → `kubectl config use-context docker-desktop` |
| Java not found by Maven | Set `JAVA_HOME`: `setx JAVA_HOME "C:\Program Files\Eclipse Adoptium\jdk-11.0.32.9-hotspot"` then restart terminal |
| Maven build fails with text block error | Java 11 doesn't support `"""` text blocks (Java 15+). Use string concatenation instead. |
| PowerShell curl doesn't work | Use `curl.exe` instead of `curl` (PowerShell aliases curl to Invoke-WebRequest) |
| Invoke-RestMethod -Form not found | Older PowerShell version. Use `curl.exe` for multipart uploads. |
| Flink job fails with checkpoint error | Run: `docker exec flink-jm bash -c "mkdir -p /tmp/checkpoints && chmod 777 /tmp/checkpoints"` |
| Grafana shows empty dashboard | Check data timestamps, refresh continuous aggregates, adjust time range |
| Long file paths fail | Enable long paths: `git config --system core.longpaths true` |
| Line ending issues | `git config --global core.autocrlf true` |
| Chocolatey permission errors | Install tools manually (see Phase 0 Option B) |

---

## Further Reading

| Topic | Resource |
|-------|----------|
| System Design | *Designing Data-Intensive Applications* by Martin Kleppmann |
| Kafka | [Kafka: The Definitive Guide](https://www.confluent.io/resources/kafka-the-definitive-guide-v2/) |
| Flink | [Official Docs](https://nightlies.apache.org/flink/flink-docs-stable/) |
| Kubernetes | [Kubernetes Up & Running](https://www.oreilly.com/library/view/kubernetes-up-and/9781098110192/) |
| Strimzi | [strimzi.io/documentation](https://strimzi.io/documentation/) |
| TimescaleDB | [docs.timescale.com](https://docs.timescale.com/) |
| VS Code C++ | [code.visualstudio.com/docs/languages/cpp](https://code.visualstudio.com/docs/languages/cpp) |
| VS Code Java | [code.visualstudio.com/docs/java/java-tutorial](https://code.visualstudio.com/docs/java/java-tutorial) |
| Docker Desktop | [docs.docker.com/desktop/windows](https://docs.docker.com/desktop/windows/) |
| vcpkg | [vcpkg.io/en/getting-started](https://vcpkg.io/en/getting-started) |
| OLAP Databases | [ClickHouse Docs](https://clickhouse.com/docs) |
| Data Modeling | [The Data Warehouse Toolkit](https://www.kimballgroup.com/data-warehouse-business-intelligence-resources/books/) |

---

## Project Completion Summary

### What Was Accomplished

✅ **Phase 0-1**: Environment setup and architecture understanding  
✅ **Phase 2**: C++ agent built with Docker (cross-platform)  
✅ **Phase 3**: Flink job built and deployed (Java 11 compatible)  
✅ **Phase 4**: Full local stack running (Kafka + Flink + TimescaleDB + Grafana)  
✅ **Phase 5**: Local scaling tested with 6 TV agents  

### Final System State (Local)

```
┌─────────────────────────────────────────────────────────────────┐
│  COMPLETED LOCAL DEPLOYMENT                                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  6 TV Agents ──▶ Kafka (bt-events) ──▶ Flink ──▶ TimescaleDB    │
│  (Docker)        (KRaft mode)          (JM+TM)   (bt_window_stats)│
│                                                                  │
│  Throughput: ~300 events / 5-min window (~1 event/sec)          │
│  Aggregation: avg/p99 connection_ms, unique devices, avg RSSI   │
│  Visualization: Grafana dashboards (port 3000)                  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Verified Data Flow

| Component | Status | Verification |
|-----------|--------|-------------|
| TV Agents (6) | ✅ Running | `docker ps --filter name=tv-sim` |
| Kafka | ✅ Receiving | `kafka-console-consumer` shows all TV IDs |
| Flink Job | ✅ Processing | Job status RUNNING at :8081 |
| TimescaleDB | ✅ Storing | 6 TVs in bt_window_stats |
| Grafana | ✅ Visualizing | Dashboard at :3000 (after refresh) |

### Sample Output from Scaled Test

```
   tv_id    | event_count | avg_ms | p99_ms | unique_devices 
------------+-------------+--------+--------+----------------
 TV-00001   |          58 | 1695.6 | 3497.0 |              5
 TV-SIM-001 |          49 | 1667.3 | 3476.0 |              5
 TV-SIM-002 |          51 | 1476.8 | 3430.0 |              5
 TV-SIM-003 |          47 | 1924.8 | 3489.0 |              5
 TV-SIM-004 |          51 | 1842.2 | 3482.0 |              5
 TV-SIM-005 |          52 | 1665.8 | 3500.0 |              5
```

### Clean Up Commands

```powershell
# Stop all TV simulator containers
docker stop tv-sim-1 tv-sim-2 tv-sim-3 tv-sim-4 tv-sim-5 tv-agent 2>$null
docker rm tv-sim-1 tv-sim-2 tv-sim-3 tv-sim-4 tv-sim-5 tv-agent 2>$null

# Stop the infrastructure stack
cd infra
docker-compose down

# Optional: Remove volumes (clean slate)
docker-compose down -v

# Optional: Remove unused networks
docker network prune -f

# Verify everything is stopped
docker ps -a
```

### Next Steps (If Continuing)

| Option | Description | Effort |
|--------|-------------|--------|
| **Scale to 50+ agents** | Test local limits with more containers | Low |
| **Deploy to AWS EKS** | Production-scale Kubernetes deployment | High |
| **Add alerting** | Implement bt-alerts consumer for slow connections | Medium |
| **Enhance dashboards** | Device type breakdown, RSSI heatmaps | Medium |
| **Load testing** | Use Python simulator for 1M+ virtual TVs | Medium |

---

## Glossary of Terms

| Term | Definition |
|------|------------|
| **Backpressure** | When a downstream system can't keep up, causing upstream to slow down |
| **Checkpoint** | Snapshot of Flink state for fault tolerance and exactly-once processing |
| **Consumer Group** | Set of Kafka consumers that share reading from topic partitions |
| **Continuous Aggregate** | TimescaleDB feature that pre-computes and maintains aggregations |
| **DAG** | Directed Acyclic Graph - Flink's internal representation of a job |
| **Event Time** | When an event actually occurred (from timestamp field) |
| **Exactly-Once** | Processing guarantee that each event is processed exactly one time |
| **Hot Key** | A partition key that receives disproportionately high traffic |
| **Hypertable** | TimescaleDB's auto-partitioned table optimized for time-series |
| **Idempotent** | Operation that produces same result regardless of how many times executed |
| **KRaft** | Kafka's Zookeeper-free consensus protocol (Kafka Raft) |
| **Offset** | Position of a message within a Kafka partition |
| **P99** | 99th percentile - value below which 99% of observations fall |
| **Partition** | Subdivision of a Kafka topic for parallelism |
| **Processing Time** | When Flink processes an event (wall clock time) |
| **Side Output** | Flink feature to route certain events to a separate stream |
| **TaskManager** | Flink worker process that executes tasks |
| **Tumbling Window** | Fixed-size, non-overlapping time window |
| **Watermark** | Flink's mechanism to track event-time progress and handle late data |

---

## Architecture Decision Records (ADR)

### ADR-001: Use Kafka Instead of Direct Database Writes

**Status**: Accepted

**Context**: 20M TVs need to send events to storage. Direct DB connections would require 20M concurrent connections.

**Decision**: Use Kafka as an intermediate buffer between TVs and processing.

**Consequences**:
- ✅ Decouples producers from consumers
- ✅ Absorbs traffic bursts
- ✅ Enables replay and multiple consumers
- ❌ Adds operational complexity (Kafka cluster)
- ❌ Adds latency (~10ms)

### ADR-002: Use Event Time Instead of Processing Time

**Status**: Accepted

**Context**: TVs can go offline and send buffered events with old timestamps.

**Decision**: Use event-time windowing with 10-second watermark tolerance.

**Consequences**:
- ✅ Correct aggregations for offline TVs
- ✅ Accurate historical data
- ❌ Events >10s late are dropped (acceptable trade-off)
- ❌ Slightly more complex watermark configuration

### ADR-003: Use TimescaleDB Instead of OLAP Database

**Status**: Accepted

**Context**: Need to store aggregated stats with real-time query capability.

**Decision**: Use TimescaleDB (PostgreSQL extension) for time-series storage.

**Consequences**:
- ✅ High write throughput (100K+ inserts/sec)
- ✅ Sub-second query latency
- ✅ PostgreSQL compatibility (familiar SQL)
- ❌ Not ideal for multi-year historical analysis
- ❌ Would need OLAP layer for BI workloads

### ADR-004: Use 5-Minute Tumbling Windows

**Status**: Accepted

**Context**: Need to aggregate per-TV stats at regular intervals.

**Decision**: Use 5-minute tumbling (non-overlapping) windows.

**Consequences**:
- ✅ Balance between freshness and write volume
- ✅ Predictable output rate (12 rows/hour/TV)
- ✅ Simple to reason about
- ❌ Dashboard has up to 5-minute staleness
- ❌ Can't answer "last 2 minutes" queries precisely
