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
```json
{
  "id": 1,
  "name": "Alice",
  "links": [
    { "rel": "self", "href": "/users/1" },
    { "rel": "delete", "href": "/users/1", "method": "DELETE" }
  ]
}
```
*   **Benefit:** The client doesn't need to hardcode URLs. It just follows links.
