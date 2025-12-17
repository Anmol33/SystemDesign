# REST: The Structure of the Web

**REST** (Representational State Transfer) is not a protocol. It is an **Architectural Style**. 
It was defined by **Roy Fielding** in his 2000 Data Science PhD dissertation.

The core idea is simple: **Treat the network like a filesystem.**

---

## 1. The Philosophy: Nouns vs Verbs

Before REST, we built APIs like this (RPC Style):
*   `POST /getAllUsers`
*   `POST /createUser`
*   `POST /deleteUser?id=5`

**REST** says: Stop thinking about **actions** (verbs). Start thinking about **resources** (nouns).
*   `GET /users`
*   `POST /users`
*   `DELETE /users/5`

### Why?
Because nouns are stable. "User" will always exist. "DeleteUser" implies an implementation detail.

---

## 2. The 6 Constraints

To call an API "RESTful", it must satisfy these rules:

1.  **Client-Server:** Separated concerns. The UI code (React) is distinct from the Database code.
2.  **Stateless:** The Server must not store "Session State" in memory between requests. Every request must contain all necessary info (Tokens, IDs).
3.  **Cacheable:** The server must explicitly say if data is cacheable (`Cache-Control: max-age=3600`).
4.  **Uniform Interface:** The API should look the same everywhere. (Use standard HTTP verbs, standard URIs).
5.  **Layered System:** The client shouldn't know if it's talking to the Server, a Load Balancer, or a CDN.
6.  **Code on Demand (Optional):** The server can send executable code (like JavaScript) to the client.

---

## 3. The Verbs & Idempotency

Using the right verb is critical for caching and reliability.

| Verb | Action | Idempotent? | Safe? |
| :--- | :--- | :--- | :--- |
| **GET** | **Read** | ✅ Yes | ✅ Yes |
| **POST** | **Create** | ❌ No | ❌ No |
| **PUT** | **Replace** | ✅ Yes | ❌ No |
| **PATCH** | **Update** | ❌ No | ❌ No |
| **DELETE** | **Remove** | ✅ Yes | ❌ No |

### What is Idempotency?
**"Can I retry this request without messing things up?"**

*   **Example: DELETE**
    *   `DELETE /users/5` -> Returns 200 (Deleted).
    *   `DELETE /users/5` (Retry) -> Returns 404 (Not Found) or 200 (OK).
    *   **Outcome:** The user is gone. The state is the same. **Safe to retry.**

*   **Example: POST**
    *   `POST /payments` -> Charges $50.
    *   `POST /payments` (Retry) -> Charges **another** $50.
    *   **Outcome:** You lost $100. **Not safe to retry.**

---

## 4. Richardson Maturity Model

How "RESTful" is your API? Leonard Richardson defined 4 levels:

### Level 0: The Swamp of POX (Plain Old XML)
Using HTTP as a tunnel for RPC.
*   **Endpoint:** `/api` (Everything goes here)
*   **Method:** `POST` only.
*   **Body:** `<action>deleteUser</action>`

### Level 1: Resources
Breaking the system into resources.
*   **Endpoints:** `/users`, `/products`, `/orders`
*   **Method:** Still using `POST` for everything.

### Level 2: Verbs (Most APIs are here)
Using the correct HTTP methods.
*   `GET` for reading.
*   `DELETE` for deleting.
*   Using Status Codes correctly (200, 404, 201).

### Level 3: Hypermedia Controls (HATEOAS)
**H**ypermedia **A**s **T**he **E**ngine **O**f **A**pplication **S**tate.
The API tells you what you can do next.

**Response:**

## 5. Practical Design: The User Module

Let's apply these rules to build a **User Management API**.

### A. The Endpoints (Design Pattern)

| Method | Endpoint | Description | Status Code |
| :--- | :--- | :--- | :--- |
| **GET** | `/users` | List all users | 200 OK |
| **GET** | `/users?role=admin` | List admins (Filtering) | 200 OK |
| **GET** | `/users/1` | Get User #1 (Detail) | 200 OK |
| **POST** | `/users` | Create new User | **201 Created** |
| **PUT** | `/users/1` | Replace User #1 | 200 OK |
| **PATCH** | `/users/1` | Update User #1 (Email) | 200 OK |
| **DELETE** | `/users/1` | Delete User #1 | **204 No Content** |

### B. Understanding PUT vs PATCH
This is a comprehensive example.

**Original Resource (ID: 1):**
```json
{ "id": 1, "name": "Alice", "email": "alice@gmail.com" }
```

**Scenario: Changing Email to `alice@yahoo.com`**

**Option 1: using PATCH (Partial Update)**
*   **Request:** `PATCH /users/1` with `{ "email": "alice@yahoo.com" }`
*   **Result:** Name stays "Alice". Email updates.
*   **Pros:** Bandwidth efficient.

**Option 2: using PUT (Replacement)**
*   **Request:** `PUT /users/1` with `{ "email": "alice@yahoo.com" }`
*   **Result:** Name is **erased** (if you didn't send it). The resource is purely `{ "email": ... }` now.
*   **Rule:** PUT implies "Here is the NEW complete object."

### C. Hands-on Experiment
I have created a demo server: `rest_demo.py`.

1.  **Run Server:** `python rest_demo.py`
2.  **View Docs:** Visit `http://localhost:8000/docs` (It has a Swagger UI).
3.  **Test with Curl:**

**Create a User:**
```bash
curl -X POST "http://localhost:8000/users" \
     -H "Content-Type: application/json" \
     -d '{"name": "Charlie", "email": "charlie@test.com", "role": "admin"}'
```

---

## 6. The 3 Golden Rules of API Design

If you take nothing else from this doc, remember these rules.

### Rule 1: Always use Plural Nouns
*   ❌ **Don't:** `/user`, `/product`, `/order`
*   ✅ **Do:** `/users`, `/products`, `/orders`
    *   `GET /users` (Collection)
    *   `GET /users/1` (Single Item from the Collection)
    *   *Why?* It matches the Database table concept (`SELECT * FROM users`).

### Rule 2: Precision with Status Codes
Don't be the developer who returns `200 OK` for an error.
*   **200 OK:** Generic success.
*   **201 Created:** Specific to `POST`. Implies "I made a new resource".
*   **204 No Content:** Specific to `DELETE` or `PUT`. "I did it, but have nothing to say".
*   **400 Bad Request:** User sent garbage JSON.
*   **401 Unauthorized:** "Who are you?" (Missing Token).
*   **403 Forbidden:** "I know you, but you can't touch this." (Admin only).
*   **404 Not Found:** "ID 999 doesn't exist".

### Rule 3: Versioning is Mandatory
Your API **will** change. If you don't version from Day 1, you will break mobile apps.
*   ❌ **Bad:** `api.example.com/users`
*   ✅ **Good:** `api.example.com/v1/users`
    *   When you need to make a breaking change, you launch `/v2/users`.
    *   Old apps continue working on `/v1/`.

