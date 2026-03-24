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
#include "../common/services/FileChunker.h"

#pragma comment(lib, "ws2_32.lib")

// =========================
// UTILS
// =========================

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

SOCKET connectToServer(int port)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        return INVALID_SOCKET;
    }
    return sock;
}

std::string extractFileName(const std::string &path)
{
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// =========================
// THREAD POOL
// =========================

class ThreadPool
{
public:
    ThreadPool(int n) : stop(false)
    {
        for (int i = 0; i < n; i++)
        {
            workers.emplace_back([this]()
                                 {
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
                } });
        }
    }
    void enqueue(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.push(task);
        }
        cv.notify_one();
    }
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }
        cv.notify_all();
        for (auto &t : workers)
            t.join();
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

void printProgressBar(std::string label, long long current, long long total, double speedMB)
{
    int barWidth = 40;
    double progress = (total > 0) ? (double)current / total : 0;
    if (progress > 1.0)
        progress = 1.0;

    std::cout << "\r" << label << ": [";
    int pos = (int)(barWidth * progress);
    for (int i = 0; i < barWidth; ++i)
    {
        if (i < pos)
            std::cout << "=";
        else if (i == pos)
            std::cout << ">";
        else
            std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "% "
              << std::fixed << std::setprecision(2) << speedMB << " MB/s";
    std::cout.flush();
}

// =========================
// MAIN
// =========================

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::vector<int> storagePorts = {9001, 9002};

    if (__argc < 3)
    {
        std::cout << "Usage: client_app upload|download <filepath>\n";
        return 1;
    }

    std::string mode = __argv[1];
    std::string filepath = __argv[2];
    std::string filename = extractFileName(filepath);

    std::ifstream fileInfo(filepath, std::ios::binary | std::ios::ate);
    long long fileSize = 0;
    if (fileInfo)
    {
        fileSize = (long long)fileInfo.tellg();
        fileInfo.close();
    }

    // =========================
    // OPTIMIZED DYNAMIC CHUNKING
    // =========================
    int chunkSize;

    if (fileSize < 1024 * 1024)
    {
        // Tiny files (< 1MB): Use a single chunk or 64KB

        chunkSize = 64 * 1024;
    }
    else if (fileSize < 50 * 1024 * 1024)
    {
        // Small files (1MB - 50MB): 256KB - 512KB
        chunkSize = 256 * 1024;
    }
    else if (fileSize < 500 * 1024 * 1024)
    {
        // Medium files (50MB - 500MB): 1MB - 2MB
        chunkSize = 1024 * 1024;
    }
    else if (fileSize < 2LL * 1024 * 1024 * 1024)
    {
        // Large files (500MB - 2GB): 4MB
        chunkSize = 4 * 1024 * 1024;
    }
    else
    {
        // Massive files (> 2GB): 8MB or 16MB
        chunkSize = 8 * 1024 * 1024;
    }
    FileChunker chunker(chunkSize);

    // =========================
    // UPLOAD / REGISTER
    // =========================
    if (mode == "upload" || mode == "sync")
    {
        std::vector<Chunk> chunks = chunker.split(filepath);
        long long totalUploadSize = 0;
        for (const auto &c : chunks)
            totalUploadSize += c.data.size();

        SOCKET metaSock = connectToServer(8001);
        if (metaSock != INVALID_SOCKET)
        {
            std::string req = "REGISTER " + filename + " " + std::to_string(chunks.size());
            sendAll(metaSock, req.c_str(), req.size() + 1);
            closesocket(metaSock);
        }

        std::atomic<long long> uploadedBytes(0);
        auto startTime = std::chrono::steady_clock::now();

        for (size_t i = 0; i < chunks.size(); i++)
        {
            int port = storagePorts[i % storagePorts.size()];
            SOCKET storSock = connectToServer(port);
            if (storSock == INVALID_SOCKET)
                continue;

            sendAll(storSock, "UPLOAD", 10);
            int nLen = (int)filename.size();
            sendAll(storSock, (char *)&nLen, sizeof(nLen));
            sendAll(storSock, filename.c_str(), nLen);
            sendAll(storSock, (char *)&chunks[i].id, sizeof(chunks[i].id));
            int cSize = (int)chunks[i].data.size();
            sendAll(storSock, (char *)&cSize, sizeof(cSize));
            sendAll(storSock, chunks[i].data.data(), cSize);

            uploadedBytes += cSize;

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double speed = (elapsed > 0) ? (uploadedBytes / 1024.0 / 1024.0) / elapsed : 0;
            printProgressBar("Uploading", uploadedBytes, totalUploadSize, speed);

            closesocket(storSock);
        }
        std::cout << "\nUpload complete: " << chunks.size() << " chunks.\n";
    }

    // =========================
    // GET METADATA
    // =========================
    SOCKET metaSock = connectToServer(8001);
    if (metaSock == INVALID_SOCKET)
        return 1;

    std::string getReq = "GET " + filename;
    sendAll(metaSock, getReq.c_str(), getReq.size() + 1);

    std::vector<std::pair<int, std::vector<int>>> chunkMap;
    while (true)
    {
        int id;
        if (!recvAll(metaSock, (char *)&id, sizeof(id)) || id == -1)
            break;
        int count;
        recvAll(metaSock, (char *)&count, sizeof(count));
        std::vector<int> ports(count);
        for (int i = 0; i < count; i++)
            recvAll(metaSock, (char *)&ports[i], sizeof(int));
        chunkMap.push_back({id, ports});
    }
    closesocket(metaSock);

    // =========================
    // DOWNLOAD
    // =========================
    if (mode == "download" || mode == "sync")
    {
        ThreadPool pool(8);
        std::map<int, Chunk> received;
        std::mutex mtxMap;
        std::atomic<long long> downloadedBytes(0);

        // Estimate total size for progress bar based on chunks
        long long totalDownloadSize = chunkMap.size() * chunkSize;
        auto startTime = std::chrono::steady_clock::now();

        for (auto &p : chunkMap)
        {
            int targetId = p.first;
            auto targetPorts = p.second;

            pool.enqueue([&received, &mtxMap, &downloadedBytes, targetId, targetPorts, filename]()
                         {
                for (int port : targetPorts) {
                    SOCKET sock = connectToServer(port);
                    if (sock == INVALID_SOCKET) continue;

                    sendAll(sock, "GET_CHUNK", 10);
                    int nLen = (int)filename.size();
                    sendAll(sock, (char*)&nLen, sizeof(nLen));
                    sendAll(sock, filename.c_str(), nLen);
                    sendAll(sock, (char*)&targetId, sizeof(targetId));

                    int respId;
                    if (!recvAll(sock, (char*)&respId, sizeof(respId)) || respId == -1) {
                        closesocket(sock); continue;
                    }

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
                    closesocket(sock);
                } });
        }

        // Monitoring loop for progress
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(mtxMap);
                if (received.size() == chunkMap.size())
                    break;
            }
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double speed = (elapsed > 0) ? (downloadedBytes / 1024.0 / 1024.0) / elapsed : 0;
            printProgressBar("Downloading", downloadedBytes, totalDownloadSize, speed);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        pool.shutdown();

        std::vector<Chunk> finalChunks;
        for (auto &p : received)
            finalChunks.push_back(p.second);
        chunker.merge("downloaded_" + filename, finalChunks);
        std::cout << "\nDownload complete and file reconstructed.\n";
    }

    WSACleanup();
    return 0;
}