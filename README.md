# Distributed High-Performance File Storage System (C++)

A robust, **self-healing**, and **multi-threaded** distributed storage solution built in **C++** using TCP sockets (**Winsock2**). This system achieves high-speed data transfer through **parallel chunking**, **thread-pooling**, and real-time **integrity verification**. It is designed to be **fault-tolerant**, automatically handling node failures through a dynamic discovery mechanism, **replication (RF=2)**, and **persistent metadata journaling**.

---

## Table of Contents
* [System Architecture](#system-architecture)
* [Key Technical Features](#key-technical-features)
* [Advanced Fault Tolerance](#advanced-fault-tolerance)
* [Storage Protocol](#storage-protocol)
* [Compilation and Execution](#compilation-and-execution)
* [Performance Benchmarks](#performance-benchmarks)
* [Future Roadmap](#future-roadmap)
* [Learning Outcomes](#learning-outcomes)
* [Author](#author)
* [System Visual Flow](#system-visual-flow)

---

## System Architecture

The system consists of three primary components working in a decentralized cluster:

### 1. Client Orchestrator
The **"Brain"** of the operation. It manages the file lifecycle: partitioning files into **optimized chunks**, calculating **SHA-256 fingerprints**, and managing a **ThreadPool of 8 concurrent workers** for simultaneous binary I/O.

### 2. Metadata Registry (MetaServer)
The central directory running on port **8001**. It tracks:
* **Active Cluster State:** Real-time list of live storage nodes via **heartbeats**.
* **Persistent File Map:** A disk-backed registry (**registry.db**) that stores which chunks belong to which nodes.
* **Integrity Anchor:** Stores the original **SHA-256 hash** of every file to ensure bit-perfect reconstruction.

### 3. Storage Node Workers
Independent worker processes that handle **binary disk I/O**. They notify the MetaServer of their health status using a **Push-based Heartbeat** model and store chunks as raw binary segments.

---

## Key Technical Features

### Parallel Transfer Engine
Utilizes a custom **ThreadPool (8 Threads)** to bypass the limitations of sequential TCP. The system saturates available bandwidth by uploading/downloading multiple chunks simultaneously, reaching observed local speeds exceeding **1.1 GB/s** on high-performance SSDs.

### End-to-End Data Integrity (SHA-256)
The system implements a **Shield of Truth** verification model:
* **Pre-Upload:** Client generates a unique **fingerprint (Hash)** of the file.
* **Storage:** MetaServer stores this hash as the global **Source of Truth**.
* **Post-Download:** Client re-hashes the reconstructed file and compares it. If a single bit was flipped during transfer, the system flags a **Content Mismatch**.

### Metadata Persistence (Journaling)
To eliminate the Metadata Server as a volatile **Single Point of Failure (SPOF)**, we implemented **Write-Ahead Logging (WAL)**:
* Every registration is appended to **registry.db**.
* Upon a crash or reboot, the MetaServer automatically restores its entire memory state from the **disk journal**.

### Dynamic Chunk Sizing
Analyzes file metadata to determine the most efficient segment size, balancing **network overhead** vs. **metadata count**:

| File Size | Chunk Size | Strategy |
| :--- | :--- | :--- |
| **< 1 MB** | 64 KB | Minimizes socket handshake overhead. |
| **50 MB - 500 MB** | 1 MB | Maximizes **sequential I/O** for medium data. |
| **> 2 GB** | 8 MB | Prevents metadata bloat in the registry. |

---

## Advanced Fault Tolerance

### Replication Factor (RF=2)
During upload, every chunk is automatically duplicated across **two different storage nodes**. Even if a node crashes, the file remains **100% available** for reconstruction from the secondary replica.

### Heartbeat and Node Janitor
* **Push Heartbeat:** Storage nodes send a signal to the MetaServer every **2 seconds**.
* **Janitor Thread:** A background process on the MetaServer that prunes any node that hasn't responded in **6 seconds**, ensuring the client never attempts to connect to a **dead node**.

### Intelligent Stall Detection
To prevent the client from hanging indefinitely during catastrophic node failures, the system monitors chunk progress. If progress stops for **12-15 consecutive seconds**, the client gracefully terminates, cleans up resources, and reports a **Stall Error** instead of merging a corrupted file.

---

## Storage Protocol

The custom TCP protocol utilizes a **Fixed-Header** approach for binary safety:
1. **Command Header:** (10 bytes) - e.g., "UPLOAD", "GET_CHUNK"
2. **Hash Metadata:** (string) - **SHA-256 Fingerprint** for integrity.
3. **Payload Size:** (int) - Total size of binary data.
4. **Binary Payload:** (bytes) - The actual raw file content.

---

## Compilation and Execution

### Compilation
Requires the **Winsock** library (`-lws2_32`) and **C++17** or higher.

```bash
# Compile Metadata Server
g++ metadata_server/MetadataServer.cpp -o meta -lws2_32

# Compile Storage Node
g++ storage_node/StorageServer.cpp -o node -lws2_32

# Compile Client (Include SHA256 and Chunker)
g++ client/main.cpp common/services/FileChunker.cpp -o client_app -lws2_32

g++ client/main.cpp common/services/FileChunker.cpp common/models/Chunk.cpp -o client_app -lws2_32


Execution
Start Metadata Server:

Bash
.\meta
Launch Storage Nodes (Multiple Terminals):

Bash
.\node 9001 data/node1
.\node 9002 data/node2
.\node 9003 data/node3
Run Client Operations:

Bash
# Upload and Verify
.\client_app upload samples/video.mp4

# Download and Failover Check
.\client_app download samples/video.mp4

# Sync (Automatic Upload then Download)
.\client_app sync samples/video.mp4


Performance Benchmarks
Transfer Speeds: Observed peaks of 1,100+ MB/s during parallel chunk retrieval.

Scalability: Linear performance increase as more physical nodes (ports) are added to the cluster.

Throughput Optimization: 1.3 GB files processed, transferred, and verified in ~1.2 seconds.

Concurrency: Stable handling of 8+ simultaneous socket connections via ThreadPool.

Future Roadmap
Containerization: Wrapping the architecture in Docker and Docker Compose for cloud-scale deployment.

Orchestration Daemon: A master controller script to automate node scaling and one-click cluster management.

Advanced Consensus: Implementing the Raft Algorithm for MetaServer high availability to fully eliminate SPOF.

Learning Outcomes
Advanced Networking: Winsock2 socket programming, TCP/IP flow control, and custom binary protocol design.

High-Concurrency: Systems design using ThreadPools, Task Queues, and the Producer-Consumer pattern.

Data Integrity: Implementation of cryptographic hashing for end-to-end consistency.

Distributed Logic: Architecture of Heartbeats, Replication (RF=2), and Dynamic Discovery.

## Author
Ayush Krishna Garg
National Institute of Technology (NIT), Jamshedpur
B.Tech, Electronics and Communication Engineering
Focused on high-performance C++ backend systems, network programming, and distributed infrastructure.

System Visual Flow

┌─────────────────────────────────────────────────────────┐
│                   CLIENT ORCHESTRATOR                   │
│  (File Splitter | ThreadPool | Parallel I/O | UI)       │
└──────────────┬───────────────────────────────▲──────────┘
               │                               │
      1. REGISTER/GET_NODES            2. LIVE NODE LIST
               │                               │
┌──────────────▼───────────────────────────────┴──────────┐
│                METADATA SERVER (Port 8001)              │
│    (Node Registry | Heartbeat Janitor | registry.db)    │
└──────────────▲───────────────────────────────▲──────────┘
               │                               │
       4. JOIN/HEARTBEAT                4. JOIN/HEARTBEAT
               │                               │
┌──────────────┴──────────┐       ┌──────────────┴──────────┐
│   STORAGE NODE (9001)   │       │   STORAGE NODE (9002)   │
│ (Chunk A-1 | Chunk B-2) │       │ (Chunk A-2 | Chunk B-1) │
└──────────────▲──────────┘       └──────────────▲──────────┘
               │                               │
               └───────────────┬───────────────┘
                               │
                  3. REPLICATED DATA TRANSFER (RF=2)
                          (Parallel TCP)


[UPLOAD FLOW]
File -> [Client] -> (Request Live Nodes) -> [MetaServer]

[Client] -> Calculates SHA-256 Hash.

[Client] -> Splits file into chunks -> Enqueues to ThreadPool.

Worker Threads -> Parallel Send (Replica 1 -> Node 9001, Replica 2 -> Node 9002).



[DOWNLOAD FLOW]


[Client] -> (Query: "Where is file.pdf?") -> [MetaServer].

[MetaServer] -> Returns Chunk Map + Expected SHA-256 Hash.

[Client] -> ThreadPool requests chunks from Primary Nodes.

Failover: IF (Primary Node Dead) -> Request from Secondary Replica -> [OK].

[Client] -> Merge Chunks -> Re-verify SHA-256 -> "Verified [MATCH]".