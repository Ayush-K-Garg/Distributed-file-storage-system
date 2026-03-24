#  Distributed High-Performance File Storage System (C++)

A robust, self-healing, and multi-threaded distributed storage solution built in C++ using TCP sockets (**Winsock2**). This system achieves high-speed data transfer through parallel chunking, thread-pooling, and real-time monitoring. It is designed to be **fault-tolerant**, automatically handling node failures through a dynamic discovery and replication mechanism.

---

##  Table of Contents
* [System Architecture](#-system-architecture)
* [Key Technical Features](#-key-technical-features)
* [Advanced Fault Tolerance](#-advanced-fault-tolerance)
* [Storage Protocol](#-storage-protocol)
* [How It Works](#-how-it-works)
* [Project Structure](#-project-structure)
* [Compilation & Execution](#-compilation--execution)
* [Performance Benchmarks](#-performance-benchmarks)
* [Future Roadmap](#-future-roadmap)
* [Learning Outcomes](#-learning-outcomes)
* [Author](#-author)

---

##  System Architecture

The system consists of three primary components working in a decentralized cluster:

### 1. Client Orchestrator
The "Brain" of the operation. It manages the file lifecycle: partitioning files into optimized chunks, querying the Metadata Server for live nodes, and managing a **ThreadPool of 8 concurrent workers** for simultaneous I/O.

### 2. Metadata Registry (MetaServer)
The central directory running on port **8001**. It tracks:
* **Active Cluster State:** Real-time list of live storage nodes via heartbeats.
* **File Map:** Which chunks of a file are stored on which specific nodes.
* **Replication Registry:** Ensures every chunk has a primary and secondary location.

### 3. Storage Node Workers
Independent worker processes that can be launched on any unique port. They handle binary disk I/O and notify the MetaServer of their health status using a **Push-based Heartbeat** model.

---

##  Key Technical Features

###  Parallel Transfer Engine
Utilizes a custom **ThreadPool (8 Threads)** to bypass the limitations of sequential TCP. The system saturates available bandwidth by uploading/downloading multiple chunks simultaneously, reaching observed speeds of **670 MB/s** on local SSDs.

###  Dynamic Chunk Sizing
Analyzes file metadata to determine the most efficient segment size, balancing network overhead vs. metadata count:
| File Size | Chunk Size | Strategy |
| :--- | :--- | :--- |
| **< 1 MB** | 64 KB | Minimizes socket handshake overhead. |
| **1 MB - 50 MB** | 256 KB | Optimized for standard documents/images. |
| **50 MB - 500 MB** | 1 MB | Maximizes sequential I/O for medium data. |
| **500 MB - 2 GB** | 4 MB | Balanced for large-scale distributed systems. |
| **> 2 GB** | 8 MB | Prevents metadata bloat in the registry. |

---

##  Advanced Fault Tolerance

###  Replication Factor ($RF=2$)
During upload, every chunk is automatically duplicated across **two different storage nodes**. Even if one node crashes, the file remains 100% available for reconstruction.

###  Heartbeat & Node Janitor
* **Push Heartbeat:** Storage nodes send a "KEEP-ALIVE" signal to the MetaServer every **2 seconds**.
* **Janitor Thread:** A background process on the MetaServer that prunes any node that hasn't responded in **6 seconds**, ensuring the client never attempts to connect to a "dead" node.

### Dynamic Node Discovery
The system is **plug-and-play**. New storage nodes can join the cluster at any time without restarting the MetaServer or re-compiling the client. The client fetches a fresh "Live List" before every operation.

###  Intelligent Stall Detection
To prevent the client from hanging indefinitely during catastrophic node failures, the system monitors throughput. If chunk progress stops for **15 consecutive seconds**, the client gracefully terminates and reports a "Stall Error."

---

##  Storage Protocol

The custom TCP protocol utilizes a **Fixed-Header** approach for binary safety:
1. **Command Header:** (10 bytes) - e.g., "UPLOAD", "GET_CHUNK"
2. **Metadata Length:** (int) - Size of the incoming filename/id
3. **Metadata Body:** (string) - Filename and Chunk ID
4. **Payload Size:** (int) - Total size of binary data
5. **Binary Payload:** (bytes) - The actual file content

---

##  Compilation & Execution

### **Compilation**
Requires the Winsock library (`-lws2_32`) on Windows.

```bash
# Compile Metadata Server
g++ metadata_server/MetadataServer.cpp -o meta -lws2_32

# Compile Storage Node
g++ storage_node/StorageServer.cpp -o node -lws2_32

# Compile Client
g++ client/main.cpp common/services/FileChunker.cpp common/models/Chunk.cpp -o client_app -lws2_32

# Execution (Local Cluster Setup)

## Start Metadata Server

```bash
.\meta
```

## Launch Storage Nodes (Separate Terminals)

```bash
.\node 9001 data/node1
.\node 9002 data/node2
.\node 9003 data/node3
```

## Run Client Operations

### Upload

```bash
.\client_app upload samples/file.pdf
```

### Download

```bash
.\client_app download samples/file.pdf
```

### Sync

```bash
.\client_app sync samples/file.pdf
```



---

# Performance Benchmarks

## Transfer Speeds

Observed peaks of 670 MB/s during parallel chunk retrieval.

## Concurrency

Stable handling of 8+ simultaneous socket connections via ThreadPool.

## Throughput Optimization

400MB files processed in less than 1.0 seconds.

## Scalability

Linear performance increase as more physical nodes (ports) are added to the cluster.

---

# Future Roadmap

## Fail-Proof MetaServer (High Availability)

Implementing a leader-follower model or Distributed Consensus (Raft/Paxos) to eliminate the single point of failure.

## Deployment & Containerization

Creating Dockerfiles and Docker Compose configurations to deploy the cluster across cloud instances (AWS/Azure).

## SHA-256 Integrity Check

Hashing chunks during upload to prevent silent data corruption and verify content post-download.

## Orchestration Daemon

A master controller script to automate node scaling and one-click cluster management.

## Metadata Persistence

Moving in-memory registry to a persistent database to survive MetaServer reboots.

---

# Learning Outcomes

## Advanced Networking

Winsock2 socket programming, TCP/IP flow control, and custom binary protocol design.

## High-Concurrency

Systems design using ThreadPools, Task Queues, and the Producer-Consumer pattern.

## Synchronization Primitives

Practical application of std::atomic, std::mutex, and std::condition_variable.

## Distributed Logic

Architecture of Heartbeats, Replication (RF = 2), and Dynamic Discovery.

---

# Author

Ayush Krishna Garg
Focused on high-performance C++ backend systems, network programming, and distributed infrastructure.

┌─────────────────────────────────────────────────────────┐
       │                   CLIENT ORCHESTRATOR                   │
       │  (File Splitter | ThreadPool | Parallel I/O | UI)       │
       └──────────────┬───────────────────────────────▲──────────┘
                      │                               │
             1. REGISTER/GET_NODES            2. LIVE NODE LIST
                      │                               │
       ┌──────────────▼───────────────────────────────┴──────────┐
       │                METADATA SERVER (Port 8001)              │
       │    (Node Registry | Heartbeat Janitor | File Map)       │
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
                    (Receive List: 9001, 9002) <-
[Client] -> [Split Chunks] -> [ThreadPool]
             |--> Send Chunk_1 (Replica 1) -> Node 9001
             |--> Send Chunk_1 (Replica 2) -> Node 9002
             |--> Send Chunk_2 (Replica 1) -> Node 9002
             |--> Send Chunk_2 (Replica 2) -> Node 9001

[DOWNLOAD FLOW]
File -> [Client] -> (Query: "Where is file.pdf?") -> [MetaServer]
                    (Receive Map: Chunk_1: [9001, 9002], Chunk_2: [9002, 9003]) <-

[Client] -> [ThreadPool (8 Workers)]
             |
             |--> Worker 1: Request Chunk_1 from 9001 (Primary)
             |    IF (9001 Dead) --> Request from 9002 (Secondary) --> [OK]
             |
             |--> Worker 2: Request Chunk_2 from 9002 (Primary) --> [OK]
             |
             |--> [Stall Detector]: Monitoring progress... (Timeout if no chunks arrive)

[Client] -> [Received Map] -> (Sort & Merge Chunks) -> "downloaded_file.pdf"             