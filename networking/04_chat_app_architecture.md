# System Design: Modern LLM Chat Architecture

This document breaks down the engineering behind applications like **DeepSeek**, **ChatGPT**, and **Claude**.

They typically use a **Hybrid Architecture**:
1.  **HTTP POST:** To send the prompt.
2.  **SSE (Server-Sent Events):** To stream the answer (Token by Token).
3.  **WebSocket:** To handle background state (Presence, Credits, Notifications).

---

## 1. The High-Level Flow (Visualized)

```mermaid
sequenceDiagram
    participant U as User (Browser)
    participant API as API Server
    participant WS as WebSocket Server
    participant LLM as GPU Cluster

    Note over U,WS: 1. Background Connection (The "Green Arrow" you saw)
    U<->WS: [WebSocket Connected] (Keep-Alive / Presence)

    Note over U,API: 2. The Prompt (Action)
    U->>API: POST /v1/chat/completions { "prompt": "Hello!" }
    API->>U: 200 OK (Content-Type: text/event-stream)

    Note over U,LLM: 3. The Stream (Response)
    loop Stream Tokens
        LLM->>API: Token: "Hi"
        API-->>U: data: { "delta": "Hi" }
        LLM->>API: Token: " there"
        API-->>U: data: { "delta": " there" }
    end
    API-->>U: data: [DONE]

    Note over U,WS: 4. Async Updates (Parallel)
    WS-->>U: { "event": "UpdateCredits", "remaining": 49 }
```

---

## 2. Step-by-Step Breakdown

### Step 1: The Control Channel (WebSocket)
*   **When:** As soon as you open the website.
*   **Protocol:** `wss://chat.deepseek.com/...`
*   **Purpose:** This is the "Heartbeat" of the app.
    *   It tells the server "I am Online".
    *   It listens for "Job Finished" alerts (e.g., if you uploaded a PDF to analyze 5 minutes ago).
    *   It checks for billing updates.
*   **Evidence:** This is the `ping` / `nx.Subscribe` traffic you saw in your Network tab.

### Step 2: Sending the Prompt (HTTP POST)
*   **When:** You hit "Enter".
*   **Protocol:** Standard `HTTP/2 POST`.
*   **Why not WebSocket?**
    *   Sending a prompt is a transactional action. It has a clear Start and Verification phase (Auth, Safety Checks).
    *   HTTP is perfect for this. WebSockets are harder to route and cache.

### Step 3: Streaming the Answer (SSE)
*   **The Magic:** The server does **NOT** wait for the full answer.
*   **Mechanism:** Server-Sent Events (`text/event-stream`).
*   **The Format:**
    ```text
    data: {"token": "Sure"}
    
    data: {"token": ", I"}
    
    data: {"token": " can"}
    
    data: {"token": " help."}
    ```
*   **Client Logic:** The Javascript `EventSource` API listens to this stream and appends the words to the innerHTML of the chat bubble instantly.

### Step 4: Closing the Loop
*   Once the LLM finishes, the HTTP connection for the stream closes.
*   The **WebSocket** stays open, waiting for the next interaction.

---

## 3. Why this split? (Trade-offs)

| Feature | Best Protocol | Why? |
| :--- | :--- | :--- |
| **Sending Prompt** | **HTTP POST** | Simple, stateless, easy to auth. |
| **Receiving Text** | **SSE** | It's a one-way firehose. Reconnects automatically (TCP). |
| **Notifications** | **WebSocket** | Full-duplex. Server can "poke" user anytime. |
