# Server-Sent Events (SSE): The One-Way Stream

While WebSockets get all the hype for being "Real-Time", **Server-Sent Events (SSE)** are the unsung heroes of the modern AI web. If you use ChatGPT, Claude, or DeepSeek, you are using SSE.

---

## 1. The Concept: Radio vs Telephone

The easiest way to understand SSE is by comparing it to WebSockets.

| Feature | WebSocket 📞 | SSE 📻 |
| :--- | :--- | :--- |
| **Direction** | **Bi-Directional** (Full Duplex) | **Uni-Directional** (Server -> Client) |
| **Analogy** | A Telephone Call | A Radio Station |
| **Complexity** | High (Custom Protocol) | Low (Standard HTTP) |
| **Reconnection** | Manual (Code it yourself) | **Automatic** (Built-in to Browser) |
| **Data Type** | Binary or Text | Text Only |

**Why use SSE?**
Most real-time apps are actually one-way.
*   *Stock Ticker?* Server pushes prices. You don't "reply" to a price.
*   *News Feed?* Server pushes headlines.
*   *AI Response?* Server pushes tokens. You don't interrupt it mid-sentence.

---

## 2. The Wire Protocol

SSE is not a fancy new protocol. It is just a **long-lived HTTP Download**.

### The Request
The client sends a normal GET request, but asks for a special `Content-Type`.
```http
GET /stream HTTP/1.1
Accept: text/event-stream
```

### The Response
The server keeps the TCP connection open and sends data in a specific format:
```http
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

data: {"price": 100}

data: {"price": 102}

event: close
data: [DONE]
```

**Key Rules:**
1.  **`data:`**: The payload prefix.
2.  **`\n\n`**: Double newline means "End of Message".
3.  **`id:`**: (Optional) Use this to track message IDs. If the connection dies, the browser auto-reconnects and sends `Last-Event-ID: <id>` so the server knows where to resume!

---

## 3. The "Killer App": AI Streaming (ChatGPT/DeepSeek)

Why do LLMs use SSE instead of WebSockets?

1.  **Latency:** An LLM takes ~50ms to generate *one token* (word part). Generating an entire paragraph takes 5 seconds.
    *   *Without Streaming:* User stares at a spinner for 5 seconds.
    *   *With Streaming:* User sees words appear instantly.
2.  **Simplicity:**
    *   We send the *Prompt* via `POST`.
    *   We receive the *Stream* via `SSE`.
    *   If the WiFi drops, the browser automatically tries to reconnect to get the rest of the sentence.

---

## 4. Hands-on Laboratory (Python Implementation)

Let's build a server that simulates an AI generating text.

### The Server (`sse_server.py`)
*(Requires `pip install fastapi uvicorn`)*

```python
import asyncio
from fastapi import FastAPI
from fastapi.responses import StreamingResponse

app = FastAPI()

async def fake_ai_generator():
    sentence = "DeepSeek is an advanced AI model designed for coding tasks."
    tokens = sentence.split(" ")
    
    for token in tokens:
        # Simulate "Thinking" time
        await asyncio.sleep(0.5) 
        
        # Format as SSE
        # Note: We simulate a JSON payload usually used in APIs
        yield f"data: {token} \n\n"

@app.get("/stream")
async def stream_tokens():
    return StreamingResponse(fake_ai_generator(), media_type="text/event-stream")

if __name__ == "__main__":
    import uvicorn
    print("Serving on http://localhost:8000/stream")
    uvicorn.run(app, host="localhost", port=8000)
```

### The Client (Browser Console)
You don't even need a special library. `EventSource` is built into every browser.

```javascript
const evtSource = new EventSource("http://localhost:8000/stream");

evtSource.onmessage = function(event) {
    console.log("Token Received:", event.data);
    // document.body.innerHTML += event.data + " ";
};

evtSource.onerror = function() {
    console.log("Stream Ended.");
    evtSource.close(); // Important! Or the browser will retry forever.
};
```

---

## 5. When to choose what?

| Scenario | Choice | Reason |
| :--- | :--- | :--- |
| **Chat App (Messenger)** | **WebSocket** | You need to type *while* receiving messages. |
| **AI Generation** | **SSE** | You send a prompt, then sit back and watch. |
| **Notification Bell** | **SSE** | Server tells you "New Like!", you don't reply `ACK`. |
| **Multiplayer Game** | **UDP / WebSocket** | Latency is critical; dropped packets are okay. |
