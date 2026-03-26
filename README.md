# Distributed High-Performance File Storage System (C++)

A robust, self-healing, multi-threaded distributed storage system built in C++ using TCP sockets. This system achieves high-speed data transfer through parallel chunking, thread pooling, replication (RF = 2), and end-to-end integrity verification (SHA-256). It is built on a Universal Multi-Node Architecture, supporting deployment across multiple physical machines and local Docker environments simultaneously.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [System Visual Flow](#system-visual-flow)
- [Detailed Logic Flows](#detailed-logic-flows)
- [Universal Networking and Smart Routing](#universal-networking-and-smart-routing)
- [Performance and Hardware Optimizations](#performance-and-hardware-optimizations)
- [Key Technical Features](#key-technical-features)
- [Compilation and Execution](#compilation-and-execution)
- [Multi-Machine Deployment](#multi-machine-deployment)
- [Fault Tolerance and Chaos Testing](#fault-tolerance-and-chaos-testing)
- [Docker Orchestration](#docker-orchestration)
- [Performance Benchmarks](#performance-benchmarks)
- [Learning Outcomes](#learning-outcomes)
- [Author](#author)

---

## System Architecture

The system follows a decentralized cluster architecture with three main components.

### Client Orchestrator (Universal Client)

- Central interface for all file operations, supporting both absolute and relative paths.
- **Adaptive Chunking:** Dynamically scales chunk sizes from 1 MB up to 128 MB based on file size to optimize HDD throughput.
- **Smart Routing:** Automatically detects whether a node is local, Docker-based, or remote.
- Uses a `ThreadPool` (8 threads) for parallel I/O.

### Metadata Registry (MetaServer)

- Runs on **Port 8001**.
- **IP-Aware Discovery:** Tracks dynamic `IP:Port` pairs for all joining nodes (Localhost, Docker Bridge, or LAN).
- Maintains persistent chunk-to-node mapping in `data/registry.db`.

### Storage Node Workers

- Independent processes (native or Dockerized).
- **High-Capacity Storage:** Designed to utilize secondary drives (e.g., 1 TB HDDs) via volume mapping.
- **Heartbeat Mechanism:** Reports health and current network IP to the MetaServer every 2 seconds.

---

## System Visual Flow

```
+----------------------------------------------------------+
|               UNIVERSAL CLIENT ORCHESTRATOR              |
|      (Smart Routing | ThreadPool | Parallel I/O)         |
+---------------+---------------+--------------+-----------+
                |               |              |
      1. REGISTER/GET_NODES     |       2. DYNAMIC IP LIST
                |               |       (e.g., 192.168.1.15:9001)
+---------------v---------------+--------------v-----------+
|                 METADATA SERVER (Port 8001)              |
|       (IP-Aware Registry | Heartbeat | registry.db)      |
+---------------^-------------------------------^-----------+
                |                               |
       4. JOIN (127.0.0.1)              4. JOIN (192.168.1.19)
                |                               |
+---------------+-----------+     +-------------+-----------+
|   LOCAL NODE (9001)       |     |   REMOTE NODE (9005)    |
|   (Host Machine SSD)      |     |   (Server Machine 1TB)  |
+---------------^-----------+     +-------------^-----------+
                |                               |
                +---------------+---------------+
                                |
                3. REPLICATED DATA TRANSFER (RF=2)
                     (Parallel TCP over Wi-Fi/LAN)
```

---

## Detailed Logic Flows

### Upload Flow

1. **Adaptive Chunking:** The client calculates the optimal chunk size based on total file size.
2. **Hash Generation:** Computes SHA-256 of the entire file for end-to-end verification.
3. **Metadata Registration:** Sends `REGISTER <filename> <chunkCount> <hash>` to the MetaServer.
4. **Node Allocation:** MetaServer performs round-robin selection across all detected IPs.
5. **Parallel Transfer:** ThreadPool executes concurrent uploads to multiple physical nodes.

### Download Flow

1. **Metadata Lookup:** Client sends `GET <filename>` to MetaServer and receives the chunk-to-node map.
2. **Parallel Retrieval:** ThreadPool fetches all chunks concurrently from their respective nodes.
3. **Integrity Verification:** Reassembled file is SHA-256 hashed and compared against the registered hash.
4. **File Reconstruction:** Verified chunks are written to disk in the correct order.

---

## Universal Networking and Smart Routing

The system handles three distinct network environments simultaneously.

- **Local Native:** Nodes running in standard terminals on the host machine (`127.0.0.1`).
- **Local Docker:** Nodes running in Docker on the host machine. The client automatically routes internal bridge IPs (`172.18.x.x`) to `127.0.0.1` to traverse the Docker NAT via mapped ports.
- **Remote Hardware:** Nodes running on a separate server machine over Wi-Fi (`192.168.x.x`). The client connects directly to the remote LAN IP.

---

## Performance and Hardware Optimizations

### Low-Latency Networking (TCP_NODELAY)

To prevent transfer stalls and stuttering during large file operations, the system disables Nagle's Algorithm on all sockets.

- **Implementation:** `setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, ...)` is called on every socket at initialization.
- **Advantage:** Small control messages (`JOIN`, `HEARTBEAT`) and the tail end of file chunks are transmitted immediately without waiting for buffer saturation, eliminating the stall observed at high transfer completion percentages during large transfers.

### Adaptive Chunking (HDD Optimization)

For a 1 TB mechanical HDD, writing thousands of small files causes disk thrashing and significantly degrades throughput. The system dynamically scales chunk size based on total file size to promote sequential write patterns.

| File Size | Chunk Size |
|-----------|------------|
| Less than 10 MB | 1 MB |
| Less than 500 MB | 4 MB |
| Less than 1 GB | 16 MB |
| Greater than 50 GB | 128 MB |

- **Advantage:** Larger chunks promote sequential write operations on the mechanical drive, significantly increasing transfer speeds and reducing disk wear over time.

---

## Key Technical Features

### Parallel Transfer Engine

- Custom `ThreadPool` with 8 threads for concurrent multi-stream TCP transfers.
- Local transfer speeds of approximately **1.1 GB/s**.
- **End-to-End Integrity:** SHA-256 verification ensures data consistency across all nodes, including remote ones.
- **Metadata Journaling:** Persistent cluster state saved in `data/registry.db`, surviving full restarts.
- **Replication Factor (RF = 2):** Every chunk is written to two independent nodes, ensuring data availability during single-node failures.

---

## Compilation and Execution

### Compilation (Windows / MinGW)

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
g++ client/main.cpp common/services/FileChunker.cpp common/models/Chunk.cpp -o client_app -lws2_32
```

---

### Execution (Native Terminal)

**Step 1 — Run Meta Server (Terminal 1)**
```powershell
.\meta
```

**Step 2 — Run Storage Nodes (Terminal 2 and Terminal 3)**
```powershell
# Format: .\node_app <PORT> <DATA_DIRECTORY>
.\node_app 9001 data/node1
.\node_app 9002 data/node2
```

---

### Execution (Docker Orchestration)

**Start the full cluster**
```bash
docker-compose up --build
```

**Stop the cluster**
```bash
docker-compose down
```

**View live logs**
```bash
docker-compose logs -f meta-server
```

---

### Client Commands

The client is location-agnostic. Files can be uploaded from any path on the system but must be downloaded using only the registered filename.

```bash
# Upload using a relative path
./client_app upload samples/video.mp4

# Upload using an absolute path
./client_app upload "D:/Videos/aws.mp4"

# Upload a document from the system
./client_app upload "C:/Users/Username/Documents/Resume.pdf"

# Download a file (filename only — no path required)
./client_app download aws.mp4

# Sync: full upload, download, and SHA-256 binary match check
./client_app sync samples/video.mp4
```

---

## Multi-Machine Deployment

To use a server machine as a dedicated 1 TB storage vault, follow the steps below.

### Prerequisites

1. Ensure both the host machine and the server machine are connected to the same Wi-Fi network.
2. Identify the host machine's local IP address (e.g., `192.168.1.15`).
3. Create the storage directory `D:/CloudVault` on the server machine.

### Server Machine — `docker-compose.yml` Configuration

```yaml
services:
  node1:
    build: .
    environment:
      - META_HOST=192.168.1.15  # IP address of the host machine
    ports:
      - "9005:9005"
    command: ["./node_app", "9005", "data/node1"]
    volumes:
      - D:/CloudVault/node1:/usr/src/app/data/node1
```

### Deploy on Server Machine

```bash
docker-compose up --build
```

Once running, the server node automatically joins the cluster by registering with the MetaServer on the host machine. No changes are required on the host side.

---

## Fault Tolerance and Chaos Testing

| Scenario | Command | Expected Behavior |
|----------|---------|-------------------|
| Node Failure | `docker-compose stop node1` | Data is retrieved from the replica node on the server machine |
| Cluster Restart | `docker-compose down` then `docker-compose up` | Registry is loaded from `registry.db`; all nodes rejoin automatically |
| Network Partition | Disconnect server machine from Wi-Fi | Host-local nodes continue serving data; server node re-registers on reconnect |

---

## Performance Benchmarks

| Metric | Value |
|--------|-------|
| Local Transfer Speed | ~1.1 GB/s |
| Replication Factor | 2 |
| Thread Pool Size | 8 threads |
| Chunk Size (Adaptive) | 1 MB to 128 MB (dynamic) |
| Heartbeat Interval | 2 seconds |
| MetaServer Port | 8001 |

---

## Learning Outcomes

- Designed and implemented a distributed system from scratch using raw TCP sockets in C++.
- Applied multi-threading via a custom `ThreadPool` to achieve true parallel I/O across physical hardware.
- Handled heterogeneous network environments (localhost, Docker NAT, LAN) within a single smart routing layer.
- Implemented end-to-end data integrity using SHA-256 checksums across replicated nodes.
- Optimized for real-world hardware constraints including mechanical HDD sequential write performance via adaptive chunking.
- Gained practical experience with Docker volume mapping and container networking for storage workloads.

---

## Author

**Ayush Krishna Garg**
National Institute of Technology (NIT), Jamshedpur
B.Tech — Electronics and Communication Engineering

This project demonstrates a production-ready private cloud architecture, optimized for real-world networking constraints and heterogeneous hardware environments.