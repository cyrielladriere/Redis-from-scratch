#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

const size_t k_max_msg = 4096;  // bytes

static int32_t read_full(int fd, char *buf, size_t n) {
    // Reads from the kernel until it gets exactly n bytes
    while (n > 0) {
        // The read() syscall just returns whatever data is available in the
        // kernel until it gets n bytes, or blocks if there is none. When we
        // read a block smaller than n bytes, read again (while loop)
        ssize_t n_bytes_read = read(fd, buf, n);
        // -1 for error, 0 for unexpected EOF
        if (n_bytes_read <= 0) {
            return -1;
        }
        assert((size_t)n_bytes_read <= n);
        n -= (size_t)n_bytes_read;
        buf += n_bytes_read;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t n_bytes_written = write(fd, buf, n);
        // -1 for error
        if (n_bytes_written <= 0) {
            return -1;
        }
        assert((size_t)n_bytes_written <= n);

        // Write may return successfully with partial data written if the kernel
        // buffer is full, we must keep trying when the write() returns fewer
        // bytes than we need.
        n -= (size_t)n_bytes_written;

        buf += n_bytes_written;
    }
    return 0;
}

static int32_t one_request(int connection_fd) {
    // 4 bytes header (request size) + msg + nullptr
    char rbuf[4 + k_max_msg + 1];
    errno = 0;

    // Request header
    int32_t err = read_full(connection_fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
        std:
            std::cerr << "EOF error while reading request header" << std::endl;
        } else {
            std::cerr << "Read error while reading request header" << std::endl;
        }
        return err;
    }

    uint32_t len = 0;

    // copies n(=4) bytes (32 bits) from memory area src(=rbuf) to memory area
    // dest(=&len). In this case, it copies the request size
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        std::cerr << "Request size too large" << std::endl;
        return -1;
    }

    // Request body
    // store in rbuf starting from index 4 (before [4] is the header)
    err = read_full(connection_fd, &rbuf[4], len);
    if (err) {
        std::cerr << "Error while reading request body";
        return err;
    }

    rbuf[4 + len] = '\0';                   // Insert null at end of message
    printf("Client says: %s\n", &rbuf[4]);  // Read message

    // reply using the same protocol
    const char *reply = "Hello, client!";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connection_fd, wbuf, 4 + len);
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
        std::cerr << "Setsockopt failed" << std::endl;
        return 1;
    }

    // bind local server address 0.0.0.0:1234 to socket.
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    // The ntohs() and ntohl() functions convert numbers to the required big
    // endian format.
    server_addr.sin_port = ntohs(1234);      // Port: 1234
    server_addr.sin_addr.s_addr = ntohl(0);  // Server address: 0.0.0.0

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
        0) {
        std::cerr << "Failed to bind Server to port 1234" << std::endl;
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "Listen for connection failed" << std::endl;
        return 1;
    }

    // The server enters a loop that accepts and processes each connection,
    // multiple requests from the same connection is supported.
    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);

        int connection_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);

        if (connection_fd < 0) {
            // Skip this connection, go to next in queue
            continue;
        }

        // Only handle one client connection at a time, all requests from this
        // connection will be handled before moving on.
        while (true) {
            int32_t err = one_request(connection_fd);
            if (err) {
                break;
            }
        }
        close(connection_fd);
    }

    return 0;
}