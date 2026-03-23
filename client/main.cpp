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

bool sendAll(SOCKET sock, char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0) return false;
        total += sent;
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

std::string extractFileName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    if (__argc < 3) {
        std::cout << "Usage:\n";
        std::cout << "client_app upload <filepath>\n";
        std::cout << "client_app download <filepath>\n";
        std::cout << "client_app sync <filepath>\n";
        return 1;
    }

    std::string mode = __argv[1];
    std::string filepath = __argv[2];
    std::string filename = extractFileName(filepath);

    FileChunker chunker(1024);
    std::vector<Chunk> chunks;

    // REGISTER

    if (mode == "upload" || mode == "sync") {
        chunks = chunker.split(filepath);
        int numChunks = chunks.size();

        SOCKET metaSock = connectToServer(8001);

        std::string registerReq = "REGISTER " + filename + " " + std::to_string(numChunks);
        sendAll(metaSock, (char*)registerReq.c_str(), registerReq.size() + 1); 

        closesocket(metaSock);
    }

    // GET METADATA

    SOCKET metaSock = connectToServer(8001);

    std::string getReq = "GET " + filename;
    sendAll(metaSock, (char*)getReq.c_str(), getReq.size() + 1); 

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

    // UPLOAD

    if (mode == "upload" || mode == "sync") {
        for (auto &p : chunkMap) {
            int chunkId = p.first;
            auto &ports = p.second;

            auto &chunk = chunks[chunkId];

            for (int port : ports) {
                SOCKET sock = connectToServer(port);
                if (sock < 0) continue;

                char cmd[10] = {0};
                strcpy(cmd, "UPLOAD");

                sendAll(sock, cmd, 10); 

                int nameLen = filename.size();
                sendAll(sock, (char*)&nameLen, sizeof(nameLen)); 
                sendAll(sock, (char*)filename.c_str(), nameLen); 

                int id = chunk.id;
                int size = chunk.data.size();

                sendAll(sock, (char*)&id, sizeof(id)); 
                sendAll(sock, (char*)&size, sizeof(size));
                sendAll(sock, chunk.data.data(), size); 

                closesocket(sock);

                std::cout << "Replicated chunk " << id 
                          << " to port " << port << "\n";
            }
        }

        std::cout << "Upload complete\n";
    }

    // DOWNLOAD

    if (mode == "download" || mode == "sync") {
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

                sendAll(sock, cmd, 10); 

                int nameLen = filename.size();
                sendAll(sock, (char*)&nameLen, sizeof(nameLen)); 
                sendAll(sock, (char*)filename.c_str(), nameLen); 

                sendAll(sock, (char*)&chunkId, sizeof(chunkId)); 

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

        std::cout << "File reconstructed as " << outputFile << "\n";
    }

    WSACleanup();
}