# Apache Flink: The Architecture of Infinite Streams

## Chapter 1: The Anatomy of the Runtime
A Flink cluster is not just a master and workers. It is a highly specialized distributed system designed for long-running stateful operators.

### 1. The JobManager Trinity
The "Master" is actually three distinct components working together:
*   **A. Dispatcher**: The entry point. It exposes the REST interface, accepts JobGraphs, and spins up a dedicated `JobMaster` for each job.
*   **B. ResourceManager**: The infrastructure liaison. It talks to K8s/YARN to request container slots. It doesn't care about "Jobs"; it cares about "Cpu/Ram".
*   **C. JobMaster**: The per-job brain. It holds the **Checkpoint Coordinator** (which triggers snapshots) and manages the execution graph.
    *   *Debug Note*: If checkpionts are failing, look at the JobMaster logs, not the Dispatcher.

### 2. The TaskManager: Network Stack
The TaskManager does the heavy lifting.
*   **TaskSlot**: A slice of resources (Fixed RAM). Unlike Spark Executors which share a heap, Flink TaskSlots enforce clearer boundaries.
*   **The Network Stack (Netty)**: This is where backpressure happens.
    *   **Credit-Based Flow Control**: A downstream operator (Consumer) must grant "credits" (buffer space) to the upstream operator (Producer) before data is sent.
    *   *Debug Note*: If a job stalls without CPU usage, it's likely a **Backpressure Deadlock**. The downstream is out of credits (stuck writing to DB), so the upstream stops reading from Kafka.

```mermaid
graph TD
    subgraph JobManager
        Disp["Dispatcher"] --> JM["JobMaster (Per Job)"]
        JM --> RM["ResourceManager"]
    end

    subgraph TaskManager
        Netty["Netty Network Stack"]
        Slot1["Task Slot 1"]
        Slot2["Task Slot 2"]
    end

    JM <-->|Checkpoint Barriers| Slot1
    Slot1 <-->|Credit-Based Transfer| Netty
    Netty <-->|Network| Slot2
```

---

## Chapter 2: The Relativity of Time (Event vs Processing)
In stream processing, time is fluid. A user in a subway tunnel generates an event at 12:00, but it arrives at 12:05.

**Apache Flink** implies strict correctness using **Watermarks**. A Watermark `W(T)` is a guarantee: *"I assert no events older than T will verify."*

```mermaid
graph LR
    subgraph Stream Flow
        E1[Event 12:01] --> E2[Event 12:02]
        E2 --> W1{{Watermark 12:01}}
        W1 --> E3[Event 12:03]
        E3 --> E4[Late Event 12:00]
    end
    
    style E4 fill:#ff9999
    style W1 fill:#99ccff
```

---

## Chapter 3: The Anatomy of State (RocksDB)
For production jobs, Flink embeds **RocksDB** on the local SSD.
1.  **MemTable**: In-memory write buffer.
2.  **SSTable**: Immutable disk file.
3.  **Compaction**: Merging SSTables in background.

**Key Architecture Note**: Because State is on Disk (Native Memory), Flink jobs are rarely heap-bound. They are **Disk I/O bound**. Monitoring IOPS is more important than monitoring GC.

---

## Chapter 4: The Promise of Correctness (Chandy-Lamport)
Flink guarantees exactly-once state using **Asynchronous Barrier Snapshots**.

```mermaid
sequenceDiagram
    participant Src as Source
    participant Op as Map/Process
    participant Sink as Sink
    participant Chk as Checkpoint Coordinator

    Chk->>Src: 1. Inject Barrier (ID=5)
    Note right of Src: Snapshot Offset
    Src->>Op: Forward Barrier
    
    Note right of Op: 2. Buffer Input<br/>Snapshot State
    Op->>Sink: Forward Barrier
    
    Note right of Sink: 3. Snapshot State<br/>Commit to External
    Sink-->>Chk: ACK Barrier 5
    
    Note over Chk: Global Checkpoint Complete
```

---

## Chapter 5: The Transactional Sink (2PC)
To guarantee Exactly-Once Output to Kafka:
1.  **Phase 1 (Pre-Commit)**: Flink writes "Uncommitted" messages to Kafka during processing.
2.  **Phase 2 (Commit)**: When the Global Checkpoint completes, Flink sends `commitTransaction()`.

---

## Chapter 6: The Control Panel (Configuration)

| Configuration | Recommendation | Why? |
| :--- | :--- | :--- |
| `state.backend` | `rocksdb` | Heap is dangerous for large state. |
| `execution.checkpointing.unaligned` | `true` | **Crucial**. Allows barriers to skip queued data, preventing timeouts during backpressure. |
| `taskmanager.memory.network.fraction` | `0.1` | Increase if backpressure errors occur. |

---

## Chapter 7: End-to-End Walkthrough: The Flow of the Stream

Let's trace a Flink Job from submission to processing to see how the "Trinity" and "Stack" interact.

### 1. Submission Phase
*   **User**: Runs `flink run -d my-job.jar`.
*   **Dispatcher**: Receives the `JobGraph`. Spins up a **JobMaster**.
*   **ResourceManager**: Asks K8s for Pods. Allocates **TaskSlots**.

### 2. Execution Phase
*   **JobMaster**: Deploys tasks to TaskSlots.
*   **TaskSlot**:
    *   Allocates Memory (Managed Memory & Network Buffers).
    *   Starts the Operator Chain: `Source -> Map -> Window -> Sink`.

### 3. The Processing Loop
*   **Source**: Reads from Kafka.
*   **Network Stack**:
    *   Serializes record into specific **Netty Buffers** (32KB chunks).
    *   **Credit Check**: Asks downstream: "Do you have space?"
    *   **Transfer**: If Yes, sends buffer. If No, waits (Backpressure).

---

## Chapter 8: Failure Scenarios (The Senior View)

### 1. Failure Scenario A: The Backpressure Deadlock
**Symptom**: Job is running (Green in UI), but Throughput is 0.
**Cause**: Downstream Sink (e.g., Postgres) is slow.
**Mechanism**:
1.  **Sink** fills its input buffers.
2.  **Netty** stops sending credits upstream.
3.  **Source** fills its output buffers and stops reading from Kafka.
**Visual**:
```mermaid
graph LR
    DB[("Slow DB")] 
    Sink["Sink Task"] --"Wait"--> DB
    Sink_Buf["Input Buffer Full"] -.-> Sink
    Source["Source Task"] --"No Credits"--> Sink_Buf
    Kafka(("Kafka")) --"Stops Reading"--> Source
    
    style Sink_Buf fill:#ff9999
    style DB fill:#ff9999
```

### 2. Failure Scenario B: The Barrier Alignment Timeout
**Symptom**: `CheckpointExpiredException`.
**Cause**: **Data Skew**. One operator instance is processing 90% of data.
**Mechanism**:
1.  **Checkpoint Coordinator** sends Barrier ID=5.
2.  **Fast Task** processes Barrier 5 and waits.
3.  **Slow Task** (Skewed) has 1GB of data *before* Barrier 5 in its queue.
4.  **Result**: Fast Task waits for minutes. Checkpoint times out before Slow Task sees Barrier 5.
**Fix**: Use `execution.checkpointing.unaligned = true` to let Barriers jump the queue.
