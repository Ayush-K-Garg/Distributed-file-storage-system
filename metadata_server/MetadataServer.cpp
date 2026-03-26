#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <algorithm>
#include "../common/utils/SocketWrapper.h" 

// --- DATA STRUCTURES ---

struct FileEntry {
    std::string hash;
    // ChunkID -> List of Ports (Logic intact: Ports are the unique identifiers)
    std::vector<std::pair<int, std::vector<int>>> chunks;
};

struct NodeInfo {
    std::string ip;
    std::time_t last_seen;
};

std::unordered_map<std::string, FileEntry> metadata;
// Port -> {IP Address, Last Heartbeat Timestamp}
std::unordered_map<int, NodeInfo> liveNodes; 
std::mutex globalMtx; 

// --- UTILS ---

void cleanString(std::string& s) {
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
}

void saveToRegistry(std::string filename, std::string hash, int numChunks, const std::vector<std::pair<int, std::vector<int>>>& chunks) {
    MKDIR("data"); 
    std::ofstream db("data/registry.db", std::ios::app); 
    db << "FILE " << filename << " " << hash << " " << numChunks << "\n";
    for (auto const& p : chunks) {
        db << "CHUNK " << p.first << " " << p.second.size();
        for (int port : p.second) db << " " << port;
        db << "\n";
    }
    db.close();
}

void loadRegistry() {
    std::ifstream db("data/registry.db");
    if (!db) return;
    std::string line;
    while (std::getline(db, line)) {
        std::stringstream ss(line);
        std::string type; ss >> type;
        if (type == "FILE") {
            std::string fname, hash; int n;
            ss >> fname >> hash >> n;
            FileEntry entry; entry.hash = hash;
            for (int i = 0; i < n; i++) {
                std::string cLine; std::getline(db, cLine);
                if (cLine.empty()) break;
                std::stringstream css(cLine);
                std::string cType; int cid, pCount;
                css >> cType >> cid >> pCount;
                std::vector<int> ports;
                for (int j = 0; j < pCount; j++) {
                    int p; css >> p; ports.push_back(p);
                }
                entry.chunks.push_back({cid, ports});
            }
            metadata[fname] = entry;
        }
    }
    std::cout << "--- Metadata Registry loaded (" << metadata.size() << " files) ---" << std::endl;
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

void nodeJanitor() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::lock_guard<std::mutex> lock(globalMtx);
        std::time_t now = std::time(nullptr);
        for (auto it = liveNodes.begin(); it != liveNodes.end(); ) {
            if (std::difftime(now, it->second.last_seen) > 6.0) {
                std::cout << "--- Node " << it->second.ip << ":" << it->first << " disconnected ---" << std::endl;
                it = liveNodes.erase(it);
            } else ++it;
        }
    }
}

// --- MAIN SERVER LOGIC ---

int main() {
    if (!InitializeSockets()) return 1;

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // Listen on ALL interfaces (Crucial for Tailscale/LAN/Localhost)
    sockaddr_in addr = { AF_INET, htons(8001), INADDR_ANY };
    
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed!" << std::endl;
        return 1;
    }
    
    listen(server_fd, 10);
    loadRegistry();
    std::thread(nodeJanitor).detach();
    
    std::cout << "Universal Metadata Server running on port 8001..." << std::endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        SOCKET client = accept(server_fd, (sockaddr*)&client_addr, &addr_len);
        
        if (client == INVALID_SOCKET) continue;

        SetTcpNoDelay(client);

        // --- UNIVERSAL IP DETECTION ---
        // This captures the IP of the sender automatically. 
        // 127.0.0.1 for local, 172.x for Docker, 100.x for Tailscale, 192.x for LAN.
        std::string senderIP = inet_ntoa(client_addr.sin_addr);

        char buffer[1024] = {0};
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) { CLOSE_SOCKET(client); continue; }

        std::string request(buffer);
        std::stringstream ss(request);
        std::string command; ss >> command;

        if (command == "JOIN" || command == "HEARTBEAT") {
            int port; ss >> port;
            std::lock_guard<std::mutex> lock(globalMtx);
            
            // Map the port to the REAL detected IP
            liveNodes[port] = {senderIP, std::time(nullptr)};
            
            if (command == "JOIN") {
                std::cout << "+++ Node " << senderIP << ":" << port << " joined +++" << std::endl;
            }
        }
        else if (command == "GET_LIVE_NODES") {
            std::lock_guard<std::mutex> lock(globalMtx);
            std::string resp = "";
            for (auto const& [port, info] : liveNodes) {
                resp += info.ip + ":" + std::to_string(port) + " ";
            }
            send(client, resp.c_str(), (int)resp.size() + 1, 0);
        }
        else if (command == "REGISTER") {
            std::string filename, fileHash; int numChunks;
            ss >> filename >> numChunks >> fileHash; 
            cleanString(filename);

            std::lock_guard<std::mutex> lock(globalMtx);
            std::vector<int> currentPorts;
            for(auto const& [port, _] : liveNodes) currentPorts.push_back(port);

            if (!currentPorts.empty()) {
                std::vector<std::pair<int, std::vector<int>>> chunks;
                for (int i = 0; i < numChunks; i++) {
                    std::vector<int> replicas;
                    replicas.push_back(currentPorts[i % currentPorts.size()]);
                    if (currentPorts.size() > 1) {
                        replicas.push_back(currentPorts[(i + 1) % currentPorts.size()]);
                    }
                    chunks.push_back({i, replicas});
                }
                metadata[filename] = {fileHash, chunks};
                saveToRegistry(filename, fileHash, numChunks, chunks);
                std::cout << "Registered: " << filename << " (Replicated on " << currentPorts.size() << " nodes)" << std::endl;
            }
            
            std::string ok = "OK";
            send(client, ok.c_str(), (int)ok.size() + 1, 0);
        }
        else if (command == "GET") {
            std::string filename; ss >> filename;
            cleanString(filename); 
            std::lock_guard<std::mutex> lock(globalMtx);
            
            if (metadata.find(filename) == metadata.end()) {
                int end = -1; 
                sendAll(client, (char*)&end, sizeof(end));
            } else {
                // 1. Send File Hash
                std::string h = metadata[filename].hash;
                int hLen = (int)h.size();
                sendAll(client, (char*)&hLen, sizeof(hLen));
                sendAll(client, h.c_str(), hLen);

                // 2. Send Chunk Map with IP resolution
                for (auto &p : metadata[filename].chunks) {
                    int id = p.first;
                    std::vector<int> filteredPorts;
                    for (int port : p.second) {
                        if (liveNodes.find(port) != liveNodes.end()) {
                            filteredPorts.push_back(port);
                        }
                    }
                    
                    int count = (int)filteredPorts.size();
                    sendAll(client, (char*)&id, sizeof(id));
                    sendAll(client, (char*)&count, sizeof(count));

                    for (int port : filteredPorts) {
                        // Dynamically look up the IP based on the port
                        std::string nodeIP = liveNodes[port].ip;
                        int ipLen = (int)nodeIP.size();
                        sendAll(client, (char*)&ipLen, sizeof(ipLen));
                        sendAll(client, nodeIP.c_str(), ipLen);
                        sendAll(client, (char*)&port, sizeof(port));
                    }
                }
                int end = -1; 
                sendAll(client, (char*)&end, sizeof(end));
            }
        }
        CLOSE_SOCKET(client);
    }
    CleanupSockets();
    return 0;
}