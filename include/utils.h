#ifndef UTILS_H
#define UTILS_H
#include <iostream>

static int32_t read_full(int fd, char *buf, size_t n);

static int32_t write_all(int fd, const char *buf, size_t n);

#endif