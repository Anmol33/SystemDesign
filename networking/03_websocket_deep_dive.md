# WebSocket Deep Dive: The Real-Time Web

This document explores **WebSocket**, the protocol that enabled true real-time interaction on the web by breaking away from the strict Request-Response model of HTTP.

---

## 1. The Evolution: Why do we need it?

Before WebSockets (2011), the web was **passive**. A server could not speak unless spoken to. If you wanted to know "Did I get a new message?", you had to ask repeatedly.

### The Legacy Hacks

1.  **Short Polling**:
    *   *Client:* "Any news?" -> *Server:* "No."
    *   *Client (1s later):* "Any news?" -> *Server:* "No."
    *   *Problem:* Extremely wasteful. 99% of requests yield no data. bandwidth = wasted.
2.  **Long Polling**:
    *   *Client:* "Any news?" -> *Server:* ... (Waits 30s) ... "Yes!"
    *   *Problem:* Still relies on opening/closing HTTP connections. Heavy header overhead.

### The Solution: WebSocket
A single, long-lived TCP connection where **both sides can talk at any time**.

```mermaid
sequenceDiagram
    participant C as Client
    participant S as Server
    
    Note over C,S: HTTP Polling (Wasteful)
    C->>S: GET /messages
    S-->>C: 200 OK (Empty)
    C->>S: GET /messages
    S-->>C: 200 OK (Empty)
    C->>S: GET /messages
    S-->>C: 200 OK (Data!)
    
    Note over C,S: WebSocket (Efficient)
    C->>S: [Handshake]
    S-->>C: [101 Switch Protocols]
    Note right of S: Connection Open
    S-->>C: Data! (Push)
    S-->>C: Data! (Push)
    C->>S: Data! (Push)
```

---

## 2. The Handshake: "Upgrading" HTTP

WebSockets don't start as WebSockets. They start as a standard HTTP 1.1 request. This allows them to bypass firewalls and load balancers that only understand HTTP.

### The Mechanism
1.  **Client Request:** "Hello Server, I speak HTTP, but I would like to **Upgrade** to `websocket`."
2.  **Server Response:** "Okay, I understand `websocket`. Let's switch."

### The Wire Protocol
```http
// Client Request
GET /chat HTTP/1.1
Host: example.com
Connection: Upgrade
Upgrade: websocket
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

```http
// Server Response
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

*   **101 Switching Protocols**: The specific status code that tells the client "We are no longer speaking HTTP."
*   **Sec-WebSocket-Accept**: The proof that the server isn't just a dumb TCP socket.

### The "Magic" Algorithm (Under the Hood)
When `websockets.serve` runs, it performs this specific RFC 6455 algorithm **automatically** for every connection:

1.  **Read** the Client's `Sec-WebSocket-Key` (e.g., `dGhlIHNhbXBsZSBub25jZQ==`).
2.  **Concatenate** it with the **Magic GUID**: `258EAFA5-E914-47DA-95CA-C5AB0DC85B11`.
    *   *Result:* `dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11`
3.  **Hash** the result using **SHA-1**.
4.  **Base64 Encode** the binary hash.
5.  **Send** it back as `Sec-WebSocket-Accept`.

If the Client calculates this locally and it doesn't match what the Server sent, the Client **closes the connection immediately**. This ensures the server explicitly "speaks WebSocket".

---

## 3. The Protocol Internals

Once the handshake is done, HTTP rules (headers, verbs, cookies) are gone. We are now exchanging raw **WebSocket Frames**.

### Features
1.  **Low Overhead**: The header is only **2 to 14 bytes** (compared to ~500+ bytes for HTTP headers).
2.  **Full Duplex**: Client and Server sends data independently.
3.  **Binary & Text**: Can send Strings (JSON) or Binary (Images/Protobuf).

### Security: Masking
*   **Rule:** All frames sent from **Client to Server** MUST be masked (XOR encryption).
*   **Why?** To prevent "Cache Poisoning" attacks on intermediate proxies.
*   **Server to Client**: Data is NOT masked.

### Frame Types (Opcodes)
The protocol uses specific "Opcodes" to define what the data is:
*   `0x1`: Text Frame.
*   `0x2`: Binary Frame.
*   `0x8`: **Close** (Polite disconnect).
*   `0x9`: **Ping** (Heartbeat).
*   `0xA`: **Pong** (Heartbeat response).

---

## 4. Use Cases

| Use Case | Why WebSocket? |
| :--- | :--- |
| **Chat Apps** (Slack, WhatsApp) | Instant message delivery without polling. |
| **Live Feeds** (Stock Tickers, Sports) | Low latency updates pushed from server. |
| **Collaboration** (Figma, Google Docs) | Sending mouse cursors and text diffs in real-time. |
| **Multiplayer Games** (Agar.io) | Syncing game state 60 times per second. |
| **System Alerts** | Pushing "Job Completed" notifications to dashboards. |

---

## 5. Production Best Practices

WebSockets are "Stateful" (the connection stays alive), which introduces problems that stateless HTTP doesn't have.

### A. The "Half-Open" Problem (Heartbeats)
*   **Scenario:** A user rips out their ethernet cable. The Server thinks the connection is still open and keeps writing data to a black hole.
*   **Solution:** **Heartbeats**.
    *   Server sends `PING` every 30s.
    *   If Client doesn't reply with `PONG`, Server kills the connection.

### B. Load Balancing (Sticky Sessions)
*   **Problem:** Client connects to Server A. Nginx is the Load Balancer.
    *   Request 1 (Handshake) -> Goes to **Server A**.
    *   The TCP connection is locked to Server A.
    *   You cannot "move" this connection to Server B.
*   **Scaling:** If you have 100k users, you need a **Redis Pub/Sub** backend so Server A can talk to users on Server B.

### C. Reconnection Logic
*   **Fact:** Mobile networks flake out. WiFi drops.
*   **Code:** Your client **MUST** have auto-reconnect logic.
*   **Strategy:** Use **Exponential Backoff** (Wait 1s, then 2s, then 4s...) to avoid hammering the server when it comes back up.

### D. Security (WSS)
*   Always use `wss://` (WebSocket Secure).
*   It encrypts the traffic using TLS (SSL), just like HTTPS.
*   Crucial because many proxies block non-encrypted WebSocket traffic on port 80.

---


---

## 6. Hands-on Experiment: The Dual Protocol Server

We built a real-world server (`dual_protocol_server.py`) to demonstrate how WebSockets and HTTP coexist on the same port.

### The Challenge
We wanted a single server (Port `8765`) to:
1.  **Serve a Website** (HTTP GET) -> The Chat UI.
2.  **Handle Chat Traffic** (WebSocket) -> The Real-time data.

### The Implementation (FastAPI)
Using `FastAPI`, we routed traffic based on the protocol:

```python
# 1. Standard HTTP Request (Browser typing path)
@app.get("/")
async def get():
    return HTMLResponse(html_page)

# 2. WebSocket Upgrade Request (JS: new WebSocket('wss://...'))
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept() # Send 101 Switching Protocols
    # ... Stream data ...
```

### Observation 1: The "Not Found" Trap
When we visited `https://localhost:8765/ws` directly in the Chrome address bar, we got a **404 Not Found**.

![Browser 404 Error](assets/ws_browser_404.png)

**Why?**
*   **Browser Address Bar** = Always sends an **HTTP GET**.
*   **Server Logic** = Expected a **WebSocket Upgrade** header on `/ws`.
*   **Result:** Mismatch. The server correctly rejected the HTTP request.
*   **Fix:** We added a fallback handler so if you visit `/ws` via HTTP, it says *"Please use a WebSocket client"*.

### Observation 2: The Successful Handshake & Streaming
Once we connected properly using the Javascript Client:
1.  **Handshake:** The Network tab confirmed `101 Switching Protocols`.
2.  **Bi-Directional Data:**
    *   **Blue Text:** Echo messages (Request/Response).
    *   **Red Text:** Server Events (Push).

![Success Logs](assets/ws_success_events.png)

**Key Takeaway:** Notice how the **Server Events** (Red) arrive independently of your user input. The server is "pushing" data (like `Server Event: Ping at 18:50:00`) instantly, without the client asking for it.

---

## 7. WebSocket Headers Reference

The initial Handshake relies on specific HTTP headers to establish the connection.

### Request Headers (Client -> Server)
| Header | Value | Purpose |
| :--- | :--- | :--- |
| **`Connection`** | `Upgrade` | Tells the server this is not a normal HTTP request. |
| **`Upgrade`** | `websocket` | Specifies the protocol we want to switch to. |
| **`Sec-WebSocket-Key`** | `dGhlIHNhbX...` | A random 16-byte value (Base64 encoded). The server must solve a puzzle with this to prove it's a WebSocket server. |
| **`Sec-WebSocket-Version`** | `13` | The protocol version (Standard is 13). |
| **`Sec-WebSocket-Protocol`** | `json`, `mqtt` | (Optional) Sub-protocol negotiation. "I speak JSON, do you?" |
| **`Sec-WebSocket-Extensions`**| `permessage-deflate` | (Optional) Requests compression (like GZIP for WS). |
| **`Origin`** | `http://my-site.com`| Security header to prevent Cross-Site WebSocket Hijacking (CSWSH). |

### Response Headers (Server -> Client)
| Header | Value | Purpose |
| :--- | :--- | :--- |
| **`Status Code`** | `101 Switching Protocols` | "I accept the upgrade." |
| **`Sec-WebSocket-Accept`** | `s3pPLMBi...` | The "Puzzle Solution". Calculated as `Base64(SHA1(Key + Magic_GUID))`. Proves the server received the handshake. |

