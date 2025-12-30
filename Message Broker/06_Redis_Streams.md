# 06. Redis Streams: The "Mini-Kafka" in Memory

## 1. Introduction
Launched in Redis 5.0, **Redis Streams** bridges the gap between the "Fire-and-Forget" nature of Pub/Sub and the heavy persistent log of Kafka.

It provides a **Persistent, Append-Only Log** with **Consumer Groups**, but runs entirely in memory (with disk persistence via RDB/AOF). It is the perfect choice for high-throughput event sourcing where you don't want the operational complexity of Zookeeper/Kafka.

---

## 2. Core Architecture

Unlike a standard Redis `List` (Linked List) or `ZSET` (Skip List), Streams use a **Radix Tree (specifically Rax)** for memory efficiency.

### The Stream Structure

```mermaid
graph LR
    Producer[Producer] -->|XADD stream:orders *| Stream["Stream: orders"]
    
    subgraph Stream["Stream (Radix Tree)"]
        E1["Entry 1<br/>ID: 1700000000000-0"]
        E2["Entry 2<br/>ID: 1700000000001-0"]
        E3["Entry 3<br/>ID: 1700000000002-0"]
    end
    
    subgraph CG["Consumer Group: billing"]
        C1[Consumer A]
        C2[Consumer B]
    end
    
    Stream --> CG
    
    style Stream fill:#ccffcc
```

### Key Components
1.  **Stream**: An append-only log, identified by a key (e.g., `events:orders`).
2.  **Entry**: A message with:
    *   **ID**: Auto-generated timestamp-sequence (e.g., `1526985054069-0`).
    *   **Fields**: Key-value pairs (e.g., `amount=50, user=123`).
3.  **Consumer Group**: A named set of consumers that divide work (like Kafka consumer groups).
4.  **PEL (Pending Entries List)**: Tracks messages that have been delivered but not acknowledged.

---

## 3. How It Works: Entry IDs and Time

### The Entry ID Format
Every entry has a unique ID: `<millisecond-timestamp>-<sequence>`

**Example**: `1526985054069-0`
*   **Top Part**: Millisecond timestamp (1526985054069).
*   **Bottom Part**: Sequence number (0) for events in the same millisecond.

**Implication**: The ID *is* the time. You can query "Give me all events from 5 minutes ago" without indexing a separate timestamp field.

```
XRANGE events:pay 1526985000000 1526985054069  # Get events in time range
```

---

## 4. Deep Dive: Internal Implementation

### A. Why Radix Trees? (Memory Efficiency)

Redis Streams use **Radix Trees (Rax)** instead of linked lists for memory density.

#### Memory Comparison

```mermaid
graph TD
    subgraph ListBased["Standard List (RPUSH/LPOP)"]
        L1["Node 1<br/>(40 bytes overhead)"] --> L2["Node 2<br/>(40 bytes overhead)"]
        L2 --> L3["Node 3<br/>(40 bytes overhead)"]
        L3 --> L4["..."]
    end
    
    subgraph RadixBased["Stream (Radix Tree)"]
        R1["Macro Node<br/>(Packs 100 entries)"]
        R2["Macro Node<br/>(Packs 100 entries)"]
        R1 --> R2
    end
    
    style ListBased fill:#ffcccc
    style RadixBased fill:#ccffcc
    
    Note1["List: 10,000 entries = 400KB overhead"]
    Note2["Radix: 10,000 entries = ~50KB overhead"]
```

**Key Difference**: 
*   **List**: Each entry is a separate heap object with pointers (prev/next).
*   **Radix Tree**: Entries are serialized into contiguous memory blocks. Redis reads chunks, not individual nodes.
*   **Result**: Streams can hold **10x more data** in the same RAM.

### B. The Radix Tree Structure

The Radix Tree (Rax) is a **compressed prefix tree**:
*   **Keys**: Entry IDs (e.g., `1700000000000-0`).
*   **Values**: Pointers to serialized field-value pairs.
*   **Compression**: Common prefixes are compressed (e.g., if 1000 entries start with `1700000000`, that prefix is stored once).

```c
// Simplified internal structure
typedef struct stream {
    rax *rax;           // Radix tree of entry IDs
    // ...
    streamCG *cgroups;  // Linked list of consumer groups
} stream;
```

### C. Consumer Groups vs Kafka

#### Kafka: The "Offset" Model
*   **State**: Stored as a single integer (Offset 500).
*   **Meaning**: "I have processed everything up to 500."
*   **Weakness**: If Consumer A crashes at offset 502, the whole group is blocked or messages are lost.

#### Redis Streams: The "PEL" (Pending Entries List) Model
*   **State**: Stored as a discrete list of **unacknowledged message IDs** for *each* consumer.
*   **Meaning**: "I have read IDs [1, 5, 7], but I haven't ACKed them yet."
*   **Benefit**: Allows for non-sequential processing and individual message re-delivery.

```mermaid
graph TD
    subgraph Stream["Stream: events:orders"]
        E1["ID: 100-0"]
        E2["ID: 100-1"]
        E3["ID: 100-2"]
    end

    subgraph CG["Consumer Group: billing"]
        C1["Consumer A"]
        C2["Consumer B"]
    end
    
    subgraph PEL["Pending Entries List (PEL)"]
        L1["Entry 100-1<br/>(Owner: A, Idle: 5s)"]
        L2["Entry 100-2<br/>(Owner: B, Idle: 0s)"]
    end

    E1 -->|ACKed| Done["Removed from PEL"]
    E2 -->|Reading| C1
    E3 -->|Reading| C2
    C1 -.->|Tracks| L1
    C2 -.->|Tracks| L2
```

**PEL Structure**:
```c
typedef struct streamCG {
    streamID last_id;       // Last delivered ID
    rax *pel;               // Pending Entries List (ID -> nack info)
    rax *consumers;         // Consumer name -> streamConsumer
} streamCG;
```

---

## 5. End-to-End Walkthrough: Life of a Stream Event

Let's trace a payment event through the complete lifecycle.

### Step 1: Production (`XADD`)
*   **Publisher**: `XADD events:pay * amount 50 user 123`
*   **Redis**:
    1.  Generates ID `1700000000000-0` (current timestamp + sequence).
    2.  Serializes fields (`amount=50, user=123`) into a listpack.
    3.  Inserts into Radix Tree.
    *   **Complexity**: $O(\log N)$ where $N$ is number of entries.

```c
// Pseudo-code
raxInsert(stream->rax, id, serialized_entry, NULL);
```

### Step 2: Consumption (`XREADGROUP`)
*   **Consumer A**: `XREADGROUP GROUP billing ConsumerA COUNT 1 STREAMS events:pay >`
    *   The `>` means "Give me new messages I haven't seen."
*   **Redis**:
    1.  Looks up consumer group `billing`.
    2.  Finds next entry after `last_id` in the Radix Tree.
    3.  **Crucial Step**: Adds this ID to Consumer A's **PEL** with timestamp.
    4.  Returns the entry to Consumer A.
*   **State**: Message is now "delivered" but "unacknowledged".

```c
// Pseudo-code
raxInsert(group->pel, entry_id, nack_info, NULL);  // Add to PEL
consumerA->pel_count++;
```

### Step 3: Acknowledgment (`XACK`)
*   **Consumer A**: Successfully processes the payment.
*   **Action**: `XACK events:pay billing 1700000000000-0`
*   **Redis**:
    1.  Removes the ID from the PEL.
    2.  Decrements Consumer A's pending count.
*   **State**: Message is now complete.

```c
// Pseudo-code
raxRemove(group->pel, entry_id, NULL);  // Remove from PEL
consumerA->pel_count--;
```

---

## 6. Failure Scenarios (The Senior View)

### Scenario A: The Crashed Consumer (Stuck in PEL)
**Event**: Consumer A reads payment event (ID `170...0`) and crashes *before* sending XACK.

**Problem**:
*   The message sits in the PEL forever.
*   New consumers (Consumer B) asking for `>` will *never* see it because Redis thinks A is working on it.
*   **Symptom**: "Missing Data" or "Stuck Job".

```mermaid
sequenceDiagram
    participant S as Stream
    participant C1 as Consumer A
    participant PEL as PEL
    
    C1->>S: XREADGROUP (">")
    S->>PEL: Add Entry 500-0 (Owner: A)
    S-->>C1: Return Entry 500-0
    
    rect rgb(255, 200, 200)
        Note over C1: CRASHES (No XACK)
    end
    
    Note right of PEL: Entry 500-0<br/>Owner: A<br/>Idle: Forever
```

### Scenario B: Recovery (`XAUTOCLAIM`)

This is the equivalent of Kafka's Rebalance, but manual or polled.

**The Fix**: Running a "Garbage Collector" process.

1.  **Inspect**: `XPENDING events:pay billing`
    *   Shows: `"ID 170...0, Consumer: A, Idle: 60 minutes"`.

2.  **Claim**: `XAUTOCLAIM events:pay billing ConsumerB 30000 0-0 COUNT 10`
    *   **Logic**: "If any message has been idle for > 30s, change ownership to ME (Consumer B)."
    *   **COUNT 10**: Claim up to 10 messages at once.

3.  **Process**: Consumer B processes it and sends `XACK`.

```mermaid
sequenceDiagram
    participant S as Stream
    participant C1 as Consumer A (Crashed)
    participant C2 as Consumer B (Recovery)
    participant PEL as PEL
    
    C1->>S: XREADGROUP ("Give me work")
    S->>PEL: Add Entry 500-0 (Owner: A)
    S-->>C1: Event 500-0
    
    rect rgb(255, 200, 200)
        Note over C1: CRASHES (No ACK)
    end
    
    Note right of PEL: Entry 500-0 (Idle: 10m)
    
    C2->>S: XAUTOCLAIM (Idle > 5m)
    S->>PEL: Change Owner: A → B
    S-->>C2: Event 500-0
    
    C2->>C2: Process Event
    C2->>S: XACK 500-0
    S->>PEL: Remove Entry 500-0
    Note right of PEL: Cleared!
```

**Internal Mechanism**:
```c
// XAUTOCLAIM pseudo-logic
for each entry in PEL {
    if (entry.idle_time > min_idle_time) {
        entry.owner = new_consumer;
        entry.delivery_count++;
        entry.last_delivery_time = now();
        return entry;
    }
}
```

---

## 7. Scaling Considerations

### A. Partitioning (Sharding by Key)
Unlike Pub/Sub, Streams **DO** shard by key in Redis Cluster:
*   **Example**: `events:user:123` and `events:user:456` might be on different nodes.
*   **Benefit**: You can scale horizontally by adding more cluster nodes.

### B. Memory Management (The Capping Problem)
Streams grow indefinitely if not capped.

**Solution**: Use `MAXLEN` to cap stream size:
```
XADD events:pay MAXLEN ~ 1000000 * amount 50 user 123
```
*   **`~` (Approximate)**: Allows Redis to delete in chunks (more efficient than exact).
*   **`1000000`**: Keep approximately 1 million entries.

**Alternative**: `XTRIM` to trim old entries:
```
XTRIM events:pay MAXLEN ~ 1000000
```

---

## 8. Constraints & Comparison

| Feature | Redis Streams | Apache Kafka | RabbitMQ |
| :--- | :--- | :--- | :--- |
| **Storage** | RAM (limited by memory) | Disk (limited by drive size) | RAM/Disk |
| **Throughput** | ~1M ops/sec (In-Memory) | ~500k ops/sec (Disk I/O) | ~50k ops/sec |
| **Latency** | Sub-millisecond | ~2-5ms | ~1ms |
| **Persistence** | Snapshot (RDB) / Append (AOF) | Commit Log (Very durable) | Queue Persistence |
| **Consumer Groups** | Yes (PEL-based) | Yes (Offset-based) | No (Work Queue) |
| **Best For** | Real-time jobs, cache invalidation | Event sourcing, data lakes | Complex routing |

**Key Limitation**: 
*   **RAM Bound**: If your stream is 100GB, you need 100GB+ of RAM.
*   **No Tiered Storage**: Unlike Kafka (which can offload to S3), Redis Streams stay in memory.

---

## 9. When to Use Redis Streams?

| Use Case | Verdict | Why? |
| :--- | :--- | :--- |
| **Job Queues** | **YES** | Better than RPUSH/LPOP due to Consumer Groups and ACKs. |
| **Event Sourcing (Small Scale)** | **YES** | If total events fit in RAM (< 100GB). |
| **Real-time ML Feature Updates** | **YES** | Fast in-memory reads for feature stores. |
| **Large-Scale Event Sourcing** | **NO** | Use Kafka. Redis can't handle TB-scale logs. |
| **Financial Transactions** | **MAYBE** | Enable AOF for durability, but Kafka is safer. |

---

## 10. Production Checklist

1.  [ ] **Capping**: Always use `XADD ... MAXLEN ~ 1000000` to prevent infinite memory growth (OOM).
2.  [ ] **Recovery Sidecar**: Implement a process that runs `XAUTOCLAIM` every 30s to clean up dead consumers.
3.  [ ] **Persistence**: Enable AOF (`appendonly yes`) if data loss on restart is unacceptable.
4.  [ ] **Monitor PEL Size**: Use `XPENDING` to check if PEL is growing (indicates stuck consumers).
5.  [ ] **Consumer Naming**: Use unique consumer names (e.g., `hostname-pid`) to avoid conflicts.
6.  [ ] **Idempotency**: Design consumers to handle duplicate processing (PEL reclaim can cause re-delivery).
