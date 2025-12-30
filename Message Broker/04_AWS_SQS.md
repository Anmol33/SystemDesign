# 04. AWS SQS (Simple Queue Service)

## 1. Problem It Solves
SQS is a **Fully Managed, Serverless** message queue. It solves the problem of "I don't want to manage servers/brokers."
*   **Origin**: One of the first AWS services (Launched 2004).
*   **Scalability**: Infinite. You don't provision "throughput". You just send messages.

---

## 2. Architecture: "The Helper Pool"

SQS behaves differently from RabbitMQ. There is no "Connection" relative to a specific node. It is a massive distributed fleet.

### The Core Components
1.  **Queue**: A logical container for messages.
2.  **Producer**: Sends specific XML/JSON payload.
3.  **Consumer**: Polls (HTTP Request) to get messages.
    *   *Note*: SQS is **Pull Only**.

### Key Concept: Visibility Timeout (The "Lock")
SQS does not delete a message when you read it. It **Hides** it.
1.  Consumer A reads Message X.
2.  SQS starts the **Visibility Timeout** timer (default 30s).
3.  Message X becomes invisible to Consumer B.
4.  **Scenario 1 (Success)**: Consumer A finishes and calls `DeleteMessage`. X is gone forever.
5.  **Scenario 2 (Crash)**: Consumer A crashes. The timer expires. Message X becomes **Visible** again.
6.  Consumer B picks up Message X (Retry logic is built-in!).

### Short Polling vs Long Polling
*   **Short Poll**: Return immediately, even if empty. (Burns CPU/Money).
*   **Long Poll** (`WaitTimeSeconds=20`): Wait up to 20s for a message to arrive. **Always use this** to save money.

---

## 3. Standard vs. FIFO Queues

| Feature | Standard Queue | FIFO Queue |
| :--- | :--- | :--- |
| **Ordering** | **Best-Effort**. (Msg 1 sent before Msg 2 might arrive after). | **Strictly Preserved**. (First-In-First-Out). |
| **Delivery** | **At-Least-Once**. (Rarely, you get a duplicate). | **Exactly-Once**. (Deduplication engine active). |
| **Throughput** | **Unlimited**. | Limit (300/s without batching, 3000/s with batching). |
| **Cost** | Cheaper. | More expensive. |

---

## 4. Scalability & Reliability
*   **Horizontal Scale**: SQS scales transparently. Whether 1 msg/s or 10,000 msg/s, the API is the same.
*   **Dead Letter Queue (DLQ)**:
    *   If a message fails processing 5 times (Visibility Timeout expires 5 times), SQS moves it to a DLQ.
    *   This prevents a "Poison Pill" (malformed message) from blocking the queue forever.

---

## 5. Summary Config
| Feature | Setting | Impact |
| :--- | :--- | :--- |
| **Visibility Timeout** | 30s - 12h | Needs to be longer than your processing time. |
| **Delay Seconds** | 0s - 15m | "Hide" message initially (e.g., Scheduled Jobs). |
| **Message Retention** | 4 Days (default) | Data expires if not consumed. |
| **Max Message Size** | 256 KB | Small. Use S3 Pointer for large data. |
