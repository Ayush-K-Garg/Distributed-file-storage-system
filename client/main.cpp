#include <iostream>
#include <winsock2.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "../common/services/FileChunker.h"

#pragma comment(lib, "ws2_32.lib")

bool recvAll(SOCKET sock, char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int bytes = recv(sock, buffer + total, size - total, 0);
        if (bytes <= 0) return false;
        total += bytes;
    }
    return true;
}

SOCKET connectToServer(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        return -1;
    }

    return sock;
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    if (__argc < 2) {
    std::cout << "Usage: client_app <filename>\n";
    return 1;
}

std::string filename = __argv[1];

    // REGISTER
    SOCKET metaSock = connectToServer(8001);
    FileChunker chunker(1024);
auto chunks = chunker.split(filename);

int numChunks = chunks.size();

std::string registerReq = "REGISTER " + filename + " " + std::to_string(numChunks);
    send(metaSock, registerReq.c_str(), registerReq.size() + 1, 0);
    closesocket(metaSock);

    // GET METADATA
    metaSock = connectToServer(8001);
    std::string getReq = "GET " + filename;
    send(metaSock, getReq.c_str(), getReq.size() + 1, 0);

    std::vector<std::pair<int, std::vector<int>>> chunkMap;

    while (true) {
        int id;
        int bytes = recv(metaSock, (char*)&id, sizeof(id), 0);
        if (bytes <= 0 || id == -1) break;

        int count;
        recv(metaSock, (char*)&count, sizeof(count), 0);

        std::vector<int> ports;

        for (int i = 0; i < count; i++) {
            int port;
            recv(metaSock, (char*)&port, sizeof(port), 0);
            ports.push_back(port);
        }

        chunkMap.push_back({id, ports});
    }

    closesocket(metaSock);

  

   
    //  UPLOAD (REPLICATION)
    
    for (auto &p : chunkMap) {
        int chunkId = p.first;
        auto &ports = p.second;

        auto &chunk = chunks[chunkId];

        for (int port : ports) {
            SOCKET sock = connectToServer(port);
            if (sock < 0) continue;

            char cmd[10] = {0};
            strcpy(cmd, "UPLOAD");

            send(sock, cmd, 10, 0);

          int nameLen = filename.size();
send(sock, (char*)&nameLen, sizeof(nameLen), 0);
send(sock, filename.c_str(), nameLen, 0);

int id = chunk.id;
int size = chunk.data.size();

send(sock, (char*)&id, sizeof(id), 0);
send(sock, (char*)&size, sizeof(size), 0);
send(sock, chunk.data.data(), size, 0);

            closesocket(sock);

            std::cout << "Replicated chunk " << id 
                      << " to port " << port << "\n";
        }
    }

    std::cout << "Upload complete\n";

  
    //  DOWNLOAD (FAULT TOLERANT)

    std::vector<Chunk> receivedChunks;

    for (auto &p : chunkMap) {
        int chunkId = p.first;
        auto &ports = p.second;

        bool success = false;

        for (int port : ports) {
            SOCKET sock = connectToServer(port);
            if (sock < 0) continue;

            char cmd[10] = {0};
            strcpy(cmd, "GET_CHUNK");

            send(sock, cmd, 10, 0);
            int nameLen = filename.size();
send(sock, (char*)&nameLen, sizeof(nameLen), 0);
send(sock, filename.c_str(), nameLen, 0);

send(sock, (char*)&chunkId, sizeof(chunkId), 0);

            int id;

            if (!recvAll(sock, (char*)&id, sizeof(id))) {
                closesocket(sock);
                continue;
            }

            if (id == -1) {
                closesocket(sock);
                continue;
            }

            int size;
            if (!recvAll(sock, (char*)&size, sizeof(size))) {
                closesocket(sock);
                continue;
            }

            std::vector<char> buffer(size);

            if (!recvAll(sock, buffer.data(), size)) {
                closesocket(sock);
                continue;
            }

            receivedChunks.emplace_back(id, buffer);

            std::cout << "Downloaded chunk " << id 
                      << " from port " << port << "\n";

            closesocket(sock);
            success = true;
            break;
        }

        if (!success) {
            std::cout << "Chunk " << chunkId << " failed on all replicas\n";
        }
    }
    std::string outputFile = "downloaded_" + filename;

chunker.merge(outputFile, receivedChunks);
    std::cout << "File reconstructed successfully\n";

    WSACleanup();
}