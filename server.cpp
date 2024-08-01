#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

void do_something(int client_fd) {
    char rbuf[64] = {};
    ssize_t n = read(client_fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        std::cerr << "Socket Server read() error" << std::endl;
        return;
    }
    printf("client says: %s\n", rbuf);

    const char *msg = "Hello, client!";
    write(client_fd, msg, strlen(msg));
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket" << std::endl;
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
        0) {
        std::cerr << "setsockopt failed" << std::endl;
        return 1;
    }

    // bind local server address 0.0.0.0:1234 to socket.
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    // The ntohs() and ntohl() functions convert numbers to the required big
    // endian format.
    serverAddress.sin_port = ntohs(1234);      // Port: 1234
    serverAddress.sin_addr.s_addr = ntohl(0);  // Server address: 0.0.0.0

    if (bind(server_fd, (struct sockaddr *)&serverAddress,
             sizeof(serverAddress)) != 0) {
        std::cerr << "Failed to bind Server to port 1234" << std::endl;
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed" << std::endl;
        return 1;
    }

    // The server enters a loop that accepts and processes each client
    // connection.
    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);

        int client_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);

        if (client_fd < 0) {
            // Skip this connection, go to next in queue
            continue;
        }

        do_something(client_fd);
        close(client_fd);
    }

    return 0;
}