# Documentation Standards for System Design Documents

This guide defines the structure, depth, and style for creating technical documentation in this repository. All framework/technology documents should follow this consistent pattern.

---

## Writing Philosophy: Steps Before Code

**Guiding Principle**: Documentation is for **understanding concepts**, not copying code.

**The 90/10 Rule**:
- **90% of content**: Step-by-step narrative explanations
- **10% of content**: Code snippets (only where absolutely necessary)

**Why Steps Over Code**:
1. **Accessibility**: Readers don't need to be language experts
2. **Clarity**: Plain English is faster to scan than code
3. **Maintainability**: Steps don't break when syntax changes
4. **Focus**: Concepts over implementation details

**When to Use Steps** (Preferred):
- ✅ Workflows and lifecycles (e.g., "How a message flows through Kafka")
- ✅ State transitions (e.g., "Raft leader election process")
- ✅ Failure scenarios (e.g., "What happens when node crashes")
- ✅ Walkthroughs (e.g., "Batch job from submission to completion")

**When Code Is Necessary** (Sparingly):
- Data structure definitions (e.g., Redis Stream internal struct)
- Configuration parameters (e.g., `mapreduce.task.io.sort.mb=200`)
- Critical algorithms (e.g., Chandy-Lamport snapshot pseudocode)
- API examples (e.g., single function call showing key concept)

**Before/After Example**:

❌ **Too Much Code** (Bad):
```java
// 40 lines of Java code
public class WordCountJob {
    public static void main(String[] args) throws Exception {
        Configuration conf = new Configuration();
        Job job = Job.getInstance(conf, "word count");
        job.setJarByClass(WordCount.class);
        job.setMapperClass(TokenizerMapper.class);
        // ... 30 more lines
    }
}
```

✅ **Step-Focused** (Good):
```markdown
### Step 1: Job Submission (t=0s)
**Component**: Client application
**Action**: Submits WordCount JAR to YARN ResourceManager
**Configuration**: 
- Job name: "word count"
- Mapper: TokenizerMapper (splits lines into words)
- Reducer: IntSumReducer (sums counts)
**State**: Job ID assigned, moves to ACCEPTED queue
```

---

## Forbidden Sections

The following sections must **NOT** appear in any documentation:

### ❌ Troubleshooting Commands
**Why Forbidden**: 
- Commands become outdated quickly
- Belong in runbooks/ops docs, not conceptual documentation
- Create false sense of comprehensive debugging (impossible to cover all cases)

**Instead**: Use "Failure Scenarios" section with conceptual resolution strategies

### ❌ Monitoring Alerts
**Why Forbidden**:
- Alert thresholds vary drastically by org (Netflix ≠ startup)
- Belong in observability platforms (Datadog, Grafana)
- Create maintenance burden (alerts change frequently)

**Instead**: Use "Critical Metrics" with descriptions and general targets (not specific alerts)

**Acceptable Alternative**:
```markdown
## 10. Production Checklist

**Critical Metrics**:

batch_shuffle_spill_bytes_total:
  Description: Bytes written to disk during shuffle
  Target: < 10% of shuffle data size
  Why it matters: High spills indicate insufficient memory
  Fix: Increase mapreduce.task.io.sort.mb
```

---

## Document Type Classification

Before writing a document, identify which category it belongs to. This determines your approach to code examples, technical depth, and explanation style.

### Type 1: Tool/Technology Documentation

**Purpose**: Explain how a specific software system works internally and how to use it.

**Examples**: Kafka, PostgreSQL, Redis, Elasticsearch, Kubernetes, RabbitMQ, Flink, Kafka Streams

**Characteristics**:
- **Code Level**: Minimal - show only key API calls (3-5 lines max per section)
- **Primary Style**: Step-by-step narrative with numbered workflows
- **Configuration**: Show exact parameter names and typical values
- **API Examples**: One or two critical function calls, not full applications
- **Walkthrough**: 8-10 detailed steps explaining lifecycle (NOT full code)

**When to Use**: Documenting a real software product that users install and operate.

**Code-to-Steps Ratio**: 5% code, 90% steps, 5% diagrams

**Example Snippet** (acceptable level of code):
```markdown
### Step 3: Consumer Polls for Messages

**API Call**:
\\`\\`\\`java
ConsumerRecords<String, String> records = consumer.poll(Duration.ofMillis(100));
\\`\\`\\`

**What Happens**:
1. Consumer sends FETCH request to partition leaders
2. Brokers check consumer's current offset (e.g., offset=1000)
3. Brokers return records from offset 1000 to latest
4. Consumer updates in-memory buffer with fetched records
5. Application iterates over records for processing
```

---

### Type 2: Concept/Pattern Documentation

**Purpose**: Explain architectural patterns, algorithms, and coordination protocols.

**Examples**: Saga Pattern, TCC, CQRS, CDC, Raft Consensus, Two-Phase Commit, Gossip Protocol

**Characteristics**:
- **Code Level**: Almost none - use pseudocode or numbered steps
- **Primary Style**: State machine diagrams + numbered procedural steps
- **Pseudocode**: Acceptable for clarity, but prefer plain English
- **Focus**: Understanding the pattern/algorithm, not implementation details

**When to Use**: Documenting a design pattern, algorithm, or protocol that can be implemented in any language.

**Code-to-Steps Ratio**: 0% code, 95% steps, 5% diagrams

**Example Snippet**:
```markdown
### Two-Phase Commit Protocol

**Phase 1: Prepare**
1. Coordinator sends PREPARE message to all participants
2. Each participant:
   - Validates transaction locally
   - Locks necessary resources
   - Writes undo/redo logs to durable storage
3. Participant replies: PREPARED (ready to commit) or ABORT (cannot commit)
4. Coordinator waits for all responses with timeout (default: 30s)

**Phase 2: Commit**
5. If ALL participants replied PREPARED:
   - Coordinator sends COMMIT to all
   - Decision logged before sending (recovery point)
6. If ANY participant replied ABORT or timed out:
   - Coordinator sends ABORT to all
7. Participants execute decision and release locks
```

---

### Type 3: First Principles Documentation

**Purpose**: Deep technical explanations of fundamental computer science concepts.

**Examples**: TCP/IP mechanics, DNS resolution, File system internals, OS scheduling, Memory management

**Characteristics**:
- **Code Level**: System-level code where necessary (kernel, syscalls)
- **Primary Style**: Packet/syscall-level trace with steps
- **Layered Explanation**: Trace through application → OS → kernel → hardware
- **Focus**: Teaching fundamental principles that apply across all implementations

**When to Use**: Documenting how foundational systems work at the OS/network/hardware level.

**Code-to-Steps Ratio**: 15% system code, 80% steps, 5% diagrams

**Example Snippet**:
```markdown
### TCP Three-Way Handshake

**Step 1: Client Sends SYN** (t=0ms)
1. Application calls `connect(sockfd, server_addr)`
2. Kernel allocates TCB (Transmission Control Block)
3. Generates random ISN (Initial Sequence Number): e.g., ISN=1000
4. Sends packet: SYN, seq=1000, window=65535
5. Starts retransmission timer (timeout=1s)
6. Moves to SYN_SENT state

**Step 2: Server Responds SYN-ACK** (t=10ms)
1. Server kernel receives SYN packet
2. Looks up listening socket on port 80
3. Creates new socket in SYN_RECEIVED state
4. Sends: SYN-ACK, seq=2000, ack=1001, window=65535
5. Adds to SYN queue (pending connections)

**Step 3: Client Sends ACK** (t=20ms)
1. Client receives SYN-ACK
2. Sends: ACK, seq=1001, ack=2001
3. Moves to ESTABLISHED state
4. `connect()` returns to application (connection ready)

**Step 4: Server Completes** (t=30ms)
1. Server receives ACK
2. Moves socket from SYN queue to accept queue
3. Transitions to ESTABLISHED state
4. `accept()` returns new socket to server application
```

---

## Document Structure (10 Sections)

All documents must follow this exact organizational flow:

### 1. Introduction
**Purpose**: Set context and explain what the technology is.

**Required Content**:
*   What problem does it solve?
*   Key differentiator from alternatives
*   Industry adoption/use case summary
*   Evolution/historical context (for Technology docs)

**Length**: 50-100 lines

---

### 2. Core Architecture
**Purpose**: Visual overview of the system's main components.

**Required Content**:
*   **Mermaid diagram** showing key components
*   Key components list (4-6 items)
*   Control plane vs data plane distinction (where applicable)

**Diagram Requirements**:
*   Quote labels with special characters: `N1["Node 1 (Leader)"]`
*   Use subgraphs to group related components
*   Include data flow arrows

**Length**: 30-50 lines

---

### 3. How It Works: Basic Mechanics
**Purpose**: Explain fundamental operations using **steps**, not code.

**Required Style**: Numbered steps describing workflows

**Required Subsections** (choose 2-4 relevant):
*   **A. Core Workflow** (step-by-step process)
*   **B. Distribution Model** (partitioning, sharding)
*   **C. Delivery Guarantees** (at-least-once, exactly-once)
*   **D. State Management** (how state is maintained)

**Length**: 80-120 lines

---

### 4. Deep Dive: Internal Implementation  
**Purpose**: Detailed technical explanation.

**Required Content**:
*   **4-6 subsections** (A, B, C, D, E, F)
*   2-3 Mermaid diagrams (state machines, sequence diagrams)
*   Performance characteristics with numbers
*   **Minimal code**: Only for data structures or critical algorithms

**Code-to-Steps Ratio**:
- Type 1: 15% code, 80% steps
- Type 2: 0% code, 95% steps  
- Type 3: 20% code, 75% steps

**Length**: 150-250 lines

---

### 5. End-to-End Walkthrough
**Purpose**: Trace a concrete example using step-by-step narrative.

**CRITICAL REQUIREMENTS**:
- **Minimum 8-10 numbered steps** (NOT 3-4)
- Each step format:
  ```markdown
  ### Step N: [Phase Name] (t=Xs)
  **Component**: Which system part acts
  **Action**: What happens (plain English)
  **State Change**: How state evolves
  **Timing**: Duration (if relevant)
  ```
- **Performance summary table** at end

**Code Usage - MINIMAL**:
- ❌ NO full application code (30+ lines)
- ❌ NO boilerplate (imports, error handling)
- ✅ Example data flowing through (JSON events)
- ✅ Small API snippets (2-3 lines) if critical
- ✅ Sequence diagram showing component interactions

**Goal**: Reader understands entire lifecycle without being a developer

**Length**: 150-250 lines

---

### 6. Failure Scenarios
**Purpose**: Common production issues with step-by-step resolution.

**Required Content**:
*   **3-4 scenarios** (Scenario A, B, C, D)
*   Each must include:
    - **Symptom**: What users/developers observe
    - **Cause**: Root technical reason
    - **Mechanism**: Step-by-step explanation of failure
    - **Diagram**: Mermaid sequence diagram showing failure
    - **The Fix**: Specific, actionable solutions (NOT bash commands)

**Forbidden**:
- ❌ Bash/kubectl commands for troubleshooting
- ❌ Log parsing scripts

**Acceptable**:
- ✅ Configuration changes (e.g., "Increase `timeout.ms` from 5000 to 10000")
- ✅ Conceptual fixes (e.g., "Enable standby replicas")

**Length**: 80-120 lines

---

### 7. Performance Tuning / Scaling Strategies
**Purpose**: How to optimize and scale.

**Required Content**:
*   **Configuration table** (parameter, recommended value, why)
*   Horizontal scaling approach
*   Vertical scaling approach

**Table Format**:
```markdown
| Configuration | Recommended | Why? |
|:--------------|:------------|:-----|
| `param.name` | Value/Formula | Technical explanation |
```

**Length**: 50-80 lines

---

### 8. Constraints & Limitations
**Purpose**: Honest assessment of what the technology cannot do well.

**Required Format**: Table

```markdown
| Constraint | Limit | Why? |
|:-----------|:------|:-----|
| **Feature** | Specific number/limit | Technical reason |
```

**Length**: 30-50 lines

---

### 9. When to Use?
**Purpose**: Decision matrix for technology selection.

**Required Format**: Table with clear verdicts

```markdown
| Use Case | Verdict | Why? |
|:---------|:--------|:-----|
| **Specific scenario** | ✅ YES / ❌ NO / ⚠️ MAYBE | Justification |
```

**Guidelines**:
*   At least 5 use cases
*   Mix YES and NO recommendations
*   Be honest about weaknesses

**Length**: 40-60 lines

---

### 10. Production Checklist
**Purpose**: Actionable deployment recommendations + critical metrics.

**Required Format**:
1. **Checklist** (6-8 items with checkboxes)
2. **Critical Metrics** (4-6 metrics with descriptions)

**Checklist Item Format**:
```markdown
1. [ ] **Action**: Specific configuration or setup step
```

**Metrics Format** (NOT alert rules):
```markdown
metric_name_total:
  Description: What it measures
  Target: Reasonable goal (not hard threshold)
  Why it matters: Impact explanation
  Fix: How to address if problematic
```

**Forbidden**:
- ❌ Bash scripts for setup
- ❌ Alert threshold rules (e.g., "Alert if > 100")
- ❌ Monitoring dashboard JSON

**Acceptable**:
- ✅ Configuration checklist
- ✅ Metric descriptions with general targets
- ✅ Resolution strategies

**Length**: 60-100 lines

---

## Technical Depth Requirements

All documents must meet these standards:

**Quantify Everything**:
- ❌ "Fast" → ✅ "1-100ms latency"
- ❌ "Large" → ✅ "TB-scale state"
- ❌ "Many" → ✅ "10,000+ events/second"

**Show State Transitions**:
- Include exact state names (e.g., SUBMITTED → ACCEPTED → RUNNING)
- Show timing between transitions

**Provide Concrete Examples**:
- Use real numbers (not variables): "offset=1050" not "offset=N"
- Use realistic scenarios

---

## Writing Style Guidelines

**Tone**:
- Technical but accessible
- Narrative style (tell a story)
- Honest about trade-offs

**Language**:
- Active voice: "Kafka stores messages" not "Messages are stored"
- Concrete examples: "10GB partition" not "large dataset"
- Define acronyms on first use

**Formatting**:
- **Bold** for emphasis on key concepts
- `Backticks` for code identifiers, parameters, file paths
- Tables for comparisons
- Numbered lists for steps (always)

---

This completes the documentation standards. Follow this structure rigorously for consistency across all documents.
