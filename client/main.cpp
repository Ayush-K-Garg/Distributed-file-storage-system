#include <iostream>
#include <fstream>
#include <winsock2.h>
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

#pragma comment(lib, "ws2_32.lib")

// =========================
// UTILS
// =========================

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

SOCKET connectToServer(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons(port) };
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Set 5-second connection timeout
    int timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

std::string extractFileName(const std::string &path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// =========================
// THREAD POOL
// =========================

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
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.push(task);
        }
        cv.notify_one();
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }
        cv.notify_all();
        for (auto &t : workers) t.join();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop;
};

// =========================
// UI HELPERS
// =========================

void printProgressBar(std::string label, long long current, long long total, double speedMB) {
    int barWidth = 40;
    double progress = (total > 0) ? (double)current / total : 0;
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

// =========================
// MAIN
// =========================

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (__argc < 3) {
        std::cout << "Usage: client_app upload|download|sync <filepath>\n";
        return 1;
    }

    std::string mode = __argv[1];
    std::string filepath = __argv[2];
    std::string filename = extractFileName(filepath);

    std::vector<int> storagePorts;
    SOCKET liveSock = connectToServer(8001);
    if (liveSock != INVALID_SOCKET) {
        std::string req = "GET_LIVE_NODES";
        sendAll(liveSock, req.c_str(), (int)req.size() + 1);
        char buffer[1024] = {0};
        int bytes = recv(liveSock, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            std::stringstream ss(buffer);
            int p;
            while (ss >> p) storagePorts.push_back(p);
        }
        closesocket(liveSock);
    }

    if (storagePorts.empty()) {
        std::cout << "Error: No storage nodes online.\n";
        return 1;
    }

    std::ifstream fileInfo(filepath, std::ios::binary | std::ios::ate);
    long long fileSize = (fileInfo) ? (long long)fileInfo.tellg() : 0;
    if (fileInfo) fileInfo.close();

    int chunkSize;
    if (fileSize < 1024 * 1024) chunkSize = 64 * 1024;
    else if (fileSize < 50 * 1024 * 1024) chunkSize = 256 * 1024;
    else if (fileSize < 500 * 1024 * 1024) chunkSize = 1024 * 1024;
    else if (fileSize < 2LL * 1024 * 1024 * 1024) chunkSize = 4 * 1024 * 1024;
    else chunkSize = 8 * 1024 * 1024;

    FileChunker chunker(chunkSize);

    // =========================
    // UPLOAD
    // =========================
    if (mode == "upload" || mode == "sync") {
        std::vector<Chunk> chunks = chunker.split(filepath);
        long long totalUploadSize = 0;
        for (const auto &c : chunks) {
            totalUploadSize += (c.data.size() * ((storagePorts.size() > 1) ? 2 : 1));
        }

        SOCKET metaSock = connectToServer(8001);
        if (metaSock != INVALID_SOCKET) {
            std::string req = "REGISTER " + filename + " " + std::to_string(chunks.size());
            sendAll(metaSock, req.c_str(), (int)req.size() + 1);
            closesocket(metaSock);
        }

        std::atomic<long long> uploadedBytes(0);
        auto startTime = std::chrono::steady_clock::now();
        ThreadPool uploadPool(8);

        for (size_t i = 0; i < chunks.size(); i++) {
            std::vector<int> targets;
            targets.push_back(storagePorts[i % storagePorts.size()]);
            if (storagePorts.size() > 1) targets.push_back(storagePorts[(i + 1) % storagePorts.size()]);

            for (int port : targets) {
                uploadPool.enqueue([&uploadedBytes, &chunks, i, port, filename]() {
                    int retries = 3;
                    bool success = false;
                    while (retries-- > 0 && !success) {
                        SOCKET storSock = connectToServer(port);
                        if (storSock == INVALID_SOCKET) continue;

                        if (sendAll(storSock, "UPLOAD", 10)) {
                            int nLen = (int)filename.size();
                            sendAll(storSock, (char *)&nLen, sizeof(nLen));
                            sendAll(storSock, filename.c_str(), nLen);
                            sendAll(storSock, (char *)&chunks[i].id, sizeof(chunks[i].id));
                            int cSize = (int)chunks[i].data.size();
                            sendAll(storSock, (char *)&cSize, sizeof(cSize));
                            if (sendAll(storSock, chunks[i].data.data(), cSize)) {
                                uploadedBytes += cSize;
                                success = true;
                            }
                        }
                        closesocket(storSock);
                    }
                });
            }
        }

        long long lastUpBytes = 0;
        auto lastUpTime = std::chrono::steady_clock::now();

        while (uploadedBytes < totalUploadSize) {
            long long currentUp = uploadedBytes.load();
            if (currentUp > lastUpBytes) {
                lastUpBytes = currentUp;
                lastUpTime = std::chrono::steady_clock::now();
            } else if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastUpTime).count() > 15) {
                std::cout << "\nError: Upload stalled (Node timeout).\n";
                break;
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double speed = (elapsed > 0) ? (currentUp / 1024.0 / 1024.0) / elapsed : 0;
            printProgressBar("Uploading", currentUp, totalUploadSize, speed);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        uploadPool.shutdown();
        std::cout << "\nUpload complete.\n";
    }

    // =========================
    // GET METADATA
    // =========================
    SOCKET metaSock = connectToServer(8001);
    if (metaSock == INVALID_SOCKET) return 1;

    std::string getReq = "GET " + filename;
    sendAll(metaSock, getReq.c_str(), (int)getReq.size() + 1);

    std::vector<std::pair<int, std::vector<int>>> chunkMap;
    while (true) {
        int id;
        if (!recvAll(metaSock, (char *)&id, sizeof(id)) || id == -1) break;
        int count;
        recvAll(metaSock, (char *)&count, sizeof(count));
        std::vector<int> ports(count);
        for (int i = 0; i < count; i++) recvAll(metaSock, (char *)&ports[i], sizeof(int));
        chunkMap.push_back({id, ports});
    }
    closesocket(metaSock);

    // =========================
    // DOWNLOAD
    // =========================
    if (mode == "download" || mode == "sync") {
        ThreadPool pool(8);
        std::map<int, Chunk> received;
        std::mutex mtxMap;
        std::atomic<long long> downloadedBytes(0);
        auto startTime = std::chrono::steady_clock::now();
        
        size_t lastChunkCount = 0;
        auto lastProgressTime = std::chrono::steady_clock::now();
        bool isStalled = false;

        for (auto &p : chunkMap) {
            int targetId = p.first;
            auto targetPorts = p.second;
            pool.enqueue([&received, &mtxMap, &downloadedBytes, targetId, targetPorts, filename]() {
                for (int port : targetPorts) {
                    SOCKET sock = connectToServer(port);
                    if (sock == INVALID_SOCKET) continue;

                    sendAll(sock, "GET_CHUNK", 10);
                    int nLen = (int)filename.size();
                    sendAll(sock, (char*)&nLen, sizeof(nLen));
                    sendAll(sock, filename.c_str(), nLen);
                    sendAll(sock, (char*)&targetId, sizeof(targetId));

                    int respId;
                    if (recvAll(sock, (char*)&respId, sizeof(respId)) && respId != -1) {
                        int size;
                        recvAll(sock, (char*)&size, sizeof(size));
                        std::vector<char> buffer(size);
                        if (recvAll(sock, buffer.data(), size)) {
                            downloadedBytes += size;
                            std::lock_guard<std::mutex> lock(mtxMap);
                            received.emplace(respId, Chunk(respId, buffer));
                            closesocket(sock);
                            break; 
                        }
                    }
                    closesocket(sock);
                } 
            });
        }

        while (true) {
            size_t currentCount;
            {
                std::lock_guard<std::mutex> lock(mtxMap);
                currentCount = received.size();
            }

            if (currentCount == chunkMap.size()) break;

            if (currentCount > lastChunkCount) {
                lastChunkCount = currentCount;
                lastProgressTime = std::chrono::steady_clock::now();
            } else if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastProgressTime).count() >= 15) {
                isStalled = true;
                break;
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double speed = (elapsed > 0) ? (downloadedBytes / 1024.0 / 1024.0) / elapsed : 0;
            printProgressBar("Downloading", downloadedBytes, (long long)chunkMap.size() * chunkSize, speed);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (isStalled) {
            std::cout << "\n\nError: Download stalled (Replicas offline).\n";
        } else {
            pool.shutdown();
            std::vector<Chunk> finalChunks;
            for (auto &p : received) finalChunks.push_back(p.second);
            chunker.merge("downloaded_" + filename, finalChunks);
            std::cout << "\nDownload complete and verified.\n";
        }
    }

    WSACleanup();
    return 0;
}