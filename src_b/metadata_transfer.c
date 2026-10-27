#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "net.h"

#define MAX_NAME 256
#define TYPE_META 2

typedef struct {
    char name[MAX_NAME];
    uint32_t checksum;
    int64_t mtime;
    uint32_t mode;
} FileMeta;

int compute_checksum(const char *path, uint32_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t sum = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        sum = sum * 31 + (unsigned char)c;
    }
    fclose(f);
    *out = sum;
    return 0;
}

int send_u32(int fd, uint32_t value) {
    uint32_t net_value = htonl(value);
    return send_all(fd, &net_value, sizeof(net_value));
}

int recv_u32(int fd, uint32_t *value) {
    uint32_t net_value;
    if (recv_all(fd, &net_value, sizeof(net_value)) < 0) return -1;
    *value = ntohl(net_value);
    return 0;
}

int send_metadata(int fd, const FileMeta *meta) {
    return send_all(fd, meta, sizeof(FileMeta));
}

int recv_metadata(int fd, FileMeta *meta) {
    return recv_all(fd, meta, sizeof(FileMeta));
}

int get_file_metadata(const char *path, FileMeta *meta) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;
    memset(meta, 0, sizeof(FileMeta));
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(meta->name, base, MAX_NAME - 1);
    if (compute_checksum(path, &meta->checksum) < 0) return -1;
    meta->mtime = (int64_t)st.st_mtime;
    meta->mode = (uint32_t)st.st_mode;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <file>\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *file = argv[3];
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
    FileMeta meta;
    if (get_file_metadata(file, &meta) < 0) {
        perror("stat");
        return 1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid address\n");
        close(fd);
        return 1;
    }
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }
    if (send_u32(fd, TYPE_META) < 0 || send_metadata(fd, &meta) < 0) {
        perror("send_metadata");
        close(fd);
        return 1;
    }
    uint32_t status;
    if (recv_u32(fd, &status) < 0) {
        fprintf(stderr, "no reply from server\n");
        close(fd);
        return 1;
    }
    close(fd);
    if (status != 0) {
        fprintf(stderr, "server rejected metadata\n");
        return 1;
    }
    return 0;
}
