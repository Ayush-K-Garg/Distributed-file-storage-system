# Distributed High-Performance File Storage System (C++)

A robust, multi-threaded distributed storage solution built in C++ using TCP sockets (Winsock2). This system achieves high-speed data transfer through parallel chunking, thread-pooling, and real-time atomic monitoring of network throughput.

---

## Table of Contents

* [Overview](#overview)
* [System Architecture](#system-architecture)
    * [Client Orchestrator](#client-orchestrator)
    * [Metadata Registry](#metadata-registry)
    * [Storage Node Workers](#storage-node-workers)
* [Key Technical Features](#key-technical-features)
    * [Parallel Transfer Engine](#parallel-transfer-engine)
    * [Dynamic Chunk Sizing](#dynamic-chunk-sizing)
    * [Thread-Safe Synchronization](#thread-safe-synchronization)
    * [Real-time Monitoring UI](#real-time-monitoring-ui)
* [Storage Format and Protocol](#storage-format-and-protocol)
* [How It Works](#how-it-works)
    * [Upload Workflow](#upload-workflow)
    * [Download Workflow](#download-workflow)
* [Project Structure](#project-structure)
* [Compilation and Execution](#compilation-and-execution)
* [Performance Benchmarks](#performance-benchmarks)
* [Future Roadmap and Scalability](#future-roadmap-and-scalability)
* [Learning Outcomes](#learning-outcomes)
* [Author](#author)

---

## Overview

This project is a decentralized storage environment designed to handle large-scale binary data. Files are partitioned into optimized segments, distributed across a cluster of storage nodes using a Round-Robin algorithm, and reconstructed with high integrity. The system is binary-safe, supporting PDFs, images, videos, and complex binary executables.

---

## System Architecture

### Client Orchestrator
The client serves as the primary manager of the distributed lifecycle. It handles file partitioning, interaction with the Metadata Server for registry updates, and manages a ThreadPool of 8 concurrent workers to facilitate simultaneous I/O operations across different storage nodes.

### Metadata Registry
The Metadata Server (running on port 8001) maintains the global state of the system. It tracks which chunks of which files are stored on which storage node ports. It ensures fault tolerance by providing the client with multiple replica locations for each chunk.

### Storage Node Workers
Storage nodes are high-performance worker processes that listen on unique ports (e.g., 9001, 9002). Each node utilizes a Producer-Consumer model where a main thread accepts incoming TCP connections and dispatches them to a pool of worker threads that perform disk I/O.

---

## Key Technical Features

### Parallel Transfer Engine
By utilizing a custom ThreadPool, the client bypasses the limitations of sequential TCP transfers. Up to 8 chunks can be uploaded or downloaded in parallel. This saturates available bandwidth, allowing local transfers to reach observed speeds of over 600 MB/s.

### Dynamic Chunk Sizing
The system intelligently analyzes file metadata to determine the most efficient chunk size:
| File Size | Chunk Size | Why? |
| :--- | :--- | :--- |
| **< 1 MB** | 64 KB | Avoids socket handshake overhead for tiny files. |
| **1 MB - 50 MB** | 256 KB | Balances metadata count and transfer speed. |
| **50 MB - 500 MB** | 1 MB | Optimizes sequential I/O for medium-sized data. |
| **500 MB - 2 GB** | 4 MB | Minimizes total chunk count for MetaServer health. |
| **> 2 GB** | 8 MB | Prevents "Metadata Bloat" in massive files. |

### Thread-Safe Synchronization
* **std::atomic:** Used for global byte counters. This allows multiple worker threads to update the total transferred data count simultaneously without the performance penalty of a mutex.
* **std::mutex & lock_guard:** Used to protect the shared map where chunks are collected during download. This ensures that the final file reconstruction is perfectly ordered despite chunks arriving out of sequence.
* **Condition Variables:** Used in the ThreadPool to manage task distribution, ensuring worker threads remain idle when no tasks are available, reducing CPU consumption.

### Real-time Monitoring UI
The system features a non-blocking UI update mechanism. Using the carriage return (\r) escape sequence, the main thread prints a 40-character progress bar, a completion percentage, and a live speedometer (MB/s) that updates every 100ms without scrolling the console.

---

## Storage Format and Protocol

Chunks are persisted on disk using a structured naming convention:
`data/node_id/filename.ext_chunk_id.bin`

The custom TCP protocol uses a fixed-header approach:
1. Command Header (10 bytes)
2. Metadata Length (int)
3. Metadata Body (String)
4. Payload Size (int)
5. Binary Payload (Variable bytes)

---

## How It Works

### Upload Workflow
1. The client splits the local file into chunks based on the dynamic sizing logic.
2. The client registers the filename and chunk count with the Metadata Server.
3. The client initiates parallel UPLOAD commands to storage nodes.
4. Chunks are distributed using a Round-Robin strategy to balance disk usage across nodes.

### Download Workflow
1. The client requests the chunk-map for a specific filename from the Metadata Server.
2. The client receives a list of ports for every chunk.
3. The ThreadPool initiates simultaneous GET_CHUNK requests. 
4. If a primary node fails, the client uses the replica list to attempt a fallback download.
5. Chunks are merged using the FileChunker service once all threads report success.

---

## Project Structure

```text
.
├── client/
│   └── main.cpp                  (ThreadPool, UI, Atomics)
├── metadata_server/
│   └── MetadataServer.cpp        (Registry Logic)
├── storage_node/
│   └── StorageServer.cpp         (Worker Threads, Task Queue)
├── common/
│   ├── services/
│   │   └── FileChunker.cpp       (Split/Merge Logic)
│   └── models/
│       └── Chunk.cpp             (Data Structure)
├── data/                         (Node storage directories)
├── samples/                      (Test files)
└── README.md

🛠️ Compilation and Execution
Compilation
The project requires the Winsock library (-lws2_32) for networking. Use the following commands:

Storage Node:

Bash
g++ storage_node/StorageServer.cpp -o node -lws2_32

Metadata Server:

Bash
g++ metadata_server/MetadataServer.cpp -o meta -lws2_32

Client Application:

Bash
g++ client/main.cpp common/services/FileChunker.cpp common/models/Chunk.cpp -o client_app -lws2_32


Execution (Local Cluster Setup)
Start Metadata Server:

Bash
.\meta
Start Storage Node Cluster (Separate Terminals):

Bash
# Terminal 1
.\node 9001 data/node1

# Terminal 2
.\node 9002 data/node2
Run Client Operations:

Bash
# Upload a file
.\client_app upload samples/file.pdf

# Sync (Upload + Download verification)
.\client_app sync samples/file.pdf


 Performance Benchmarks
Tested on a local Windows environment (SSD, Loopback Interface 127.0.0.1):

Transfer Speeds: Observed peaks up to 670 MB/s during parallel chunk retrieval.

Concurrency: Stable handling of 8+ simultaneous socket connections via ThreadPool.

Throughput Optimization: 25MB files processed in < 2.0 seconds.

Scalability: Linear performance increase observed as more physical nodes (ports) are added to the cluster.

 Future Roadmap and Scalability
1. Automated Node Discovery (Variable Nodes)
To eliminate hardcoded ports, we plan to implement a Discovery Service. Storage nodes will "check-in" with the Metadata Server upon startup. The Metadata Server will then provide the client with a dynamic, live list of available nodes, allowing the cluster to scale from 2 nodes to hundreds without recompiling code.

2. Process Orchestration (One-Click Launch)
To solve the manual terminal management issue, we are developing:

Controller Daemon: A master script (Python or C++) that reads a configuration file and uses the Windows CreateProcess API to spawn and monitor all storage nodes in the background.

Auto-Restart: A supervisor logic that automatically relaunches any node process that crashes.

3. Deployment and Containerization
Dockerization: Creating Dockerfiles for the Node and Meta Server to allow one-click deployment to cloud environments (AWS/Azure).

Orchestration: Using Docker Compose or Kubernetes to start a whole cluster with a single command (e.g., docker-compose up --scale node=10).

4. Advanced Fault Tolerance
Heartbeat System: Implementing a UDP/TCP heartbeat where nodes ping the server every 2 seconds to report health.

Replication Factor: Configuring the system to save every chunk on N nodes (N=2 or N=3) to ensure data safety if a disk fails.

Integrity Hashing: Adding SHA-256 checksums to the metadata to verify file content after reconstruction.

 Learning Outcomes
Advanced TCP/IP: Winsock2 socket programming and custom protocol design.

High-Concurrency: Systems design using ThreadPools and Producer-Consumer patterns.

Synchronization Primitives: Practical application of std::atomic, std::mutex, and std::condition_variable.

Binary Handling: Design for binary-safe, large-scale distributed transfers.

 Author
Developed as a high-performance C++ distributed systems exploration. Focus on network synchronization and parallel I/O.