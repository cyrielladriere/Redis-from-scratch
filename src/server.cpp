#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <vector>

const size_t k_max_msg = 4096;  // bytes

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,  // mark the connection for deletion
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;  // either STATE_REQ or STATE_RES
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void fd_set_nb(int fd) {
    errno = 0;
    // Returns all file descriptor flags
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        std::cerr << "Fcntl error when trying to retrieve file descriptor flags"
                  << std::endl;
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    // Set file descriptor with non-blocking mode flag on
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        std::cerr << "Fcntl error when trying to set fd non-blocking mode"
                  << std::endl;
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connection_fd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connection_fd < 0) {
        std::cerr << "Accept error when accepting new connection" << std::endl;
        return -1;
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connection_fd);
    // creating the struct Conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn) {
        close(connection_fd);
        return -1;
    }
    conn->fd = connection_fd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

static void state_res(Conn *conn);

// Takes one request from the read buffer, generates a response, then transits
// to the STATE_RES state.
static bool try_one_request(Conn *conn) {
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len = 0;
    // copies n(=4) bytes (32 bits) from memory area src(=&conn->rbuf[0]) to
    // memory area dest(=&len). In this case, it copies the request size in
    // bytes
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        std::cerr << "Request size too large" << std::endl;
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // Not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one client request
    printf("Client says: %.*s\n", len, &conn->rbuf[4]);

    // generating server echoing response
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}

// Fills the read buffer with data until it gets EAGAIN
static bool try_fill_buffer(Conn *conn) {
    // rbuf_size initialized at 0 for a Conn
    // Here we can interpret rbuf_size as total connection bytes read
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t n_bytes_read = 0;

    // The EINTR means the syscall was interrupted by a signal, retrying is
    // needed
    do {
        // Only read bytes that have not been read yet
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        n_bytes_read = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (n_bytes_read < 0 && errno == EINTR);

    if (n_bytes_read < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (n_bytes_read < 0) {
        std::cerr << "Read error when reading request" << std::endl;
        conn->state = STATE_END;  // End connection
        return false;
    }
    if (n_bytes_read == 0) {
        if (conn->rbuf_size > 0) {
            std::cerr << "Unexpected EOF error when reading request"
                      << std::endl;
        } else {
            printf("EOF request\n");
        }
        conn->state = STATE_END;  // End connection
        return false;
    }

    // Add the bytes that have been read to rbuf_size
    conn->rbuf_size += (size_t)n_bytes_read;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // Try to process requests one by one, there can be more than one request in
    // the read buffer. For a request/response protocol, clients are not limited
    // to sending one request and waiting for the response at a time, clients
    // can save some latency by sending multiple requests without waiting for
    // responses in between, this mode of operation is called “pipelining”
    while (try_one_request(conn)) {
    }
    return (conn->state == STATE_REQ);
}

// Flushes the write buffer until it gets EAGAIN, or transits back to the
// STATE_REQ if the flushing is done
static bool try_flush_buffer(Conn *conn) {
    ssize_t n_bytes_read = 0;

    // The EINTR means the syscall was interrupted by a signal, retrying is
    // needed
    do {
        // conn->wbuf_sent: number of total bytes already sent in this
        // connection
        // remain: only send bytes that have not been sent yet
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        n_bytes_read = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (n_bytes_read < 0 && errno == EINTR);

    if (n_bytes_read < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }

    if (n_bytes_read < 0) {
        std::cerr << "Write error when sending request response" << std::endl;
        conn->state = STATE_END;
        return false;
    }

    // Add the bytes that have been written to wbuf_size
    conn->wbuf_sent += (size_t)n_bytes_read;
    assert(conn->wbuf_sent <= conn->wbuf_size);

    if (conn->wbuf_sent == conn->wbuf_size) {
        // response was fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}

static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {
    }
}

static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {
    }
}

static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);  // not expected
    }
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

    // A map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // Set the listen server_fd to nonblocking mode
    fd_set_nb(server_fd);

    // The event loop
    std::vector<struct pollfd> poll_args;
    while (true) {
        // Prepare the arguments of the poll()
        poll_args.clear();

        // For convenience, the listening server_fd is put in the first position
        // POLLIN flag just specifies that the poll fd is for reading data
        struct pollfd poll_fd = {server_fd, POLLIN, 0};
        poll_args.push_back(poll_fd);

        // Connection fds, first iteration does not enter this loop
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            // POLLIN flag: fd will read data (STATE_REQ: reading request)
            // POLLOUT flag: fd will write data (STATE_RES: sending response)
            // Never reading AND writing
            // pollfd.events: requested events
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // Poll for active fds
        // Waits for one of a set of file descriptors (poll_args.data() = active
        // fds) to become ready to perform I/O.
        // The timeout argument doesn't matter here
        int n_bytes_read = poll(poll_args.data(), (nfds_t)poll_args.size(),
                                1000);  // (active_fds, n_fds, timeout)
        if (n_bytes_read < 0) {
            std::cerr << "Error during poll for active file descriptors"
                      << std::endl;
        }

        // process active connections
        for (size_t i = 1; i < poll_args.size(); ++i) {
            // pollfd.revents: returned events
            if (poll_args[i].revents) {  // Checks if connection is active
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END) {
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        // try to accept a new connection if the listening fd is active
        if (poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, server_fd);
        }
    }

    return 0;
}