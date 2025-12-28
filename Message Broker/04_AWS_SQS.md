# 04. AWS SQS: Serverless Distributed Queuing

## 1. Introduction
Amazon Simple Queue Service (SQS) is a fully managed message queuing service that enables the decoupling of serverless and microservices applications. Unlike managing a RabbitMQ cluster, SQS abstracts away the underlying infrastructure completely, offering "Infinite" scaling but with higher latency trade-offs.

---

## 2. Core Architecture

SQS operates on a massive fleet of redundant storage nodes.

```mermaid
sequenceDiagram
    participant P as Producer
    participant API as SQS Load Balancer
    participant Fleet as Storage Fleet
    participant C as Consumer

    P->>API: SendMessage()
    API->>Fleet: Replicate to 3 AZs
    API-->>P: 200 OK (MessageID)
    
    Note over Fleet,C: Message is durable but 'Invisible'

    loop Long Polling (20s)
        C->>API: ReceiveMessage(WaitTime=20s)
        API->>Fleet: Check for Visible Messages?
    end

    API-->>C: Return Payload + ReceiptHandle
    Note right of C: "Visibility Timeout" starts.

    C->>C: Process Job...
    C->>API: DeleteMessage(ReceiptHandle)
```

### Key Components
1.  **Storage Fleet**: AWS stores messages across multiple Availability Zones (AZs) for durability.
2.  **The Polling Model**: SQS does *not* push messages. Consumers must make HTTP requests (`ReceiveMessage`) to pull data.
3.  **Visibility Timeout**: The mechanism that "locks" a message for a consumer.

---

## 3. Internal Mechanics

### A. The "Distributed Visibility" Problem
Since SQS is a distributed fleet, there is no single "Master" node.
*   **Behavior**: When you read a message, SQS does not delete it. It sets a "Visibility Timer" on that specific message ID.
*   **Implication**: If your consumer crashes, the timer expires. The message usually reappears to other consumers.
*   **Edge Case**: "At-Least-Once" delivery means occasionally *two* consumers might get the same message if the distributed lock propagation is slow (rare, but possible).

### B. Long Polling vs Short Polling
*   **Short Polling** (Default): Returns immediately. Problem: If the queue is empty, you waste money and CPU on empty cycles.
*   **Long Polling** (`WaitTimeSeconds=20`): The HTTP connection hangs open for 20s. If a message arrives at $t=5s$, it returns immediately.
    *   **Recommendation**: ALWAYS use Long Polling. It reduces cost by ~99%.

---

## 4. Scaling SQS

### A. Standard Queues (Infinite)
*   **Throughput**: Unlimited. AWS automatically provisions more storage nodes behind the scenes.
*   **Trade-off**: Best-effort ordering. Duplicates are possible.

### B. FIFO Queues (Limited)
*   **Requirement**: Strict Ordering (First-In-First-Out) and Exactly-Once processing.
*   **Scaling Limit**: ~300 transactions/second (TPS) default.
*   **Sharding (The Fix)**:
    *   Use `MessageGroupId`.
    *   Messages with GroupID `A` are ordered strictly. Messages with GroupID `B` are ordered strictly. `A` and `B` process in parallel.
    *   By using high-cardinality Group IDs, you can scale FIFO beyond 300 TPS.

---

## 5. Constraints & Limitations

| Constraint | Limit | Why? |
| :--- | :--- | :--- |
| **Payload Size** | 256 KB | SQS is for signaling, not data transfer. Use S3 for large objects. |
| **Latency** | 10ms - 100ms | HTTP Polling overhead. Not suitable for real-time RPC. |
| **Visibility Timeout** | Max 12 Hours | If a job takes >12 hours, SQS is the wrong tool (Use Step Functions). |
| **Message Retention** | Max 14 Days | SQS is ephemeral storage. |

---

## 6. Production Checklist

1.  [ ] **Enable Long Polling**: Set `ReceiveMessageWaitTimeSeconds` to 20.
2.  [ ] **Configure DLQ**: Attach a Dead Letter Queue with `maxReceiveCount=5`.
3.  [ ] **Batching**: Use `SendMessageBatch` and `DeleteMessageBatch` (10 msgs) to reduce costs by 10x.
4.  [ ] **Lambda Concurrency**: If triggering Lambda, set `ReservedConcurrency` to prevent SQS from launching 10,000 Lambdas and crashing your database.

---

## Chapter 7: End-to-End Walkthrough: Life and Death of a Message

Let's trace a Payment Event through the distributed SQS fleet.

### 1. Submission Phase
*   **Producer**: Calls `SendMessage("PaymentID: 123")`.
*   **Load Balancer**: Routes request to a Front-End Node.
*   **Storage Fleet**:
    *   Writes message to AZ-1, AZ-2, and AZ-3 (Durability).
    *   Returns `MessageID` to Producer (Success).
    *   **State**: `Visible`.

### 2. Polling Phase
*   **Consumer**: Calls `ReceiveMessage(WaitTime=20s)`.
*   **SQS Backend**:
    *   Samples a subset of storage nodes (Weighted Random).
    *   Finds "PaymentID: 123".
    *   **Action**: Starts the **Visibility Timer** (e.g., 30s).
    *   **State**: `In-Flight` (Invisible to others).
    *   Returns `ReceiptHandle` (A temporary access key) to Consumer.

### 3. Completion Phase
*   **Consumer**: Processes payment successfully.
*   **Action**: Calls `DeleteMessage(ReceiptHandle)`.
*   **SQS Backend**: Permanently removes the message from all AZs.

---

## Chapter 8: Failure Scenarios (The Senior View)

### 1. Failure Scenario A: The Visibility Loop
**Symptom**: The same job is processed multiple times, indefinitely.
**Cause**: **Processing Time > Visibility Timeout**.
**Mechanism**:
1.  Consumer A gets message. Timeout = 30s.
2.  Consumer A is slow (processing takes 35s).
3.  At T=30s, SQS makes the message **Visible** again.
4.  Consumer B gets the message.
5.  Consumer A finishes at T=35s and tries to Delete, but the `ReceiptHandle` is expired/invalid.
**Visual**:
```mermaid
sequenceDiagram
    participant SQS as SQS Queue
    participant C as Consumer A
    
    SQS->>C: Receive Message (T=0)
    Note right of SQS: Timer Started (30s)
    
    rect rgb(255, 200, 200)
        Note over C: Processing... (Slow)
    end
    
    Note right of SQS: T=30s (Timer Expired)
    Note right of SQS: Message Visible Again!
    
    C->>SQS: Delete Message (T=35s)
    SQS-->>C: 400 Error (ReceiptHandle Invalid)
```

### 2. Failure Scenario B: The Zombie Message (Poison Pill)
**Symptom**: Consumers are crashing randomly. Queue depth stays at `1`.
**Cause**: A malformed message (e.g., `{ "price": "NaN" }`) that causes unhandled code exceptions.
**Mechanism**:
1.  Consumer reads message -> Crashes -> Visibility Timeout Expires.
2.  Message reappears -> Consumer reads -> Crashes.
3.  **Result**: Infinite loop of crashes (Zombie Message).
**The Fix**:
*   Configure **MaxReceiveCount = 5**.
*   After 5 attempts, SQS moves it to a **Dead Letter Queue (DLQ)**.
**Visual**:
```mermaid
graph TD
    Q["Main Queue"] --"Attempt 1 (Crash)"--> C["Consumer"]
    Q --"Attempt 2 (Crash)"--> C
    Q --"Attempt 5 (Fail)"--> C
    C --"Move to DLQ"--> DLQ["Dead Letter Queue"]
    
    style DLQ fill:#ff9999
```
