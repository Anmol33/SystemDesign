

# **The Raft Consensus Algorithm: A Guide to Understandable Distributed Consensus**

## **Introduction**

In the world of distributed systems, one of the most fundamental challenges is achieving **consensus**: the process of getting a group of independent computers to agree on a shared state, even in the presence of failures.1 For years, the go-to solution for this problem was the Paxos algorithm, a protocol known for its mathematical correctness but also for its notorious complexity.3 This complexity made Paxos difficult to implement and even harder to understand, creating a gap between theory and practice.

In 2014, Diego Ongaro and John Ousterhout introduced **Raft**, a new consensus algorithm designed with a primary goal: understandability.4 Raft was engineered to be as effective as Paxos but far easier to grasp by separating the logic of consensus into three distinct, manageable subproblems: leader election, log replication, and safety.4 This focus on understandability has made Raft the de-facto standard for achieving consistency in modern distributed systems, with popular implementations in critical infrastructure tools like Consul, etcd, and CockroachDB.1

Raft provides a generic way to build fault-tolerant, strongly-consistent services by implementing a **replicated state machine**.4 In this model, a log of commands is replicated across all servers in a cluster. Each server's state machine executes the same sequence of commands from the log, ensuring that all servers arrive at the same state and produce the same output.2 Raft's job is to ensure this replicated log remains consistent across the entire cluster.

## **Core Concepts of Raft**

To understand how Raft works, it's essential to first grasp its core terminology and components.5

* **Server States:** At any given time, every server in a Raft cluster is in one of three states 4:  
  1. **Leader:** The leader is responsible for managing the cluster. It handles all client requests, manages the replication of the log to other servers, and sends out regular "heartbeats" to maintain its authority.6 There can be only one leader at a time.  
  2. **Follower:** Followers are passive. They respond to requests from the leader and candidates. They do not initiate communication themselves. All servers start in the follower state.2  
  3. **Candidate:** A follower becomes a candidate when it starts a new election to become the leader, which typically happens when it stops receiving heartbeats from the current leader.4  
* **Term:** Raft divides time into arbitrary periods called **terms**.2 Each term begins with a leader election. If an election is successful, a single leader manages the cluster for the rest of the term. If the election results in a split vote, the term ends, and a new term (with a new election) begins immediately.4 Terms act as a logical clock, allowing servers to detect obsolete information, such as stale leaders.2  
* **Replicated Log:** The primary unit of work in Raft is a log entry. The problem of achieving consensus is decomposed into managing a **replicated log**—an ordered sequence of commands that each server stores.2 The log is considered consistent if all servers agree on the entries and their order.  
* **Finite State Machine (FSM):** Each server has a finite state machine. As log entries are committed, they are applied to the FSM in order, causing the server to transition between states. A critical rule is that the FSM must be deterministic: applying the same sequence of log entries must always result in the same state.5  
* **Quorum:** A quorum is a majority of the servers in a cluster. For a cluster of N servers, a quorum requires at least (N/2) \+ 1 members.5 Raft requires a quorum for key decisions, such as electing a leader and committing a log entry. This majority rule is what makes the system fault-tolerant; the cluster can continue to operate as long as a quorum of servers is available.1  
* **Remote Procedure Calls (RPCs):** Raft servers communicate using two primary types of RPCs 2:  
  1. **RequestVote RPC:** Used by candidates during an election to request votes from other servers.  
  2. **AppendEntries RPC:** Used by the leader to replicate log entries to followers. These RPCs also serve as heartbeats to prevent followers from starting new elections.4

## **The Three Subproblems of Raft**

Raft's design elegantly breaks down the complex problem of consensus into three more-or-less independent subproblems.2

### **1\. Leader Election**

Raft uses a leader-based approach, where one server is elected to take primary responsibility for managing the cluster.4 The election process ensures that if the leader fails, a new one is quickly and safely chosen.

1. **Starting an Election:** All servers begin as followers. A follower remains in this state as long as it receives valid heartbeats (AppendEntries RPCs) from a leader. If a follower goes a certain amount of time without hearing from a leader—a period called the **election timeout** (typically between 150-300ms)—it assumes the leader has failed and begins an election.4  
2. **The Candidate State:** To start an election, the follower transitions to the candidate state. It immediately does three things 4:  
   * Increments its current term number.  
   * Votes for itself.  
   * Sends a RequestVote RPC to all other servers in the cluster.  
3. **Voting and Election Outcomes:** A candidate remains in this state until one of three things happens 2:  
   * **It wins the election:** If the candidate receives votes from a **quorum** (majority) of the servers, it is promoted to leader. It then immediately starts sending heartbeats to all other servers to establish its authority and prevent new elections.4  
   * **Another server becomes leader:** While waiting for votes, the candidate may receive an AppendEntries RPC from another server claiming to be the leader. If that leader's term number is greater than or equal to the candidate's current term, the candidate recognizes the new leader as legitimate and transitions back to the follower state.4  
   * **A split vote occurs:** If multiple followers become candidates at the same time, they may split the votes such that no candidate achieves a majority. In this case, the term ends, and a new election begins after another timeout. Raft uses **randomized election timeouts** for each server to make split votes rare and to ensure they are resolved quickly.4

### **2\. Log Replication**

Once a leader has been elected, it is responsible for servicing all client requests and ensuring the log is replicated consistently across all followers.4

1. **Receiving Client Requests:** All client requests (commands to be executed by the state machines) are sent to the leader. The leader appends the command to its own log as a new entry.4  
2. **Replicating to Followers:** The leader then issues AppendEntries RPCs in parallel to all of its followers to replicate the new entry. These RPCs contain the new log entry as well as the term number and index of the preceding entry.4  
3. **Committing an Entry:** When a follower receives an AppendEntries RPC, it appends the new entry to its own log and sends an acknowledgment back to the leader. Once the leader has received acknowledgments from a **quorum** of followers, the entry is considered **committed**.4 This is the point of no return; a committed entry is guaranteed to be durable and will eventually be executed by all state machines.  
4. **Applying to the State Machine:** After an entry is committed, the leader applies the command to its own state machine and returns the result to the client. The leader also notifies the followers (in subsequent AppendEntries RPCs) that the entry has been committed, and the followers then apply the command to their own state machines.4 This ensures that all servers in the cluster execute the same sequence of commands in the same order.2

### **3\. Safety**

Raft includes several safety rules to guarantee that the system behaves correctly and consistently, even in the face of failures. These rules ensure that if any server has applied a log entry, no other server can ever apply a different entry for the same log index.2

* **Election Safety:** Only one leader can be elected in a given term. This is guaranteed because a candidate must obtain a majority of votes to become leader, and each server can only vote once per term.  
* **Leader Append-Only:** A leader never overwrites or deletes entries in its log; it only appends new ones.  
* **Log Matching Property:** This is a critical guarantee: if two logs contain an entry with the same index and term, then the logs are identical in all entries up to that index.4 The leader enforces this property by forcing followers' logs to match its own. If a follower's log is inconsistent, the leader will find the last log entry where they agree and then overwrite all of the follower's subsequent entries with its own.4  
* **Leader Completeness Property:** This property states that if a log entry is committed in a given term, it will be present in the logs of the leaders for all subsequent terms.4 This is ensured by the voting process: a server will not vote for a candidate unless the candidate's log is at least as up-to-date as its own. "Up-to-date" is defined by comparing the index and term of the last entries in the logs.4 This restriction ensures that a newly elected leader will always have all previously committed entries.  
* **State Machine Safety:** As a consequence of the above properties, Raft guarantees that if a server has applied a log entry at a given index to its state machine, no other server will ever apply a different log entry for the same index.2

## **Advanced Features**

Beyond the core consensus mechanism, Raft also includes protocols for practical system management.

### **Log Compaction**

In a real system, the replicated log cannot be allowed to grow without bounds. Raft addresses this with **snapshotting**. As entries are committed and applied to the state machine, a server can take a snapshot of its current FSM state and save it to stable storage.4 Once a snapshot is saved, the server can safely discard all log entries up to that point. This process happens automatically and prevents the log from consuming unlimited disk space.5

### **Cluster Membership Changes**

Raft provides a safe, automated way to change the set of servers in a cluster (e.g., to add a new server or replace a failed one). This is handled using a two-phase approach called **joint consensus**.4 The cluster first transitions to a transitional configuration where decisions require a majority from both the old and new configurations. Once the joint consensus configuration is committed, the system transitions to the new configuration. This ensures that there is no point during the transition where a split vote could lead to two different leaders being elected in the same term.4

## **Conclusion**

The Raft consensus algorithm represents a significant step forward in making distributed systems more accessible and reliable. By prioritizing understandability through the separation of concerns, it provides a clear and provably safe foundation for building strongly-consistent, fault-tolerant services.4 Its leader-based approach simplifies the logic for managing a replicated log, and its safety mechanisms ensure that data remains correct even when servers fail. As the de-facto standard for consensus in modern distributed systems, a solid understanding of Raft is essential for any engineer or architect working on scalable and resilient applications.

#### **Works cited**

1. What is the Raft Consensus Algorithm? \- Yugabyte, accessed on November 1, 2025, [https://www.yugabyte.com/key-concepts/raft-consensus-algorithm/](https://www.yugabyte.com/key-concepts/raft-consensus-algorithm/)  
2. Deep Dive into Raft: Consensus Algorithms in Distributed Systems | Medium, accessed on November 1, 2025, [https://medium.com/@hsinhungw/deep-dive-into-raft-consensus-algorithms-in-distributed-systems-6052231ca0e5](https://medium.com/@hsinhungw/deep-dive-into-raft-consensus-algorithms-in-distributed-systems-6052231ca0e5)  
3. What is Paxos Consensus Algorithm? Definition & FAQs | ScyllaDB, accessed on November 1, 2025, [https://www.scylladb.com/glossary/paxos-consensus-algorithm/](https://www.scylladb.com/glossary/paxos-consensus-algorithm/)  
4. Raft (algorithm) \- Wikipedia, accessed on November 1, 2025, [https://en.wikipedia.org/wiki/Raft\_(algorithm)](https://en.wikipedia.org/wiki/Raft_\(algorithm\))  
5. Consensus | Consul \- HashiCorp Developer, accessed on November 1, 2025, [https://developer.hashicorp.com/consul/docs/concept/consensus](https://developer.hashicorp.com/consul/docs/concept/consensus)  
6. Leader Follower Pattern in Distributed Systems \- GeeksforGeeks, accessed on November 1, 2025, [https://www.geeksforgeeks.org/system-design/leader-follower-pattern-in-distributed-systems/](https://www.geeksforgeeks.org/system-design/leader-follower-pattern-in-distributed-systems/)