# Distributed File Storage System (C++)

A distributed file storage system built in C++ using TCP sockets (Winsock).
It supports chunk-based storage, replication, and fault-tolerant file retrieval across multiple nodes.

---

## Table of Contents

* Overview
* Architecture
* Features
* Storage Format
* How It Works
* Getting Started
* Compilation
* Execution
* Project Structure
* Future Improvements
* Learning Outcomes
* Author

---

## Overview

This project demonstrates a distributed storage system where files are:

* Split into chunks
* Distributed across multiple storage nodes
* Replicated for fault tolerance
* Reconstructed during download

It is fully binary-safe and works with all file types including images, videos, and PDFs.

---

## Architecture

### Components

#### Client

* Splits files into chunks
* Uploads chunks to storage nodes
* Downloads and reconstructs files
* Supports **upload, download, and sync modes**

---

#### Metadata Server

* Maintains mapping: file → chunk → node(s)
* Handles registration and lookup requests

---

#### Storage Nodes

* Store chunks on disk
* Serve chunk read/write requests

---

## Features

* Chunk-based file storage
* Distributed architecture
* Replication for fault tolerance
* Multi-file support
* Binary-safe transfer
* Custom TCP communication protocol
* Command-based client (upload/download/sync)
* Supports files from any folder path

---

## Storage Format

Chunks are stored as:

```bash
data/node1/file.txt_chunk_0.bin
data/node2/image.jpg_chunk_1.bin
```

This prevents overwriting and allows multiple files to coexist safely.

---

## How It Works

### Upload Flow

1. Client splits file into chunks
2. Sends metadata (file name and chunk count)
3. Metadata server assigns storage nodes
4. Client uploads chunks to assigned nodes (with replication)

---

### Download Flow

1. Client requests metadata
2. Receives chunk-to-node mapping
3. Attempts retrieval from available nodes (fault tolerant)
4. Reconstructs the original file

---

## Getting Started

### Prerequisites

* Windows OS
* g++ (MinGW or equivalent)
* Winsock library (`ws2_32`)

---

## Compilation

### Storage Node

```bash
g++ storage_node/StorageServer.cpp -o node -lws2_32
```

### Metadata Server

```bash
g++ metadata_server/MetadataServer.cpp -o meta -lws2_32
```

### Client

```bash
g++ client/main.cpp common/services/FileChunker.cpp common/models/Chunk.cpp -o client_app -lws2_32
```

---

## Execution

### Step 1: Start Metadata Server

```bash
.\meta
```

---

### Step 2: Start Storage Nodes (in separate terminals)

```bash
.\node 9001 data/node1
.\node 9002 data/node2
```

---

### Step 3: Run Client

#### Upload a file

```bash
.\client_app upload samples/file.pdf
```

---

#### Download a file

```bash
.\client_app download samples/file.pdf
```

---

#### Upload + Download (Sync)

```bash
.\client_app sync samples/file.pdf
```

---

## Notes

* You can provide **file paths**, not just files in root directory
  Example:

```bash
.\client_app upload samples/image.jpg
```

* Output file will be:

```bash
downloaded_image.jpg
```

* Ensure all servers are running before using the client

---

## Project Structure

```bash
.
├── client/
├── metadata_server/
├── storage_node/
├── common/
│   ├── models/
│   └── services/
├── data/
├── samples/        ← (optional input files)
└── README.md
```

---

## Future Improvements

* Dynamic chunk sizing (based on file size)
* Multithreading support (parallel clients)
* Heartbeat system for node health monitoring
* Automatic node discovery
* Parallel chunk upload/download
* Persistent connections (performance improvement)
* Cloud deployment support

---

## Learning Outcomes

This project helps in understanding:

* TCP socket programming
* Distributed system design
* Fault tolerance strategies
* File I/O in C++
* Client-server architecture

---

## Author

Built as a hands-on distributed systems project using C++.
