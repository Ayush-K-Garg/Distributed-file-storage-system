#include <iostream>
#include <winsock2.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

// Central Registry: filename -> list of [chunkId, list of ports]
std::unordered_map<std::string, std::vector<std::pair<int, std::vector<int>>>> metadata;

// Discovery Registry: port -> last_seen_timestamp
std::unordered_map<int, std::time_t> liveNodes;
std::mutex globalMtx; // Protects shared maps from concurrent thread access

// Ensures full transmission of binary data over the socket
bool sendAll(SOCKET sock, char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

// Background Janitor: Runs every 2s, removes nodes that haven't pinged in 6s
void nodeJanitor() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::lock_guard<std::mutex> lock(globalMtx);
        std::time_t now = std::time(nullptr);
        
        for (auto it = liveNodes.begin(); it != liveNodes.end(); ) {
            if (std::difftime(now, it->second) > 6.0) {
                std::cout << "--- Node " << it->first << " disconnected (Timeout) ---\n";
                it = liveNodes.erase(it);
            } else {
                ++it;
            }
        }
    }
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons(8001), INADDR_ANY };

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    // Launch janitor thread to monitor node health
    std::thread(nodeJanitor).detach();
    std::cout << "Metadata Server running on port 8001 (Heartbeat enabled)\n";

    while (true) {
        SOCKET client = accept(server_fd, NULL, NULL);
        char buffer[1024] = {0};
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            closesocket(client);
            continue;
        }

        std::string request(buffer);
        std::stringstream ss(request);
        std::string command;
        ss >> command;

        // JOIN/HEARTBEAT: Update or add node to the live list
        if (command == "JOIN" || command == "HEARTBEAT") {
            int port; ss >> port;
            std::lock_guard<std::mutex> lock(globalMtx);
            liveNodes[port] = std::time(nullptr);
            if (command == "JOIN") std::cout << "+++ Node " << port << " joined +++\n";
        }
        // GET_LIVE_NODES: Returns a space-separated string of all active ports
        else if (command == "GET_LIVE_NODES") {
            std::lock_guard<std::mutex> lock(globalMtx);
            std::string resp = "";
            for (auto const& [port, _] : liveNodes) {
                resp += std::to_string(port) + " ";
            }
            send(client, resp.c_str(), (int)resp.size() + 1, 0);
        }
        // REGISTER: Maps a file to chunks and assigns 2 live replicas per chunk
        else if (command == "REGISTER") {
            std::string filename; int numChunks;
            ss >> filename >> numChunks;

            std::lock_guard<std::mutex> lock(globalMtx);
            std::vector<int> currentPorts;
            for(auto const& [port, _] : liveNodes) currentPorts.push_back(port);

            if (currentPorts.empty()) {
                std::cout << "Error: Registration failed. No live nodes.\n";
            } else {
                std::vector<std::pair<int, std::vector<int>>> chunks;
                for (int i = 0; i < numChunks; i++) {
                    std::vector<int> replicas;
                    // Primary Node
                    replicas.push_back(currentPorts[i % currentPorts.size()]);
                    // Secondary Node (if available)
                    if (currentPorts.size() > 1) {
                        replicas.push_back(currentPorts[(i + 1) % currentPorts.size()]);
                    }
                    chunks.push_back({i, replicas});
                }
                metadata[filename] = chunks;
                std::cout << "Registered: " << filename << " with RF=2\n";
            }
        }
        // GET: Returns chunk-to-node mapping, filtering out any dead nodes
        else if (command == "GET") {
            std::string filename; ss >> filename;
            std::lock_guard<std::mutex> lock(globalMtx);
            if (metadata.find(filename) == metadata.end()) {
                int end = -1;
                sendAll(client, (char*)&end, sizeof(end));
            } else {
                for (auto &p : metadata[filename]) {
                    int id = p.first;
                    std::vector<int> filteredPorts;
                    // Proactive check: only return ports that are currently in liveNodes
                    for (int port : p.second) {
                        if (liveNodes.find(port) != liveNodes.end()) filteredPorts.push_back(port);
                    }
                    int count = (int)filteredPorts.size();
                    sendAll(client, (char*)&id, sizeof(id));
                    sendAll(client, (char*)&count, sizeof(count));
                    for (int port : filteredPorts) sendAll(client, (char*)&port, sizeof(port));
                }
                int end = -1; // Sentinel value to signal end of chunk list
                sendAll(client, (char*)&end, sizeof(end));
            }
        }
        closesocket(client);
    }
    return 0;
}