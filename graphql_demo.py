import strawberry
from fastapi import FastAPI
from strawberry.fastapi import GraphQLRouter
from typing import List, Optional

# --- Data Source (In-Memory) ---
users_db = [
    {"id": "1", "name": "Alice", "email": "alice@custom.com"},
    {"id": "2", "name": "Bob", "email": "bob@builder.com"},
]

posts_db = [
    {"id": "101", "title": "GraphQL is Cool", "author_id": "1"},
    {"id": "102", "title": "Understanding Resolvers", "author_id": "1"},
    {"id": "103", "title": "REST vs GraphQL", "author_id": "2"},
]

# --- Schema Definition ---

@strawberry.type
class Post:
    id: strawberry.ID
    title: str
    author_id: str

    @strawberry.field
    def author(self) -> 'User':
        # Resolve the Author for this post
        for u in users_db:
            if u["id"] == self.author_id:
                return User(id=u["id"], name=u["name"], email=u["email"])
        raise Exception("Author not found")

@strawberry.type
class User:
    id: strawberry.ID
    name: str
    email: str

    @strawberry.field
    def posts(self) -> List[Post]:
        # Resolve all posts for this user (N+1 Solution is DataLoader, but let's keep it simple)
        return [
            Post(id=p["id"], title=p["title"], author_id=p["author_id"])
            for p in posts_db if p["author_id"] == self.id
        ]

# --- Query Definition ---

@strawberry.type
class Query:
    @strawberry.field
    def users(self) -> List[User]:
        return [User(**u) for u in users_db]

    @strawberry.field
    def user(self, id: strawberry.ID) -> Optional[User]:
        for u in users_db:
            if u["id"] == id:
                return User(**u)
        return None


# --- Mutation Definition ---

@strawberry.type
class Mutation:
    @strawberry.mutation
    def create_user(self, name: str, email: str) -> User:
        new_id = str(len(users_db) + 1)
        new_user = {"id": new_id, "name": name, "email": email}
        users_db.append(new_user)
        return User(**new_user)

    @strawberry.mutation
    def create_post(self, title: str, author_id: str) -> Post:
        new_id = str(len(posts_db) + 101) # Simple ID gen
        new_post = {"id": new_id, "title": title, "author_id": author_id}
        posts_db.append(new_post)
        return Post(id=new_id, title=title, author_id=author_id)

# --- App Setup ---
schema = strawberry.Schema(query=Query, mutation=Mutation)
graphql_app = GraphQLRouter(schema)

app = FastAPI()
app.include_router(graphql_app, prefix="/graphql")

if __name__ == "__main__":
    import uvicorn
    print("\n🚀 Starting GraphQL Demo on http://localhost:8000/graphql")
    uvicorn.run(app, host="0.0.0.0", port=8000)
