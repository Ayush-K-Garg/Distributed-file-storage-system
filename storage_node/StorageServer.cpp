#include <iostream>
#include <winsock2.h>
#include <vector>
#include <fstream>
#include <string>
#include <direct.h>
#include <thread>

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

// CLIENT HANDLER THREAD
void handleClient(SOCKET client_socket, int port, std::string folder) {

    char command[10] = {0};

    if (!recvAll(client_socket, command, 10)) {
        closesocket(client_socket);
        return;
    }

    std::string cmd(command);

    std::cout << "[PORT " << port << "] Command: " << cmd << "\n";

    // UPLOAD 

    if (cmd == "UPLOAD") {

        int nameLen;

        if (!recvAll(client_socket, (char*)&nameLen, sizeof(nameLen))) {
            closesocket(client_socket);
            return;
        }

        std::vector<char> nameBuffer(nameLen);
        if (!recvAll(client_socket, nameBuffer.data(), nameLen)) {
            closesocket(client_socket);
            return;
        }

        std::string fileName(nameBuffer.begin(), nameBuffer.end());

        int id, size;

        if (!recvAll(client_socket, (char*)&id, sizeof(id))) return;
        if (!recvAll(client_socket, (char*)&size, sizeof(size))) return;

        std::vector<char> buffer(size);

        if (!recvAll(client_socket, buffer.data(), size)) return;

        std::string filepath = folder + "/" + fileName + "_chunk_" + std::to_string(id) + ".bin";

        std::ofstream out(filepath, std::ios::binary);
        out.write(buffer.data(), buffer.size());
        out.close();

        std::cout << "[PORT " << port << "] Saved " << filepath << "\n";
    }

    // GET_CHUNK 

    else if (cmd == "GET_CHUNK") {
        int nameLen;

        if (!recvAll(client_socket, (char*)&nameLen, sizeof(nameLen))) {
            closesocket(client_socket);
            return;
        }

        std::vector<char> nameBuffer(nameLen);
        recvAll(client_socket, nameBuffer.data(), nameLen);

        std::string fileName(nameBuffer.begin(), nameBuffer.end());

        int chunkId;
        recvAll(client_socket, (char*)&chunkId, sizeof(chunkId));

        std::string filepath = folder + "/" + fileName + "_chunk_" + std::to_string(chunkId) + ".bin";

        std::ifstream in(filepath, std::ios::binary);

        if (!in) {
            int notFound = -1;
            send(client_socket, (char*)&notFound, sizeof(notFound), 0);
        } else {
            std::vector<char> buffer((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());

            int size = buffer.size();

            send(client_socket, (char*)&chunkId, sizeof(chunkId), 0);
            send(client_socket, (char*)&size, sizeof(size), 0);
            send(client_socket, buffer.data(), size, 0);

            std::cout << "[PORT " << port << "] Sent " << filepath << "\n";
        }
    }

    closesocket(client_socket);
}

// MAIN
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: node <port> <folder>\n";
        return 1;
    }

    int port = atoi(argv[1]);
    std::string folder = argv[2];

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));

    listen(server_fd, 10);

    _mkdir("data");
    _mkdir(folder.c_str());

    std::cout << "Storage Node running on port " << port << "\n";

    while (true) {
        SOCKET client_socket = accept(server_fd, NULL, NULL);

        std::thread t(handleClient, client_socket, port, folder);
        t.detach();
    }

    closesocket(server_fd);
    WSACleanup();
}