#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h> 
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET(s) closesocket(s)
#define MKDIR(path) _mkdir(path)
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h> 
typedef int SOCKET;
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)
#define CLOSE_SOCKET(s) close(s)
#define MKDIR(path) mkdir(path, 0777)
#endif

// --- HELPER FUNCTIONS ---

inline bool InitializeSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#endif
    return true;
}

inline void CleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Disables Nagle's Algorithm (TCP_NODELAY).
 * This makes the progress bar smoother and prevents the  hang
 * by forcing data to be sent immediately without buffering.
 */
inline void SetTcpNoDelay(SOCKET sock)
{
    if (sock == INVALID_SOCKET)
        return;
    int opt = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt, sizeof(opt)) == SOCKET_ERROR)
    {
        
        std::cerr << "[SocketWrapper] Warning: Could not set TCP_NODELAY" << std::endl;
    }
}

#endif