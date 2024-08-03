#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

const size_t k_max_msg = 4096;  // bytes

static int32_t read_full(int fd, char* buf, size_t n) {
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

static int32_t write_all(int fd, const char* buf, size_t n) {
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

static int32_t send_req(int fd, const char* text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);        // Insert text length in request header
    memcpy(&wbuf[4], text, len);  // Insert text in request body
    if (int32_t err = write_all(fd, wbuf, 4 + len)) {
        return err;
    }
    return 0;
}

static int32_t read_res(int fd) {
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

    uint32_t len;
    memcpy(&len, rbuf, 4);
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

    // multiple pipelined requests
    const char* query_list[3] = {"hello1", "hello2", "hello3"};
    for (size_t i = 0; i < 3; ++i) {
        int32_t err = send_req(client_fd, query_list[i]);
        if (err) {
            goto L_DONE;
        }
    }
    for (size_t i = 0; i < 3; ++i) {
        int32_t err = read_res(client_fd);
        if (err) {
            goto L_DONE;
        }
    }

L_DONE:
    close(client_fd);
    return 0;
}