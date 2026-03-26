#  Distributed High-Performance File Storage System (C++)

> A robust, self-healing, multi-threaded distributed storage system built in **C++** using **TCP sockets**.

This system achieves high-speed data transfer through **parallel chunking**, **thread pooling**, **replication (RF = 2)**, and **end-to-end integrity verification (SHA-256)**. It is fully containerized using **Docker** and designed for fault tolerance, scalability, and performance.

---

##  Table of Contents

- [System Architecture](#system-architecture)
- [System Visual Flow](#system-visual-flow)
- [Detailed Logic Flows](#detailed-logic-flows)
- [Key Technical Features](#key-technical-features)
- [Scaling the Cluster](#scaling-the-cluster)
- [Fault Tolerance & Chaos Testing](#fault-tolerance--chaos-testing)
- [Docker Orchestration](#docker-orchestration)
- [Compilation and Execution](#compilation-and-execution)
- [Performance Benchmarks](#performance-benchmarks)
- [Future Roadmap](#future-roadmap)
- [Learning Outcomes](#learning-outcomes)
- [Author](#author)

---

##  System Architecture

The system follows a **decentralized cluster architecture** with three main components:

### 1.  Client Orchestrator
- Central interface for all file operations
- Splits files into optimized chunks (**1MB / 4MB**)
- Generates **SHA-256** hash for integrity
- Uses **ThreadPool (8 threads)** for parallel I/O

### 2.  Metadata Registry (MetaServer)
- Runs on **Port `8001`**
- Maintains:
  - Node heartbeats
  - Chunk-to-node mapping
- Stores persistent state in: `data/registry.db`

### 3.  Storage Node Workers
- Independent processes
- Handle binary disk I/O
- Use **heartbeat mechanism** to report health
- Store chunks as raw binary files

---

##  System Visual Flow

```
┌─────────────────────────────────────────────────────────┐
│                 CLIENT ORCHESTRATOR                     │
│  (File Splitter | ThreadPool | Parallel I/O | UI)       │
└──────────────┬───────────────────────────────▲──────────┘
               │                               │
      1. REGISTER/GET_NODES            2. LIVE NODE LIST
               │                               │
┌──────────────▼───────────────────────────────┴──────────┐
│                METADATA SERVER (Port 8001)              │
│     (Node Registry | Heartbeat | registry.db)           │
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
```

---

## 🔍 Detailed Logic Flows

###  Upload Flow (`PUT`)

| Step | Action | Detail |
|------|--------|--------|
| 1 | **File Chunking** | Split file into segments (1MB / 4MB) |
| 2 | **Hash Generation** | Compute SHA-256 of entire file |
| 3 | **Metadata Registration** | `REGISTER <filename> <chunkCount> <hash>` |
| 4 | **Node Allocation** | Round-robin selection, assign **2 nodes per chunk** (RF = 2) |
| 5 | **Parallel Transfer** | ThreadPool executes concurrent uploads |
| 6 | **Acknowledgment** | MetaServer responds `"OK"` after persistence |

###  Download Flow (`GET`)

| Step | Action | Detail |
|------|--------|--------|
| 1 | **Request** | `GET <filename>` |
| 2 | **Metadata Lookup** | Retrieve chunk map + hash |
| 3 | **Node Filtering** | Remove inactive nodes (heartbeat timeout) |
| 4 | **Parallel Retrieval** | Fetch chunks concurrently |
| 5 | **Fallback** | Switch to replica if primary node is down |
| 6 | **Reconstruction** | Merge chunks sequentially |
| 7 | **Integrity Verification** | Compare SHA-256 hashes |

---

##  Key Technical Features

###  Parallel Transfer Engine
- Custom **ThreadPool (8 threads)**
- Multi-stream **TCP transfers**
- Achieves **~1.1 GB/s** (local environment)

###  End-to-End Data Integrity
- **SHA-256** verification at:
  - Upload
  - Storage
  - Download

###  Metadata Journaling
- Persistent storage in `data/registry.db`
- Enables **full recovery** after restart

---

##  Scaling the Cluster

To add a new node, append it to your `docker-compose.yml`:

```yaml
node3:
  build:
    context: .
    dockerfile: storage_node/Dockerfile
  environment:
    - META_HOST=meta-server
  ports:
    - "9003:9003"
  command: ["./node_app", "9003", "data/node3"]
  volumes:
    - ./data/node3:/usr/src/app/data/node3
```

Then apply the changes:

```bash
docker-compose up -d --build
```

---

##  Fault Tolerance & Chaos Testing

###  Node Failure Test
```bash
docker-compose stop node1
```
>  System automatically retrieves data from the **replica node**.

###  Metadata Server Failure
```bash
docker-compose stop meta-server
```
>  Client operations fail — **Single Point of Failure confirmed** (known limitation).

###  Persistence Test
```bash
docker-compose down
docker-compose up
```
>  Data fully restored from `registry.db`.

---

##  Docker Orchestration

| Action | Command |
|--------|---------|
| **Start Cluster** | `docker-compose up --build` |
                        or `docker-compose up`
| **Stop Cluster** | `docker-compose down` |
| **View Logs** | `docker-compose logs -f meta-server` |

---

##  Compilation and Execution

### Compilation

**Metadata Server**
```bash
g++ metadata_server/MetadataServer.cpp -o meta -lws2_32
```

**Storage Node**
```bash
g++ storage_node/StorageServer.cpp -o node_app -lws2_32
```

**Client**
```bash
g++ client/main.cpp \
  common/services/FileChunker.cpp \
  common/models/Chunk.cpp \
  -o client_app -lws2_32
```

### Execution

**Using Docker (Recommended)**
```bash
docker-compose up --build
```

**Client Commands**

```bash
# 1. Upload from the project's samples folder (Relative Path)
./client_app upload samples/video.mp4

# 2. Upload from a different drive/folder (Absolute Path)
# Use quotes if the path contains spaces
./client_app upload "D:/Videos/vacation_vlog.mp4"

# 3. Upload a document from your system
./client_app upload "C:/Users/Ayush/Documents/Resume.pdf"

# Download a file
# When downloading, you do not need the original path. You only need the filename as it was registered.

./client_app download video.mp4


# Sync (Verification)
./client_app sync samples/video.mp4
```

---

##  Performance Benchmarks

| Metric | Value |
|--------|-------|
| **Transfer Speed** | ~1.1 GB/s |
| **Thread Pool Size** | 8 threads |
| **Replication Factor** | 2 |
| **Chunk Size** | 1MB / 4MB |

---

##  Future Roadmap

- [ ] Smart load balancing (CPU + disk aware)
- [ ] Client-side **AES-256** encryption
- [ ] Flutter dashboard for live monitoring
- [ ] Distributed geo-replication
- [ ] Adaptive chunk sizing

---

##  Learning Outcomes

- Distributed systems design
- Fault tolerance and replication
- Socket programming (**Winsock2**)
- Multithreading and concurrency
- Docker and containerization
- Data integrity and hashing

---

##  Author

**Ayush Krishna Garg**
*National Institute of Technology (NIT), Jamshedpur*
*B.Tech — Electronics and Communication Engineering*

**Focus Areas:**
- High-performance C++ systems
- Distributed architecture
- Network programming

---

##  Final Notes

This project demonstrates a **production-grade distributed storage system** with:

-  High performance
-  Strong consistency
-  Fault tolerance
-  Scalability