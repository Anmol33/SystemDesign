# Complete OIDC "Login with Google" - Deep Dive

This document shows every detail of what happens during "Login with Google" including all server-side processing.

## The Three Actors

1. **Alice** (User) - Wants to log into PhotoApp
2. **Google** (Identity Provider) - Authenticates Alice, issues tokens
3. **PhotoApp** (Client Application) - Wants to know who Alice is

---

## Step 1: User Clicks "Login with Google"

**PhotoApp constructs authorization URL:**

```
https://accounts.google.com/o/oauth2/v2/auth?
  client_id=photoapp-123.apps.googleusercontent.com
  &redirect_uri=https://photoapp.com/auth/callback
  &response_type=code              
  &scope=openid email profile      
  &state=random_csrf_token_xyz     
  &nonce=random_nonce_abc          
```

**Parameters explained:**
- `client_id`: PhotoApp's ID (registered with Google)
- `redirect_uri`: Where Google sends user back
- `response_type=code`: Want authorization code (not token directly)
- `scope=openid`: Makes it OIDC (not just OAuth 2.0)
- `scope=email profile`: What identity info PhotoApp wants
- `state`: Random value to prevent CSRF attacks
- `nonce`: Binds ID token to this specific request

**Browser redirects Alice to Google**

---

## Step 2: Alice Authenticates at Google

**Google checks:** Is Alice already logged into Google?

**If NO:**
```
Google shows login page
Alice enters: email + password
Google verifies credentials
Google creates session for Alice
Browser stores Google session cookie (SID)
```

**If YES:**
```
Google recognizes Alice (checks existing session cookie)
Skips password step
```

**Google shows consent screen:**
```
┌────────────────────────────────────┐
│ PhotoApp wants to:                 │
│                                    │
│ ✅ Know who you are (openid)       │
│ ✅ View your email address         │
│ ✅ View your basic profile info    │
│                                    │
│  [Allow]         [Deny]            │
└────────────────────────────────────┘
```

Alice clicks: **[Allow]**

---

## Step 3: Google Creates Authorization Code

**What Google does internally:**

```javascript
// 1. Generate authorization code
const authCode = {
  code: crypto.randomBytes(32).toString('hex'),  // "4/0AX4XfWh-abc123xyz"
  userId: "103547991597142817347",  // Alice's Google ID
  clientId: "photoapp-123.apps.googleusercontent.com",
  redirectUri: "https://photoapp.com/auth/callback",
  scope: "openid email profile",
  nonce: "random_nonce_abc",  // From request
  createdAt: Date.now(),
  expiresAt: Date.now() + 60000,  // 60 seconds
  used: false
}

// 2. Store in database
await db.authorizationCodes.create(authCode)

// 3. Redirect browser
res.redirect(`https://photoapp.com/auth/callback?code=${authCode.code}&state=random_csrf_token_xyz`)
```

**Browser receives redirect:**
```
https://photoapp.com/auth/callback?
  code=4/0AX4XfWh-abc123xyz
  &state=random_csrf_token_xyz
```

---

## Step 4: Browser Sends Code to PhotoApp

**Browser makes request:**
```http
GET /auth/callback?code=4/0AX4XfWh-abc123xyz&state=random_csrf_token_xyz HTTP/1.1
Host: photoapp.com
Cookie: (PhotoApp might have previous session)
```

**PhotoApp receives:**
- Authorization code: `4/0AX4XfWh-abc123xyz`
- State: `random_csrf_token_xyz`

**PhotoApp validates state:**
```javascript
if (req.query.state !== req.session.originalState) {
  throw new Error('CSRF attack detected!')
}
```

---

## Step 5: PhotoApp Exchanges Code for Tokens (Back-Channel)

**PhotoApp makes server-to-server request:**

```http
POST /token HTTP/1.1
Host: oauth2.googleapis.com
Content-Type: application/x-www-form-urlencoded

grant_type=authorization_code
&code=4/0AX4XfWh-abc123xyz
&client_id=photoapp-123.apps.googleusercontent.com
&client_secret=GOCSPX-photoapp_secret_key
&redirect_uri=https://photoapp.com/auth/callback
```

**Key point:** Browser NEVER sees this request! `client_secret` stays on server.

---

## Step 6: Google Validates and Issues Tokens

**What Google does (detailed):**

### 6.1: Validate Client Credentials
```javascript
const client = await db.clients.findOne({ 
  clientId: request.client_id 
})

if (!client) {
  return { error: "invalid_client" }
}

if (client.clientSecret !== request.client_secret) {
  return { error: "invalid_client" }
}

// ✓ Client authenticated
```

### 6.2: Validate Authorization Code
```javascript
const authCode = await db.authorizationCodes.findOne({
  code: request.code
})

if (!authCode) {
  return { error: "invalid_grant" }
}

if (authCode.expiresAt < Date.now()) {
  return { error: "invalid_grant", error_description: "Code expired" }
}

if (authCode.used === true) {
  // Security: Code reuse detected!
  await revokeAllTokensForUser(authCode.userId, authCode.clientId)
  return { error: "invalid_grant" }
}

if (authCode.clientId !== request.client_id) {
  return { error: "invalid_grant" }
}

if (authCode.redirectUri !== request.redirect_uri) {
  return { error: "invalid_grant" }
}

// ✓ Authorization code valid
```

### 6.3: Mark Code as Used
```javascript
await db.authorizationCodes.update(request.code, {
  used: true,
  usedAt: Date.now()
})
```

### 6.4: Get User Data
```javascript
const user = await db.users.findOne({ id: authCode.userId })

// user = {
//   id: "103547991597142817347",
//   email: "alice@gmail.com",
//   emailVerified: true,
//   name: "Alice Smith",
//   picture: "https://lh3.googleusercontent.com/..."
// }
```

### 6.5: Generate Access Token
```javascript
const accessToken = {
  token: crypto.randomBytes(32).toString('hex'),
  userId: authCode.userId,
  clientId: authCode.clientId,
  scope: authCode.scope,
  expiresAt: Date.now() + 3600000  // 1 hour
}

await db.accessTokens.create(accessToken)
```

### 6.6: Generate ID Token (JWT)
```javascript
const idTokenPayload = {
  iss: "https://accounts.google.com",
  sub: user.id,
  aud: authCode.clientId,
  email: user.email,
  email_verified: user.emailVerified,
  name: user.name,
  picture: user.picture,
  iat: Math.floor(Date.now() / 1000),
  exp: Math.floor(Date.now() / 1000) + 3600,
  nonce: authCode.nonce
}

// Sign with Google's PRIVATE key
const idToken = jwt.sign(idTokenPayload, GOOGLE_PRIVATE_KEY, {
  algorithm: 'RS256',
  keyid: 'current-key-id'
})
```

### 6.7: Generate Refresh Token
```javascript
const refreshToken = {
  token: crypto.randomBytes(64).toString('hex'),
  userId: authCode.userId,
  clientId: authCode.clientId,
  scope: authCode.scope,
  expiresAt: Date.now() + 2592000000  // 30 days
}

await db.refreshTokens.create(refreshToken)
```

### 6.8: Delete Authorization Code
```javascript
await db.authorizationCodes.delete(request.code)
```

### 6.9: Send Response
```json
{
  "access_token": "ya29.a0AfH6SMB...",
  "id_token": "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "scope": "openid email profile",
  "refresh_token": "1//0gLN8e3BHs..."
}
```

---

## Step 7: PhotoApp Processes ID Token

### 7.1: Decode Token (Without Verification)
```javascript
const idToken = response.id_token
const decoded = jwt.decode(idToken, { complete: true })

// decoded = {
//   header: { alg: "RS256", kid: "abc123key", typ: "JWT" },
//   payload: { iss: "...", sub: "...", email: "..." },
//   signature: "SflKxw..."
// }
```

### 7.2: Fetch Google's Public Keys
```javascript
let googleKeys = cache.get('google-public-keys')

if (!googleKeys) {
  const response = await fetch('https://www.googleapis.com/oauth2/v3/certs')
  googleKeys = await response.json()
  cache.set('google-public-keys', googleKeys, 86400)  // Cache 24h
}
```

### 7.3: Select Correct Public Key
```javascript
const keyId = decoded.header.kid
const publicKey = googleKeys.keys.find(k => k.kid === keyId)
const publicKeyPEM = jwkToPem(publicKey)
```

### 7.4: Verify Signature
```javascript
try {
  const verified = jwt.verify(idToken, publicKeyPEM, {
    algorithms: ['RS256'],
    audience: 'photoapp-123.apps.googleusercontent.com',
    issuer: 'https://accounts.google.com',
    clockTolerance: 10
  })
  
  // ✓ Signature valid! Token is authentic from Google
  
} catch (err) {
  return res.status(401).json({ error: 'Invalid token' })
}
```

### 7.5: Validate Claims
```javascript
const claims = verified

if (claims.nonce !== req.session.nonce) {
  throw new Error('Nonce mismatch')
}

if (!claims.email_verified) {
  throw new Error('Email not verified')
}

// ✓ All claims validated
```

### 7.6: Extract User Info
```javascript
const userData = {
  googleId: claims.sub,
  email: claims.email,
  emailVerified: claims.email_verified,
  name: claims.name,
  picture: claims.picture
}
```

### 7.7: Find or Create User
```javascript
let user = await db.users.findOne({ googleId: userData.googleId })

if (!user) {
  // First time login
  user = await db.users.create({
    googleId: userData.googleId,
    email: userData.email,
    emailVerified: userData.emailVerified,
    name: userData.name,
    picture: userData.picture,
    authProvider: 'google',
    createdAt: new Date(),
    lastLoginAt: new Date()
  })
} else {
  // Returning user
  await db.users.update(user.id, {
    email: userData.email,
    name: userData.name,
    picture: userData.picture,
    lastLoginAt: new Date()
  })
}
```

### 7.8: Create Session
```javascript
const sessionId = crypto.randomBytes(32).toString('hex')

await db.sessions.create({
  sessionId: sessionId,
  userId: user.id,
  createdAt: new Date(),
  expiresAt: new Date(Date.now() + 86400000),  // 24 hours
  ip: req.ip,
  userAgent: req.headers['user-agent']
})
```

### 7.9: Store Refresh Token (Encrypted)
```javascript
if (response.refresh_token) {
  await db.userTokens.create({
    userId: user.id,
    provider: 'google',
    refreshToken: encrypt(response.refresh_token),
    createdAt: new Date()
  })
}
```

### 7.10: Send Session Cookie
```javascript
res.cookie('session_id', sessionId, {
  httpOnly: true,
  secure: true,
  sameSite: 'lax',
  maxAge: 86400000,
  path: '/',
  domain: 'photoapp.com'
})
```

### 7.11: Redirect to Dashboard
```javascript
res.redirect('/dashboard')
```

---

## Step 8: User is Logged In!

**Browser now has:**
- ✅ Google session cookie (for accessing Gmail, YouTube, etc.)
- ✅ PhotoApp session cookie (for accessing PhotoApp)

**PhotoApp's database now has:**
- ✅ User record (email, name, picture)
- ✅ Session record
- ✅ Encrypted refresh token (for future API calls)

**Browser NEVER saw:**
- ❌ client_secret
- ❌ ID Token (JWT)
- ❌ Access Token
- ❌ Refresh Token

**Only browser saw:**
- ✅ Authorization code (used once, now deleted)
- ✅ PhotoApp's session cookie

---

## Security Checkpoints

### Google's Security Checks:
1. ✓ Client credentials valid (`client_id` + `client_secret`)
2. ✓ Authorization code exists
3. ✓ Authorization code not expired (60 seconds)
4. ✓ Authorization code not used before
5. ✓ `redirect_uri` matches original request
6. ✓ Code issued to this specific client

### PhotoApp's Security Checks:
1. ✓ State parameter matches (CSRF protection)
2. ✓ ID Token signature valid (from Google)
3. ✓ ID Token not expired
4. ✓ ID Token audience matches (`aud` claim)
5. ✓ ID Token issuer is Google (`iss` claim)
6. ✓ Nonce matches (replay protection)
7. ✓ Email verified

---

## What Gets Stored Where

### Browser:
```
Google Session Cookie:
  Domain: .google.com
  Name: SID
  Purpose: Access Google services
  
PhotoApp Session Cookie:
  Domain: photoapp.com
  Name: session_id
  Purpose: Access PhotoApp
```

### Google's Database:
```
Authorization Codes: DELETED after use
  
Access Tokens:
  token: "ya29..."
  userId: "103547..."
  expiresAt: +1 hour
  
Refresh Tokens:
  token: "1//0gLN..."
  userId: "103547..."
  expiresAt: +30 days
```

### PhotoApp's Database:
```
Users:
  id: 1
  googleId: "103547..."
  email: "alice@gmail.com"
  name: "Alice Smith"
  
Sessions:
  sessionId: "abc123..."
  userId: 1
  expiresAt: +24 hours
  
User Tokens:
  userId: 1
  provider: "google"
  refreshToken: encrypted("1//0gLN...")
```

---

## Timeline

```
T+0s:   User clicks "Login with Google"
T+1s:   Browser redirects to Google
T+2s:   User enters password (if not logged in)
T+3s:   User clicks "Allow"
T+4s:   Google creates authorization code (60s lifespan)
T+4s:   Browser redirects to PhotoApp with code
T+5s:   PhotoApp exchanges code for tokens (back-channel)
T+6s:   Google validates, issues ID Token (signed with private key)
T+7s:   PhotoApp verifies ID Token (with public key)
T+8s:   PhotoApp creates user account / session
T+9s:   PhotoApp sends session cookie to browser
T+10s:  User sees dashboard - LOGGED IN!
```

---

## Summary

**Complete flow:**
1. User clicks sign-in → PhotoApp redirects to Google
2. User authenticates → Google creates authorization code
3. Browser sends code to PhotoApp
4. PhotoApp exchanges code for ID Token (server-to-server)
5. Google validates everything, signs ID Token with private key
6. PhotoApp verifies ID Token with Google's public key
7. PhotoApp creates local user account and session
8. User is logged in!

**Key principles:**
- Authorization code in front-channel (browser)
- ID Token in back-channel (server-to-server)
- Browser never sees sensitive tokens
- Cryptographic signatures ensure authenticity
- Multiple layers of validation at every step
