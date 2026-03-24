#include <iostream>
#include <winsock2.h>
#include <vector>
#include <fstream>
#include <string>
#include <direct.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

std::queue<SOCKET> taskQueue;
std::mutex mtx;
std::condition_variable cv;

// Helper to send signals to the MetaServer
void notifyMeta(int myPort, std::string cmd) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons(8001) };
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
        std::string msg = cmd + " " + std::to_string(myPort);
        send(sock, msg.c_str(), (int)msg.size() + 1, 0);
    }
    closesocket(sock);
}

// Background thread to send heartbeats every 2 seconds
void heartbeatLoop(int myPort) {
    while (true) {
        notifyMeta(myPort, "HEARTBEAT");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

bool recvAll(SOCKET sock, char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int bytes = recv(sock, buffer + total, size - total, 0);
        if (bytes <= 0) return false;
        total += bytes;
    }
    return true;
}

bool sendAll(SOCKET sock, const char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

void worker(int port, std::string folder) {
    while (true) {
        SOCKET client_socket;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [] { return !taskQueue.empty(); });
            client_socket = taskQueue.front();
            taskQueue.pop();
        }

        char command[10] = {0};
        if (!recvAll(client_socket, command, 10)) {
            closesocket(client_socket);
            continue;
        }

        std::string cmd(command);
        if (cmd == "UPLOAD") {
            int nameLen;
            recvAll(client_socket, (char*)&nameLen, sizeof(nameLen));
            std::vector<char> nBuf(nameLen);
            recvAll(client_socket, nBuf.data(), nameLen);
            std::string fileName(nBuf.begin(), nBuf.end());

            int id, size;
            recvAll(client_socket, (char*)&id, sizeof(id));
            recvAll(client_socket, (char*)&size, sizeof(size));

            std::vector<char> buffer(size);
            recvAll(client_socket, buffer.data(), size);

            std::string filepath = folder + "/" + fileName + "_chunk_" + std::to_string(id) + ".bin";
            std::ofstream out(filepath, std::ios::binary);
            out.write(buffer.data(), size);
            out.close();
            std::cout << "[PORT " << port << "] Received Chunk " << id << "\n";
        } 
        else if (cmd == "GET_CHUNK") {
            int nameLen;
            recvAll(client_socket, (char*)&nameLen, sizeof(nameLen));
            std::vector<char> nBuf(nameLen);
            recvAll(client_socket, nBuf.data(), nameLen);
            std::string fileName(nBuf.begin(), nBuf.end());

            int chunkId;
            recvAll(client_socket, (char*)&chunkId, sizeof(chunkId));

            std::string filepath = folder + "/" + fileName + "_chunk_" + std::to_string(chunkId) + ".bin";
            std::ifstream in(filepath, std::ios::binary | std::ios::ate);

            if (!in) {
                int fail = -1;
                sendAll(client_socket, (char*)&fail, sizeof(fail));
            } else {
                int size = (int)in.tellg();
                in.seekg(0, std::ios::beg);

                sendAll(client_socket, (char*)&chunkId, sizeof(chunkId));
                sendAll(client_socket, (char*)&size, sizeof(size));

                char buffer[64 * 1024];
                int totalSent = 0;
                while (totalSent < size) {
                    int toRead = std::min((int)sizeof(buffer), size - totalSent);
                    in.read(buffer, toRead);
                    int actualRead = (int)in.gcount();
                    sendAll(client_socket, buffer, actualRead);
                    totalSent += actualRead;
                }
                in.close();
                std::cout << "[PORT " << port << "] Sent Chunk " << chunkId << "\n";
            }
        }
        closesocket(client_socket);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) return 1;
    int port = atoi(argv[1]);
    std::string folder = argv[2];

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    _mkdir("data");
    _mkdir(folder.c_str());

    // --- New Discovery Logic ---
    notifyMeta(port, "JOIN");
    std::thread(heartbeatLoop, port).detach();

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons(port), INADDR_ANY };
    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    std::cout << "Storage Node on port " << port << " running with heartbeat...\n";

    for (int i = 0; i < 8; i++) {
        std::thread(worker, port, folder).detach();
    }

    while (true) {
        SOCKET client = accept(server_fd, NULL, NULL);
        {
            std::lock_guard<std::mutex> lock(mtx);
            taskQueue.push(client);
        }
        cv.notify_one();
    }
}