# GraphQL: Asking for Exactly What You Want

In 2012, Facebook was struggling. Their mobile app was slow and crashed often because it had to make dozens of REST calls to fetch a news feed (User, Posts, Comments, Likes).

They invented **GraphQL** (Graph Query Language) to solve this. It was open-sourced in 2015.

---

## 1. The Problem: REST's Rigidity

REST is "Resource-Based". You get what the endpoint gives you.

### A. Over-fetching (Too Much Data)
*   **Scenario:** You just want a user's *name*.
*   **Action:** `GET /users/1`
*   **Result:** The server sends *Name, Email, Age, Address, History...* (20KB of data).
*   **Waste:** You burn the user's data plan for nothing.

### B. Under-fetching (The N+1 Problem)
*   **Scenario:** You want the names of the authors of the last 10 posts.
*   **Action:**
    1.  `GET /posts` (Get 10 posts).
    2.  `GET /users/1` (Author of Post 1)
    3.  `GET /users/2` (Author of Post 2)
    4.  ...
*   **Waste:** You made **11 network requests** for one screen. This kills mobile battery and latency.

---

## 2. The Solution: GraphQL

GraphQL changes the paradigm: **"The Client decides what it gets."**

*   **Single Endpoint:** There is only one URL: `/graphql`.
*   **Flexible Query:** You send a query describing the shape of the data you want.

### The Analogy
*   **REST** is like buying a pre-packaged set lunch. You get the burger, fries, and coke, even if you only wanted the burger.
*   **GraphQL** is like a buffet. You take exactly one burger and two fries. Nothing else.

---

## 3. The Core Mechanics

GraphQL has three main parts:

### A. The Schema (The Menu)
The server defines what is *possible*.
```graphql
type User {
  id: ID!
  name: String!
  posts: [Post]
}

type Post {
  title: String!
  author: User!
}
```

### B. The Query (The Order)
The client sends a JSON-like request.
```graphql
query {
  user(id: "123") {
    name       # I only want the name
    posts {
      title    # I only want titles of their posts
    }
  }
}
```

### C. The Resolver (The Chef)
For every field (`name`, `posts`), there is a function on the server that knows how to get that data.
*   `User.name` -> Returns string from DB.
*   `User.posts` -> Runs `SELECT * FROM posts WHERE author_id = ...`

---

## 4. REST vs GraphQL

| Feature | REST | GraphQL |
| :--- | :--- | :--- |
| **Endpoints** | Multiple (`/users`, `/posts`) | Single (`/graphql`) |
| **Data Fetching** | Fixed (Over/Under fetching) | Precise (Exact shape) |
| **Versioning** | `v1`, `v2` | Deprecation of fields |
| **Caching** | HTTP Native (Easy) | Application Level (Hard) |
| **Error Handling** | HTTP Status Codes (404, 500) | JSON `errors` array (Always 200 OK) |

---

## 5. Challenges
1.  **Complexity:** Setting up a GraphQL server (Schema + Resolvers) is harder than a simple REST API.
2.  **Caching:** You can't just use standard HTTP caching (CDN/Browser) because every request is a `POST` to the same URL. You need specialized clients (Apollo, Relay).
3.  **Security (Depth Attack):** A malicious user can write a query like `user { friends { friends { friends ... } } }` to crash your server. You need "Query Depth Limiting".
