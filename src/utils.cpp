#include "utils.h"

#include <unistd.h>

#include <cassert>
#include <iostream>

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