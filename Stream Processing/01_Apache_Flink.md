# Apache Flink: Stream Processing with Exactly-Once Guarantees

## 1. Introduction

**Apache Flink** is an open-source, distributed stream processing framework designed for **stateful computations over unbounded and bounded data streams**. Unlike Spark (batch-first with streaming as an afterthought), Flink is **stream-native from the ground up**, treating batch as a special case of streaming.

### The Problem: Batch is Too Slow

**Real-Time Requirements** emerged across industries:
- **Fraud detection**: Catch fraudulent transactions within milliseconds, not hours
- **Recommendations**: Update user profiles based on clicks in real-time
- **Monitoring**: Alert on anomalies within seconds of occurrence
- **Pricing**: Adjust ride-sharing prices based on current demand

**Batch Processing Limitations**:
- **Latency**: Hadoop jobs take minutes to hours
- **Freshness**: Data is stale by the time results arrive
- **Use Case Mismatch**: Event-driven applications need continuous processing

### The Evolution: From Unreliable to Exactly-Once

The history of stream processing is defined by one challenge: **Correctness under failure**.

---

### Generation 1: Apache Storm (2011) - Speed Without Guarantees

**Nathan Marz at Twitter** created the first widely-adopted stream processor.

**Architecture**:
- **Spouts**: Data sources
- **Bolts**: Processing operators
- **Topology**: DAG of spouts and bolts

**Key Limitation**: **At-least-once** processing only
- On failure, replay records from source
- No state checkpointing
- Result: Duplicate processing, incorrect aggregations

**The Lambda Architecture Hack** (2011-2014):
Because Storm couldn't guarantee correctness, teams ran **two pipelines**:

```mermaid
graph LR
    Source[Data Source] -->|Real-time| Storm["Speed Layer: Storm"]
    Source -->|Batch| Hadoop["Batch Layer: Hadoop"]
    
    Storm -->|99% accurate<br/>100ms latency| Serving[Serving DB]
    Hadoop -->|100% accurate<br/>24h latency| Serving
    
    Serving --> User[User Query]
    
    style Storm fill:#ff9999
    style Hadoop fill:#99ff99
```

**The Problem**: Violates DRY (Don't Repeat Yourself)
1. **Logic Divergence**: Write `calculate_revenue()` twice (Java for Storm, SQL for Hive)
2. **Operational Nightmare**: Maintain two clusters, two failure modes
3. **Eventually Consistent**: Speed layer gradually corrected by batch layer

**Why It Failed**:
- Teams spent 60% of time reconciling differences between layers
- Bugs in one layer often missed in the other
- Complexity killed agility

---

### Generation 2: Spark Streaming (2013) - Micro-Batch Compromise

**Berkeley AMPLab** added streaming to Spark via **micro-batching**.

**Mechanism**: Chop stream into tiny batches (e.g., 500ms), run batch job on each

**Advantages**:
- **Exactly-once**: Each micro-batch is transactional
- **Unified API**: Same code for batch and streaming
- **Leverage Spark**: Reuse batch infrastructure

**Fatal Flaw: Latency Floor**
```
Latency >= Micro-batch interval + Scheduler overhead
Typical: 500ms + 500ms = 1 second minimum
```

**Why It's Not True Streaming**:
- Must wait for entire micro-batch to complete before processing
- Scheduler overhead dominates at small batch sizes
- Backpressure is reactive (slow down source after memory fills)

**Use Cases Where It's Acceptable**:
- Simple ETL (5-second latency OK)
- Aggregations over windows (e.g., "count clicks per 5 minutes")
- Analytics dashboards

**Use Cases Where It Fails**:
- Fraud detection (need <100ms)
- Alerting (need real-time)
- CEP (complex event patterns require stateful processing)

---

### Generation 3: Apache Flink (2014) - True Streaming Solution

**TU Berlin's Stratosphere Project** (2010-2014) became Apache Flink.

**Core Innovations**:

**1. True Streaming** (not micro-batch)

Unlike Spark's micro-batch approach (which processes data in 500ms chunks), Flink uses **long-running operators** that continuously pass messages between each other. This means:
- **No artificial batching**: Events processed individually as they arrive
- **Ultra-low latency**: 1-100ms end-to-end (only network overhead)
- **No scheduler delay**: Operators stay running; no JVM startup for each batch
- **Continuous computation**: Like a pipeline where data flows continuously, not in chunks

**Why it matters**: Fraud detection needs decisions in <100ms, impossible with 500ms+ micro-batches.

---

**2. Exactly-Once via Chandy-Lamport Snapshots**

Flink achieves **exactly-once processing** without pausing the data stream. The mechanism:
- **Distributed snapshots**: Takes a consistent snapshot of entire job state while processing continues
- **Checkpoint barriers**: Special markers flow with data to coordinate snapshots across operators
- **Durable storage**: State saved to S3/HDFS (survives machine failures)
- **Failure recovery**: On crash, restore state from last successful checkpoint and replay from that point

**Why it matters**: Guarantees no data loss and no duplicates, even when machines fail mid-processing. Critical for financial transactions where "exactly once" is legally required.

---

**3. Advanced State Management**

Flink can maintain **massive amounts of state** (terabytes) efficiently:
- **Embedded RocksDB**: Each operator has local LSM-tree database stored on SSD (not RAM)
- **Off-heap storage**: State lives outside JVM to avoid garbage collection pauses
- **TB-scale per operator**: Single operator can manage terabytes of state (user sessions, aggregations)
- **Queryable state**: External systems can query running job's state via REST API (e.g., "What's the current count for user123?")
- **Incremental checkpoints**: Only save changed data, not entire state (100GB state → 5GB checkpoint)

**Why it matters**: Enables stateful computations like "count all events per user in last 30 days" without external databases. State co-located with computation = fast lookups.

---

**4. Event-Time Processing**

Flink processes events based on **when they happened** (event time), not when they arrived (processing time). This handles:
- **Out-of-order events**: Mobile app generates event at 10:00 AM, arrives at 10:05 AM due to network delay
- **Watermarks**: Flink tracks "event time progress" using watermarks (guarantees like "no events before 10:00 AM will arrive")
- **Late event buffering**: Holds events temporarily to wait for stragglers
- **Correct window results**: 5-minute window from 10:00-10:05 includes all events with timestamps in that range, regardless of arrival time

**Why it matters**: Getting accurate "clicks in last hour" when events arrive out of order. Without event-time, results would be wrong.

---

**5. Credit-Based Backpressure**

Flink prevents memory overflows using a **flow control** system:
- **Credit system**: Downstream operators advertise "I have 10 free buffers" to upstream
- **Sender respects credits**: Upstream only sends data if downstream has space
- **Natural throttling**: When downstream is slow, upstream automatically slows down (no manual tuning)
- **Prevents OOM**: Can't overwhelm operators with more data than they can handle
- **Propagates to source**: Backpressure flows all the way to Kafka source (stops reading when system full)

**Why it matters**: **Reactive** backpressure (Spark) waits for OOM, then crashes. **Proactive** backpressure (Flink) prevents crashes before they happen.

---

### The Kappa Architecture: Streaming Unification

Flink (+ Kafka's log retention) enabled **Kappa Architecture**:

**Premise**: "The batch layer is redundant if the stream layer is correct."

```mermaid
graph LR
    Source[Data Source] --> Kafka[Kafka Log<br/>30-day retention]
    Kafka --> Flink["Streaming Engine: Flink"]
    Flink --> Serving[Serving DB]
    
    Kafka -.->|Replay from offset 0<br/>for code changes| Flink
    
    style Flink fill:#99ff99
```

**How It Works**:
1. **Normal Operation**: Process stream in real-time
2. **Code Change**: Deploy new version, replay Kafka from beginning
3. **Result**: Single codebase, single cluster, 100% accuracy

**Why Kafka is Critical**:
- 30-day retention = "replayable batch"
- Offset tracking = "bookmark" for where you are
- Partitioning = parallelism

---

### Why Flink Won

| Aspect | Storm | Spark Streaming | Flink |
|:-------|:------|:----------------|:------|
| **Guarantees** | At-least-once | Exactly-once (micro-batch) | Exactly-once (true streaming) |
| **Latency** | 10-100ms | 500ms-5s | 1-100ms |
| **State** | None (external only) | Limited (in-memory) | Advanced (RocksDB, TB-scale) |
| **Backpressure** | Manual throttling | Reactive | Credit-based (proactive) |
| **Event Time** | Not supported | Added later | Native from start |
| **Complexity** | Low (simple API) | Medium (batch API) | High (powerful but steep learning curve) |
| **Use Case** | Deprecated | Simple ETL | Mission-critical streaming |

---

### Key Differentiator

Flink's **Chandy-Lamport distributed snapshots** enable exactly-once processing without sacrificing throughput. The **credit-based backpressure** system prevents memory explosions, and **unaligned checkpoints** allow sub-second checkpoint completion even under heavy load.

### Industry Adoption

- **Alibaba**: Processes 4+ trillion events/day for real-time recommendations
- **Uber**: Real-time pricing, fraud detection, trip monitoring
- **Netflix**: Real-time quality-of-experience monitoring
- **LinkedIn**: Metrics computation, abuse detection
- **ByteDance (TikTok)**: Real-time content recommendation
- **Airbnb**: Real-time pricing and fraud detection

### Historical Timeline

- **2010**: Stratosphere research project begins at TU Berlin
- **2014**: Apache incubation as "Flink"
- **2015**: Top-level Apache project
- **2016**: Flink 1.0 - Core APIs stabilized
- **2017**: Queryable state, incremental checkpoints
- **2019**: Flink 1.9 - Fine-grained recovery
- **2020**: Flink 1.10 - Python support (PyFlink)
- **2022**: Flink 1.15 - Unified batch/streaming
- **2024**: Flink 1.18+ - Adaptive scheduler, speculative execution

### Current Version Features (Flink 1.18+)

- **Adaptive batch scheduler**: Dynamic resource allocation for batch jobs
- **Fine-grained resource management**: Per-operator memory/CPU limits
- **Speculative execution**: Retry slow tasks on different workers
- **PyFlink maturity**: Production-ready Python API
- **Kubernetes-native**: First-class K8s integration

---

## 2. Core Architecture

Flink follows a **master-worker** architecture with a unique "Trinity" structure for job management.

```mermaid
graph TD
    subgraph JobManager["JobManager (Master)"]
        Disp["Dispatcher"] --> JM["JobMaster (Per Job)"]
        JM --> RM["ResourceManager"]
    end

    subgraph TaskManager1["TaskManager 1"]
        Netty1["Netty Network Stack"]
        Slot1["Task Slot 1"]
        Slot2["Task Slot 2"]
    end
    
    subgraph TaskManager2["TaskManager 2"]
        Netty2["Netty Network Stack"]
        Slot3["Task Slot 3"]
        Slot4["Task Slot 4"]
    end

    JM <-->|"Checkpoint Barriers"| Slot1
    JM <-->|"Checkpoint Barriers"| Slot3
    Slot1 <-->|"Credit-Based Transfer"| Netty1
    Slot3 <-->|"Credit-Based Transfer"| Netty2
    Netty1 <-->|Network| Netty2
```

### Key Components

**1. JobManager Trinity (Control Plane)**:

**Dispatcher**:
- REST API entry point (`http://jobmanager:8081`)
- Accepts job submissions
- Spins up dedicated JobMaster per job
- Manages job lifecycle (start, cancel, stop)

**JobMaster** (Per-job brain):
- **Checkpoint Coordinator**: Triggers distributed snapshots every N seconds
- Execution graph management
- Failure recovery coordination
- **Debug Note**: Checkpoint failures? Check JobMaster logs, not Dispatcher

**ResourceManager**:
- Infrastructure liaison (talks to Kubernetes, YARN, Mesos)
- Requests/releases TaskManager containers
- Doesn't understand jobs, only resources (CPU, RAM)

**2. TaskManager (Data Plane)**:

**Task Slots**:
- Fixed resource allocation (e.g., 4GB RAM, 2 CPU cores)
- Unlike Spark executors (shared heap), Flink slots have clear boundaries
- Multiple tasks can run in one slot via **operator chaining**

**Network Stack (Netty)**:
- **Credit-based flow control**: Downstream grants "credits" (buffer space) to upstream
- Prevents buffer overflow and OOM
- 32KB buffer size (default)

**3. State Backend (RocksDB)**:
- Embedded LSM-tree database
- **Off-heap**: Lives outside JVM to avoid GC pauses
- **Disk-backed**: Can hold TB-scale state on SSD
- **Incremental checkpoints**: Only snapshot changed data

---

## 3. How It Works: Time, State, and Watermarks

### A. Event Time vs Processing Time

**Challenge**: Events arrive out-of-order

```
Real-world timeline:
  User in Tokyo tunnel: Generates event at 12:00
  Network delay: Event arrives at Flink at 12:05
  
Processing time: 12:05 (when Flink sees it)
Event time: 12:00 (when it actually happened)
```

**Flink's Solution: Watermarks**

A watermark `W(T)` is a guarantee: **"No events older than T will arrive"**

```mermaid
graph LR
    subgraph StreamFlow["Stream Flow"]
        E1["Event 12:01"] --> E2["Event 12:02"]
        E2 --> W1{{"Watermark 12:01"}}
        W1 --> E3["Event 12:03"]
        E3 --> E4["Late Event 12:00"]
    end
    
    style E4 fill:#ff9999
    style W1 fill:#99ccff
```

**How to Configure**:
1. Choose **out-of-order tolerance** (e.g., 10 seconds)
2. Flink extracts timestamp from each event
3. Watermark advances: `current_max_timestamp - tolerance`
4. When watermark reaches window end → trigger computation

**Example Configuration** (Java):
```java
WatermarkStrategy<Event> strategy = WatermarkStrategy
    .forBoundedOutOfOrderness(Duration.ofSeconds(10))
    .withTimestampAssigner((event, ts) -> event.getTimestamp());
```

**Meaning**: Allow 10 seconds of out-of-order arrival. After seeing event at T=100, emit watermark W(90) = "No events before T=90 will arrive"

---

### B. Stateful Operations (The Power of Flink)

**State Types**:

**1. Keyed State** (per key):

Flink maintains state **per key** automatically. Example: Counting events per user.

**How It Works**:
1. Flink routes events by key to same operator instance
2. Operator declares state variable (e.g., `ValueState<Long> count`)
3. On each event:
   - Read current value from state: `long current = count.value()`
   - Update: `count.update(current + 1)`
   - State persisted to RocksDB (off-heap)

**State Varieties**:
- `ValueState<T>`: Single value per key (e.g., counter, last seen timestamp)
- `ListState<T>`: List of values per key (e.g., recent events buffer)
- `MapState<K,V>`: Key-value map per key (e.g., session attributes)

**2. Operator State** (non-keyed):
- **Broadcast State**: Same state replicated across all parallel instances
- **List State**: Redistributable list elements (for source offset tracking)

**Storage: RocksDB Architecture**:
```
TaskManager Memory Allocation:
├─ JVM Heap: 4 GB
│   └─ User functions, operators, control plane
├─ Off-Heap (Managed): 8 GB
│   └─ RocksDB state backend (LSM tree on SSD)
└─ Off-Heap (Network): 2 GB
    └─ Network buffers for shuffle
```

**Why Off-Heap**: Avoids JVM garbage collection pauses, can exceed heap size

---

## 4. Deep Dive: Internal Mechanisms

### A. Checkpoint Mechanism (Chandy-Lamport Algorithm)

**Goal**: Distributed snapshot without pausing the stream

**Algorithm**:
```
1. Checkpoint Coordinator (JobMaster): "Time for checkpoint!"
2. Inject barrier into source operators
3. Barrier flows downstream with data
4. When operator sees barrier on ALL inputs:
   - Snapshot state
   - Forward barrier downstream
5. Sink acknowledges completion
6. Global checkpoint committed
```

**Detailed Flow**:

```mermaid
sequenceDiagram
    participant Chk as Checkpoint Coordinator
    participant Src as Source
    participant Op as Operator
    participant Sink as Sink
    
    Chk->>Src: "1. Inject Barrier (ID=5)"
    Note right of Src: "Snapshot Offset (Kafka partition 0, offset 1000)"
    Src->>Op: "Forward Barrier"
    
    Note right of Op: "2. Buffer Input, Snapshot State (aggregate: count=42)"
    Op->>Sink: "Forward Barrier"
    
    Note right of Sink: "3. Snapshot State, Pre-commit transaction"
    Sink-->>Chk: "ACK Barrier 5"
    
    Note over Chk: "Global Checkpoint Complete"
```

**State Storage**:
```
Checkpoint 5:
  s3://bucket/checkpoints/5/
    ├─ _metadata (coordinatorstate, operator list)
    ├─ source-0/state (Kafka offset: 1000)
    ├─ operator-1/state (RocksDB snapshot: 500 MB)
    └─ sink-2/state (Pre-committed Kafka transaction ID)
```

---

### B. Exactly-Once Sink (2-Phase Commit)

**Challenge**: How to guarantee exactly-once writes to external systems (Kafka, databases)?

**Solution**: Two-Phase Commit (2PC) protocol coordinated with checkpoints

**Phase 1: Pre-commit** (during processing):
1. Sink receives records from operator
2. Opens transaction in external system (e.g., Kafka transaction)
3. Writes records as "uncommitted" (invisible to consumers)
4. Accumulates writes until checkpoint barrier arrives

**Phase 2: Commit** (on checkpoint complete):
1. Checkpoint Coordinator confirms: "Checkpoint 5 SUCCESS"
2. Notifies all sinks via RPC
3. Each sink commits its transaction
4. Records now visible atomically to external consumers

**Failure Handling**:
- **If checkpoint fails**: 
  - Sink aborts transaction
  - Uncommitted writes discarded
  - Flink restarts from last successful checkpoint
  - Re-processes and re-writes same records (idempotent)

**Configuration** (enable exactly-once):
```java
FlinkKafkaProducer.Semantic.EXACTLY_ONCE  // 2PC enabled
```

**Transaction Timeout**: Must exceed checkpoint interval (e.g., 15 minutes for 60s checkpoints)

---

### C. Credit-Based Backpressure

**Problem**: Fast upstream overwhelms slow downstream

**Flink's Solution**: Credit system

**Mechanism**:
```
Downstream (Sink):
  - Has 10 input buffers (32KB each)
  - Grants credits to upstream: "I have 10 free buffers"

Upstream (Source):
  - Sends data only if it has credits
  - Decrements credit counter per buffer sent

When credits = 0:
  - Upstream blocks
  - Source stops reading from Kafka (backpressure propagation)
```

**Monitoring**:
```
Flink Web UI → Job → Tasks → Backpressure
  Status: OK (green) | Low (yellow) | High (red)
```

---

## 5. End-to-End Walkthrough: Stream Processing Flow

**Scenario**: Real-time fraud detection on credit card transactions

### Job Submission

```bash
flink run -d \
  --parallelism 16 \
  --jobmanager-memory 2G \
  --taskmanager-memory 8G \
  fraud-detection.jar
```

### Step 1: Job Deployment (t=0s)

```
Dispatcher: Receives job JAR
  → Creates JobMaster
  → ResourceManager: Requests 4 TaskManagers from Kubernetes
  → K8s: Allocates 4 pods (8GB RAM each)
  → JobMaster: Deploys operators to Task Slots
```

### Step 2: Stream Processing Loop (ongoing)

```
Source Operator (Kafka):
  t=0.001s: Read transaction event
  {user: "alice", amount: 5000, card: "1234", time: 1704400000000}

Map Operator (Feature Extraction):
  t=0.002s: Enrich with user history
  {user: "alice", amount: 5000, avg_30d: 200, score: 0.95}

Keyed State (Fraud Model):
  t=0.003s: Lookup user state (RocksDB off-heap)
  State: {alice: {tx_count_1h: 3, total_amount_1h: 15000}}
  
  Rule: If tx_count_1h > 5 or amount > avg_30d * 10:
    Flag as fraud

Window Aggregation (1-hour tumbling):
  t=0.004s: Increment counters
  Update state: {alice: {tx_count_1h: 4, total_amount_1h: 20000}}

Sink Operator (Alert to Kafka):
  t=0.005s: Write alert to Kafka (uncommitted)
  Pre-commit transaction

Total latency: 5ms (sub-second!)
```

### Step 3: Checkpointing (every 60s)

```
t=60s: Checkpoint Coordinator triggers checkpoint #5

Source:
  Snapshot: Kafka offset = partition 0: offset 120000
  Forward barrier

Operator:
  Snapshot: RocksDB state (alice: {tx_count_1h: 4, ...})
  Write to S3: s3://checkpoints/5/operator-1/state (500 MB)
  Forward barrier

Sink:
  Pre-commit Kafka transaction
  Acknowledge checkpoint

t=65s: Global checkpoint complete
  → Commit all Kafka transactions
  → Alerts now visible to consumers

Checkpoint duration: 5 seconds
```

### Step 4: Failure Recovery (TaskManager crashes)

```
t=120s: TaskManager 2 crashes (OOM, spot instance loss)

JobMaster detects failure:
  → Cancel all tasks
  → Reload state from checkpoint #5 (S3)
  → Rewind Kafka offsets to checkpoint #5 position
  → Resume processing

Recovery time: 30 seconds
  (Checkpoint #5 to #7 = 2 minutes of reprocessing)
```

---

## 6. Failure Scenarios (The Senior View)

### Scenario A: Backpressure Deadlock

**Symptom**: Job running (green UI), but throughput = 0 records/sec, Kafka lag growing

**Cause**: Downstream sink (e.g., PostgreSQL) is slow (100ms/write)

#### The Mechanism

**Credit-based flow control prevents OOM, but can cause deadlock**:

```mermaid
graph LR
    DB[("Slow DB (100ms/write)")] 
    Sink["Sink Task (Input Buffer Full)"] --"Wait"--> DB
    Sink_Buf["Input Buffer (0 credits available)"] -.-> Sink
    Source["Source Task (Reading Kafka)"] --"No Credits BLOCKED"--> Sink_Buf
    Kafka(("Kafka")) --"Stops Reading (Lag grows)"--> Source
    
    style Sink_Buf fill:#ff9999
    style DB fill:#ff9999
```

**Timeline**:
```
t=0: Sink writes to DB at 100ms/record
t=1s: Upstream sends 10 records (fills all 10 buffers)
t=1s: Sink buffers = FULL → credits = 0
t=1.1s: Upstream waits for credits (blocked)
t=1.2s: Source stops reading Kafka (backpressure propagates)
Result: Kafka lag grows, but job appears "healthy" (no errors)
```

#### The Fix

**Option 1: Scale Sink Parallelism**
- Increase sink operator parallelism: `sink.setParallelism(10)` (was 1)
- Distributes load across 10 instances
- Each handles 10% of traffic → 10× throughput

**Option 2: Async I/O** (Recommended)
- Use non-blocking database writes
- Allow 100 concurrent requests per operator
- Prevents blocking while waiting for DB response
- **Result**: 100× higher throughput (100ms/write but 100 concurrent = 1000 writes/sec)

**Option 3: Increase Network Buffers**
- Configuration:
  ```yaml
  taskmanager.memory.network.fraction: 0.2  # 20% for network (was 10%)
  taskmanager.network.numberOfBuffers: 4096  # More buffers
  ```
- More buffers = more tolerance for slow downstream

---

### Scenario B: Barrier Alignment Timeout

**Symptom**: `CheckpointExpiredException: Checkpoint 123 expired before completing`

**Cause**: Data skew - one parallel instance processing 90% of data

#### The Mechanism

**Chandy-Lamport requires barrier alignment across all inputs**:

```mermaid
sequenceDiagram
    participant Coord as Checkpoint Coordinator
    participant Fast as "Fast Task (10 rec/sec)"
    participant Slow as "Slow Task (1 rec/sec) [Skew]"
    
    Coord->>Fast: "Barrier ID=5"
    Coord->>Slow: "Barrier ID=5"
    
    rect rgb(200, 255, 200)
        Note over Fast: "Processes 10 records, Sees Barrier 5 (1 sec)"
        Fast->>Fast: "Snapshot State"
        Fast->>Coord: "ACK Barrier 5"
    end
    
    rect rgb(255, 200, 200)
        Note over Slow: "Processing 100 skewed records before Barrier 5 (100 seconds!)"
        Note over Coord: "Timeout after 60s, Checkpoint FAILED"
    end
```

**Problem**:
```
Checkpoint config: timeout = 60 seconds
Fast task: Processes 10 records → barrier (1s)
Slow task: Has 100 skewed records queued → barrier (100s)
Result: Checkpoint fails, recovery window grows
```

#### The Fix

**Option 1: Enable Unaligned Checkpoints** (Critical!)
- Configuration: `execution.checkpointing.unaligned: true`
- **How it works**:
  1. Barrier "jumps ahead" of queued data in buffers
  2. Snapshots include in-flight buffered records
  3. Checkpoint completes in milliseconds (doesn't wait for slow task)
- **Trade-off**: Larger checkpoint size (includes buffered data)

**Option 2: Fix Data Skew (Key Salting)**
- **Problem**: 99% of data has same key (e.g., `user_id="bot_123"`)
- **Solution**: Add random suffix to distribute across subtasks
  - Before: `keyBy(event -> event.getUserId())` → all to 1 subtask
  - After: `keyBy(event -> event.getUserId() + "_" + random(0-9))` → spread across 10 subtasks

**Option 3: Increase Checkpoint Timeout**
- Configuration: `execution.checkpointing.timeout: 600000` (10 min, was 60s)
- **When to use**: Temporary fix while investigating skew root cause

---

### Scenario C: State Backend OOM

**Symptom**: TaskManager killed (OOM), `java.lang.OutOfMemoryError`

**Cause**: RocksDB state grows unbounded, no TTL configured

#### The Mechanism

```
User session state:
  Key: user_id
  Value: {cart: [...], last_activity: timestamp}

Problem:
  - Store session for every user ever
  - No cleanup for inactive users (90 days old)
  - State grows: 1 GB → 10 GB → 100 GB → OOM

RocksDB uses disk, but index/bloom filters in RAM
  → RAM exhausted → OOM
```

#### The Fix

**Option 1: Configure State TTL**:
```java
// Java
StateTtlConfig ttlConfig = StateTtlConfig
    .newBuilder(Time.days(30))
    .setUpdateType(StateTtlConfig.UpdateType.OnCreateAndWrite)
    .setStateVisibility(StateTtlConfig.StateVisibility.NeverReturnExpired)
    .cleanupFullSnapshot()
    .build();

ValueStateDescriptor<Session> descriptor = new ValueStateDescriptor<>("session", Session.class);
descriptor.enableTimeToLive(ttlConfig);
```

```scala
// Scala
val ttlConfig = StateTtlConfig
  .newBuilder(Time.days(30))
  .setUpdateType(UpdateType.OnCreateAndWrite)
  .setStateVisibility(StateVisibility.NeverReturnExpired)
  .cleanupFullSnapshot()
  .build()

val descriptor = new ValueStateDescriptor[Session]("session", classOf[Session])
descriptor.enableTimeToLive(ttlConfig)
```

**Option 2: Increase RocksDB Memory**:
```yaml
taskmanager.memory.managed.fraction: 0.5  # 50% of total memory for RocksDB
state.backend.rocksdb.memory.managed: true
```

**Option 3: Enable Compaction Filters**:
```yaml
state.backend.rocksdb.ttl.compaction.filter.enabled: true
```

---

### Scenario D: Exactly-Once Sink Timeout

**Symptom**: `TransactionTimeoutException` on Kafka sink

**Cause**: 2PC transaction timeout (default 15min) exceeded during checkpoint

#### The Mechanism

```
Checkpoint triggers:
  t=0: Sink pre-commits Kafka transaction
  t=0-900s: Waiting for global checkpoint
  t=901s: Kafka aborts transaction (transaction.timeout.ms=900000)
  t=920s: Checkpoint completes, tries to commit → FAIL
```

#### The Fix

**Option 1: Increase Kafka Transaction Timeout**
- Configuration: `transaction.timeout.ms=3600000` (1 hour, was 15 min)
- **Why**: Kafka aborts transactions exceeding timeout
- Must exceed: checkpoint interval + checkpoint duration + buffer

**Option 2: Reduce Checkpoint Interval**
- Configuration: `execution.checkpointing.interval=30000` (30s, was 60s)
- Shorter interval = faster commits = less likely to timeout
- **Trade-off**: More frequent checkpoints = 2× overhead

**Option 3: Pre-aggregate Before Sink**
- Aggregate in Flink before writing to Kafka
- Example: Write per-minute aggregates instead of raw events
- **Result**: 60× fewer writes, 60× shorter transaction duration

---

## 7. Performance Tuning / Scaling Strategies

### Configuration Table

| Configuration | Recommendation | Why? |
|:--------------|:---------------|:-----|
| `state.backend` | rocksdb | Heap dangerous for large state (GC issues) |
| `execution.checkpointing.unaligned` | true | **CRITICAL**: Prevents timeout under backpressure |
| `execution.checkpointing.interval` | 60000 (60s) | Balance recovery time vs overhead |
| `execution.checkpointing.timeout` | 600000 (10min) | Prevent timeout under high load |
| `execution.checkpointing.max-concurrent-checkpoints` | 1 | Prevent checkpoint storms |
| `taskmanager.memory.network.fraction` | 0.1-0.2 | Increase for backpressure tolerance |
| `taskmanager.memory.managed.fraction` | 0.4-0.5 | RocksDB off-heap memory allocation |
| `state.checkpoints.dir` | s3://bucket/chk | Distributed storage (NOT local disk) |
| `state.backend.rocksdb.memory.managed` | true | Use Flink's managed memory for RocksDB |
| `state.backend.rocksdb.ttl.compaction.filter.enabled` | true | Cleanup expired state during compaction |
| `parallelism.default` | Match Kafka partitions | Maximize parallelism |
| `taskmanager.numberOfTaskSlots` | 2-4 | Balance: too high = resource contention |

### Scaling Strategies

**1. Horizontal Scaling (Add TaskManagers)**:
- Add more TaskManager pods/instances
- Example: Scale from 4 to 8 TaskManagers
- **Result**: 2× CPU capacity, 2× parallel task slots
- **When**: CPU-bound workloads, need more parallelism

**2. Vertical Scaling (Increase Resources)**:
- Increase memory/CPU per TaskManager
- Example: 8GB → 16GB RAM per TaskManager
- **Result**: Handle larger state per instance, fewer checkpoints
- **When**: Memory-bound (large state), avoid too many small instances

**3. State Sharding (Increase Parallelism)**:
- Increase job parallelism (redistribute keys across more subtasks)
- Example: Parallelism 8 → 16
  - Before: 10GB state per instance × 8 = 80GB total
  - After: 5GB state per instance × 16 = 80GB total
- **Benefit**: Faster checkpoints (smaller snapshots per instance)
- **Limit**: Cannot exceed Kafka partition count

**4. Operator Chaining Optimization**:
- Disable chaining for CPU-heavy operators to spread load
- Configuration: `.disableChaining()` on heavy map/filter
- **Trade-off**: More network overhead, better parallelism
```

---

## 8. Constraints & Limitations

| Constraint | Limit | Why? |
|:-----------|:------|:-----|
| **Checkpoint size** | < 100GB recommended | Large checkpoints slow recovery (30s → 5min) |
| **State size** | TB-scale with RocksDB | Limited by disk, not RAM (SSD recommended) |
| **Parallelism max** | 1000s (practical: 100-500) | Too high = coordination overhead, small tasks |
| **Watermark latency** | Seconds to minutes | Depends on event-time skew, out-of-order arrival |
| **Exactly-once overhead** | 10-30% throughput reduction | 2PC commits add latency vs at-least-once |
| **Backpressure propagation** | Milliseconds | Credit-based system responds fast |
| **Network buffer size** | 32KB fixed | Not configurable per-task |
| **TaskSlot isolation** | Memory only, shares CPU | Not container-level isolation |
| **Checkpoint alignment** | Can cause timeouts | Use unaligned checkpoints for > 1min skew |
| **Savepoint compatibility** | Between minor versions only | 1.15 → 1.16 OK, 1.15 → 2.0 may break |

### Why Not Flink?

| Use Case | Better Alternative | Reason |
|:---------|:-------------------|:-------|
| **Batch ETL only** | Apache Spark | More mature batch ecosystem, SQL integration |
| **< 1GB/day throughput** | Kafka Streams | Simpler deployment (library, not cluster) |
| **Simple map/filter** | Kafka Streams/KSQL | No need for Flink's complexity |
| **Sub-millisecond latency** | Custom system | Flink's 1-100ms latency may not suffice |
| **Read-only dashboards** | Presto/Trino | Better for ad-hoc queries |

---

## 9. When to Use Flink?

| Use Case | Verdict | Why? |
|:---------|:--------|:-----|
| **Real-time analytics** | **YES** ✅ | Sub-second latency, event-time windows, exactly-once |
| **Fraud detection** | **YES** ✅ | Stateful pattern matching (CEP), complex rules |
| **Stream joins** | **YES** ✅ | More mature than Spark (interval joins, temporal joins) |
| **IoT data processing** | **YES** ✅ | TB-scale state, watermarks for sensor skew |
| **Change data capture (CDC)** | **YES** ✅ | Exactly-once writes, stateful transformations |
| **Batch ETL** | **MAYBE** ⚠️ | Flink can do batch, but Spark ecosystem more mature |
| **Small jobs (< 1GB/day)** | **NO** ❌ | Overhead not worth it; use Kafka Streams |
| **Simple aggregations** | **NO** ❌ | ksqlDB or Kafka Streams simpler |

### Flink vs Alternatives

| Aspect | Flink | Spark Streaming | Kafka Streams |
|:-------|:------|:----------------|:--------------|
| **Latency** | 1-100ms | 500ms-5s | 1-10ms |
| **State** | Advanced (RocksDB, queryable) | Limited (memory) | RocksDB |
| **Exactly-once** | Native (Chandy-Lamport) | Micro-batch | Native (changelog) |
| **Deployment** | Cluster (complex) | Cluster | Library (simple) |
| **SQL support** | Mature (Flink SQL) | Very mature (Spark SQL) | Limited (ksqlDB) |
| **Backpressure** | Credit-based (built-in) | Reactive throttle | Manual |
| **Use case** | Complex CEP, low-latency | Batch + streaming hybrid | Simple transforms |
| **Operational complexity** | High (cluster management) | High | Low (embedded) |

---

## 10. Production Checklist

1. [ ] **Enable checkpointing**: `execution.checkpointing.interval=60000` (60s)
2. [ ] **Use RocksDB state backend**: `state.backend=rocksdb`
3. [ ] **Enable unaligned checkpoints**: `execution.checkpointing.unaligned=true` (CRITICAL)
4. [ ] **Configure distributed storage**: `state.checkpoints.dir=s3://bucket/checkpoints` (not local)
5. [ ] **Set state TTL**: Configure TTL for non-permanent state (30-90 days typical)
6. [ ] **Configure managed memory**: `taskmanager.memory.managed.fraction=0.4` (40% for RocksDB)
7. [ ] **Set checkpoint timeout**: `execution.checkpointing.timeout=600000` (10 min)
8. [ ] **Match Kafka parallelism**: `parallelism = number of Kafka partitions`
9. [ ] **Enable metrics**: Integrate Prometheus for monitoring
10. [ ] **Configure failure recovery**: `restart-strategy: fixed-delay` with attempts=3
11. [ ] **Test failure recovery**: Kill TaskManager pod, verify checkpoint recovery
12. [ ] **Set up alerting**: Checkpoint failures, Kafka lag, backpressure, state growth

### Critical Metrics

```
checkpoint_duration_milliseconds:
  Description: Time to complete distributed snapshot
  Target: < 60 seconds (95th percentile)
  Why it matters: Long checkpoints = wide recovery window, more data reprocessed on failure
  Fix: Enable unaligned checkpoints, reduce state size with TTL, use faster storage (S3 vs HDFS)

checkpoint_failure_count_total:
  Description: Count of failed checkpoint attempts
  Target: < 1% of total checkpoints
  Why it matters: Failed checkpoints = no recovery points, wider blast radius on failures
  Fix: Increase checkpoint timeout, fix data skew, verify S3 connectivity

kafka_consumer_lag_messages:
  Description: Number of unprocessed messages in Kafka
  Target: < 10,000 messages per partition
  Why it matters: Growing lag = processing slower than input rate, backpressure active
  Fix: Scale parallelism, optimize sink (async I/O), increase TaskManager count

backpressure_time_percentage:
  Description: Percentage of time tasks are blocked waiting for credits
  Target: < 5%
  Why it matters: Sustained backpressure = sink bottleneck, throughput degradation
  Fix:Identify slow operator (Flink Web UI), use async I/O for external calls, scale sink parallelism

task_failure_count_total:
  Description: Count of individual task failures (before job restart)
  Target: < 0.1% of total tasks
  Why it matters: High failure rate = instability, resource contention, or bugs
  Fix: Check logs for OOM/exceptions, increase memory, fix code bugs

state_size_bytes:
  Description: Total state backend size (RocksDB on disk)
  Target: Monitor growth rate (should be bounded)
  Why it matters: Unbounded growth = missing TTL, eventual OOM or slow checkpoints
  Fix: Configure state TTL (30-90 days typical), investigate state accumulation bugs

throughput_records_per_second:
  Description: Records processed per second (baseline for regression detection)
  Target: Establish baseline, alert on >20% drop
  Why it matters: Sudden drop = backpressure, failures, data skew, or resource exhaustion
  Fix: Investigate backpressure, check for failures, verify resource allocation

gc_time_percentage:
  Description: Percentage of time spent in JVM garbage collection
  Target: < 10% (off-heap RocksDB should minimize GC)
  Why it matters: High GC = heap pressure, performance degradation
  Fix: RocksDB state backend uses off-heap (verify config), increase managed memory fraction
```

---

**Remember**: Flink's power comes from its **Chandy-Lamport distributed snapshots** (exactly-once without stopping the stream), **credit-based backpressure** (preventing OOM), and **unaligned checkpoints** (sub-second checkpoints under load). Mastering state management, watermarks, and failure recovery unlocks truly reliable stream processing at scale.
