# AWS EKS Deployment - Complete Learning Guide

## Table of Contents
1. [Kubernetes Architecture & Concepts](#1-kubernetes-architecture--concepts)
2. [How EKS Simplifies Kubernetes](#2-how-eks-simplifies-kubernetes)
3. [Our AWS Pipeline - How It Works](#3-our-aws-pipeline---how-it-works)
4. [Scalability - How AWS Handles 20M Devices](#4-scalability---how-aws-handles-20m-devices)
5. [Production Deployment Walkthrough](#5-production-deployment-walkthrough)

---

## 1. Kubernetes Architecture & Concepts

### 1.1 What is Kubernetes?

Kubernetes (K8s) is a **container orchestration platform** that automates:
- Deploying containers across multiple servers
- Scaling up/down based on load
- Restarting failed containers
- Load balancing traffic
- Managing storage and secrets

```
WITHOUT KUBERNETES:
  "Server 3 crashed!" → SSH in → Check logs → Restart manually → 30 min downtime

WITH KUBERNETES:
  "Server 3 crashed!" → K8s detects in 10s → Reschedules pods → 0 downtime
```

### 1.2 Kubernetes Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         KUBERNETES CLUSTER                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                     CONTROL PLANE (Master)                           │    │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌───────────────┐  │    │
│  │  │ API Server  │ │   etcd      │ │ Scheduler   │ │ Controller    │  │    │
│  │  │             │ │ (database)  │ │             │ │ Manager       │  │    │
│  │  │ REST API    │ │ stores all  │ │ assigns pods│ │ ensures       │  │    │
│  │  │ for all     │ │ cluster     │ │ to nodes    │ │ desired state │  │    │
│  │  │ operations  │ │ state       │ │             │ │ matches actual│  │    │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └───────────────┘  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                    │                                         │
│                                    │ kubectl commands                        │
│                                    ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                     WORKER NODES (Data Plane)                        │    │
│  │                                                                      │    │
│  │  ┌─────────────────────┐    ┌─────────────────────┐                 │    │
│  │  │      NODE 1         │    │      NODE 2         │    ...          │    │
│  │  │  (EC2 instance)     │    │  (EC2 instance)     │                 │    │
│  │  │                     │    │                     │                 │    │
│  │  │  ┌───────────────┐  │    │  ┌───────────────┐  │                 │    │
│  │  │  │    kubelet    │  │    │  │    kubelet    │  │                 │    │
│  │  │  │ (node agent)  │  │    │  │ (node agent)  │  │                 │    │
│  │  │  └───────────────┘  │    │  └───────────────┘  │                 │    │
│  │  │                     │    │                     │                 │    │
│  │  │  ┌─────┐ ┌─────┐   │    │  ┌─────┐ ┌─────┐   │                 │    │
│  │  │  │ Pod │ │ Pod │   │    │  │ Pod │ │ Pod │   │                 │    │
│  │  │  │     │ │     │   │    │  │     │ │     │   │                 │    │
│  │  │  └─────┘ └─────┘   │    │  └─────┘ └─────┘   │                 │    │
│  │  └─────────────────────┘    └─────────────────────┘                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.3 Key Kubernetes Objects Explained

#### Pod - The Smallest Unit
```yaml
# A Pod is one or more containers that share storage/network
apiVersion: v1
kind: Pod
metadata:
  name: tv-agent-pod
spec:
  containers:
    - name: tv-agent
      image: tv-bt-agent:latest
      env:
        - name: BROKERS
          value: "kafka:9092"
```

```
POD LIFECYCLE:
  Pending → ContainerCreating → Running → Succeeded/Failed

  ┌─────────────────────────────────────┐
  │              POD                     │
  │  ┌───────────┐  ┌───────────┐       │
  │  │ Container │  │ Container │       │
  │  │ (app)     │  │ (sidecar) │       │
  │  └───────────┘  └───────────┘       │
  │                                      │
  │  Shared: Network (localhost)         │
  │  Shared: Storage (volumes)           │
  │  Shared: IP address                  │
  └─────────────────────────────────────┘
```

#### Deployment - Manages Pod Replicas
```yaml
# Deployment ensures N copies of your pod are always running
apiVersion: apps/v1
kind: Deployment
metadata:
  name: tv-agent
spec:
  replicas: 10                    # Run 10 copies
  selector:
    matchLabels:
      app: tv-agent
  template:                       # Pod template
    metadata:
      labels:
        app: tv-agent
    spec:
      containers:
        - name: tv-agent
          image: tv-bt-agent:latest
```

```
DEPLOYMENT BEHAVIOR:

  You say: replicas: 10
  
  Kubernetes ensures:
  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐
  │Pod 1│ │Pod 2│ │Pod 3│ │Pod 4│ │Pod 5│  ... (10 total)
  └─────┘ └─────┘ └─────┘ └─────┘ └─────┘
  
  If Pod 3 crashes:
  ┌─────┐ ┌─────┐    ✗    ┌─────┐ ┌─────┐
  │Pod 1│ │Pod 2│  (dead) │Pod 4│ │Pod 5│
  └─────┘ └─────┘         └─────┘ └─────┘
                    │
                    ▼ K8s automatically creates
               ┌─────┐
               │Pod 6│ (replacement)
               └─────┘
```

#### StatefulSet - For Databases & Kafka
```yaml
# StatefulSet gives pods stable identities and persistent storage
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: kafka
spec:
  serviceName: kafka
  replicas: 3
  template:
    spec:
      containers:
        - name: kafka
          image: kafka:latest
          volumeMounts:
            - name: data
              mountPath: /var/lib/kafka
  volumeClaimTemplates:           # Each pod gets its own disk
    - metadata:
        name: data
      spec:
        resources:
          requests:
            storage: 100Gi
```

```
STATEFULSET vs DEPLOYMENT:

  Deployment pods:     Random names, no persistent storage
    pod-abc123         (can be replaced by any other pod)
    pod-def456
    pod-ghi789
  
  StatefulSet pods:    Ordered names, persistent storage
    kafka-0  ←→  disk-0   (always kafka-0, always same disk)
    kafka-1  ←→  disk-1   (always kafka-1, always same disk)
    kafka-2  ←→  disk-2   (always kafka-2, always same disk)
```

#### Service - Stable Network Endpoint
```yaml
# Service provides a stable DNS name for a set of pods
apiVersion: v1
kind: Service
metadata:
  name: kafka
spec:
  selector:
    app: kafka              # Routes to pods with this label
  ports:
    - port: 9092
  clusterIP: None           # Headless service for StatefulSet
```

```
SERVICE TYPES:

  ClusterIP (default):
    Internal only. Other pods reach it via: kafka.telemetry.svc.cluster.local
  
  NodePort:
    Exposes on each node's IP at a static port (30000-32767)
  
  LoadBalancer:
    Creates cloud load balancer (AWS ALB/NLB)
    External traffic → Load Balancer → Pods

  ┌──────────────────────────────────────────────────────┐
  │  Service: kafka-bootstrap                             │
  │  DNS: kafka-bootstrap.telemetry.svc.cluster.local    │
  │                        │                              │
  │           ┌────────────┼────────────┐                │
  │           ▼            ▼            ▼                │
  │       ┌───────┐   ┌───────┐   ┌───────┐             │
  │       │kafka-0│   │kafka-1│   │kafka-2│             │
  │       └───────┘   └───────┘   └───────┘             │
  └──────────────────────────────────────────────────────┘
```

#### ConfigMap & Secret - Configuration
```yaml
# ConfigMap for non-sensitive config
apiVersion: v1
kind: ConfigMap
metadata:
  name: app-config
data:
  KAFKA_BROKERS: "kafka:9092"
  LOG_LEVEL: "INFO"

---
# Secret for sensitive data (base64 encoded)
apiVersion: v1
kind: Secret
metadata:
  name: db-credentials
type: Opaque
data:
  username: cG9zdGdyZXM=      # "postgres" in base64
  password: c2VjcmV0MTIz      # "secret123" in base64
```

#### PersistentVolumeClaim (PVC) - Storage
```yaml
# PVC requests storage from the cluster
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: timescaledb-data
spec:
  accessModes:
    - ReadWriteOnce
  storageClassName: gp3        # AWS EBS gp3 volumes
  resources:
    requests:
      storage: 100Gi
```

```
STORAGE FLOW:

  PVC (request)  →  StorageClass  →  PV (actual disk)  →  Pod
  
  "I need 100GB"    "Use AWS EBS     "Here's an EBS     "Mounted at
                     gp3 type"        volume"            /data"
```

### 1.4 How Kubernetes Networking Works

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     KUBERNETES NETWORKING                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  RULE 1: Every pod gets its own IP address                                  │
│  RULE 2: Pods can communicate with any other pod (no NAT)                   │
│  RULE 3: Services provide stable DNS names                                  │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  Node 1 (10.0.1.10)                                                  │   │
│  │  ┌─────────────────┐  ┌─────────────────┐                           │   │
│  │  │ Pod: tv-agent-1 │  │ Pod: tv-agent-2 │                           │   │
│  │  │ IP: 10.244.1.5  │  │ IP: 10.244.1.6  │                           │   │
│  │  └────────┬────────┘  └────────┬────────┘                           │   │
│  │           │                    │                                     │   │
│  │           └──────────┬─────────┘                                     │   │
│  │                      │                                               │   │
│  └──────────────────────┼───────────────────────────────────────────────┘   │
│                         │                                                    │
│                         │  Pod-to-Pod: Direct IP communication              │
│                         │                                                    │
│  ┌──────────────────────┼───────────────────────────────────────────────┐   │
│  │  Node 2 (10.0.1.11)  │                                               │   │
│  │  ┌─────────────────┐ │ ┌─────────────────┐                           │   │
│  │  │ Pod: kafka-0    │◀┘ │ Pod: flink-tm   │                           │   │
│  │  │ IP: 10.244.2.10 │   │ IP: 10.244.2.11 │                           │   │
│  │  └─────────────────┘   └─────────────────┘                           │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  DNS RESOLUTION:                                                             │
│    kafka-kafka-bootstrap.telemetry.svc.cluster.local → 10.100.55.182       │
│    (Service ClusterIP, load balances to kafka pods)                         │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```



---

## 2. How EKS Simplifies Kubernetes

### 2.1 What is Amazon EKS?

**EKS = Elastic Kubernetes Service** - AWS manages the Kubernetes control plane for you.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    SELF-MANAGED vs EKS                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  SELF-MANAGED KUBERNETES:                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  YOU MANAGE EVERYTHING:                                              │   │
│  │  ✗ Install & upgrade Kubernetes                                      │   │
│  │  ✗ Manage etcd cluster (backups, scaling)                           │   │
│  │  ✗ Secure API server                                                 │   │
│  │  ✗ Handle control plane high availability                           │   │
│  │  ✗ Patch security vulnerabilities                                    │   │
│  │  ✗ Certificate management                                            │   │
│  │  ✗ Manage worker nodes                                               │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  AMAZON EKS:                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  AWS MANAGES:                        YOU MANAGE:                     │   │
│  │  ✓ Control plane (API server)        • Worker nodes (EC2)           │   │
│  │  ✓ etcd (3 AZs, auto-backup)         • Your applications            │   │
│  │  ✓ Security patches                  • Kubernetes manifests         │   │
│  │  ✓ High availability                 • Scaling policies             │   │
│  │  ✓ Kubernetes upgrades               • Monitoring/logging           │   │
│  │  ✓ IAM integration                                                   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  COST: $0.10/hour for control plane + EC2 costs for worker nodes           │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 EKS Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         AWS REGION (us-east-1)                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │              EKS CONTROL PLANE (AWS Managed)                         │   │
│  │              ════════════════════════════════                        │   │
│  │                                                                      │   │
│  │   AZ-1a          AZ-1b          AZ-1c                               │   │
│  │  ┌──────┐       ┌──────┐       ┌──────┐                             │   │
│  │  │ etcd │ ←───→ │ etcd │ ←───→ │ etcd │   (replicated)              │   │
│  │  └──────┘       └──────┘       └──────┘                             │   │
│  │      │              │              │                                 │   │
│  │      └──────────────┼──────────────┘                                 │   │
│  │                     │                                                │   │
│  │              ┌──────────────┐                                        │   │
│  │              │  API Server  │  ← kubectl commands go here            │   │
│  │              │  (HA, multi-AZ)                                       │   │
│  │              └──────────────┘                                        │   │
│  │                     │                                                │   │
│  └─────────────────────┼────────────────────────────────────────────────┘   │
│                        │                                                     │
│                        │  AWS manages above this line                        │
│  ══════════════════════╪═══════════════════════════════════════════════════ │
│                        │  You manage below this line                         │
│                        │                                                     │
│  ┌─────────────────────┼────────────────────────────────────────────────┐   │
│  │              YOUR VPC (10.0.0.0/16)                                  │   │
│  │                     │                                                │   │
│  │   ┌─────────────────┼─────────────────┐                             │   │
│  │   │     EKS NODE GROUP (EC2 instances)│                             │   │
│  │   │                 │                 │                             │   │
│  │   │  ┌──────────────┴──────────────┐  │                             │   │
│  │   │  │                             │  │                             │   │
│  │   │  │  Node 1      Node 2      Node 3                              │   │
│  │   │  │  (t3.small)  (t3.small)  (t3.small)                          │   │
│  │   │  │  ┌─────┐     ┌─────┐     ┌─────┐                             │   │
│  │   │  │  │Pods │     │Pods │     │Pods │                             │   │
│  │   │  │  └─────┘     └─────┘     └─────┘                             │   │
│  │   │  │                             │  │                             │   │
│  │   │  └─────────────────────────────┘  │                             │   │
│  │   └───────────────────────────────────┘                             │   │
│  │                                                                      │   │
│  │   Other AWS Services:                                                │   │
│  │   • ECR (container images)                                          │   │
│  │   • EBS (persistent volumes)                                        │   │
│  │   • ALB/NLB (load balancers)                                        │   │
│  │   • IAM (permissions)                                               │   │
│  │                                                                      │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Key EKS Integrations

| AWS Service | Kubernetes Integration | Purpose |
|-------------|----------------------|---------|
| **IAM** | IRSA (IAM Roles for Service Accounts) | Pods get AWS permissions without access keys |
| **ECR** | Image pull secrets | Store and pull container images |
| **EBS** | CSI Driver + StorageClass | Persistent volumes for databases |
| **ALB/NLB** | AWS Load Balancer Controller | Expose services to internet |
| **CloudWatch** | Fluent Bit / Container Insights | Logs and metrics |
| **Secrets Manager** | External Secrets Operator | Sync secrets to K8s |

### 2.4 What We Created with Terraform

```hcl
# Our terraform/eks/main.tf creates:

1. EKS Cluster
   └── Name: tv-telemetry
   └── Version: Kubernetes 1.30
   └── Cost: $0.10/hour

2. Node Group  
   └── 3x t3.small instances
   └── Cost: ~$0.02/hour each
   └── Auto-scaling: 2-4 nodes

3. IAM Roles
   └── Cluster role (EKS service)
   └── Node role (EC2 instances)
   └── EBS CSI driver role

4. ECR Repository
   └── tv-bt-agent image storage

5. OIDC Provider
   └── Enables IRSA for pod permissions
```

---

## 3. Our AWS Pipeline - How It Works

### 3.1 Complete Data Flow on AWS

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    PRODUCTION DATA FLOW ON AWS                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  STEP 1: TV Agent Produces Events                                           │
│  ════════════════════════════════                                           │
│                                                                              │
│  ┌─────────────┐                                                            │
│  │ TV Agent    │  Container running in EKS                                  │
│  │ (C++ app)   │                                                            │
│  │             │  Generates: {"tv_id":"tv-agent-xyz",                       │
│  │             │              "device_name":"Sony WH-1000XM5",              │
│  │             │              "connection_ms":342, ...}                     │
│  └──────┬──────┘                                                            │
│         │                                                                    │
│         │ Kafka produce (BROKERS env var)                                   │
│         ▼                                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ STEP 2: Kafka Receives & Stores Events                               │   │
│  │ ═══════════════════════════════════════                              │   │
│  │                                                                      │   │
│  │  Kafka Cluster (Strimzi on EKS)                                     │   │
│  │  ┌─────────────────────────────────────────────────────────────┐   │   │
│  │  │  Topic: bt-events (3 partitions)                             │   │   │
│  │  │                                                               │   │   │
│  │  │  Partition 0: [msg1] [msg4] [msg7] ...                       │   │   │
│  │  │  Partition 1: [msg2] [msg5] [msg8] ...                       │   │   │
│  │  │  Partition 2: [msg3] [msg6] [msg9] ...                       │   │   │
│  │  │                                                               │   │   │
│  │  │  Messages partitioned by: hash(tv_id) % 3                    │   │   │
│  │  │  Same TV always goes to same partition (ordering preserved)  │   │   │
│  │  └─────────────────────────────────────────────────────────────┘   │   │
│  │                                                                      │   │
│  │  Storage: EBS gp3 volume (8GB)                                      │   │
│  │  Retention: 7 days (configurable)                                   │   │
│  │                                                                      │   │
│  └──────────────────────────────────┬──────────────────────────────────┘   │
│                                     │                                        │
│                                     │ Kafka consume                          │
│                                     ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ STEP 3: Flink Processes Events (NOT YET DEPLOYED)                    │   │
│  │ ═════════════════════════════════════════════════                    │   │
│  │                                                                      │   │
│  │  Flink Cluster (on EKS)                                             │   │
│  │  ┌─────────────────────────────────────────────────────────────┐   │   │
│  │  │  JobManager: Coordinates the job                             │   │   │
│  │  │  TaskManager: Executes the processing                        │   │   │
│  │  │                                                               │   │   │
│  │  │  Processing Logic:                                            │   │   │
│  │  │  1. Read from Kafka topic bt-events                          │   │   │
│  │  │  2. Key by tv_id (group events per TV)                       │   │   │
│  │  │  3. 5-minute tumbling window                                 │   │   │
│  │  │  4. Aggregate: avg, p99, count, unique devices               │   │   │
│  │  │  5. Write to TimescaleDB                                     │   │   │
│  │  └─────────────────────────────────────────────────────────────┘   │   │
│  │                                                                      │   │
│  └──────────────────────────────────┬──────────────────────────────────┘   │
│                                     │                                        │
│                                     │ JDBC insert                            │
│                                     ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ STEP 4: TimescaleDB Stores Aggregated Stats                          │   │
│  │ ═══════════════════════════════════════════                          │   │
│  │                                                                      │   │
│  │  TimescaleDB (PostgreSQL + time-series extension)                   │   │
│  │  ┌─────────────────────────────────────────────────────────────┐   │   │
│  │  │  Table: bt_window_stats                                      │   │   │
│  │  │                                                               │   │   │
│  │  │  tv_id     | window_start | event_count | avg_conn_ms | ...  │   │   │
│  │  │  ----------|--------------|-------------|-------------|---   │   │   │
│  │  │  tv-agent-1| 10:00:00     | 58          | 1695.6      |      │   │   │
│  │  │  tv-agent-2| 10:00:00     | 51          | 1476.8      |      │   │   │
│  │  │  tv-agent-3| 10:00:00     | 47          | 1924.8      |      │   │   │
│  │  └─────────────────────────────────────────────────────────────┘   │   │
│  │                                                                      │   │
│  │  Storage: EBS gp3 volume (10GB)                                     │   │
│  │  Features: Hypertable, compression, continuous aggregates           │   │
│  │                                                                      │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Current Deployment Status

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    WHAT'S RUNNING ON YOUR EKS CLUSTER                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  NAMESPACE: telemetry                                                        │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  KAFKA (Strimzi Operator)                                            │   │
│  │  ├── strimzi-cluster-operator    (manages Kafka)                    │   │
│  │  ├── kafka-dual-role-0           (broker + controller)              │   │
│  │  └── kafka-entity-operator       (manages topics)                   │   │
│  │                                                                      │   │
│  │  Topics Created:                                                     │   │
│  │  • bt-events (3 partitions) ✓                                       │   │
│  │  • bt-alerts (1 partition)  ✓                                       │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  FLINK                                                               │   │
│  │  ├── flink-jobmanager            (coordinates jobs)                 │   │
│  │  └── flink-taskmanager           (executes processing)              │   │
│  │                                                                      │   │
│  │  Status: Running but NO JOB SUBMITTED YET                           │   │
│  │  (Flink JAR needs to be uploaded and started)                       │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  TIMESCALEDB                                                         │   │
│  │  └── timescaledb-0               (PostgreSQL + TimescaleDB)         │   │
│  │                                                                      │   │
│  │  Tables Created:                                                     │   │
│  │  • bt_events (raw events)       ✓                                   │   │
│  │  • bt_window_stats (aggregated) ✓                                   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  TV AGENTS (10 replicas)                                             │   │
│  │  ├── tv-agent-xxxxx-1                                               │   │
│  │  ├── tv-agent-xxxxx-2                                               │   │
│  │  ├── ...                                                             │   │
│  │  └── tv-agent-xxxxx-10                                              │   │
│  │                                                                      │   │
│  │  Status: PRODUCING EVENTS TO KAFKA ✓                                │   │
│  │  Verified: 411+ messages in bt-events topic                         │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 How Services Communicate

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    KUBERNETES SERVICE DISCOVERY                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Every service gets a DNS name:                                             │
│  <service-name>.<namespace>.svc.cluster.local                               │
│                                                                              │
│  OUR SERVICES:                                                               │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                                                                      │   │
│  │  kafka-kafka-bootstrap.telemetry.svc.cluster.local:9092             │   │
│  │  └── TV agents connect here to produce events                       │   │
│  │                                                                      │   │
│  │  flink-jobmanager.telemetry.svc.cluster.local:8081                  │   │
│  │  └── Flink web UI and job submission                                │   │
│  │                                                                      │   │
│  │  timescaledb.telemetry.svc.cluster.local:5432                       │   │
│  │  └── Flink writes aggregated stats here                             │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  CONNECTION FLOW:                                                            │
│                                                                              │
│  TV Agent Pod                                                                │
│       │                                                                      │
│       │ env: BROKERS=kafka-kafka-bootstrap.telemetry.svc.cluster.local:9092│
│       │                                                                      │
│       ▼                                                                      │
│  ┌─────────────┐     DNS lookup      ┌─────────────┐                       │
│  │ CoreDNS     │ ──────────────────► │ Service IP  │                       │
│  │ (K8s DNS)   │     10.100.55.182   │ (ClusterIP) │                       │
│  └─────────────┘                      └──────┬──────┘                       │
│                                              │                               │
│                                              │ Routes to pod                 │
│                                              ▼                               │
│                                       ┌─────────────┐                       │
│                                       │ kafka-dual- │                       │
│                                       │ role-0      │                       │
│                                       └─────────────┘                       │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```



---

## 4. Scalability - How AWS Handles 20M Devices

### 4.1 The Scalability Challenge

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    THE 20 MILLION DEVICE PROBLEM                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  SCALE NUMBERS:                                                              │
│  • 20,000,000 TVs                                                           │
│  • Each TV sends 1 event per minute                                         │
│  • Each event is ~300 bytes                                                 │
│                                                                              │
│  CALCULATIONS:                                                               │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  Events per second = 20,000,000 ÷ 60 = 333,333 events/sec           │   │
│  │  Data rate = 333,333 × 300 bytes = 100 MB/sec                       │   │
│  │  Daily data = 100 MB/sec × 86,400 sec = 8.6 TB/day                  │   │
│  │  Monthly data = 8.6 TB × 30 = 258 TB/month                          │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  PEAK LOAD (Prime time 7-10 PM):                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  80% of TVs active = 16,000,000 TVs                                 │   │
│  │  Events per second = 16,000,000 ÷ 60 = 266,667 events/sec          │   │
│  │  With burst factor (2x) = 533,334 events/sec                        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  QUESTION: How do you handle 500K+ events per second?                       │
│  ANSWER: Horizontal scaling at every layer                                  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Horizontal Scaling Strategy

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    HORIZONTAL SCALING AT EACH LAYER                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  LAYER 1: TV AGENTS (Producers)                                             │
│  ═══════════════════════════════                                            │
│                                                                              │
│  Current: 10 pods (demo)                                                    │
│  Production: 20,000,000 real TVs                                            │
│                                                                              │
│  Each TV is independent - no coordination needed                            │
│  Scaling: Just add more TVs (they self-register)                           │
│                                                                              │
│  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐        ┌─────┐                           │
│  │TV 1 │ │TV 2 │ │TV 3 │ │TV 4 │  ...   │TV 20M│                          │
│  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘        └──┬──┘                           │
│     │       │       │       │              │                                │
│     └───────┴───────┴───────┴──────────────┘                                │
│                      │                                                       │
│                      ▼                                                       │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  LAYER 2: KAFKA (Message Buffer)                                            │
│  ═══════════════════════════════                                            │
│                                                                              │
│  Current: 1 broker, 3 partitions (demo)                                     │
│  Production: 3+ brokers, 200 partitions                                     │
│                                                                              │
│  WHY 200 PARTITIONS?                                                        │
│  • Each partition handles ~1 MB/sec safely                                  │
│  • 100 MB/sec ÷ 1 MB/sec = 100 partitions minimum                          │
│  • 200 partitions = 2x headroom for growth                                  │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  Kafka Cluster                                                       │   │
│  │                                                                      │   │
│  │  Broker 1          Broker 2          Broker 3                       │   │
│  │  ┌──────────┐     ┌──────────┐     ┌──────────┐                    │   │
│  │  │Part 0,3,6│     │Part 1,4,7│     │Part 2,5,8│  ...               │   │
│  │  │9,12...   │     │10,13...  │     │11,14...  │                    │   │
│  │  └──────────┘     └──────────┘     └──────────┘                    │   │
│  │                                                                      │   │
│  │  Partitions distributed across brokers for load balancing           │   │
│  │  Each partition is replicated to 2 other brokers (RF=3)            │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  SCALING KAFKA:                                                              │
│  • Add more brokers → partitions rebalance automatically                   │
│  • Add more partitions → more parallelism (can't decrease!)               │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  LAYER 3: FLINK (Stream Processing)                                         │
│  ══════════════════════════════════                                         │
│                                                                              │
│  Current: 1 TaskManager (demo)                                              │
│  Production: 16-32 TaskManagers                                             │
│                                                                              │
│  WHY MULTIPLE TASKMANAGERS?                                                 │
│  • Each TM processes a subset of Kafka partitions                          │
│  • More TMs = more parallel processing                                     │
│  • Flink parallelism = number of Kafka partitions                          │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  Flink Cluster                                                       │   │
│  │                                                                      │   │
│  │  JobManager (1)                                                      │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │ Coordinates job, manages checkpoints, handles failures        │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  │                                                                      │   │
│  │  TaskManagers (N)                                                    │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐              │   │
│  │  │   TM 1   │ │   TM 2   │ │   TM 3   │ │   TM 4   │  ...         │   │
│  │  │Part 0-49 │ │Part 50-99│ │Part100-149│Part150-199│              │   │
│  │  │          │ │          │ │          │ │          │              │   │
│  │  │ Window   │ │ Window   │ │ Window   │ │ Window   │              │   │
│  │  │ State    │ │ State    │ │ State    │ │ State    │              │   │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘              │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  SCALING FLINK:                                                              │
│  • kubectl scale deployment flink-taskmanager --replicas=16                │
│  • HorizontalPodAutoscaler can auto-scale based on CPU/lag                 │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  LAYER 4: TIMESCALEDB (Storage)                                             │
│  ══════════════════════════════                                             │
│                                                                              │
│  Current: 1 instance (demo)                                                 │
│  Production: Primary + Read replicas                                        │
│                                                                              │
│  SCALING STRATEGIES:                                                         │
│  • Vertical: Bigger instance (more CPU/RAM)                                │
│  • Read replicas: Offload dashboard queries                                │
│  • Partitioning: TimescaleDB auto-chunks by time                           │
│  • Compression: 10-20x compression on old data                             │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  TimescaleDB                                                         │   │
│  │                                                                      │   │
│  │  Hypertable: bt_window_stats                                        │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │  Chunk 1      Chunk 2      Chunk 3      Chunk 4              │  │   │
│  │  │  (Week 1)     (Week 2)     (Week 3)     (Week 4)             │  │   │
│  │  │  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐             │  │   │
│  │  │  │ 140M   │  │ 140M   │  │ 140M   │  │ 140M   │  rows       │  │   │
│  │  │  │ rows   │  │ rows   │  │ rows   │  │ rows   │             │  │   │
│  │  │  │        │  │        │  │        │  │        │             │  │   │
│  │  │  │Compress│  │Compress│  │        │  │        │             │  │   │
│  │  │  │ (10x)  │  │ (10x)  │  │        │  │        │             │  │   │
│  │  │  └────────┘  └────────┘  └────────┘  └────────┘             │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  │                                                                      │   │
│  │  Auto-compression policy: Compress chunks older than 7 days         │   │
│  │  Retention policy: Drop chunks older than 1 year                    │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.3 Kubernetes Auto-Scaling

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    KUBERNETES AUTO-SCALING                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  THREE LEVELS OF AUTO-SCALING:                                              │
│                                                                              │
│  1. HORIZONTAL POD AUTOSCALER (HPA)                                         │
│  ═══════════════════════════════════                                        │
│  Scales pods based on CPU/memory/custom metrics                             │
│                                                                              │
│  apiVersion: autoscaling/v2                                                 │
│  kind: HorizontalPodAutoscaler                                              │
│  metadata:                                                                   │
│    name: flink-taskmanager-hpa                                              │
│  spec:                                                                       │
│    scaleTargetRef:                                                          │
│      apiVersion: apps/v1                                                    │
│      kind: Deployment                                                       │
│      name: flink-taskmanager                                                │
│    minReplicas: 4                                                           │
│    maxReplicas: 32                                                          │
│    metrics:                                                                  │
│    - type: Resource                                                         │
│      resource:                                                              │
│        name: cpu                                                            │
│        target:                                                              │
│          type: Utilization                                                  │
│          averageUtilization: 70    # Scale up when CPU > 70%               │
│                                                                              │
│  BEHAVIOR:                                                                   │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                                                                      │   │
│  │  CPU < 70%                    CPU > 70%                             │   │
│  │  ┌─────┐ ┌─────┐             ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐      │   │
│  │  │ TM1 │ │ TM2 │    ───►    │ TM1 │ │ TM2 │ │ TM3 │ │ TM4 │      │   │
│  │  └─────┘ └─────┘             └─────┘ └─────┘ └─────┘ └─────┘      │   │
│  │  (2 replicas)                (4 replicas - scaled up)              │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  2. CLUSTER AUTOSCALER                                                       │
│  ═════════════════════                                                       │
│  Scales EC2 nodes when pods can't be scheduled                              │
│                                                                              │
│  BEHAVIOR:                                                                   │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                                                                      │   │
│  │  Pods pending (no room)           Nodes added automatically         │   │
│  │                                                                      │   │
│  │  Node 1    Node 2    Node 3       Node 1    Node 2    Node 3    Node 4│  │
│  │  ┌─────┐  ┌─────┐  ┌─────┐       ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐│  │
│  │  │█████│  │█████│  │█████│  ───► │█████│  │█████│  │█████│  │░░░░░││  │
│  │  │█████│  │█████│  │█████│       │█████│  │█████│  │█████│  │░░░░░││  │
│  │  └─────┘  └─────┘  └─────┘       └─────┘  └─────┘  └─────┘  └─────┘│  │
│  │  (all full)                       (new node added for pending pods) │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  3. KEDA (Kubernetes Event-Driven Autoscaling)                              │
│  ═════════════════════════════════════════════                              │
│  Scales based on external metrics (Kafka lag, queue depth)                  │
│                                                                              │
│  apiVersion: keda.sh/v1alpha1                                               │
│  kind: ScaledObject                                                         │
│  metadata:                                                                   │
│    name: flink-kafka-scaler                                                 │
│  spec:                                                                       │
│    scaleTargetRef:                                                          │
│      name: flink-taskmanager                                                │
│    triggers:                                                                 │
│    - type: kafka                                                            │
│      metadata:                                                              │
│        bootstrapServers: kafka:9092                                         │
│        consumerGroup: flink-consumer                                        │
│        topic: bt-events                                                     │
│        lagThreshold: "100000"   # Scale up when lag > 100K messages        │
│                                                                              │
│  BEHAVIOR:                                                                   │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                                                                      │   │
│  │  Kafka Lag Graph:                                                    │   │
│  │                                                                      │   │
│  │  Messages │                    ╱╲                                   │   │
│  │  behind   │                   ╱  ╲    Scale up triggered            │   │
│  │           │    ──────────────╱    ╲──────────────                   │   │
│  │  100K ────│───────────────────────────────────── threshold          │   │
│  │           │  ╱╲            ╱        ╲                               │   │
│  │           │ ╱  ╲__________╱          ╲________                      │   │
│  │           └─────────────────────────────────────► time              │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.4 Production Architecture for 20M TVs

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    PRODUCTION ARCHITECTURE (20M TVs)                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  AWS REGION: us-east-1 (3 Availability Zones)                               │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                         INGESTION LAYER                              │   │
│  │                                                                      │   │
│  │  20M TVs ──► Network Load Balancer ──► Kafka Cluster                │   │
│  │              (handles 500K conn/sec)   (MSK or self-managed)        │   │
│  │                                                                      │   │
│  │  Kafka Cluster:                                                      │   │
│  │  • 6 brokers (i3.2xlarge: 8 vCPU, 61GB RAM, 1.9TB NVMe)            │   │
│  │  • 200 partitions on bt-events topic                                │   │
│  │  • Replication factor: 3                                            │   │
│  │  • Throughput: 500 MB/sec sustained                                 │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                         PROCESSING LAYER                             │   │
│  │                                                                      │   │
│  │  Flink Cluster (on EKS):                                            │   │
│  │  • 2 JobManagers (HA)                                               │   │
│  │  • 32 TaskManagers (c6i.2xlarge: 8 vCPU, 16GB RAM)                 │   │
│  │  • Parallelism: 200 (matches Kafka partitions)                      │   │
│  │  • Checkpointing: Every 60s to S3                                   │   │
│  │                                                                      │   │
│  │  Processing:                                                         │   │
│  │  • 333K events/sec ingested                                         │   │
│  │  • 5-minute tumbling windows                                        │   │
│  │  • Output: 66K aggregated rows/sec (20M TVs ÷ 300 sec)             │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                         STORAGE LAYER                                │   │
│  │                                                                      │   │
│  │  TimescaleDB (RDS or self-managed):                                 │   │
│  │  • Primary: r6g.2xlarge (8 vCPU, 64GB RAM)                         │   │
│  │  • 2 Read replicas for dashboard queries                            │   │
│  │  • Storage: 5TB gp3 (scales automatically)                          │   │
│  │  • Write throughput: 100K rows/sec with batching                    │   │
│  │                                                                      │   │
│  │  S3 (Raw Event Archive):                                            │   │
│  │  • 8.6 TB/day raw events                                            │   │
│  │  • Lifecycle: Standard → Glacier after 30 days                      │   │
│  │  • Enables replay and reprocessing                                  │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                         OBSERVABILITY                                │   │
│  │                                                                      │   │
│  │  • Prometheus + Grafana (metrics)                                   │   │
│  │  • CloudWatch (AWS metrics, logs)                                   │   │
│  │  • PagerDuty (alerting)                                             │   │
│  │  • Jaeger (distributed tracing)                                     │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  MONTHLY COST ESTIMATE:                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  EKS Control Plane:     $73                                         │   │
│  │  Kafka (6x i3.2xlarge): $5,400                                      │   │
│  │  Flink (32x c6i.2xlarge): $7,800                                    │   │
│  │  TimescaleDB:           $2,000                                      │   │
│  │  S3 (with Glacier):     $1,500                                      │   │
│  │  Network/Other:         $1,000                                      │   │
│  │  ─────────────────────────────                                      │   │
│  │  TOTAL:                 ~$18,000/month                              │   │
│  │                                                                      │   │
│  │  With Reserved Instances (1-year): ~$11,000/month                   │   │
│  │  With Spot for Flink:              ~$8,000/month                    │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```



---

## 5. Production Deployment Walkthrough

### 5.1 What We Did Step-by-Step

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    DEPLOYMENT STEPS COMPLETED                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  STEP 1: CREATE EKS CLUSTER (Terraform)                                     │
│  ═══════════════════════════════════════                                    │
│                                                                              │
│  Command: terraform apply                                                    │
│  Time: ~15 minutes                                                          │
│                                                                              │
│  What it created:                                                           │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  ✓ EKS Cluster (tv-telemetry)                                       │   │
│  │  ✓ Node Group (3x t3.small)                                         │   │
│  │  ✓ IAM Roles (cluster + node)                                       │   │
│  │  ✓ OIDC Provider (for IRSA)                                         │   │
│  │  ✓ ECR Repository (tv-bt-agent)                                     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 2: CONFIGURE KUBECTL                                                   │
│  ═════════════════════════                                                   │
│                                                                              │
│  Command: aws eks update-kubeconfig --name tv-telemetry --region us-east-1 │
│                                                                              │
│  This adds the EKS cluster to your ~/.kube/config file                     │
│  Now kubectl commands go to your AWS cluster                                │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 3: INSTALL EBS CSI DRIVER                                             │
│  ═══════════════════════════════                                            │
│                                                                              │
│  Commands:                                                                   │
│  1. eksctl utils associate-iam-oidc-provider --cluster tv-telemetry        │
│  2. eksctl create iamserviceaccount --name ebs-csi-controller-sa ...       │
│  3. eksctl create addon --name aws-ebs-csi-driver ...                      │
│                                                                              │
│  Why needed: Kubernetes needs this driver to create EBS volumes             │
│  for persistent storage (Kafka logs, TimescaleDB data)                     │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 4: CREATE NAMESPACE AND STORAGE CLASS                                  │
│  ═══════════════════════════════════════════                                │
│                                                                              │
│  Commands:                                                                   │
│  kubectl apply -f k8s/namespace.yaml                                        │
│  kubectl apply -f k8s/storage-class.yaml                                    │
│                                                                              │
│  Created:                                                                    │
│  • Namespace: telemetry (isolates our workloads)                           │
│  • StorageClass: gp3 (default for EBS volumes)                             │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 5: DEPLOY KAFKA (Strimzi Operator)                                    │
│  ═══════════════════════════════════════                                    │
│                                                                              │
│  Commands:                                                                   │
│  1. kubectl create -f https://strimzi.io/install/latest?namespace=telemetry│
│  2. kubectl apply -f k8s/kafka-strimzi.yaml                                │
│                                                                              │
│  What Strimzi does:                                                         │
│  • Deploys Kafka in KRaft mode (no Zookeeper)                              │
│  • Manages Kafka lifecycle (upgrades, scaling)                             │
│  • Creates topics via KafkaTopic CRD                                       │
│                                                                              │
│  Created:                                                                    │
│  • kafka-dual-role-0 (broker + controller)                                 │
│  • kafka-entity-operator (manages topics)                                  │
│  • Topics: bt-events (3 partitions), bt-alerts (1 partition)              │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 6: DEPLOY TIMESCALEDB                                                  │
│  ═══════════════════════════                                                │
│                                                                              │
│  Command: kubectl apply -f k8s/timescaledb.yaml                            │
│                                                                              │
│  Created:                                                                    │
│  • StatefulSet with 1 replica                                              │
│  • PersistentVolumeClaim (10GB gp3)                                        │
│  • ConfigMap with init SQL (creates tables)                                │
│  • Service for internal access                                             │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 7: DEPLOY FLINK                                                        │
│  ════════════════════                                                        │
│                                                                              │
│  Command: kubectl apply -f k8s/flink.yaml                                   │
│                                                                              │
│  Created:                                                                    │
│  • JobManager deployment (1 replica)                                       │
│  • TaskManager deployment (1 replica)                                      │
│  • Service for JobManager (port 8081)                                      │
│                                                                              │
│  Note: Flink job JAR not yet submitted                                     │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 8: PUSH TV AGENT IMAGE TO ECR                                         │
│  ═══════════════════════════════════                                        │
│                                                                              │
│  Commands:                                                                   │
│  1. aws ecr get-login-password | docker login --username AWS ...           │
│  2. docker tag tv-bt-agent:latest <account>.dkr.ecr.us-east-1.../tv-bt-agent│
│  3. docker push <account>.dkr.ecr.us-east-1.amazonaws.com/tv-bt-agent      │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 9: DEPLOY TV AGENTS                                                    │
│  ════════════════════════                                                    │
│                                                                              │
│  Command: kubectl apply -f k8s/tv-agent.yaml                                │
│                                                                              │
│  Created:                                                                    │
│  • Deployment with 3 replicas (scaled to 10)                               │
│  • Each pod runs tv-bt-agent container                                     │
│  • Connects to Kafka via service DNS                                       │
│                                                                              │
│  ════════════════════════════════════════════════════════════════════════   │
│                                                                              │
│  STEP 10: SCALE TV AGENTS                                                    │
│  ════════════════════════                                                    │
│                                                                              │
│  Command: kubectl scale deployment tv-agent -n telemetry --replicas=10     │
│                                                                              │
│  Result: 10 TV agent pods producing events to Kafka                        │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 Verification Commands

```bash
# Check all pods are running
kubectl get pods -n telemetry

# Expected output:
# NAME                                       READY   STATUS    RESTARTS   AGE
# flink-jobmanager-xxx                       1/1     Running   0          12m
# flink-taskmanager-xxx                      1/1     Running   0          10m
# kafka-dual-role-0                          1/1     Running   0          16h
# kafka-entity-operator-xxx                  1/1     Running   0          16h
# strimzi-cluster-operator-xxx               1/1     Running   0          17h
# timescaledb-0                              1/1     Running   0          80m
# tv-agent-xxx-1                             1/1     Running   0          5m
# tv-agent-xxx-2                             1/1     Running   0          5m
# ... (10 tv-agent pods)

# Check Kafka topics
kubectl exec kafka-dual-role-0 -n telemetry -- \
  /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --list

# Expected: bt-events, bt-alerts

# Check messages in Kafka
kubectl exec kafka-dual-role-0 -n telemetry -- \
  /opt/kafka/bin/kafka-get-offsets.sh --bootstrap-server localhost:9092 --topic bt-events

# Expected: bt-events:0:XXX (shows message count per partition)

# Check Kafka cluster status
kubectl get kafka -n telemetry

# Expected: kafka   True   ...   4.2.0
```

### 5.3 Key Files Created

```
k8s/
├── namespace.yaml          # telemetry namespace
├── storage-class.yaml      # gp3 EBS storage class
├── kafka-strimzi.yaml      # Kafka cluster + topics
├── timescaledb.yaml        # TimescaleDB StatefulSet
├── flink.yaml              # Flink JobManager + TaskManager
└── tv-agent.yaml           # TV Agent Deployment

terraform/eks/
└── main.tf                 # EKS cluster, node group, IAM, ECR
```

### 5.4 Current System Status

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    CURRENT DEPLOYMENT STATUS                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ✅ WORKING:                                                                 │
│  • EKS Cluster running (3 nodes)                                           │
│  • Kafka receiving events (400+ messages)                                  │
│  • TimescaleDB ready (tables created)                                      │
│  • Flink cluster running (no job yet)                                      │
│  • 10 TV agents producing events                                           │
│                                                                              │
│  ⏳ NOT YET DONE:                                                           │
│  • Flink job submission (would process events)                             │
│  • Grafana deployment (would visualize data)                               │
│  • Alerting setup (would notify on issues)                                 │
│                                                                              │
│  💰 COST:                                                                    │
│  • Running at ~$0.16/hour                                                  │
│  • Your $96 budget = ~600 hours = ~25 days                                 │
│                                                                              │
│  🛑 TO STOP COSTS:                                                          │
│  cd terraform/eks                                                           │
│  terraform destroy                                                          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Quick Reference

### 6.1 Essential kubectl Commands

```bash
# View resources
kubectl get pods -n telemetry              # List pods
kubectl get svc -n telemetry               # List services
kubectl get pvc -n telemetry               # List storage claims
kubectl get all -n telemetry               # List everything

# Debugging
kubectl describe pod <pod-name> -n telemetry    # Pod details
kubectl logs <pod-name> -n telemetry            # Pod logs
kubectl logs <pod-name> -n telemetry --previous # Previous crash logs
kubectl exec -it <pod-name> -n telemetry -- sh  # Shell into pod

# Scaling
kubectl scale deployment tv-agent -n telemetry --replicas=20

# Port forwarding (access services locally)
kubectl port-forward svc/flink-jobmanager -n telemetry 8081:8081
kubectl port-forward svc/timescaledb -n telemetry 5432:5432
```

### 6.2 Key Concepts Summary

| Concept | What It Does | Our Usage |
|---------|--------------|-----------|
| **Pod** | Runs containers | tv-agent, kafka, flink pods |
| **Deployment** | Manages pod replicas | tv-agent (10 replicas) |
| **StatefulSet** | Pods with stable identity | kafka, timescaledb |
| **Service** | Stable network endpoint | kafka-kafka-bootstrap |
| **PVC** | Persistent storage | kafka data, timescaledb data |
| **ConfigMap** | Configuration | timescaledb init SQL |
| **Namespace** | Resource isolation | telemetry |

### 6.3 Architecture Summary

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   TV Agents (10)  →  Kafka  →  Flink  →  TimescaleDB           │
│   (produce)          (buffer)  (process)  (store)               │
│                                                                  │
│   Current: Demo scale (10 agents, 1 Kafka broker)               │
│   Production: 20M TVs, 6 Kafka brokers, 32 Flink workers       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 7. Next Steps (Optional)

1. **Submit Flink Job**: Upload JAR and start processing
2. **Deploy Grafana**: Visualize metrics and data
3. **Add HPA**: Auto-scale based on load
4. **Production Hardening**: Multi-AZ, backups, monitoring

---

*This guide was created during the hands-on deployment of the TV Telemetry Analytics system to AWS EKS.*
