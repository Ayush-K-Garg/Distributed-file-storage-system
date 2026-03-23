# Distributed File Storage System (C++)

A distributed file storage system built in C++ using TCP sockets (Winsock).
It supports chunk-based storage, replication, multithreading, parallel transfer, and fault-tolerant file retrieval across multiple nodes.

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
* Notes
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
* Retrieved using fallback mechanisms
* Reconstructed during download

The system is **binary-safe** and supports all file types including:

* PDFs
* Images
* Videos
* Large binary files

---

## Architecture

### Client

* Splits files into chunks
* Uploads chunks to storage nodes
* Downloads and reconstructs files
* Supports 3 modes:

  * upload
  * download
  * sync (upload + download)

---

### Metadata Server

* Maintains mapping:

  file → chunk → [replica nodes]

* Assigns nodes for chunk storage

* Enables fault tolerance by returning multiple replicas

---

### Storage Nodes

* Store chunks on disk
* Serve chunk read/write requests
* Multithreaded (handles multiple clients simultaneously)

---

## Features

* Chunk-based file storage
* Distributed architecture
* Replication for fault tolerance
* Multithreaded storage nodes
* Multi-file support
* Binary-safe transfer
* Custom TCP protocol
* Command-based client
* File path support (not limited to root directory)

---

## Storage Format

Chunks are stored as:

data/node1/file.pdf_chunk_0.bin
data/node2/file.pdf_chunk_1.bin

This ensures:

* No overwriting across files
* Proper separation of chunks
* Support for multiple files simultaneously

---

## How It Works

### Upload Flow

1. Client splits file into chunks
2. Sends metadata (filename + chunk count)
3. Metadata server assigns storage nodes
4. Client uploads chunks (with replication)

---

### Download Flow

1. Client requests metadata
2. Receives chunk-to-node mapping
3. For each chunk:

   * tries replicas one by one
4. Downloads chunks 
5. Reconstructs original file

---

## Getting Started

### Prerequisites

* Windows OS
* g++ (MinGW / MSYS2)
* Winsock library (`ws2_32`)

---

## Compilation

### Storage Node

g++ storage_node/StorageServer.cpp -o node -lws2_32

---

### Metadata Server

g++ metadata_server/MetadataServer.cpp -o meta -lws2_32

---

### Client

g++ client/main.cpp common/services/FileChunker.cpp common/models/Chunk.cpp -o client_app -lws2_32

---

## Execution

### Step 1: Start Metadata Server

.\meta

---

### Step 2: Start Storage Nodes (separate terminals)

.\node 9001 data/node1
.\node 9002 data/node2

---

### Step 3: Run Client

#### Upload

.\client_app upload samples/file.pdf

---

#### Download

.\client_app download samples/file.pdf

---

#### Sync (Upload + Download)

.\client_app sync samples/file.pdf

---

## Notes

* You can use file paths:

.\client_app upload samples/video.mp4

* Output file:

downloaded_video.mp4

* Ensure all servers are running before execution

---

## Project Structure

.
├── client/
├── metadata_server/
├── storage_node/
├── common/
│   ├── models/
│   └── services/
├── data/
├── samples/
└── README.md

---

## Future Improvements

* Dynamic chunk sizing (based on file size)
* Heartbeat system for node health
* Automatic node discovery
* Persistent TCP connections
* Load balancing
* Cloud deployment
* File versioning

---

## Learning Outcomes

This project helps in understanding:

* TCP socket programming
* Distributed system design
* Replication strategies
* Fault tolerance
* Multithreading
* Network protocol design

---

## Author

Built as a hands-on distributed systems project using C++.
