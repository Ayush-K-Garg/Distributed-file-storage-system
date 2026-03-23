# Distributed File Storage System (C++)

A distributed file storage system built in C++ using TCP sockets (Winsock).
It supports chunk-based storage, replication, and fault-tolerant file retrieval across multiple nodes.

---

## Table of Contents

* [Overview](#overview)
* [Architecture](#architecture)
* [Features](#features)
* [Storage Format](#storage-format)
* [How It Works](#how-it-works)
* [Getting Started](#getting-started)
* [Compilation](#compilation)
* [Execution](#execution)
* [Project Structure](#project-structure)
* [Future Improvements](#future-improvements)
* [Learning Outcomes](#learning-outcomes)
* [Author](#author)

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

#### Metadata Server

* Maintains mapping: file → chunk → node(s)
* Handles registration and lookup requests

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

---

## Storage Format

Chunks are stored as:

```bash
data/node1/file.txt_chunk_0.bin
data/node2/image.jpg_chunk_1.bin
```

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
3. Attempts retrieval from available nodes
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

### Step 2: Start Storage Nodes (separate terminals)

```bash
.\node 9001 data/node1
.\node 9002 data/node2
```

### Step 3: Run Client

```bash
.\client_app file.txt

or 

.\client_app sample.pdf ... whatever file needed to be done
```

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
└── README.md
```

---

## Future Improvements

* Separate upload and download commands
* Dynamic chunk sizing
* Multithreading support
* Heartbeat system for node health monitoring
* Automatic node discovery
* Parallel chunk upload/download
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
