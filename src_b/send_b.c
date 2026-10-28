#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include "net.h"

#define MAX_NAME 256
#define BUF_SIZE 4096
#define TYPE_SYNC 3

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

int compare_metadata(const FileMeta *a, const FileMeta *b) {
    if (a->checksum != b->checksum) return 1;
    if (a->mtime != b->mtime) return 1;
    return 0;
}

int send_content(int fd, const char *path) {
    struct stat st;
    char buffer[BUF_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fstat(fileno(f), &st) < 0) {
        fclose(f);
        return -1;
    }
    uint32_t size = (uint32_t)st.st_size;
    if (send_u32(fd, size) < 0) {
        fclose(f);
        return -1;
    }
    uint32_t remaining = size;
    while (remaining > 0) {
        size_t want = remaining < BUF_SIZE ? remaining : BUF_SIZE;
        size_t got = fread(buffer, 1, want, f);
        if (got == 0 || send_all(fd, buffer, got) < 0) {
            fclose(f);
            return -1;
        }
        remaining -= (uint32_t)got;
    }
    fclose(f);
    return 0;
}

int Send_file(const char ip[], const char file_name[], int PORT) {
    FileMeta local, remote;
    if (get_file_metadata(file_name, &local) < 0) {
        perror(file_name);
        return -1;
    }

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) return -1;

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid address\n");
        close(client_fd);
        return -1;
    }
    if (connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(client_fd);
        return -1;
    }

    if (send_u32(client_fd, TYPE_SYNC) < 0 || send_metadata(client_fd, &local) < 0 ||
        recv_metadata(client_fd, &remote) < 0) {
        close(client_fd);
        return -1;
    }

    int has_old = remote.mtime != 0;
    int changed = !has_old || compare_metadata(&local, &remote);
    if (send_u32(client_fd, changed) < 0 || (changed && send_content(client_fd, file_name) < 0)) {
        close(client_fd);
        return -1;
    }

    uint32_t status;
    if (recv_u32(client_fd, &status) < 0 || status != 0) {
        close(client_fd);
        return -1;
    }
    close(client_fd);
    printf("%s\n", !changed ? "UNCHANGED" : has_old ? "SENT (updated)" : "SENT (new file)");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <file>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
    if (Send_file(argv[1], argv[3], port) < 0) {
        fprintf(stderr, "transfer failed\n");
        return 1;
    }
    return 0;
}
