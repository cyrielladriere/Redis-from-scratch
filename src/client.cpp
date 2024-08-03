#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

#include "utils.h"

const size_t k_max_msg = 4096;  // bytes

static int32_t query(int fd, const char* text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);  // assume little endian
    memcpy(&wbuf[4], text, len);
    if (int32_t err = write_all(fd, wbuf, 4 + len)) {
        return err;
    }

    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            std::cerr << "EOF error while reading request header" << std::endl;
        } else {
            std::cerr << "Read error while reading request header" << std::endl;
        }
        return err;
    }

    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        std::cerr << "Request size too large" << std::endl;
        return -1;
    }

    // reply body
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        std::cerr << "Error while reading request body";
        return err;
    }

    // do something
    rbuf[4 + len] = '\0';
    printf("server says: %s\n", &rbuf[4]);
    return 0;
}

int main() {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        std::cerr << "Failed to create client socket" << std::endl;
        return 1;
    }

    // Define server address 0.0.0.0:1234 to connect client to.
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    // The ntohs() and ntohl() functions convert numbers to the required big
    // endian format.
    server_addr.sin_port = ntohs(1234);      // Port: 1234
    server_addr.sin_addr.s_addr = ntohl(0);  // Server address: 0.0.0.0

    if (connect(client_fd, (struct sockaddr*)&server_addr,
                sizeof(server_addr)) != 0) {
        std::cerr << "Failed to connect Client to Server" << std::endl;
        return 1;
    }

    // multiple requests
    int32_t err = query(client_fd, "hello1");
    if (err) {
        goto L_DONE;
    }
    err = query(client_fd, "hello2");
    if (err) {
        goto L_DONE;
    }
    err = query(client_fd, "hello3");
    if (err) {
        goto L_DONE;
    }

L_DONE:
    close(client_fd);
    return 0;
}