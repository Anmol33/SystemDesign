from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List, Optional

app = FastAPI()

# --- Data Model ---
class User(BaseModel):
    id: int
    name: str
    email: str
    role: str = "user"  # Default role is "user"

class UserCreate(BaseModel):
    name: str
    email: str
    role: Optional[str] = "user"

class UserUpdate(BaseModel):
    name: Optional[str] = None
    email: Optional[str] = None
    role: Optional[str] = None

# --- In-Memory Database ---
db: List[User] = [
    User(id=1, name="Alice", email="alice@example.com", role="admin"),
    User(id=2, name="Bob", email="bob@example.com", role="user"),
]

# --- CRUD Endpoints ---

# 1. LIST Users (With Filter)
@app.get("/users", response_model=List[User])
def get_users(role: Optional[str] = None):
    if role:
        return [u for u in db if u.role == role]
    return db

# 2. GET One User
@app.get("/users/{user_id}", response_model=User)
def get_user(user_id: int):
    for user in db:
        if user.id == user_id:
            return user
    raise HTTPException(status_code=404, detail="User not found")

# 3. CREATE User
@app.post("/users", response_model=User, status_code=201)
def create_user(user_in: UserCreate):
    new_id = len(db) + 1
    new_user = User(id=new_id, **user_in.dict())
    db.append(new_user)
    return new_user

# 4. UPDATE User (Full Replace - PUT)
@app.put("/users/{user_id}", response_model=User)
def update_user(user_id: int, user_in: UserCreate):
    for i, user in enumerate(db):
        if user.id == user_id:
            # PUT replaces the entire resource. ID stays the same.
            updated_user = User(id=user_id, **user_in.dict())
            db[i] = updated_user
            return updated_user
    raise HTTPException(status_code=404, detail="User not found")

# 5. PATCH User (Partial Update)
@app.patch("/users/{user_id}", response_model=User)
def patch_user(user_id: int, user_in: UserUpdate):
    for i, user in enumerate(db):
        if user.id == user_id:
            # Only update fields that are provided
            updated_data = user_in.dict(exclude_unset=True)
            updated_user = user.copy(update=updated_data)
            db[i] = updated_user
            return updated_user
    raise HTTPException(status_code=404, detail="User not found")

# 6. DELETE User
@app.delete("/users/{user_id}", status_code=204)
def delete_user(user_id: int):
    for i, user in enumerate(db):
        if user.id == user_id:
            db.pop(i)
            return
    raise HTTPException(status_code=404, detail="User not found")

if __name__ == "__main__":
    import uvicorn
    print("\n🚀 Starting REST Demo Server on http://localhost:8000")
    print("📜 Documentation available at http://localhost:8000/docs\n")
    uvicorn.run(app, host="0.0.0.0", port=8000)
