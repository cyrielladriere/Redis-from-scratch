#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <map>
#include <vector>

const size_t k_max_msg = 4096;   // Bytes
const size_t k_max_args = 1024;  // Args in request

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,  // Mark the connection for deletion
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;  // Either STATE_REQ or STATE_RES
    // Buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // Buffer for writing
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

static bool cmd_is(const std::string &word, const char *cmd) {
    return 0 == strcasecmp(word.c_str(), cmd);
}

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

// The data structure for the key space. This is just a placeholder
static std::map<std::string, std::string> g_map;

// cmd[0] == "get"
// cmd[1] = key
static uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
    if (!g_map.count(cmd[1])) {
        // Key not found in database
        return RES_NX;
    }
    std::string &val = g_map[cmd[1]];
    assert(val.size() <= k_max_msg);
    // Copy found value that corresponds to key into res
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

// cmd[0] == "set"
// cmd[1] = key
// cmd[2] = value
static uint32_t do_set(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
    (void)res;
    (void)reslen;
    g_map[cmd[1]] = cmd[2];
    return RES_OK;
}

// cmd[0] == "del"
// cmd[1] = key
static uint32_t do_del(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
    (void)res;
    (void)reslen;
    g_map.erase(cmd[1]);
    return RES_OK;
}

static int32_t parse_req(const uint8_t *data, size_t len,
                         std::vector<std::string> &out) {
    if (len < 4) {
        return -1;
    }
    uint32_t n = 0;

    // Copies n(=4) bytes (32 bits) from memory area src(=&data[0]) to
    // memory area dest(=&n). In this case, it copies the request size in
    // bytes
    memcpy(&n, &data[0], 4);
    if (n > k_max_args) {
        return -1;
    }

    size_t pos = 4;
    // Go over request byte-by-byte
    // request: (str_len -> str -> str_len -> str->..)
    while (n--) {
        if (pos + 4 > len) {
            return -1;
        }
        uint32_t sz = 0;
        // Copies the size of the next string in bytes
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len) {
            return -1;
        }
        // Push the string into vector
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }

    if (pos != len) {
        return -1;  // trailing garbage
    }
    return 0;
}

// Handles the request. Only 3 commands (get, set, del) are supported right now.
// *req: pointer to start of request body (str_len -> str -> str_len -> str->..)
// reqlen: request size (in bytes)
// *rescode: response code
// *res: pointer to start of response (rescode -> data -> rescode -> data ->..)
// reslen: response size (in bytes)
static int32_t do_request(const uint8_t *req, uint32_t reqlen,
                          uint32_t *rescode, uint8_t *res, uint32_t *reslen) {
    std::vector<std::string> cmd;
    if (0 != parse_req(req, reqlen, cmd)) {
        std::cerr << "Bad request" << std::endl;
        return -1;
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
        *rescode = do_get(cmd, res, reslen);
    } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
        *rescode = do_set(cmd, res, reslen);
    } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
        *rescode = do_del(cmd, res, reslen);
    } else {
        // cmd is not recognized
        *rescode = RES_ERR;
        const char *msg = "Unknown command";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
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

    // got one request, generate the response.
    uint32_t rescode = 0;  // Response code
    uint32_t wlen = 0;     // Length of the response (in bytes)
    int32_t err =
        do_request(&conn->rbuf[4], len, &rescode, &conn->wbuf[4 + 4], &wlen);
    if (err) {
        conn->state = STATE_END;
        return false;
    }
    wlen += 4;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + wlen;

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