# Distributed High-Performance File Storage System (C++)

A C++ distributed storage cluster that turns multiple computers into one private cloud. It uses a Master-Node architecture to break files into chunks and spread them across different devices (Windows/Linux) for better privacy. A central registry tracks all filenames and locations, allowing users to access their data securely from anywhere via a Mesh VPN.

A robust, self-healing, multi-threaded distributed storage system built in C++ using TCP sockets. This system achieves high-speed data transfer through parallel chunking, thread pooling, replication (RF = 2), and end-to-end integrity verification (SHA-256). It is built on a Universal Multi-Node Architecture, supporting deployment across local terminals, Docker environments, LAN (same Wi-Fi), and Global Networks (different Wi-Fi via Tailscale VPN) simultaneously.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [System Visual Flow](#system-visual-flow)
- [Detailed Logic Flows](#detailed-logic-flows)
- [Universal Networking and Smart Routing](#universal-networking-and-smart-routing)
- [Advanced NAT Traversal and Docker Masquerade Bypass](#advanced-nat-traversal-and-docker-masquerade-bypass)
- [Performance and Hardware Optimizations](#performance-and-hardware-optimizations)
- [Space Safety and Guardrail Logic](#space-safety-and-guardrail-logic)
- [Key Technical Features](#key-technical-features)
- [The .env Configuration System](#the-env-configuration-system)
- [Compilation and Execution](#compilation-and-execution)
- [Client Commands and Advanced Usage](#client-commands-and-advanced-usage)
- [Multi-Machine Deployment](#multi-machine-deployment)
- [Fault Tolerance and Chaos Testing](#fault-tolerance-and-chaos-testing)
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
- **Local Space Guard:** Checks for `SAFETY_BUFFER_GB` before starting any download to prevent host OS instability.
- Uses a `ThreadPool` (2 threads for cross-device / 8 threads for local) for parallel I/O.

### Metadata Registry (MetaServer)

- Runs on **Port 8001** (internal) / **Port 8003** (Docker mapped).
- **IP-Aware Discovery:** Tracks dynamic `IP:Port` pairs for all joining nodes (Localhost, Docker Bridge, LAN, or Tailscale Mesh VPN).
- **Node Health Dashboard:** Maintains a "Last Seen" heartbeat registry for real-time cluster monitoring.
- Maintains persistent chunk-to-node mapping in `data/registry.db`.

### Storage Node Workers

- Independent processes (native or Dockerized).
- **High-Capacity Storage:** Designed to utilize secondary drives (e.g., 1 TB HDDs) via volume mapping.
- **Disk Space Protection:** Implements `MIN_FREE_SPACE_GB` checks; refuses chunks if the drive is nearly full.
- **Heartbeat Mechanism:** Reports health and current network IP (LAN or Tailscale) to the MetaServer every 2 seconds.

---

## System Visual Flow

```
+----------------------------------------------------------+
|               UNIVERSAL CLIENT ORCHESTRATOR              |
|      (Smart Routing | ThreadPool | Tailscale Tunnel)     |
+---------------+---------------+--------------+-----------+
                |               |              |
      1. REGISTER/GET_NODES     |       2. DYNAMIC IP LIST
                |               |  (127.0.0.1 | 192.168.x.x | 100.x.x.x)
+---------------v---------------+--------------v-----------+
|                 METADATA SERVER (Port 8001)              |
|        (IP-Aware Registry | Heartbeat | registry.db)     |
+---------------^-------------------------------^-----------+
                |                               |
       4. JOIN (127.0.0.1)          4. JOIN (LAN / Tailscale IP)
                |                               |
+---------------+-----------+     +-------------+-----------+
|   LOCAL NODE (9001)       |     |   REMOTE NODE (9005)    |
|   (Master Machine SSD)    |     |   (Node Machine 1TB)    |
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
2. **Space Verification:** Client confirms local disk has `fileSize + SAFETY_BUFFER_GB` available before proceeding.
3. **Parallel Retrieval:** ThreadPool fetches all chunks concurrently from their respective nodes.
4. **Integrity Verification:** Reassembled file is SHA-256 hashed and compared against the registered hash.
5. **File Reconstruction:** Verified chunks are written to disk in the correct order.

---

## Universal Networking and Smart Routing

The system handles four distinct network environments simultaneously via a unified smart routing layer. The `META_HOST` environment variable drives context selection at startup.

- **Local Native:** Nodes running in standard terminals on the master machine (`127.0.0.1`). No environment variable required; defaults automatically.
- **Local Docker:** Nodes running in Docker on the master machine. The client automatically routes internal bridge IPs (`172.18.x.x`) to `127.0.0.1` to traverse the Docker NAT via mapped ports.
- **Remote LAN (Same Wi-Fi):** Nodes running on a separate node machine over the same local Wi-Fi network (`192.168.x.x`). The client connects directly to the LAN IP.
- **Global WAN (Different Wi-Fi — Tailscale):** Utilizes Tailscale VPN to create a secure peer-to-peer mesh network. Nodes on entirely different networks or in different cities connect via permanent `100.x.x.x` Tailscale IPs, bypassing NAT and firewall restrictions without requiring any port forwarding configuration.

---

## Advanced NAT Traversal and Docker Masquerade Bypass

A common challenge in containerized distributed systems is IP Masquerading, where the Docker Gateway (`172.18.0.1`) hides the actual identity of a node. This system implements a **Self-Reporting Identity Protocol** to ensure global reachability.

### Explicit IP Reporting (`MY_IP`)

Storage nodes detect their own environment at startup and explicitly report their Tailscale or LAN IP to the MetaServer via the `MY_IP` environment variable. This bypasses the Docker NAT mask entirely, allowing the MetaServer to register the node's globally reachable address — whether a LAN `192.168.x.x` or a Tailscale `100.x.x.x` — instead of its internal container address that would otherwise be invisible to the rest of the cluster.

### OOM Resilience (Code 137 Protection)

The orchestration layer applies strict memory limits and resource reservations to every container. This prevents the Metadata Server from being forcibly terminated by the Docker engine (exit code 137 — Out of Memory kill) during high-load indexing operations or large file metadata transfers, which can produce sudden spikes in heap usage.

### Automatic Handshake Fallback

The system maintains full backward compatibility for zero-configuration local testing. If no `META_HOST` or `MY_IP` environment variables are detected at startup, the node automatically falls back to socket-level IP detection. This ensures that running a bare `.\node_app` in a local terminal requires no configuration whatsoever, while the same binary supports full global deployment when environment variables are present.

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

## Space Safety and Guardrail Logic

To prevent unrecoverable disk-full crashes across the cluster, two independent guards are implemented at different layers.

### Storage Node Guard (`MIN_FREE_SPACE_GB`)

The storage node calculates available disk space before accepting every chunk upload. If the available space drops below `MIN_FREE_SPACE_GB`, the node returns an explicit error to the client and refuses the chunk. This prevents a node from consuming the remaining space needed by the host OS, which would cause system instability or a crash on the machine running the node.

### Client Download Guard (`SAFETY_BUFFER_GB`)

Before initiating any download, the client calculates `required_file_size + SAFETY_BUFFER_GB` and checks it against the free space on the local download drive (`DOWNLOAD_DIR`). If the drive is too full to safely accommodate the file and the buffer, the download is aborted immediately with a clear error — before any data is transferred.

---

## Key Technical Features

### Parallel Transfer Engine

- Custom `ThreadPool` with **8 threads** for local SSD transfers and **2 threads** for cross-device transfers over LAN or Tailscale.
- Local transfer speeds of approximately **1.1 GB/s**.
- Cross-device transfer speeds optimized to approximately **5–6 MB/s**, achieved by reducing thread contention and context-switching overhead on mechanical drives over a network link.
- **Network Status Dashboard:** View live node health and "Last Seen" heartbeat status via the `nodes` command.
- **End-to-End Integrity:** SHA-256 verification ensures data consistency across all nodes, including remote ones.
- **Metadata Journaling:** Persistent cluster state saved in `data/registry.db`, surviving full restarts.
- **Replication Factor (RF = 2):** Every chunk is written to two independent nodes, ensuring data availability during single-node failures.

---

## The .env Configuration System

The cluster is configured entirely via environment variables. For Docker deployments, these are set inside `docker-compose.yml` or a `.env` file. For native terminal usage, they are set directly in the PowerShell session before running any binary.

### Full Configuration Reference

```bash
# --- Network Routing ---
META_HOST=127.0.0.1      # IP of the MetaServer (Master Machine)
                         # Use 127.0.0.1 for local, 192.168.x.x for LAN, 100.x.x.x for Tailscale
META_PORT=8001           # Use 8003 if connecting to a Docker-mapped MetaServer

# --- Identity (Required for Docker / Tailscale deployments) ---
MY_IP=<YOUR_TAILSCALE_OR_LAN_IP>   # The globally reachable IP this node reports to MetaServer
                                   # Bypasses Docker NAT masquerading

# --- Paths ---
UPLOAD_DIR=.             # Default source folder for uploads (defaults to current directory)
DOWNLOAD_DIR=D:/Cloud    # Target directory for reconstructed downloaded files

# --- Guardrails ---
SAFETY_BUFFER_GB=4       # Client reserves this much free space on DOWNLOAD_DIR drive before downloading
MIN_FREE_SPACE_GB=5      # Node refuses incoming chunks if available drive space falls below this value
```

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

### Execution — Mode 1: Local Native (Single Machine, Native Terminals)

All components run on the master machine. No environment variables are required; `META_HOST` defaults to `127.0.0.1` automatically.

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

**Terminal 4 — Verify Cluster Health**
```powershell
.\client_app nodes
```

---

### Execution — Mode 2: Local Docker (Single Machine, Containerized Nodes)

Nodes run inside Docker containers on the master machine. The client automatically routes Docker bridge IPs via mapped ports.

**A. Standard Startup (Default Port 8001)**
```bash
docker-compose up --build
```

**B. Custom Startup (Example: Port 8003)**

If you need to change the port via the command line to avoid conflicts:

```powershell
# PowerShell (Windows)
$env:META_PORT="8003"; docker-compose up --build
```
```bash
# Bash (Linux/Mac)
META_PORT=8003 docker-compose up --build
```

**Stop and clean the cluster**
```bash
docker-compose down
```

**View live logs for the Meta Server**
```bash
docker-compose logs -f meta-server
```

---

### Execution — Mode 3: Multi-Machine LAN (Same Wi-Fi Network)

The MetaServer runs on the master machine. The node machine joins via its LAN IP. To ensure the MetaServer knows exactly who is joining, the node machine should explicitly report its own IP.

**Master Machine — Start Meta Server and local nodes**
```powershell
.\meta
.\node_app 9001 data/node1
```

**Node Machine — Using Native Binary**

Set `META_HOST` to tell the node where the master is, and `MY_IP` to declare its own identity.

```powershell
# Set the variables for the session
$env:META_HOST="192.168.1.15"  # LAN IP of the master machine
$env:MY_IP="192.168.1.20"      # LAN IP of this node machine

.\node_app 9005 data/remote_node
```

**Node Machine — Using Docker**

Inject the LAN variables directly into the docker-compose command:

```powershell
$env:META_HOST="192.168.1.15"; $env:MY_IP="192.168.1.20"; docker-compose up --build
```

---

### Execution — Mode 4: Global Multi-Machine (Different Wi-Fi via Tailscale)

Utilizes Tailscale mesh VPN. Each machine must be logged into the same Tailscale account.

**Master Machine — Start Meta Server**
```powershell
.\meta
```

**Node Machine — Global Tailscale Join**

This is the most critical step for global functionality.

```powershell
# 1. Provide the master machine's 100.x.x.x Tailscale IP
$env:META_HOST="<MASTER_TAILSCALE_IP>"

# 2. Provide THIS machine's 100.x.x.x Tailscale IP (required for Docker NAT bypass)
$env:MY_IP="<NODE_TAILSCALE_IP>"

# 3. Launch
.\node_app 9005 data/remote_node
```

**Node Machine — Using Docker Globally**
```powershell
$env:META_HOST="<MASTER_TAILSCALE_IP>"; $env:MY_IP="<NODE_TAILSCALE_IP>"; docker-compose up --build
```

---

## Client Commands and Advanced Usage

The client is location-agnostic. If the MetaServer is running in Docker on port 8003, set `META_PORT` before running any client command.

### Cluster Management

```powershell
# Standard check (defaults to 127.0.0.1:8001)
.\client_app nodes

# Docker-mapped check (if MetaServer is on port 8003)
$env:META_PORT="8003"; .\client_app nodes

# View all files registered in the cluster
.\client_app list
```

### File Operations and Space Safety

```powershell
# Upload (checks MIN_FREE_SPACE_GB on storage nodes before accepting chunks)
.\client_app upload "D:/Videos/aws.mp4"

# Download with custom directory and space buffer check
# Verifies that D:/ has (fileSize + SAFETY_BUFFER_GB) free before starting
$env:DOWNLOAD_DIR="D:/CloudDownloads"; $env:SAFETY_BUFFER_GB="4"; .\client_app download aws.mp4

# Sync — full cycle: Upload -> Cooldown -> Download -> SHA-256 match verification
.\client_app sync samples/video.mp4
```

---

## Multi-Machine Deployment (Deep Dive)

### Scenario A — Same Wi-Fi (LAN)

Set `META_HOST` to the master machine's LAN IP (e.g., `192.168.1.15`) on the node machine. Set `MY_IP` to the node machine's own LAN IP to ensure accurate registration and bypass any intermediate NAT.

### Scenario B — Global Access (Tailscale VPN)

To use a node machine in a different location as a dedicated 1 TB storage vault:

**Prerequisites**
1. Install Tailscale on both the master machine and the node machine.
2. Log both into the same Tailscale account.
3. Identify the master machine's Tailscale IP and the node machine's Tailscale IP from the Tailscale admin dashboard.

---

**Master Machine — `docker-compose.yml`**

This machine hosts the MetaServer. Dual port mapping allows both local and external clients to connect.

- **Key Logic:** The MetaServer is protected with a 1 GB RAM limit to prevent Code 137 (OOM) crashes. Local nodes explicitly report their Tailscale IP via `MY_IP` to bypass Docker NAT masquerading.

```yaml
services:
  meta-server:
    deploy:
      resources:
        limits:
          memory: 1G             # Protection against Code 137 (OOM)
    environment:
      - META_PORT=8001           # Internal port
    ports:
      - "8001:8001"              # Local access
      - "8003:8001"              # External / Docker-mapped access
    volumes:
      - ./data:/usr/src/app/data

  node1:
    environment:
      - META_HOST=meta-server            # Internal Docker service discovery
      - MY_IP=<MASTER_TAILSCALE_IP>      # Tailscale IP of the master machine
    ports:
      - "9001:9001"
```

---

**Node Machine — `docker-compose.yml`**

This machine is a pure storage worker. It must report its own Tailscale IP so the MetaServer can tell the client where to route data directly.

- **Key Logic:** The node points to the master's Tailscale IP for discovery and reports its own mesh IP for data routing. `MIN_FREE_SPACE_GB` prevents the node from filling the drive to exhaustion.

```yaml
services:
  node_remote:
    environment:
      - META_HOST=<MASTER_TAILSCALE_IP>  # Tailscale IP of the master machine
      - MY_IP=<NODE_TAILSCALE_IP>        # Tailscale IP of this node machine
      - NODE_PORT_1=9005
      - MIN_FREE_SPACE_GB=10             # Node refuses uploads if less than 10 GB free
    ports:
      - "9005:9005"
    volumes:
      - D:/CloudVault/node1:/usr/src/app/data/storage
```

**Deploy on Node Machine**
```bash
docker-compose up --build
```

**Deploy on Node Machine using terminal variables (no YAML edits required)**
```powershell
$env:META_HOST="<MASTER_TAILSCALE_IP>"; $env:MY_IP="<NODE_TAILSCALE_IP>"; docker-compose up --build
```

Once running, the node registers with the MetaServer on the master machine through the Tailscale tunnel. No port forwarding or firewall changes are required on either machine.
```

Once running, the node machine registers with the MetaServer on the master machine through the Tailscale tunnel. No port forwarding or firewall changes are required on either machine.

---

## Fault Tolerance and Chaos Testing

| Scenario | Command / Action | Expected Behavior |
|----------|-----------------|-------------------|
| Node Failure | `docker-compose stop node1` | Data is retrieved from the replica node automatically |
| Cluster Restart | `docker-compose down` then `docker-compose up` | Registry is loaded from `registry.db`; all nodes rejoin via heartbeat |
| Network Partition (LAN) | Disconnect node machine from Wi-Fi | Master-local nodes continue serving data; node machine re-registers on reconnect |
| Tunnel Interruption (Tailscale) | Turn off Tailscale on node machine | Node is marked disconnected in MetaServer; re-registers instantly on Tailscale reconnection |
| Disk Full on Node | Fill the node machine's drive | Node refuses further chunks via `MIN_FREE_SPACE_GB` guard; client is notified |
| Client Disk Full | Fill the master machine's download drive | Download aborted immediately via `SAFETY_BUFFER_GB` guard before any data is transferred |

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
| MetaServer Port (Native) | 8001 |
| MetaServer Port (Docker Mapped) | 8003 |

---

## Learning Outcomes

- Designed and implemented a distributed system from scratch using raw TCP sockets in C++.
- **Advanced Networking:** Mastered NAT traversal and VPN mesh integration using Tailscale for global node communication without port forwarding.
- Applied multi-threading via a custom `ThreadPool` to achieve true parallel I/O, with context-aware thread tuning (2 vs 8 threads) for different deployment environments.
- Handled heterogeneous network environments (localhost, Docker NAT, LAN, WAN/Tailscale) within a single unified smart routing layer.
- Implemented end-to-end data integrity using SHA-256 checksums across all replicated nodes, including remote ones.
- **Network Address Translation (NAT) Mastery:** Successfully implemented an identity-reporting protocol to bypass Docker Bridge NAT masquerading, enabling seamless peer-to-peer communication between containers across different physical hardware.
- **Resource Management:** Optimized for real-world hardware constraints including mechanical HDD sequential write performance via adaptive chunking and reduced thread contention.
- Engineered multi-layer disk space guardrails (`MIN_FREE_SPACE_GB` and `SAFETY_BUFFER_GB`) to prevent OS-level instability caused by disk exhaustion across distributed nodes.
- Gained practical experience with Docker volume mapping, container networking, memory limit enforcement, and environment-variable-driven configuration for distributed storage workloads.

---

## Author

**Ayush Krishna Garg**
National Institute of Technology (NIT), Jamshedpur
B.Tech — Electronics and Communication Engineering

This project demonstrates a production-ready private cloud architecture, optimized for real-world networking constraints and heterogeneous hardware environments — from a single laptop to a globally distributed multi-machine storage cluster.