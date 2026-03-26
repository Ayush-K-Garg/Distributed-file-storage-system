# Distributed High-Performance File Storage System (C++)

A robust, self-healing, multi-threaded distributed storage system built in C++ using TCP sockets. This system achieves high-speed data transfer through parallel chunking, thread pooling, replication (RF = 2), and end-to-end integrity verification (SHA-256). It is built on a Universal Multi-Node Architecture, supporting deployment across multiple physical machines and local Docker environments simultaneously.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [System Visual Flow](#system-visual-flow)
- [Detailed Logic Flows](#detailed-logic-flows)
- [Universal Networking and Smart Routing](#universal-networking-and-smart-routing)
- [Key Technical Features](#key-technical-features)
- [Compilation and Execution](#compilation-and-execution)
- [Multi-Machine Deployment](#multi-machine-deployment)
- [Fault Tolerance and Chaos Testing](#fault-tolerance-and-chaos-testing)
- [Docker Orchestration](#docker-orchestration)
- [Performance Benchmarks](#performance-benchmarks)
- [Future Roadmap](#future-roadmap)
- [Learning Outcomes](#learning-outcomes)
- [Author](#author)

---

## System Architecture

The system follows a decentralized cluster architecture with three main components.

### Client Orchestrator (Universal Client)

- Central interface for all file operations, supporting both absolute and relative paths.
- Splits files into optimized chunks (1 MB / 4 MB).
- **Smart Routing:** Automatically detects whether a node is local, Docker-based, or remote.
- Uses a `ThreadPool` (8 threads) for parallel I/O.

### Metadata Registry (MetaServer)

- Runs on **Port 8001**.
- Tracks dynamic `IP:Port` pairs for all joining nodes.
- Maintains persistent chunk-to-node mapping in `data/registry.db`.

### Storage Node Workers

- Independent processes (native or Dockerized).
- Utilize high-capacity secondary storage via volume mapping.
- Use a heartbeat mechanism to report health and current network IP.

---

## System Visual Flow

```
+----------------------------------------------------------+
|              UNIVERSAL CLIENT ORCHESTRATOR               |
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
       4. JOIN (127.0.0.1)             4. JOIN (192.168.1.20)
                |                               |
+---------------+-----------+     +-------------+-----------+
|   LOCAL NODE (9001)       |     |   REMOTE NODE (9005)    |
|   (Host Machine SSD)      |     |   (Spare Laptop 1TB)    |
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

| Step | Action | Detail |
|------|--------|--------|
| 1 | File Chunking | Split file into segments (1 MB / 4 MB) |
| 2 | Hash Generation | Compute SHA-256 of entire file |
| 3 | Metadata Registration | `REGISTER <filename> <chunkCount> <hash>` |
| 4 | Node Allocation | Round-robin selection across all detected IPs |
| 5 | Parallel Transfer | ThreadPool executes concurrent uploads to multiple nodes |

---

## Universal Networking and Smart Routing

The system is designed to handle three distinct network environments simultaneously.

- **Local Native:** Nodes running in standard terminals (`127.0.0.1`).
- **Local Docker:** Nodes running in Docker on the same machine. The client automatically routes internal bridge IPs (`172.18.x.x`) to `127.0.0.1` to traverse the Docker NAT via mapped ports.
- **Remote Hardware:** Nodes running on a separate machine over Wi-Fi (`192.168.x.x`). The client connects directly to the remote LAN IP.

---

## Key Technical Features

### Parallel Transfer Engine

- Custom `ThreadPool` with 8 threads.
- Multi-stream TCP transfers achieving approximately **1.1 GB/s** (local).
- **End-to-End Integrity:** SHA-256 verification ensures data consistency across remote nodes.
- **Metadata Journaling:** Persistent state saved in `data/registry.db`.

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

### Execution (Native Terminal)

**Run Meta Server — Terminal 1**
```powershell
.\meta
```

**Run Storage Nodes — Terminal 2 and 3**
```powershell
# Format: .\node_app <PORT> <DATA_DIRECTORY>
.\node_app 9001 data/node1
.\node_app 9002 data/node2
```

### Client Commands

The client is location-agnostic. Files can be uploaded from any path, but downloaded using only the registered filename.

```bash
# Upload from the project's samples folder (relative path)
./client_app upload samples/video.mp4

# Upload from a different drive or folder (absolute path)
./client_app upload "D:/Videos/vacation_vlog.mp4"

# Upload a document from the system
./client_app upload "C:/Users/Username/Documents/Resume.pdf"

# Download a file (no path needed, just the registered filename)
./client_app download video.mp4

# Sync: full upload, download, and SHA-256 binary match check
./client_app sync samples/video.mp4
```

---

## Multi-Machine Deployment

To scale the cluster to a second machine (e.g., using a 1 TB spare drive):

1. **Networking:** Ensure both machines are on the same Wi-Fi network.
2. **Meta IP:** Identify the host machine's local IP (e.g., `192.168.1.15`).
3. **Deploy on spare machine:** Update the `docker-compose.yml` on the spare machine:

```yaml
environment:
  - META_HOST=192.168.1.15  # IP of the host machine
volumes:
  - D:/CloudVault:/usr/src/app/data/vault  # Mapping to 1TB drive
```

4. **Run:** Execute `docker-compose up --build` on the spare machine.

---

## Fault Tolerance and Chaos Testing

| Scenario | Command | Expected Behavior |
|----------|---------|-------------------|
| Node Failure | `docker-compose stop node1` | Data retrieved from replica |
| Persistence | `docker-compose down` then `up` | Data restored via `registry.db` |

---

## Docker Orchestration

| Action | Command |
|--------|---------|
| Start Cluster | `docker-compose up --build` |
| Stop Cluster | `docker-compose down` |
| View Logs | `docker-compose logs -f meta-server` |

---

## Performance Benchmarks

| Metric | Value |
|--------|-------|
| Local Transfer Speed | ~1.1 GB/s |
| Replication Factor | 2 |
| Thread Pool Size | 8 threads |
| Chunk Size | 1 MB / 4 MB |

---

## Author

**Ayush Krishna Garg**
National Institute of Technology (NIT), Jamshedpur
B.Tech — Electronics and Communication Engineering

This project represents a production-ready private cloud architecture, capable of turning spare hardware into a unified, high-performance storage pool.