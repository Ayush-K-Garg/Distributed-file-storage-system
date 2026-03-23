#include <iostream>
#include <winsock2.h>
#include <unordered_map>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// file → [(chunkId, [ports])]
std::unordered_map<std::string, std::vector<std::pair<int, std::vector<int>>>> metadata;

// safe send
bool sendAll(SOCKET sock, char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int sent = send(sock, buffer + total, size - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8001);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 3);

    std::cout << "Metadata Server running on port 8001\n";

    int ports[] = {9001, 9002};
    int numNodes = 2;

    while (true) {
        SOCKET client = accept(server_fd, NULL, NULL);

        char buffer[1024] = {0};
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            closesocket(client);
            continue;
        }

        buffer[bytes] = '\0';
        std::string request(buffer);

        std::cout << "Request: " << request << "\n";

        // REGISTER
        if (request.find("REGISTER") == 0) {
            std::string rest = request.substr(9);

int spacePos = rest.find(' ');
std::string filename = rest.substr(0, spacePos);
int numChunks = std::stoi(rest.substr(spacePos + 1));

            std::vector<std::pair<int, std::vector<int>>> chunks;

            for (int i = 0; i < numChunks; i++) {
                std::vector<int> replicaNodes;

                int p1 = ports[i % numNodes];
                int p2 = ports[(i + 1) % numNodes];

                replicaNodes.push_back(p1);
                replicaNodes.push_back(p2);

                chunks.push_back({i, replicaNodes});
            }

            metadata[filename] = chunks;

            std::cout << "Registered file with replication: " << filename << "\n";
        }

        // GET
        else if (request.find("GET") == 0) {
            std::string filename = request.substr(4);

            if (metadata.find(filename) == metadata.end()) {
                int end = -1;
                sendAll(client, (char*)&end, sizeof(end));
                closesocket(client);
                continue;
            }

            for (auto &p : metadata[filename]) {
                int id = p.first;
                auto &portsList = p.second;

                int count = portsList.size();

                sendAll(client, (char*)&id, sizeof(id));
                sendAll(client, (char*)&count, sizeof(count));

                for (int port : portsList) {
                    sendAll(client, (char*)&port, sizeof(port));
                }
            }

            int end = -1;
            sendAll(client, (char*)&end, sizeof(end));
        }

        else {
            std::cout << "Unknown request received\n";
        }

        closesocket(client);
    }

    closesocket(server_fd);
    WSACleanup();
}