# Distributed High-Performance File Storage System (C++)

A robust, self-healing, multi-threaded distributed storage system built in C++ using TCP sockets. This system achieves high-speed data transfer through parallel chunking, thread pooling, replication (RF = 2), and end-to-end integrity verification (SHA-256). It is built on a Universal Multi-Node Architecture, supporting deployment across local terminals, Docker environments, LAN (same Wi-Fi), and Global Networks (different Wi-Fi via Tailscale VPN) simultaneously.

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
- **Smart Routing:** Automatically detects whether a node is local, Docker-based, LAN-remote, or globally remote.
- **Global Reach:** Supports Tailscale Tunneling, allowing the client to communicate with nodes across different networks and locations via secure `100.x.x.x` Tailscale mesh IPs.
- Uses a `ThreadPool` (2 threads for cross-device / 8 threads for local) for parallel I/O.

### Metadata Registry (MetaServer)

- Runs on **Port 8001**.
- **IP-Aware Discovery:** Tracks dynamic `IP:Port` pairs for all joining nodes (Localhost, Docker Bridge, LAN, or Tailscale Mesh VPN).
- Maintains persistent chunk-to-node mapping in `data/registry.db`.

### Storage Node Workers

- Independent processes (native or Dockerized).
- **High-Capacity Storage:** Designed to utilize secondary drives (e.g., 1 TB HDDs) via volume mapping.
- **Heartbeat Mechanism:** Reports health and current network IP (LAN or Tailscale) to the MetaServer every 2 seconds.

---

## System Visual Flow

```
+----------------------------------------------------------+
|               UNIVERSAL CLIENT ORCHESTRATOR              |
|    (Smart Routing | ThreadPool | Tailscale Tunnel)       |
+---------------+---------------+--------------+-----------+
                |               |              |
      1. REGISTER/GET_NODES     |       2. DYNAMIC IP LIST
                |               |  (127.0.0.1 | 192.168.x.x | 100.x.x.x)
+---------------v---------------+--------------v-----------+
|                 METADATA SERVER (Port 8001)              |
|       (IP-Aware Registry | Heartbeat | registry.db)      |
+---------------^-------------------------------^-----------+
                |                               |
       4. JOIN (127.0.0.1)         4. JOIN (LAN / Tailscale IP)
                |                               |
+---------------+-----------+     +-------------+-----------+
|   LOCAL NODE (9001)       |     |   REMOTE NODE (9005)    |
|   (Host Machine SSD)      |     |   (Server Machine 1TB)  |
+---------------^-----------+     +-------------^-----------+
                |                               |
                +---------------+---------------+
                                |
                3. REPLICATED DATA TRANSFER (RF=2)
                     (Parallel TCP over LAN / WAN)
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

The system handles four distinct network environments simultaneously via a unified smart routing layer. The `META_HOST` environment variable drives context selection at startup.

- **Local Native:** Nodes running in standard terminals on the host machine (`127.0.0.1`). No environment variable required; defaults automatically.
- **Local Docker:** Nodes running in Docker on the host machine. The client automatically routes internal bridge IPs (`172.18.x.x`) to `127.0.0.1` to traverse the Docker NAT via mapped ports.
- **Remote LAN (Same Wi-Fi):** Nodes running on a separate server machine over the same local Wi-Fi network (`192.168.x.x`). The client connects directly to the LAN IP.
- **Global WAN (Different Wi-Fi — Tailscale):** Utilizes Tailscale VPN to create a secure peer-to-peer mesh network. Nodes on entirely different networks or in different cities connect via permanent `100.x.x.x` Tailscale IPs, bypassing NAT and firewall restrictions without requiring any port forwarding configuration.

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

### Thread Pool Tuning (Context-Aware Concurrency)

The thread count is tuned based on the deployment context to balance throughput against overhead.

| Deployment Mode | Thread Count | Rationale |
|-----------------|-------------|-----------|
| Local (SSD, single machine) | 8 threads | High-bandwidth local I/O benefits from maximum parallelism |
| Cross-device (LAN / Tailscale) | 2 threads | Reduces context-switching overhead on mechanical drives; network latency is the bottleneck, not thread count |

---

## Key Technical Features

### Parallel Transfer Engine

- Custom `ThreadPool` with **8 threads** for local SSD transfers and **2 threads** for cross-device transfers over LAN or Tailscale.
- Local transfer speeds of approximately **1.1 GB/s**.
- Cross-device transfer speeds optimized to approximately **5–6 MB/s**, achieved by reducing thread contention and context-switching overhead on mechanical drives over a network link.
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

### Execution — Mode 1: Local (Single Machine, Native Terminals)

All components run on the same machine. No environment variable required; `META_HOST` defaults to `127.0.0.1`.

**Terminal 1 — Start Meta Server**
```powershell
.\meta
```

**Terminal 2 — Start Storage Node 1**
```powershell
.\node_app 9001 data/node1
```

**Terminal 3 — Start Storage Node 2**
```powershell
.\node_app 9002 data/node2
```

---

### Execution — Mode 2: Local Docker (Single Machine, Containerized Nodes)

Nodes run inside Docker containers on the same host. The client routes Docker bridge IPs automatically.

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

### Execution — Mode 3: Multi-Machine LAN (Same Wi-Fi Network)

The MetaServer runs on the host machine. The server machine joins via its LAN IP.

**Host Machine — Start Meta Server and local nodes (Terminals 1, 2, 3)**
```powershell
.\meta
.\node_app 9001 data/node1
.\node_app 9002 data/node2
```

**Server Machine — Set META_HOST to the host machine's LAN IP and start a node**
```powershell
$env:META_HOST="192.168.1.15"
.\node_app 9005 data/remote_node
```

---

### Execution — Mode 4: Global Multi-Machine (Different Wi-Fi via Tailscale)

Both machines must have [Tailscale](https://tailscale.com) installed and be logged into the same Tailscale account. Each machine receives a permanent `100.x.x.x` Tailscale IP.

**Host Machine — Start Meta Server (no variable needed; Tailscale routes automatically)**
```powershell
.\meta
.\node_app 9001 data/node1
```

**Server Machine — Set META_HOST to the host machine's Tailscale IP**
```powershell
$env:META_HOST="100.80.1.2"
.\node_app 9005 data/remote_node
```

> Replace `100.80.1.2` with the actual Tailscale IP shown in the Tailscale admin dashboard for the host machine.

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

### Scenario A — Same Wi-Fi (LAN)

Set `META_HOST` to the host machine's LAN IP (e.g., `192.168.1.15`) on the server machine before launching any node process or Docker container.

### Scenario B — Global Access (Tailscale VPN)

To use a server machine in a different location as a dedicated 1 TB storage vault over the internet:

**Prerequisites**
1. Install Tailscale on both the host machine and the server machine.
2. Log both into the same Tailscale account. Each machine will receive a permanent `100.x.x.x` IP.
3. Identify the host machine's Tailscale IP from the Tailscale admin dashboard (e.g., `100.80.1.2`).
4. Create the storage directory `D:/CloudVault` on the server machine.

**Server Machine — `docker-compose.yml` Configuration (Global)**
```yaml
services:
  node_global:
    build: .
    environment:
      - META_HOST=100.80.1.2  # Tailscale IP of the host machine
    ports:
      - "9005:9005"
    command: ["./node_app", "9005", "data/node1"]
    volumes:
      - D:/CloudVault/node1:/usr/src/app/data/node1
```

**Deploy on Server Machine**
```bash
docker-compose up --build
```

Once running, the server node registers with the MetaServer on the host machine through the Tailscale tunnel. No port forwarding or firewall changes are required on either machine.

---

## Fault Tolerance and Chaos Testing

| Scenario | Command / Action | Expected Behavior |
|----------|-----------------|-------------------|
| Node Failure | `docker-compose stop node1` | Data is retrieved from the replica node on the server machine |
| Cluster Restart | `docker-compose down` then `docker-compose up` | Registry is loaded from `registry.db`; all nodes rejoin automatically |
| Network Partition (LAN) | Disconnect server machine from Wi-Fi | Host-local nodes continue serving data; server node re-registers on reconnect |
| Tunnel Interruption (Tailscale) | Turn off Tailscale on server machine | Server node is marked disconnected in MetaServer; re-registers instantly on Tailscale reconnection |

---

## Performance Benchmarks

| Metric | Value |
|--------|-------|
| Local Transfer Speed | ~1.1 GB/s |
| Remote Transfer Speed (LAN / Tailscale) | ~5–6 MB/s (network and hardware limited) |
| Replication Factor | 2 |
| Thread Pool Size — Local (SSD) | 8 threads |
| Thread Pool Size — Cross-Device (LAN / Tailscale) | 2 threads |
| Chunk Size (Adaptive) | 1 MB to 128 MB (dynamic) |
| Heartbeat Interval | 2 seconds |
| MetaServer Port | 8001 |

---

## Learning Outcomes

- Designed and implemented a distributed system from scratch using raw TCP sockets in C++.
- **Advanced Networking:** Mastered NAT traversal and VPN mesh integration using Tailscale for global node communication without port forwarding.
- Applied multi-threading via a custom `ThreadPool` to achieve true parallel I/O, with context-aware thread tuning (2 vs 8 threads) for different deployment environments.
- Handled heterogeneous network environments (localhost, Docker NAT, LAN, WAN/Tailscale) within a single unified smart routing layer.
- Implemented end-to-end data integrity using SHA-256 checksums across all replicated nodes, including remote ones.
- Optimized for real-world hardware constraints including mechanical HDD sequential write performance via adaptive chunking and reduced thread contention.
- Gained practical experience with Docker volume mapping and container networking for distributed storage workloads.

---

## Author

**Ayush Krishna Garg**
National Institute of Technology (NIT), Jamshedpur
B.Tech — Electronics and Communication Engineering

This project demonstrates a production-ready private cloud architecture, optimized for real-world networking constraints and heterogeneous hardware environments — from a single laptop to a globally distributed multi-machine storage cluster.