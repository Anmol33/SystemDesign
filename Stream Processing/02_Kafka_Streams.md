# 02. Kafka Streams: Stream Processing Without a Cluster

## 1. Introduction

**Kafka Streams** is a client library for building stream processing applications on top of Apache Kafka. Unlike Flink (which requires a separate cluster), Kafka Streams is **embedded directly into your application** - it's just a JAR dependency.

### The Problem: Flink is Overkill for Simple Use Cases

**Complexity Spectrum**:
- **Simple**: Transform Kafka topic A → topic B (map, filter)
- **Medium**: Windowed aggregations, joins between topics
- **Complex**: CEP, ML inference, multi-source joins

**Flink Overhead**:
- Deploy and manage separate cluster (JobManager, TaskManagers)
- Learn complex API (DataStream, ProcessFunction)
- Monitor additional infrastructure
- **Total Cost**: 3-5 engineers to maintain Flink cluster

**Kafka Streams Simplicity**:
- Add one JAR to your existing microservice
- No cluster to manage (scales with your app instances)
- Exactly-once semantics out of the box
- **Total Cost**: 0 additional infrastructure

### Historical Context

**Timeline**:
- **2016**: Kafka Streams 0.10 released (Confluent)
- **2017**: Production-ready (exactly-once semantics added)
- **2018**: Interactive queries (queryable state stores)
- **2020**: Improved DSL, better windowing
- **2024**: Kafka 3.6+ with enhanced exactly-once performance

**Why It Was Created**:
- Many Kafka users only needed simple transformations
- Running Flink/Storm for `map()` felt wasteful
- Tight Kafka integration enables optimizations impossible in Flink

### Key Differentiators

- **No Cluster**: Runs embedded in your application (just a library)
- **Exactly-Once by Default**: Kafka transactions ensure no duplicates
- **Elastic**: Add app instances = automatic rebalancing
- **Stateful**: Local RocksDB stores for aggregations, joins
- **Interactive Queries**: Query running application's state via REST API

### When to Use

**Kafka Streams** (library):
- Source and sink are Kafka topics
- Transformations are simple (map, filter, aggregate)
- Want to avoid managing separate cluster
- Team knows Java/Scala (or Python via ksqlDB)

**Apache Flink** (cluster):
- Multiple sources (Kafka + databases + files)
- Complex CEP patterns
- ML inference requiring GPU
- Sub-10ms latency requirements
- Need SQL interface without ksqlDB

### Industry Adoption

- **Pinterest**: Real-time spam detection
- **Zalando**: Fashion recommendations
- **New York Times**: Real-time analytics
- **Rabobank**: Fraud detection
- **Line Corp**: Messaging analytics

---

## 2. Core Architecture

Kafka Streams applications are **distributed** but **decentralized** - no master node.

```mermaid
graph TD
    subgraph Kafka_Cluster["Kafka Cluster"]
        T1["Topic: clicks (P0, P1, P2)"]
        T2["Topic: aggregated (P0, P1, P2)"]
    end
    
    subgraph App_Instance_1["App Instance 1"]
        KS1["Kafka Streams<br/>(P0, P1)"]
        Store1[("RocksDB<br/>State Store")]
        KS1 -.-> Store1
    end
    
    subgraph App_Instance_2["App Instance 2"]
        KS2["Kafka Streams<br/>(P2)"]
        Store2[("RocksDB<br/>State Store")]
        KS2 -.-> Store2
    end
    
    T1 -->|Consume| KS1
    T1 -->|Consume| KS2
    KS1 -->|Produce| T2
    KS2 -->|Produce| T2
    
    KS1 <-.->|Rebalance Protocol| KS2
```

### Key Components

1. **Stream**: Unbounded sequence of records from Kafka topic
2. **KStream**: Record stream (every event is independent)
3. **KTable**: Changelog stream (latest value per key)
4. **GlobalKTable**: Fully replicated table (all partitions on all instances)
5. **State Store**: Local RocksDB database for stateful operations
6. **Processor Topology**: DAG of stream transformations

### No Master Node

Unlike Flink (JobManager), Kafka Streams uses **consumer group protocol**:
- All instances join same consumer group
- Kafka coordinator assigns partitions automatically
- Rebalance when instance added/removed
- **Result**: Self-managing, no SPOF

---

## 3. How It Works: The Lifecycle

### Application Startup

**Step 1: Topology Definition** (compile time)
```java
StreamsBuilder builder = new StreamsBuilder();
KStream<String, String> clicks = builder.stream("clicks");
KTable<String, Long> counts = clicks
    .groupByKey()
    .count(Materialized.as("counts-store"));
counts.toStream().to("aggregated");
```

**Step 2: Streams Instance Creation**
```java
Properties props = new Properties();
props.put(StreamsConfig.APPLICATION_ID_CONFIG, "click-counter");
props.put(StreamsConfig.BOOTSTRAP_SERVERS_CONFIG, "kafka:9092");

KafkaStreams streams = new KafkaStreams(builder.build(), props);
streams.start(); // Non-blocking
```

**Step 3: Partition Assignment**
- Joins consumer group `click-counter`
- Kafka assigns topic partitions (e.g., P0, P1 to Instance 1)
- Creates local state stores for assigned partitions

**Step 4: Processing Loop**
- Poll records from assigned partitions
- Apply transformations
- Write to state store (if stateful)
- Produce output to Kafka
- Commit transaction (exactly-once)

### Exactly-Once Semantics

**Mechanism**: Kafka transactions (since 0.11)

```
1. Begin transaction
2. Read N records from input topic
3. Process transformations
4. Write to state store (RocksDB)
5. Produce output records
6. Commit transaction:
   - Output records visible atomically
   - Consumer offset committed
   - State store checkpoint
```

**Guarantee**: Either all succeed or all rollback (no partial results)

### Rebalancing

**Trigger**: App instance added/removed/crashed

**Process**:
1. Pause processing
2. Kafka reassigns partitions
3. New owner restores state from changelog topic
4. Resume processing

**Restoration**:
- Each state store has changelog topic in Kafka
- New partition owner replays changelog to rebuild RocksDB
- Can take minutes for large state (GB)
- **Optimization**: Standby replicas (keep state warm)

---

## 4. Deep Dive: State Management

### State Stores

Kafka Streams provides **local state stores** backed by RocksDB.

**Types**:
1. **Key-Value Store**: `counts.put("user123", 42L)`
2. **Window Store**: `clicks.put(window(2024-01-05 10:00), "user123", 5)`
3. **Session Store**: Variable-length windows based on inactivity

**Storage**:
```
/tmp/kafka-streams/
  click-counter/           # Application ID
    0_0/                   # Partition 0, Task 0
      rocksdb/
        counts-store/      # State store name
          000001.sst       # RocksDB SSTable files
          MANIFEST
```

### Changelog Topics

**Purpose**: Durability + restoration after rebalance

**Mechanism**:
```
State Store: counts-store
Changelog Topic: click-counter-counts-store-changelog

Every put() to RocksDB:
  1. Write to local RocksDB
  2. Send to changelog topic
  3. Both succeed or transaction aborts
```

**Compaction**: Changelog topics are **log-compacted**
- Only latest value per key retained
- Enables fast restoration (don't replay full history)
- Cleanup policy: `cleanup.policy=compact`

### Interactive Queries

**Problem**: State is local to each instance - how to query?

**Solution**: Key-value lookup API

```java
// In your Kafka Streams app
ReadOnlyKeyValueStore<String, Long> store = 
    streams.store(StoreQueryParameters.fromNameAndType(
        "counts-store", QueryableStoreTypes.keyValueStore()));

Long count = store.get("user123"); // Read local state
```

**REST Endpoint**:
```java
@GetMapping("/count/{userId}")
public Long getCount(@PathVariable String userId) {
    // Lookup which instance owns this key
    KeyQueryMetadata metadata = streams.queryMetadataForKey(
        "counts-store", userId, Serdes.String().serializer());
    
    if (metadata.activeHost().equals(thisHost)) {
        return localStore.get(userId); // Local read
    } else {
        return httpClient.get(metadata.activeHost(), "/count/" + userId); 
        // Proxy to correct instance
    }
}
```

---

## 5. End-to-End Walkthrough: Click Aggregation

**Scenario**: Count user clicks per 5-minute window

### Java Code

```java
public class ClickAggregationApp {
    public static void main(String[] args) {
        // 1. Topology Definition
        StreamsBuilder builder = new StreamsBuilder();
        
        // 2. Read stream
        KStream<String, ClickEvent> clicks = builder.stream(
            "user-clicks",
            Consumed.with(Serdes.String(), clickEventSerde())
        );
        
        // 3. Window and aggregate
        KTable<Windowed<String>, Long> windowedCounts = clicks
            .groupByKey()
            .windowedBy(TimeWindows.ofSizeWithNoGrace(Duration.ofMinutes(5)))
            .count(Materialized.as("click-counts-store"));
        
        // 4. Write output
        windowedCounts
            .toStream()
            .map((k, v) -> KeyValue.pair(
                k.key() + "@" + k.window().start(), 
                v
            ))
            .to("aggregated-clicks", Produced.with(Serdes.String(), Serdes.Long()));
        
        // 5. Configure
        Properties props = new Properties();
        props.put(StreamsConfig.APPLICATION_ID_CONFIG, "click-aggregator");
        props.put(StreamsConfig.BOOTSTRAP_SERVERS_CONFIG, "localhost:9092");
        props.put(StreamsConfig.PROCESSING_GUARANTEE_CONFIG, 
                  StreamsConfig.EXACTLY_ONCE_V2); // Enable exactly-once
        
        // 6. Start
        KafkaStreams streams = new KafkaStreams(builder.build(), props);
        streams.start();
        
        // 7. Shutdown hook
        Runtime.getRuntime().addShutdownHook(new Thread(streams::close));
    }
}
```

### Execution Timeline

**t=0s: Application Start**
- Join consumer group `click-aggregator`
- Assigned partitions: [0, 1, 2]
- Restore state from changelog topics (if exists)

**t=10s: Processing**
```
Record 1: key="user123", value={url="/home", timestamp=10:00:00}
  → Window: [10:00:00 - 10:05:00]
  → State: click-counts-store["user123@10:00:00"] = 1
  → Output: ("user123@10:00:00", 1)

Record 2: key="user123", value={url="/profile", timestamp=10:01:30}
  → Same window: [10:00:00 - 10:05:00]
  → State: click-counts-store["user123@10:00:00"] = 2
  → Output: ("user123@10:00:00", 2)  // Updated count
```

**t=5m: Window Close**
- Window [10:00:00 - 10:05:00] complete
- Final count emitted
- State retained for grace period (if configured)

---

## 6. Failure Scenarios

### Scenario A: Instance Crash

**Symptom**: App crashes mid-processing

**Mechanism**:
1. Consumer group detects missing heartbeat (30s default)
2. Kafka triggers rebalance
3. Surviving instances claim orphaned partitions
4. Restore state from changelog topic
5. Resume from last committed offset

**Exactly-Once Guarantee**: Transaction uncommitted → rolled back, no duplicates

### Scenario B: Slow Restoration

**Symptom**: Rebalance takes 10+ minutes due to large state

**Cause**: New partition owner must replay entire changelog (100GB+)

**Fix**:
```java
props.put(StreamsConfig.NUM_STANDBY_REPLICAS_CONFIG, 1);
```
- Maintains hot standby replicas on other instances
- Standby already has state → instant takeover
- Trade-off: 2× memory usage

### Scenario C: Kafka Unavailable

**Symptom**: Kafka cluster unreachable

**Behavior**:
- Streams app retries indefinitely (configurable)
- Processing paused
- State not lost (local RocksDB intact)
- Resumes when Kafka returns

---

## 7. Performance Tuning

| Configuration | Recommended | Purpose |
|:--------------|:------------|:--------|
| `num.stream.threads` | 2-4 per instance | Parallelism within instance |
| `commit.interval.ms` | 1000 (1s) | Transaction frequency (latency vs throughput) |
| `cache.max.bytes.buffering` | 10MB | In-memory cache before RocksDB write |
| `num.standby.replicas` | 1 | Fast rebalancing for stateful apps |
| `rocksdb.block.cache.size` | 50MB per store | RocksDB read cache |

### Scalability

**Horizontal Scaling**:
- Add app instances → Kafka automatically rebalances
- Max parallelism = input topic partition count
- Example: 12 partitions = max 12 app instances

**Optimization**:
- Use`repartition()` to increase parallelism mid-topology
- Avoid `GlobalKTable` for large datasets (full replication)

---

## 8. Constraints & Limitations

| Constraint | Limit | Why? |
|:-----------|:------|:-----|
| **Kafka Only** | Cannot read from databases, files | Tied to Kafka ecosystem |
| **Java/Scala** | No native Python (use ksqlDB or PyFlink) | JVM library |
| **State Size** | Limited by disk on instance | RocksDB local storage |
| **Latency** | 10-100ms typical | Transaction commit overhead |
| **Single Cluster** | One Kafka cluster per app | No multi-cluster joins |

---

## 9. When to Use Kafka Streams?

| Use Case | Verdict | Why? |
|:---------|:--------|:-----|
| **Kafka → Transform → Kafka** | ✅ **YES** | Perfect fit, no cluster needed |
| **Stateless ETL** | ✅ **YES** | Simple map/filter operations |
| **Windowed Aggregations** | ✅ **YES** | Built-in windowing support |
| **Stream-Table Joins** | ✅ **YES** | `KStream.join(KTable)` optimized |
| **Multiple Sources (DB + Kafka)** | ❌ **NO** | Use Flink (supports JDBC, files, etc.) |
| **Complex CEP** | ❌ **NO** | Use Flink (richer pattern matching) |
| **Sub-10ms Latency** | ❌ **NO** | Transaction commits add ~10ms overhead |
| **Team Prefers SQL** | ⚠️ **MAYBE** | Use ksqlDB (Kafka Streams under the hood) |

---

## 10. Production Checklist

1. [ ] **Enable Exactly-Once**: Set `processing.guarantee=exactly_once_v2`
2. [ ] **Configure Standby Replicas**: Set `num.standby.replicas=1` for stateful apps
3. [ ] **Tune Commit Interval**: Balance latency (`500ms`) vs throughput (`5000ms`)
4. [ ] **Monitor Lag**: Track consumer lag per partition
5. [ ] **Disk Space**: Allocate 2× expected state size for RocksDB + changelog
6. [ ] **Graceful Shutdown**: Use shutdown hooks to close streams cleanly
7. [ ] **Changelog Compaction**: Verify changelog topics have `cleanup.policy=compact`
8. [ ] **Partition Count**: Ensure input topics have enough partitions for scaling

**Critical Metrics**:

```
kafka_streams_commit_latency_avg:
  Description: Average transaction commit time
  Target: < 100ms
  Alert: if > 500ms
  Why: High commit latency = pipeline backup

kafka_streams_restore_time_total:
  Description: Time to restore state after rebalance
  Target: < 60s
  Alert: if > 300s
  Why: Long restoration = app unavailable
  Fix: Enable standby replicas

kafka_streams_state_store_size_bytes:
  Description: Local RocksDB size per store
  Target: Monitor growth trend
  Alert: if > 80% of allocated disk
  Why: Out of disk = crash

kafka_streams_failed_stream_threads:
  Description: Count of stream threads that died
  Target: 0
  Alert: if > 0
  Why: Indicates bugs or resource exhaustion
```
