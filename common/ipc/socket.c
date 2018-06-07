#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <errno.h>

#include "socket.h"
#include "../log/log.h"

///TODO: Cambié exit's por return's; revisar que siempre chequeo el return y fallo si hace falta

int create_client_socket(const char* server_ip, uint16_t server_port) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        log_error("Error creating client socket.");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    struct hostent* server = gethostbyname(server_ip) ;
    if (!server) {
        log_error("Error calling gethostbyname.");
        return -1;
    }
    bcopy(server->h_addr, &(server_addr.sin_addr.s_addr), (size_t) server->h_length);

    if ( connect(socket_fd, (struct sockaddr*) &server_addr, sizeof(server_addr)) ) {
        log_error("Error calling connect.");
        return -1;
    }

    return socket_fd;
}

int create_server_socket(uint16_t client_port) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        log_error("Error creating server socket.");
        return -1;
    }

    // Reutilizar dirección aunque esté en el estado TIME_WAIT
    int reuse = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuse, sizeof(reuse)) < 0) {
        log_error("setsockopt(SO_REUSEADDR) failed");
    }

    struct sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(client_port);
    myaddr.sin_addr.s_addr = INADDR_ANY;

    socklen_t myaddr_size = sizeof(myaddr);

    if (bind(socket_fd, (struct sockaddr*) &myaddr, myaddr_size) < 0) {
        log_error("Error calling bind.");
        return -1;
    }

    if (listen(socket_fd, 10) < 0) {
        log_error("Error calling listen.");
        return -1;
    }

    return socket_fd;
}

int accept_client(int server_socket) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    int client_fd = accept(server_socket, (struct sockaddr*) &client_addr, &client_addr_size);
    if (client_fd < 0 && errno != EINTR) {
        log_error("Error accepting client.");
    }

    return client_fd;
}
