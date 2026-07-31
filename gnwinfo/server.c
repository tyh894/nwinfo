#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include "server.h"
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT "8080"
#define MAX_CLIENTS 10

static HANDLE g_server_thread = NULL;
static volatile BOOL g_server_running = FALSE;
static SOCKET g_listen_socket = INVALID_SOCKET;
static SOCKET g_clients[MAX_CLIENTS];
static CRITICAL_SECTION g_clients_cs;

void gnwinfo_server_broadcast(const char* message)
{
    if (!g_server_running) return;
    int len = (int)strlen(message);
    
    // Allocate buffer to append \n (and \r just in case) for a single send call
    char* buf = (char*)malloc(len + 3);
    if (!buf) return;
    memcpy(buf, message, len);
    buf[len] = '\r';
    buf[len + 1] = '\n';
    buf[len + 2] = '\0';

    EnterCriticalSection(&g_clients_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != INVALID_SOCKET) {
            send(g_clients[i], buf, len + 2, 0);
        }
    }
    LeaveCriticalSection(&g_clients_cs);
    
    free(buf);
}

static DWORD WINAPI server_thread(LPVOID lpParam)
{
    WSADATA wsaData;
    int iResult;
    struct addrinfo *result = NULL;
    struct addrinfo hints;

    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) return 1;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    LPCSTR port = DEFAULT_PORT; // Could be configurable in the future
    
    iResult = getaddrinfo(NULL, port, &hints, &result);
    if (iResult != 0) {
        WSACleanup();
        return 1;
    }

    g_listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (g_listen_socket == INVALID_SOCKET) {
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    iResult = bind(g_listen_socket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        freeaddrinfo(result);
        closesocket(g_listen_socket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = listen(g_listen_socket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        closesocket(g_listen_socket);
        WSACleanup();
        return 1;
    }

    while (g_server_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_listen_socket, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(0, &readfds, NULL, NULL, &tv);
        if (ret > 0) {
            SOCKET client_socket = accept(g_listen_socket, NULL, NULL);
            if (client_socket != INVALID_SOCKET) {
                EnterCriticalSection(&g_clients_cs);
                BOOL added = FALSE;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (g_clients[i] == INVALID_SOCKET) {
                        g_clients[i] = client_socket;
                        added = TRUE;
                        break;
                    }
                }
                LeaveCriticalSection(&g_clients_cs);
                if (!added) closesocket(client_socket);
            }
        }
        
        EnterCriticalSection(&g_clients_cs);
        FD_ZERO(&readfds);
        int max_fd = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i] != INVALID_SOCKET) {
                FD_SET(g_clients[i], &readfds);
                max_fd = 1;
            }
        }
        if (max_fd > 0) {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            ret = select(0, &readfds, NULL, NULL, &tv);
            if (ret > 0) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (g_clients[i] != INVALID_SOCKET && FD_ISSET(g_clients[i], &readfds)) {
                        char buf[1];
                        int recv_ret = recv(g_clients[i], buf, 1, MSG_PEEK);
                        if (recv_ret <= 0) {
                            closesocket(g_clients[i]);
                            g_clients[i] = INVALID_SOCKET;
                        }
                    }
                }
            }
        }
        LeaveCriticalSection(&g_clients_cs);
    }

    closesocket(g_listen_socket);
    WSACleanup();
    return 0;
}

void gnwinfo_server_init(void)
{
    InitializeCriticalSection(&g_clients_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) g_clients[i] = INVALID_SOCKET;
    g_server_running = TRUE;
    g_server_thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
}

void gnwinfo_server_fini(void)
{
    g_server_running = FALSE;
    if (g_server_thread) {
        WaitForSingleObject(g_server_thread, INFINITE);
        CloseHandle(g_server_thread);
        g_server_thread = NULL;
    }
    EnterCriticalSection(&g_clients_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != INVALID_SOCKET) {
            closesocket(g_clients[i]);
            g_clients[i] = INVALID_SOCKET;
        }
    }
    LeaveCriticalSection(&g_clients_cs);
    DeleteCriticalSection(&g_clients_cs);
}
