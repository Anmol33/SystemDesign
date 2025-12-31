# 06. Authorization Models: RBAC, ABAC, ACL, ReBAC

## 1. Introduction

**Authorization (AuthZ)** determines *access rights*: "Authorized to do what?"

Choosing the right model is critical for scalability. If usage patterns change, retrofitting a new model is extremely expensive.

**The Major Contenders**:
1.  **ACL (Access Control Lists)**: Simple, granular.
2.  **RBAC (Role-Based)**: Group-centric, scalable admin.
3.  **ABAC (Attribute-Based)**: Policy-centric, fine-grained.
4.  **ReBAC (Relationship-Based)**: Graph-centric (social).

---

## 2. Core Architecture Overview

| Model | Focus | Question Answered | Example |
| :--- | :--- | :--- | :--- |
| **ACL** | Resource | "Who can touch *this specific file*?" | "User Bob can read secrets.txt" |
| **RBAC** | Role | "What can *people like this* do?" | "Admins can read any file" |
| **ABAC** | Policy | "Do attributes align?" | "Access allowed if user.dept == doc.dept" |
| **ReBAC** | Graph | "How are we related?" | "Allowed if User is Friend of Owner" |

---

## 3. How It Works: Comparative Analysis

### A. ACL (Access Control List)
Each resource carries a list of allowed users/groups.

```mermaid
graph TD
    File["File: secrets.txt"]
    ACL["Access Control List"]
    U1["User: Bob → Read"]
    U2["User: Alice → Write"]
    G1["Group: Admins → Full Control"]
    
    File --> ACL
    ACL --> U1
    ACL --> U2
    ACL --> G1
    
    Check{"Check: Can Bob Write?"}
    Check -->|"Lookup ACL"| ACL
    ACL -->|"Bob: Read only"| Deny["❌ Deny"]
    
    style File fill:#e6f3ff
    style Deny fill:#ff9999
```

*   **Structure**: `FileA -> [User1: Read, User2: Write]`
*   **Pros**: Dead simple. Good for "sharing" features (Google Drive individual sharing).
*   **Cons**: Nightmare to audit ("What files can User1 see?"). No centralization.

### B. RBAC (Role-Based Access Control)
Users have Roles. Roles have Permissions.

```mermaid
graph LR
    User["User: Alice"]
    Role["Role: Editor"]
    P1["Permission: Read Documents"]
    P2["Permission: Edit Documents"]
    P3["Permission: Delete Documents"]
    
    User -->|"Has Role"| Role
    Role --> P1
    Role --> P2
    Role -.->|"Not granted"| P3
    
    Check{"Can Alice Delete?"}
    Check -->|"Check Role Permissions"| Role
    Role -->|"No Delete permission"| Deny["❌ Deny"]
    
    style Role fill:#e6ccff
    style Deny fill:#ff9999
```

*   **Structure**: `User -> Role -> [Permissions]`
*   **Pros**: Easy to reason about organizationally. "Hire Engineering Manager -> Give EngMgr Role".
*   **Cons**: **Role Explosion**. If you need "Manager who can edit *only* NY Office", you create `NY_Manager`, `CA_Manager`... (too many roles).

### C. ABAC (Attribute-Based Access Control)
Access is a boolean calculation based on attributes of User, Resource, and Environment.

```mermaid
graph TD
    User["User Attributes<br/>dept: Sales<br/>clearance: 3"]
    Resource["Resource Attributes<br/>dept: Sales<br/>classification: 2"]
    Env["Environment<br/>time: 14:30<br/>location: Office"]
    
    Policy{"Policy Engine<br/>IF user.dept == resource.dept<br/>AND user.clearance >= resource.classification<br/>AND time < 17:00<br/>THEN Allow"}
    
    User --> Policy
    Resource --> Policy
    Env --> Policy
    
    Policy -->|"All conditions met"| Allow["✅ Allow"]
    Policy -.->|"Any condition fails"| Deny["❌ Deny"]
    
    style Policy fill:#fff3cd
    style Allow fill:#ccffcc
    style Deny fill:#ff9999
```

*   **Structure**: `IF user.clearance > doc.classification AND time < 5pm THEN Allow`
*   **Pros**: Infinite flexibility. Solves Role Explosion.
*   **Cons**: Complex rules engine. Performance impact (evaluating logical rules on every read).

### D. ReBAC (Relationship-Based Access Control)
Permissions derive from relationships in a graph. (Popularized by Google Zanzibar).

```mermaid
graph LR
    Alice["User: Alice"]
    Team["Team: Sales"]
    Folder["Folder: Q4_Reports"]
    Doc["Document: Revenue.xlsx"]
    
    Alice -->|"member_of"| Team
    Team -->|"owner_of"| Folder
    Folder -->|"parent_of"| Doc
    
    Query{"Can Alice edit Revenue.xlsx?"}
    Query -->|"Graph traversal"| Path["Alice → member_of → Team<br/>Team → owner_of → Folder<br/>Folder → parent_of → Doc<br/>Owners can edit"]
    Path --> Allow["✅ Allow"]
    
    style Path fill:#e6f3ff
    style Allow fill:#ccffcc
```

*   **Structure**: `User -> member_of -> Group -> viewer_of -> Document`
*   **Pros**: Perfect for hierarchical/social apps (Google Docs folders, Social Networks).
*   **Cons**: Requires graph database or specialized Tuple Store.

---

## 4. Deep Dive: The Logic differences

```mermaid
graph TD
    subgraph ACL
    R1[Resource] -->|List| U1[User List]
    end
    
    subgraph RBAC
    U2[User] -->|Assigned| Role[Role]
    Role -->|Contains| Perm[Permission]
    end
    
    subgraph ABAC
    Policy{Policy Engine}
    Msg["User Attr + Resource Attr + Env"] --> Policy
    Policy -->|Eval| Dec[Allow/Deny]
    end
    
    subgraph ReBAC
    U3[User] -->|Link| G[Group]
    G -->|Link| Doc[Document]
    Note[Walk the Graph]
    end
```

---

## 5. End-to-End Walkthrough: "Edit Document" Scenario

Scenario: Alice wants to Edit "Doc-101".

**ACL Approach**:
1. Check `Doc-101` ACL table.
2. Is `Alice` in list with `Edit`? Yes → Allow.

**RBAC Approach**:
1. Check roles for `Alice`: Has `Editor` role.
2. Check `Editor` role: Has `Static:EditDocument` permission? Yes.
3. *Challenge*: Does `Editor` apply to *all* docs? Usually yes. RBAC struggles with specific instances.

**ABAC Approach**:
1. Fetch Alice attributes (`dept=Sales`).
2. Fetch Doc-101 attributes (`dept=Sales`, `status=Draft`).
3. Evaluate Policy: `Allow if user.dept == doc.dept AND doc.status == Draft`.
4. Match? Yes → Allow.

**ReBAC Approach**:
1. Query Graph: `Check (Alice, 'editor', Doc-101)`.
2. Graph search: Alice is `member` of SalesTeam. SalesTeam is `owner` of Doc-101. Owners are `editors`. Path found. Yes → Allow.

---

## 6. Failure Scenarios

### Scenario A: Role Explosion (RBAC Failure)
**Symptom**: Hundreds of roles, impossible to manage, role assignment errors.
**Cause**: Creating hyper-specific roles instead of using attributes.
**Mechanism**: Business requirements outpace RBAC's coarse-grained model.

```mermaid
gantt
    title Role Explosion Timeline
    dateFormat YYYY
    section Year 1
    5 Base Roles (Admin, Manager, Engineer, Viewer, Guest) :done, 2020, 2021
    section Year 2
    Add Shift Variants (DayShift, NightShift, WeekendShift) :done, 2021, 2022
    20 Roles Total :milestone, 2022, 0d
    section Year 3
    Add Department + Shift (Sales_DayShift, Eng_NightShift...) :crit, 2022, 2023
    100 Roles Total :milestone, 2023, 0d
    section Year 4
    Add Location + Dept + Shift (NYC_Sales_DayShift...) :crit, 2023, 2024
    500 Roles - UNMANAGEABLE :milestone, 2024, 0d
```

**Example Progression**:
- **Year 1**: `Admin`, `Manager`, `Engineer`, `Viewer`, `Guest` (5 roles)
- **Year 2**: Add shifts → `DayShiftNurse`, `NightShiftNurse`, `WeekendNurse` (20 roles)
- **Year 3**: Add departments → `ICU_DayShiftNurse`, `ER_NightShiftNurse` (100 roles)
- **Year 4**: Add locations → `NYC_ICU_DayShiftNurse` (500 roles) ❌

**The Fix**:
- **Switch to ABAC**: `Role: Nurse` + `Attributes: {shift: "day", department: "ICU", location: "NYC"}`
- **Policy**: `Allow if user.role == "Nurse" AND user.shift == current_shift`
- **Result**: 5 base roles + dynamic attribute evaluation

---

### Scenario B: Performance Crawl (ABAC Failure)
**Symptom**: API latency increases from 50ms to 500ms.
**Cause**: Policy evaluation requires multiple external service calls.
**Mechanism**: Each request fetches attributes from HR DB, Asset DB, Time Server.

**The Fix**:
- **Cache Attributes**: Store user attributes in JWT (dept, clearance)
- **Simplify Policies**: Avoid complex joins or external calls
- **Decision Caching**: Cache policy decisions for 5 minutes
- **Local Evaluation**: Use OPA (Open Policy Agent) for in-process evaluation

---

### Scenario C: Orphaned Access (ACL Failure)
**Symptom**: Former employee still has access to sensitive files.
**Cause**: Direct user-to-resource ACL entries not cleaned up.
**Mechanism**: Alice removed from "Sales Team" but still has individual ACL entries on 50 files.

**The Fix**:
- **Use Groups**: Grant ACL to groups, not individual users
- **Automated Cleanup**: When user deactivated, scan and remove all ACL entries
- **Audit**: Regular access reviews ("Who can access this file?")
- **Prefer RBAC/ABAC**: Centralized permission management

---

## 7. Performance Tuning

| configuration | Recommendation |
| :--- | :--- |
| **ACL** | Store in SQL (indexed by ResourceID). Fast. |
| **RBAC** | Embed Role/Permissions in JWT. Zero DB lookups at runtime. Very Fast. |
| **ABAC** | Evaluate locally (OPA - Open Policy Agent). Don't make external HTTP calls during eval. |
| **ReBAC** | Use specialized index (Google Zanzibar stores recursive relationships in flat lists for speed). |

---

## 8. Constraints & Limitations

| Model | Complexity | Scalability (Data) | Flexibility |
| :--- | :--- | :--- | :--- |
| **ACL** | Low | Low (1M docs x 10 users = Huge table) | High (Per item) |
| **RBAC** | Low | High (Roles are static) | Low (All or nothing) |
| **ABAC** | High | High (Rules scale well) | Highest |
| **ReBAC**| Very High | Very High (Google scale) | Very High |

---

## 9. When to Use? (Decision Matrix)

| Scenario | Model | Why? |
| :--- | :--- | :--- |
| **B2B SaaS (CRM, Admin Panels)** | **RBAC** | Structures correspond to Job Titles (Admin, User, Viewer). |
| **Google Drive / Dropbox** | **ACL / ReBAC** | Need sharing at individual file/folder level. |
| **AWS / Cloud Infrastructure** | **ABAC** | "Allow if ResourceTag == UserTag". |
| **Social Network** | **ReBAC** | "Friends of friends can see photo". |
| **Government / Healthcare** | **ABAC** | Strict rules ("Only doctor of patient X can view records"). |

---

## 10. Production Checklist

1.  [ ] **Default Deny**: Implicitly deny everything unless an Allow rule exists.
2.  [ ] **Decouple Policy**: Don't hardcode `if (user == 'bob')` in code. Usage middleware or policy engine (OPA).
3.  [ ] **Audit Logging**: Log *why* access was granted/denied (e.g., "Matched Rule #5").
4.  [ ] **Fail Closed**: If AuthZ service is down, nobody gets in.
5.  [ ] **Test Permissions**: Unit test authorization logic ("Ensure 'Intern' cannot 'DeleteDB'").
6.  [ ] **Break-glass**: Mechanism for SuperAdmin to bypass rules in emergency.
7.  [ ] **Start Simple**: Start with RBAC. only add ABAC/ReBAC if business logic effectively demands it.
