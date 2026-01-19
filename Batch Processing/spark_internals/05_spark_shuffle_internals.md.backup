# Spark Shuffle Internals: Complete Guide

> **All information verified against Spark source code**

## Table of Contents

1. [What is Shuffle?](#what-is-shuffle)
2. [Fundamentals: BlockId & File Naming](#fundamentals-blockid--file-naming)
3. [Complete Shuffle Write Flow](#complete-shuffle-write-flow)
4. [Complete Shuffle Read Flow](#complete-shuffle-read-flow)
5. [Component Reference](#component-reference)
6. [File Format Details](#file-format-details)
7. [Critical Understanding: One RDD, Two Shuffles?](#critical-understanding-one-rdd-two-shuffles)

---

## What is Shuffle?

**Shuffle** redistributes data across partitions so records with the same key end up together.

### Triggers

Wide transformations: `groupByKey()`, `reduceByKey()`, `join()`, `repartition()`

### Cost

1. **Disk I/O**: Write in Stage N, read in Stage N+1
2. **Network I/O**: Transfer across executors  
3. **Serialization**: Object ↔ bytes conversion
4. **Sorting**: By partition ID (+ optional key sorting)

---

## Fundamentals: BlockId & File Naming

### BlockId Hierarchy

**BlockId** uniquely identifies ANY data stored by BlockManager.

**Source**: [core/src/main/scala/org/apache/spark/storage/BlockId.scala](file:///Users/anmol.shrivastava/spark-code/spark/core/src/main/scala/org/apache/spark/storage/BlockId.scala)

| BlockId Type | Format | Example | Purpose |
|--------------|--------|---------|---------|
| RDDBlockId | `rdd_<rddId>_<partitionId>` | `rdd_0_5` | Cached RDD partition |
| **ShuffleBlockId** | `shuffle_<shuffleId>_<mapId>_<reduceId>` | `shuffle_0_1_2` | Shuffle block reference |
| **ShuffleDataBlockId** | `shuffle_<shuffleId>_<mapId>_<reduceId>.data` | `shuffle_0_0_0.data` | Shuffle data file |
| **ShuffleIndexBlockId** | `shuffle_<shuffleId>_<mapId>_<reduceId>.index` | `shuffle_0_0_0.index` | Shuffle index file |
| BroadcastBlockId | `broadcast_<id>` | `broadcast_42` | Broadcast variable |

###  Shuffle File Naming

```
shuffle_<shuffleId>_<mapId>_<reduceId>.data
        │           │        │
        │           │        └─ NOOP_REDUCE_ID (always 0)
        │           └────────── Map task ID
        └────────────────────── Shuffle operation ID
```

**Why reduceId = 0?**  
From `IndexShuffleBlockResolver.scala:673-676`:
```scala
// No-op reduce ID used in interactions with disk store.
// The disk store expects (map, reduce) pair, but in sort shuffle
// outputs for several reduces are glommed into a single file.
val NOOP_REDUCE_ID = 0
```

**One .data file contains ALL partitions!** The `.index` file marks partition boundaries.

---

---

## Complete Shuffle Write Flow

### Overview

Map task → ShuffleWriter → Files on disk → MapStatus → MapOutputTracker

### Step-by-Step

**1. Task Gets Writer**

```scala
// DAGScheduler assigns ShuffleMapTask
// Task asks ShuffleManager for writer

val writer = shuffleManager.getWriter(
  handle, mapId=0, context, metrics
)
// Returns: SortShuffleWriter instance
```

**2. Writer Processes Data**

From `SortShuffleWriter.scala:65-88`:

```scala
override def write(records: Iterator[Product2[K, V]]): Unit = {
  // Create sorter
  sorter = new ExternalSorter(context, dep.partitioner, ...)
  
  // Insert and sort by partition ID
  sorter.insertAll(records)
  
  // Create file writer
  val mapOutputWriter = shuffleExecutorComponents.createMapOutputWriter(
    dep.shuffleId, mapId, dep.partitioner.numPartitions)
  
  // Write to disk - ONE file for ALL partitions
  sorter.writePartitionedMapOutput(shuffleId, mapId, mapOutputWriter, ...)
  
  // Commit files (creates .data and .index)
  partitionLengths = mapOutputWriter.commitAllPartitions(...)
  
  // Get BlockManager address
  mapStatus = MapStatus(
    blockManager.shuffleServerId,  // ← Just getting address!
    partitionLengths,
    mapId
  )
}
```

**3. Files Created**

From `IndexShuffleBlockResolver.scala:132-150`:

```scala
def getDataFile(shuffleId: Int, mapId: Long, ...): File = {
  val blockId = ShuffleDataBlockId(shuffleId, mapId, NOOP_REDUCE_ID)
  // Creates: shuffle_<shuffleId>_<mapId>_0.data
}

def getIndexFile(shuffleId: Int, mapId: Long, ...): File = {
  val blockId = ShuffleIndexBlockId(shuffleId, mapId, NOOP_REDUCE_ID)
  // Creates: shuffle_<shuffleId>_<mapId>_0.index
}
```

**Example Files:**

```
shuffle_0_0_0.data:
┌──────────────────┬──────────────────┐
│ Partition 0 data │ Partition 1 data │
│ ("apple",1)      │ ("banana",2)     │
│ ("apple",3)      │                  │
│ [200 bytes]      │ [100 bytes]      │
└──────────────────┴──────────────────┘
Byte positions: 0               200            300

shuffle_0_0_0.index:
[0, 200, 300]
 ↑   ↑    ↑
 │   │    └── End of file
 │   └─────── Partition 1 starts at byte 200
 └─────────── Partition 0 starts at byte 0
```

**4. MapStatus Registration**

From `DAGScheduler.scala:2163-2179`:

```scala
case smt: ShuffleMapTask =>
  val status = event.result.asInstanceOf[MapStatus]
  
  // DAGScheduler updates MapOutputTracker (NOT BlockManagerMaster!)
  mapOutputTracker.registerMapOutput(
    shuffleStage.shuffleDep.shuffleId,
    smt.partitionId,
    status  // Contains: location + partition sizes
  )
```

### Complete Flow Diagram

```mermaid
sequenceDiagram
    participant Task as Map Task
    participant SM as ShuffleManager
    participant SW as ShuffleWriter
    participant Disk as Local Disk
    participant BM as BlockManager
    participant DAG as DAGScheduler
    participant MOT as MapOutputTracker

    Task->>SM: getWriter(shuffleId, mapId)
    SM-->>Task: new SortShuffleWriter()
    
    Task->>SW: write(records)
    SW->>SW: Sort & partition data
    SW->>Disk: Write shuffle_0_0_0.data
    SW->>Disk: Write shuffle_0_0_0.index
    
    SW->>BM: What's my address?
    BM-->>SW: executor-1:7337
    
    SW->>SW: Create MapStatus(executor-1, [200,100])
    SW-->>Task: Return MapStatus
    
    Task-->>DAG: Send MapStatus
    DAG->>MOT: registerMapOutput(shuffleId, mapId, status)
    MOT->>MOT: Store in shuffleStatuses map
```

### Key Points

✅ **ShuffleWriter creates files DIRECTLY** (not via BlockManager)  
✅ **BlockManager only provides network address**  
✅ **DAGScheduler updates MapOutputTracker** (not BlockManagerMaster)  
✅ **One .data file per map task** contains ALL partitions  

---

## Complete Shuffle Read Flow

### Overview

Reduce task → ShuffleReader → MapOutputTracker → BlockManager client → Fetch blocks

### Step-by-Step

**1. Reader Gets Block Locations**

From `BlockStoreShuffleReader.scala:72-78`:

```scala
override def read(): Iterator[Product2[K, C]] = {
  // Query MapOutputTracker for locations
  val blocksByAddress = mapOutputTracker.getMapSizesByExecutorId(
    shuffleId,
    startPartition,
    endPartition
  )
  // Returns: [(executor-1, [(shuffle_0_0_2, 500 bytes)]), 
  //           (executor-2, [(shuffle_0_1_2, 300 bytes)])]
```

**2. Fetch Blocks via BlockManager**

```scala
  // Use BlockManager's network client
  val wrappedStreams = new ShuffleBlockFetcherIterator(
    context,
    blockManager.blockStoreClient,  // ← Network transport!
    blockManager,                    // ← For local blocks
    mapOutputTracker,
    blocksByAddress,
    ...
  )
```

**3. BlockManager Serves Blocks**

On each executor, BlockManager:
- Reads from `shuffle_X_Y_0.data` file
- Uses `.index` file to find partition boundaries
- Serves data over network

### Complete Example

**Scenario**: Reduce Task 0 needs partition 2

```scala
// Step 1: Query MapOutputTracker
val locations = mapOutputTracker.getMapSizesByExecutorId(
  shuffleId = 0,
  startPartition = 2,
  endPartition = 3
)

// Returns:
[
  (executor-1@192.168.1.10:7337,
   Seq((shuffle_0_0_2, 200 bytes, mapId=0))),
  (executor-2@192.168.1.20:7337,
   Seq((shuffle_0_1_2, 300 bytes, mapId=1)))
]

// Step 2: Fetch using BlockManager client
val fetcher = new ShuffleBlockFetcherIterator(
  blockManager.blockStoreClient,
  locations
)

// Step 3: BlockManager serves blocks
// executor-1's BlockManager:
//   - Opens shuffle_0_0_0.data
//   - Reads shuffle_0_0_0.index
//   - Finds partition 2 is at bytes [400, 600]
//   - Sends 200 bytes over network

// executor-2's BlockManager:
//   - Opens shuffle_0_1_0.data
//   - Reads shuffle_0_1_0.index  
//   - Finds partition 2 is at bytes [500, 800]
//   - Sends 300 bytes over network

// Step 4: Reader deserializes and aggregates
val records = fetcher.flatMap { stream =>
  deserialize(stream)
}
```

### Flow Diagram

```mermaid
sequenceDiagram
    participant RT as Reduce Task
    participant SR as ShuffleReader
    participant MOT as MapOutputTracker
    participant BM2 as BlockManager<br/>(Executor 2)
    participant BM1 as BlockManager<br/>(Executor 1)
    participant Disk as Disk

    RT->>SR: read() for partition 2
    SR->>MOT: Where is partition 2?
    MOT-->>SR: executor-1 has 200 bytes<br/>executor-2 has 300 bytes
    
    SR->>BM2: Fetch from executor-1
    BM2->>BM1: GET shuffle_0_0_0 partition 2
    BM1->>Disk: Read bytes [400,600]
    Disk-->>BM1: [bytes...]
    BM1-->>BM2: [200 bytes]
    BM2-->>SR: Stream 1
    
    SR->>BM2: Fetch from executor-2  
    BM2->>Disk: Read bytes [500,800]
    Disk-->>BM2: [bytes...]
    BM2-->>SR: Stream 2
    
    SR->>SR: Deserialize & aggregate
    SR-->>RT: Final records
```

---

## Component Reference

Deep dives into each component.

### ShuffleManager

**Role**: Factory for creating ShuffleWriter instances

**What it DOES**:
- Creates `SortShuffleWriter` or `BypassMergeSortShuffleWriter`
- Selects writer based on configuration

**What it DOES NOT do**:
- ❌ Create files
- ❌ Write data
- ❌ Manage metadata

**Source**: `SortShuffleManager.scala`

```scala
override def getWriter[K, V](
    handle: ShuffleHandle,
    mapId: Long,
    context: TaskContext,
    metrics: ShuffleWriteMetricsReporter): ShuffleWriter[K, V] = {
  // Just creates and returns a writer - that's it!
  new SortShuffleWriter(handle, mapId, context, metrics, ...)
}
```

### ShuffleWriter

**Role**: Writes shuffle data to disk

**Responsibilities**:
- Sort and partition data (`ExternalSorter`)
- Create .data and .index files DIRECTLY
- Ask BlockManager for network address
- Return MapStatus

**Key Method**: `write(records)`

**Important**: ShuffleWriter does NOT delegate file I/O to anyone - it writes directly!

### ShuffleReader

**Role**: Fetches shuffle data

**Responsibilities**:
- Query MapOutputTracker for block locations
- Use BlockManager's network client to fetch blocks
- Deserialize and aggregate data

**Key Method**: `read()`

### BlockManager

**Role**: Generic storage infrastructure

**For Shuffle Write**:
- Provides network address (`blockManager.shuffleServerId`)
- That's it! No file creation involvement

**For Shuffle Read**:
- Provides network client (`blockManager.blockStoreClient`)
- Serves shuffle blocks from disk over network

**Important**: BlockManager is generic - works with ANY block type (RDD cache, broadcast, shuffle)

### MapOutputTracker

**Role**: Shuffle metadata coordinator

**Master (Driver)**:
```scala
def registerMapOutput(shuffleId: Int, mapId: Int, status: MapStatus)
def getMapSizesByExecutorId(shuffleId, startPartition, endPartition)
```

**Data Structure**:
```scala
shuffleStatuses: Map[shuffleId, ShuffleStatus]

ShuffleStatus {
  mapStatuses: Array[MapStatus]
  // mapStatuses(0) = MapStatus for map task 0
  // mapStatuses(1) = MapStatus for map task 1
}

MapStatus(
  location: BlockManagerId,   // Which executor?
  mapSizes: Array[Long]        // Size per partition
)
```

### BlockManagerMaster

**Role**: BlockManager registry (NOT shuffle metadata!)

**What it tracks**:
- Which BlockManagers exist on which executors
- Generic block locations

**What it DOES NOT track**:
- ❌ Shuffle metadata (partition sizes)
- ❌ MapStatus information
- ❌ Shuffle-specific details

**Key difference from MapOutputTracker**:

| Aspect | BlockManagerMaster | MapOutputTracker |
|--------|-------------------|------------------|
| Scope | All block types | Shuffle only |
| Data | Block locations | Locations + partition sizes |
| Query | `getLocations(blockId)` | `getMapSizesByExecutorId(shuffleId, partition)` |
| Returns | `Seq[BlockManagerId]` | `Iterator[(BlockManagerId, Seq[(BlockId, Long, Int)])]` |

---

## File Format Details

### Data File Structure

**File**: `shuffle_<shuffleId>_<mapId>_0.data`

**Content**: Concatenated partition data

```
[Partition 0 data][Partition 1 data][Partition 2 data]...
│                 │                 │
0              offset1           offset2
```

**Created by**: `ExternalSorter.writePartitionedMapOutput()`

### Index File Structure

**File**: `shuffle_<shuffleId>_<mapId>_0.index` 

**Content**: Array of `Long` offsets (8 bytes each)

```
[offset0][offset1][offset2]...[offsetN]
   0       200       500        800
   
offset0 = 0 (start of partition 0)
offset1 = 200 (start of partition 1, end of partition 0)
offset2 = 500 (start of partition 2, end of partition 1)
offsetN = 800 (end of file)
```

**Size**: `(numPartitions + 1) * 8 bytes`

**Usage**: To read partition 2:
```scala
val startOffset = indexFile.readLong(2 * 8)  // Offset at position 2
val endOffset = indexFile.readLong(3 * 8)    // Offset at position 3
val partitionSize = endOffset - startOffset
dataFile.read(startOffset, partitionSize)
```

### Real Example

**Scenario**: 2 partitions, Map Task 0

```
shuffle_0_0_0.data (300 bytes):
┌─────────────────────┬─────────────────────┐
│ Partition 0         │ Partition 1         │
│ ("apple", 1)        │ ("banana", 2)       │
│ ("apple", 3)        │                     │
│ 200 bytes           │ 100 bytes           │
└─────────────────────┴─────────────────────┘
0                    200                   300

shuffle_0_0_0.index (24 bytes = 3 * 8):
[0, 200, 300]
 8   8    8   bytes each
```

**Reading partition 0**:
- Read offsets at position 0 and 1: `0` and `200`
- Read `shuffle_0_0_0.data` from byte 0 to 200

**Reading partition 1**:
- Read offsets at position 1 and 2: `200` and `300`
- Read `shuffle_0_0_0.data` from byte 200 to 300

---

## Summary

**Shuffle Write**: Task → ShuffleWriter → Disk → MapStatus → MapOutputTracker  
**Shuffle Read**: Task → ShuffleReader → MapOutputTracker → BlockManager → Disk

**Key Takeaways**:
1. ShuffleWriter creates files DIRECTLY (not via BlockManager)
2. ONE .data file per map task contains ALL partitions
3. BlockManager provides infrastructure (address, network client)
4. MapOutputTracker stores shuffle metadata
5. BlockManagerMaster is NOT involved in shuffle metadata

**All verified against Spark source code!** ✅

---

## Critical Understanding: One RDD, Two Shuffles?

> **The Question**: If one RDD is used in two different groupBys (leading to two actions), how does the shuffle write process handle it?

### The Scenario

```scala
// One parent RDD
val rdd1 = sc.textFile("data.txt")

// Two child RDDs (different branches)
val rdd2 = rdd1.groupBy("key1")  // Branch 1
val rdd3 = rdd1.groupBy("key2")  // Branch 2

// Two Actions
val c1 = rdd2.collect()  // Triggers Job 1
val c2 = rdd3.collect()  // Triggers Job 2
```

### Visualizing the Shuffle Flows

Spark handles this as **two completely independent execution paths**.

```mermaid
graph TB
    subgraph "Parent"
        RDD1["RDD1\n(The Common Parent)"]
    end
    
    subgraph "Branch 1 Execution (Triggered by c1)"
        S0["Stage 0: ShuffleMapStage\n\n1. Compute RDD1\n2. Group by 'key1'\n3. Write shuffle_0files"]
        Shuffle0[/"Shuffle Write\nID: 0\nFiles: shuffle_0_*.data"/]
        S1["Stage 1: ResultStage\n\n1. Read shuffle_0\n2. Create RDD2\n3. Collect"]
    end
    
    subgraph "Branch 2 Execution (Triggered by c2)"
        S2["Stage 2: ShuffleMapStage\n\n1. RE-COMPUTE RDD1\n2. Group by 'key2'\n3. Write shuffle_1 files"]
        Shuffle1[/"Shuffle Write\nID: 1\nFiles: shuffle_1_*.data"/]
        S3["Stage 3: ResultStage\n\n1. Read shuffle_1\n2. Create RDD3\n3. Collect"]
    end
    
    RDD1 --> S0
    S0 --> Shuffle0
    Shuffle0 --> S1
    S1 --> Result1(("c1\nResult"))
    
    RDD1 --> S2
    S2 --> Shuffle1
    Shuffle1 --> S3
    S3 --> Result2(("c2\nResult"))
    
    classDef stage fill:#ffecec,stroke:#ff0000,stroke-width:2px;
    classDef file fill:#e6f3ff,stroke:#0066cc,stroke-dasharray: 5 5;
    class S0,S1,S2,S3 stage;
    class Shuffle0,Shuffle1 file;
```

### What Actually Happens (Step-by-Step)

#### 1. First Action (`c1.collect()`)

1.  **Job 1 Started**: DAGScheduler creates **Stage 0** (ShuffleMapStage) and **Stage 1** (ResultStage).
2.  **Stage 0 Execution**:
    *   **Computation**: `RDD1` is computed from source (read `data.txt`).
    *   **Transformation**: The `groupBy("key1")` logic is applied.
    *   **Shuffle Write**: Tasks write outputs to **Shuffle ID 0** files (`shuffle_0_0_0.data`).
3.  **Stage 1 Execution**: Reads from shuffle 0, produces `RDD2`, collects result `c1`.

#### 2. Second Action (`c2.collect()`)

1.  **Job 2 Started**: DAGScheduler creates **Stage 2** (ShuffleMapStage) and **Stage 3** (ResultStage).
2.  **Stage 2 Execution**:
    *   **Re-Computation**: Since `RDD1` was **NOT cached**, Spark computes it **AGAIN** from source!
    *   **Transformation**: The new `groupBy("key2")` logic is applied.
    *   **Shuffle Write**: Tasks write outputs to **Shuffle ID 1** files (`shuffle_1_0_0.data`).
    *   *Note: These are completely new files. Spark does not overwrite Shuffle 0 files.*
3.  **Stage 3 Execution**: Reads from shuffle 1, produces `RDD3`, collects result `c2`.

### The "Double Write" Reality

Because `RDD1` acts as the input for *both* shuffle stages, and those stages run independently:

1.  **RDD1 partitions are computed twice.**
2.  **Two sets of shuffle files are written to disk.**
    *   `shuffle_0_*.data` (for correct grouping of `key1`)
    *   `shuffle_1_*.data` (for correct grouping of `key2`)

> **Optimization**: If you `rdd1.cache()` before the actions:
> *   Job 1 computes RDD1, caches it, and writes Shuffle 0.
> *   Job 2 reads cached RDD1 (skipping compute), and writes Shuffle 1.
> *   You **still get two sets of shuffle files** (because the groupings are different!), but you save the CPU/IO cost of computing RDD1.

