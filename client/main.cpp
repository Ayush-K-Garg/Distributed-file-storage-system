#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>
#include <algorithm>
#include <utility>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "../common/services/FileChunker.h"
#include "../common/utils/SHA256.h"
#include "../common/utils/SocketWrapper.h" 

// --- UTILS ---

struct StorageNode {
    std::string ip;
    int port;
};

// Minimal cleaner to ensure "file.txt" is the same on Windows and Linux/Docker
void cleanString(std::string& s) {
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
}

bool recvAll(SOCKET sock, char *buffer, int size) {
    int total = 0;
    while (total < size) {
        int bytes = recv(sock, buffer + total, size - total, 0);
        if (bytes <= 0) return false;
        total += bytes;
    }
    return true;
}

bool sendAll(SOCKET sock, const char *buffer, int size) {
    int total = 0;
    while (total < size) {
        int sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

SOCKET connectToServer(const std::string& host, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons(port) };
    addr.sin_addr.s_addr = inet_addr(host.c_str());

#ifdef _WIN32
    int timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

std::string extractFileName(const std::string &path) {
    size_t pos = path.find_last_of("/\\");
    std::string fname = (pos == std::string::npos) ? path : path.substr(pos + 1);
    cleanString(fname); // Ensure no hidden chars
    return fname;
}

// --- THREAD POOL ---

class ThreadPool {
public:
    ThreadPool(int n) : stop(false) {
        for (int i = 0; i < n; i++) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->mtx);
                        this->cv.wait(lock, [this]() { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                } 
            });
        }
    }
    void enqueue(std::function<void()> task) {
        { std::lock_guard<std::mutex> lock(mtx); tasks.push(task); }
        cv.notify_one();
    }
    void shutdown() {
        { std::lock_guard<std::mutex> lock(mtx); stop = true; }
        cv.notify_all();
        for (auto &t : workers) if(t.joinable()) t.join();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop;
};

void printProgressBar(std::string label, long long current, long long total, double speedMB) {
    if (total <= 0) return; 
    int barWidth = 40;
    double progress = (double)current / total;
    if (progress > 1.0) progress = 1.0;
    std::cout << "\r" << label << ": [";
    int pos = (int)(barWidth * progress);
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "% " << std::fixed << std::setprecision(2) << speedMB << " MB/s";
    std::cout.flush();
}

// --- MAIN ---

int main(int argc, char* argv[]) {
    if (!InitializeSockets()) return 1;

    if (argc < 3) {
        std::cout << "Usage: client_app upload|download|sync <filepath>" << std::endl;
        return 1;
    }

    const char* envHost = std::getenv("META_HOST");
    std::string metaHost = (envHost == nullptr) ? "127.0.0.1" : envHost;

    std::string mode = argv[1];
    std::string filepath = argv[2];
    std::string filename = extractFileName(filepath);

    // Track both IP and Port for nodes
    std::vector<StorageNode> liveNodes;
    SOCKET liveSock = connectToServer(metaHost, 8001);
    if (liveSock != INVALID_SOCKET) {
        std::string req = "GET_LIVE_NODES";
        sendAll(liveSock, req.c_str(), (int)req.size() + 1);
        char buffer[2048] = {0};
        int bytes = recv(liveSock, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            std::stringstream ss(buffer);
            std::string entry; 
            // Parsing "IP:Port IP:Port"
            while (ss >> entry) {
                size_t colon = entry.find(':');
                if (colon != std::string::npos) {
                    liveNodes.push_back({entry.substr(0, colon), std::stoi(entry.substr(colon + 1))});
                }
            }
        }
        CLOSE_SOCKET(liveSock);
    }

    if (mode == "upload" || mode == "sync") {
        if (liveNodes.empty()) { std::cout << "Error: No storage nodes online." << std::endl; return 1; }
        
        std::string fileHash = SHA256::hashFile(filepath);
        if(fileHash == "") { std::cout << "Error: Source file missing or unreadable." << std::endl; return 1; }

        std::ifstream fileInfo(filepath, std::ios::binary | std::ios::ate);
        long long fileSize = (long long)fileInfo.tellg(); fileInfo.close();
        
        int chunkSize = (fileSize < 500*1024*1024) ? 1024*1024 : 4*1024*1024;
        FileChunker chunker(chunkSize);
        std::vector<Chunk> chunks = chunker.split(filepath);
        
        long long totalUploadSize = 0;
        for (const auto &c : chunks) totalUploadSize += (c.data.size() * ((liveNodes.size() > 1) ? 2 : 1));

        SOCKET metaSock = connectToServer(metaHost, 8001);
        if (metaSock != INVALID_SOCKET) {
            std::string req = "REGISTER " + filename + " " + std::to_string(chunks.size()) + " " + fileHash;
            sendAll(metaSock, req.c_str(), (int)req.size() + 1);
            
            // Wait for handshake to prevent "Unexpected EOF" in Docker
            char ack[10] = {0};
            recv(metaSock, ack, 10, 0); 
            CLOSE_SOCKET(metaSock);
        }

        std::atomic<long long> uploadedBytes(0);
        auto startTime = std::chrono::steady_clock::now();
        ThreadPool uploadPool(8);

        for (size_t i = 0; i < chunks.size(); i++) {
            std::vector<StorageNode> targets;
            targets.push_back(liveNodes[i % liveNodes.size()]);
            if (liveNodes.size() > 1) targets.push_back(liveNodes[(i + 1) % liveNodes.size()]);
            
            for (auto& node : targets) {
                uploadPool.enqueue([&uploadedBytes, &chunks, i, node, filename]() {
                    int retries = 1; bool success = false;
                    while (retries-- >= 0 && !success) {
                        // --- SMART ROUTING LOGIC ---
                        // If the IP is a Docker internal bridge (172.x.x.x), 
                        // and we are on the same laptop, use 127.0.0.1 instead.
                        std::string targetIP = node.ip;
                        if (targetIP.substr(0, 4) == "172.") {
                            targetIP = "127.0.0.1";
                        }

                        SOCKET storSock = connectToServer(targetIP, node.port);
                        if (storSock == INVALID_SOCKET) continue;
                        if (sendAll(storSock, "UPLOAD", 10)) {
                            int nLen = (int)filename.size();
                            sendAll(storSock, (char *)&nLen, sizeof(nLen));
                            sendAll(storSock, filename.c_str(), nLen);
                            sendAll(storSock, (char *)&chunks[i].id, sizeof(chunks[i].id));
                            int cSize = (int)chunks[i].data.size();
                            sendAll(storSock, (char *)&cSize, sizeof(cSize));
                            if (sendAll(storSock, chunks[i].data.data(), cSize)) { uploadedBytes += cSize; success = true; }
                        }
                        CLOSE_SOCKET(storSock);
                    }
                });
            }
        }
        
        while (uploadedBytes < totalUploadSize) {
            long long cur = uploadedBytes.load();
            printProgressBar("Uploading", cur, totalUploadSize, (cur/1024.0/1024.0)/std::chrono::duration<double>(std::chrono::steady_clock::now()-startTime).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        uploadPool.shutdown();
        // Force 100% display
        printProgressBar("Uploading", totalUploadSize, totalUploadSize, (totalUploadSize/1024.0/1024.0)/std::chrono::duration<double>(std::chrono::steady_clock::now()-startTime).count());
        std::cout << "\nUpload complete." << std::endl;
    }

    if (mode == "download" || mode == "sync") {
        SOCKET metaSock = connectToServer(metaHost, 8001);
        if (metaSock == INVALID_SOCKET) return 1;
        std::string getReq = "GET " + filename;
        sendAll(metaSock, getReq.c_str(), (int)getReq.size() + 1);

        std::string expectedHash = ""; int hLen;
        if (recvAll(metaSock, (char*)&hLen, sizeof(hLen))) {
            std::vector<char> hBuf(hLen); recvAll(metaSock, hBuf.data(), hLen);
            expectedHash.assign(hBuf.begin(), hBuf.end());
            cleanString(expectedHash);
        }

        // Updated chunkMap to store IP strings
        std::vector<std::pair<int, std::vector<std::pair<std::string, int>>>> chunkMap;
        while (true) {
            int id; if (!recvAll(metaSock, (char *)&id, sizeof(id)) || id == -1) break;
            int count; recvAll(metaSock, (char *)&count, sizeof(count));
            
            std::vector<std::pair<std::string, int>> nodeLocations;
            for (int i = 0; i < count; i++) {
                // Read IP string from protocol
                int ipLen; recvAll(metaSock, (char*)&ipLen, sizeof(ipLen));
                std::vector<char> ipBuf(ipLen); recvAll(metaSock, ipBuf.data(), ipLen);
                std::string nodeIP(ipBuf.begin(), ipBuf.end());
                
                int port; recvAll(metaSock, (char *)&port, sizeof(port));
                nodeLocations.push_back({nodeIP, port});
            }
            chunkMap.push_back({id, nodeLocations});
        }
        CLOSE_SOCKET(metaSock);

        if (chunkMap.empty()) { std::cout << "Error: File metadata not found." << std::endl; return 1; }

        ThreadPool downloadPool(8);
        std::map<int, Chunk> received; std::mutex mtxMap;
        std::atomic<long long> downloadedBytes(0);
        auto startTime = std::chrono::steady_clock::now();
        
        for (auto &p : chunkMap) {
            int tid = p.first; auto locations = p.second;
            downloadPool.enqueue([&received, &mtxMap, &downloadedBytes, tid, locations, filename]() {
                for (auto &loc : locations) {
                    // --- SMART ROUTING LOGIC ---
                    // If the IP starts with 172., it's a local Docker bridge.
                    // Route to localhost (127.0.0.1) for Windows host-to-container communication.
                    std::string targetIP = loc.first;
                    if (targetIP.substr(0, 4) == "172.") {
                        targetIP = "127.0.0.1";
                    }

                    SOCKET sock = connectToServer(targetIP, loc.second); 
                    if (sock == INVALID_SOCKET) continue;
                    sendAll(sock, "GET_CHUNK", 10);
                    int nLen = (int)filename.size(); sendAll(sock, (char*)&nLen, sizeof(nLen));
                    sendAll(sock, filename.c_str(), nLen); sendAll(sock, (char*)&tid, sizeof(tid));
                    int rid;
                    if (recvAll(sock, (char*)&rid, sizeof(rid)) && rid != -1) {
                        int sz; recvAll(sock, (char*)&sz, sizeof(sz));
                        std::vector<char> buf(sz);
                        if (recvAll(sock, buf.data(), sz)) {
                            downloadedBytes += sz;
                            std::lock_guard<std::mutex> lock(mtxMap);
                            received.emplace(rid, Chunk(rid, buf));
                            CLOSE_SOCKET(sock); break;
                        }
                    }
                    CLOSE_SOCKET(sock);
                } 
            });
        }

        while (true) {
            size_t cur; { std::lock_guard<std::mutex> lock(mtxMap); cur = received.size(); }
            if (cur == chunkMap.size()) break;
            printProgressBar("Downloading", (long long)cur, (long long)chunkMap.size(), (downloadedBytes/1024.0/1024.0)/std::chrono::duration<double>(std::chrono::steady_clock::now()-startTime).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        downloadPool.shutdown(); 
        printProgressBar("Downloading", chunkMap.size(), chunkMap.size(), (downloadedBytes/1024.0/1024.0)/std::chrono::duration<double>(std::chrono::steady_clock::now()-startTime).count());

        std::vector<Chunk> finalChunks;
        for (auto &p : received) finalChunks.push_back(p.second);
        std::string out = "downloaded_" + filename;
        FileChunker(1024).merge(out, finalChunks);
        if (SHA256::hashFile(out) == expectedHash) std::cout << "\nVerified [MATCH]." << std::endl;
        else std::cout << "\nVerification [FAILED] - Content mismatch." << std::endl;
    }

    CleanupSockets();
    return 0;
}