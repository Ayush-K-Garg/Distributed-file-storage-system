#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <filesystem> 
#include "../common/utils/SocketWrapper.h"

#ifndef _WIN32
#include <netdb.h>
#include <cstring>
#endif

namespace fs = std::filesystem;

std::queue<SOCKET> taskQueue;
std::mutex mtx;
std::condition_variable cv;

// Helper to send signals to the MetaServer
void notifyMeta(int myPort, std::string cmd)
{
    // 1. Get the MetaServer's location
    const char *hostEnv = std::getenv("META_HOST");
    std::string metaHost = (hostEnv == nullptr) ? "127.0.0.1" : hostEnv;

    // 2. NEW: Get our OWN "Public" identity (Optional)
    // If running in Docker or across Tailscale, set this in your environment
    const char *myIpEnv = std::getenv("MY_IP");
    std::string myIP = (myIpEnv == nullptr) ? "" : myIpEnv;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = {0}; 
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8001);

    // Resolution logic
    struct hostent* he = gethostbyname(metaHost.c_str());
    if (he != nullptr) {
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    } else {
        addr.sin_addr.s_addr = inet_addr(metaHost.c_str());
    }

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
    {
        SetTcpNoDelay(sock); 
        
        // 3. Construct the "Universal Message"
        // If myIP is empty, it sends "JOIN 9001" (Backward Compatible)
        // If myIP is set, it sends "JOIN 9001 100.x.x.x" (Docker/Global Fix)
        std::string msg = cmd + " " + std::to_string(myPort);
        if (!myIP.empty()) {
            msg += " " + myIP;
        }

        send(sock, msg.c_str(), (int)msg.size() + 1, 0);
    }
    
    CLOSE_SOCKET(sock);
}

// Background thread to send heartbeats every 2 seconds
void heartbeatLoop(int myPort)
{
    while (true)
    {
        notifyMeta(myPort, "HEARTBEAT");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

bool recvAll(SOCKET sock, char *buffer, int size)
{
    int total = 0;
    while (total < size)
    {
        int bytes = recv(sock, buffer + total, size - total, 0);
        if (bytes <= 0)
            return false;
        total += bytes;
    }
    return true;
}

bool sendAll(SOCKET sock, const char *buffer, int size)
{
    int total = 0;
    while (total < size)
    {
        int sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0)
            return false;
        total += sent;
    }
    return true;
}

void worker(int port, std::string folder)
{
    while (true)
    {
        SOCKET client_socket;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, []
                    { return !taskQueue.empty(); });
            client_socket = taskQueue.front();
            taskQueue.pop();
        }

        char command[10] = {0};
        if (!recvAll(client_socket, command, 10))
        {
            CLOSE_SOCKET(client_socket);
            continue;
        }

        std::string cmd(command);
        if (cmd == "UPLOAD")
        {
            // --- SPACE GUARD CHECK ---
            const char* minSpaceEnv = std::getenv("MIN_FREE_SPACE_GB");
            long long minSpaceGB = (minSpaceEnv) ? std::stoll(minSpaceEnv) : 5;
            long long minFreeBytes = minSpaceGB * 1024LL * 1024 * 1024;

            try {
                fs::space_info si = fs::space(folder);
                if (si.available < minFreeBytes) {
                    std::cout << "[PORT " << port << "] ERROR: Insufficient space. Node requires " << minSpaceGB << "GB free." << std::endl;
                    CLOSE_SOCKET(client_socket);
                    continue;
                }
            } catch(...) {  }

            int nameLen;
            recvAll(client_socket, (char *)&nameLen, sizeof(nameLen));
            std::vector<char> nBuf(nameLen);
            recvAll(client_socket, nBuf.data(), nameLen);
            std::string fileName(nBuf.begin(), nBuf.end());

            int id, size;
            recvAll(client_socket, (char *)&id, sizeof(id));
            recvAll(client_socket, (char *)&size, sizeof(size));

            std::vector<char> buffer(size);
            recvAll(client_socket, buffer.data(), size);

            std::string filepath = folder + "/" + fileName + "_chunk_" + std::to_string(id) + ".bin";
            std::ofstream out(filepath, std::ios::binary);
            out.write(buffer.data(), size);
            out.close();
            std::cout << "[PORT " << port << "] Received Chunk " << id << std::endl;
        }
        else if (cmd == "GET_CHUNK")
        {
            int nameLen;
            recvAll(client_socket, (char *)&nameLen, sizeof(nameLen));
            std::vector<char> nBuf(nameLen);
            recvAll(client_socket, nBuf.data(), nameLen);
            std::string fileName(nBuf.begin(), nBuf.end());

            int chunkId;
            recvAll(client_socket, (char *)&chunkId, sizeof(chunkId));

            std::string filepath = folder + "/" + fileName + "_chunk_" + std::to_string(chunkId) + ".bin";
            std::ifstream in(filepath, std::ios::binary | std::ios::ate);

            if (!in)
            {
                int fail = -1;
                sendAll(client_socket, (char *)&fail, sizeof(fail));
            }
            else
            {
                int size = (int)in.tellg();
                in.seekg(0, std::ios::beg);

                sendAll(client_socket, (char *)&chunkId, sizeof(chunkId));
                sendAll(client_socket, (char *)&size, sizeof(size));

                char buffer[64 * 1024];
                int totalSent = 0;
                while (totalSent < size)
                {
                    int toRead = std::min((int)sizeof(buffer), size - totalSent);
                    in.read(buffer, toRead);
                    int actualRead = (int)in.gcount();
                    sendAll(client_socket, buffer, actualRead);
                    totalSent += actualRead;
                }
                in.close();
                std::cout << "[PORT " << port << "] Sent Chunk " << chunkId << std::endl;
            }
        }
        CLOSE_SOCKET(client_socket);
    }
}

int main(int argc, char *argv[])
{
    
    int port;
    std::string folder;

    if (argc >= 3) {
        port = atoi(argv[1]);
        folder = argv[2];
    } else {
        const char* pEnv = std::getenv("NODE_PORT_1");
        const char* fEnv = std::getenv("NODE_STORAGE_1");
        if (pEnv && fEnv) {
            port = atoi(pEnv);
            folder = fEnv;
        } else {
            std::cout << "Usage: node_app <port> <folder> OR set NODE_PORT_1 and NODE_STORAGE_1" << std::endl;
            return 1;
        }
    }

    if (!InitializeSockets())
        return 1;

    
    try {
        if (!fs::exists(folder)) {
            fs::create_directories(folder);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error creating storage directory: " << e.what() << std::endl;
        return 1;
    }

    // Connect to MetaServer using the environment variable (Localhost/Tailscale/LAN)
    notifyMeta(port, "JOIN");
    std::thread(heartbeatLoop, port).detach();

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // Bind to all interfaces (Allows traffic from Docker, LAN, and Tailscale)
    sockaddr_in addr = {AF_INET, htons(port), INADDR_ANY};
    bind(server_fd, (sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 10);

    std::cout << "Storage Node on port " << port << " running with heartbeat..."<<std::endl;

    for (int i = 0; i < 8; i++)
    {
        std::thread(worker, port, folder).detach();
    }

    while (true)
    {
        SOCKET client = accept(server_fd, NULL, NULL);
        if (client != INVALID_SOCKET) {
            SetTcpNoDelay(client);
            {
                std::lock_guard<std::mutex> lock(mtx);
                taskQueue.push(client);
            }
            cv.notify_one();
        }
    }
    CleanupSockets();
}