#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

int main() {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        std::cerr << "Failed to create server socket" << std::endl;
        return 1;
    }

    // Define server address 0.0.0.0:1234 to connect client to.
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    // The ntohs() and ntohl() functions convert numbers to the required big
    // endian format.
    serverAddress.sin_port = ntohs(1234);      // Port: 1234
    serverAddress.sin_addr.s_addr = ntohl(0);  // Server address: 0.0.0.0

    if (connect(client_fd, (struct sockaddr*)&serverAddress,
                sizeof(serverAddress)) != 0) {
        std::cerr << "Failed to connect Client to Server" << std::endl;
        return 1;
    }

    // Writing to server
    const char* msg = "Hello, server!";
    write(client_fd, msg, strlen(msg));

    // Reading from server
    char rbuf[64] = {};
    ssize_t n = read(client_fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        std::cerr << "Socket Client read() error" << std::endl;
        return 1;
    }
    printf("Server says: %s\n", rbuf);

    close(client_fd);

    return 0;
}