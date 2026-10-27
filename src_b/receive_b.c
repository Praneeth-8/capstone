#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "net.h"

#define MAX_NAME 256
#define BUF_SIZE 4096
#define MAX_FILE_SIZE 104857600
#define TYPE_FILE 1
#define TYPE_META 2
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

int safe_name(char *dst, const char *src) {
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    if (base[0] == '\0') return -1;
    if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0) return -1;
    if (strlen(base) >= MAX_NAME) return -1;
    strcpy(dst, base);
    return 0;
}

int recv_content(int fd, const char *name, int64_t mtime) {
    uint32_t size;
    char buffer[BUF_SIZE];
    if (recv_u32(fd, &size) < 0) return -1;
    if (size > MAX_FILE_SIZE) return -1;
    FILE *fptr = fopen(name, "wb");
    if (!fptr) return -1;
    uint32_t remaining = size;
    while (remaining > 0) {
        size_t chunk = remaining < BUF_SIZE ? remaining : BUF_SIZE;
        if (recv_all(fd, buffer, chunk) < 0) {
            fclose(fptr);
            remove(name);
            return -1;
        }
        if (fwrite(buffer, 1, chunk, fptr) != chunk) {
            fclose(fptr);
            remove(name);
            return -1;
        }
        remaining -= (uint32_t)chunk;
    }
    if (fclose(fptr) != 0) {
        remove(name);
        return -1;
    }
    if (mtime > 0) {
        struct timeval times[2];
        times[0].tv_sec = (time_t)mtime;
        times[0].tv_usec = 0;
        times[1].tv_sec = (time_t)mtime;
        times[1].tv_usec = 0;
        utimes(name, times);
    }
    printf("stored %s (%u bytes)\n", name, size);
    fflush(stdout);
    return 0;
}

int handle_file(int fd) {
    uint32_t name_len;
    char raw_name[MAX_NAME] = {0};
    char name[MAX_NAME] = {0};
    if (recv_u32(fd, &name_len) < 0) return -1;
    if (name_len == 0 || name_len >= MAX_NAME) return -1;
    if (recv_all(fd, raw_name, name_len) < 0) return -1;
    if (safe_name(name, raw_name) < 0) return -1;
    return recv_content(fd, name, 0);
}

int handle_meta(int fd) {
    FileMeta meta;
    char name[MAX_NAME] = {0};
    char meta_path[MAX_NAME + 8];
    if (recv_metadata(fd, &meta) < 0) return -1;
    meta.name[MAX_NAME - 1] = '\0';
    if (safe_name(name, meta.name) < 0) return -1;
    snprintf(meta_path, sizeof(meta_path), "%s.meta", name);
    FILE *fptr = fopen(meta_path, "wb");
    if (!fptr) return -1;
    if (fwrite(&meta, sizeof(FileMeta), 1, fptr) != 1) {
        fclose(fptr);
        return -1;
    }
    if (fclose(fptr) != 0) return -1;
    printf("stored metadata %s (checksum %u, mtime %lld)\n", meta_path, meta.checksum, (long long)meta.mtime);
    fflush(stdout);
    return 0;
}

int handle_sync(int fd) {
    FileMeta remote, local;
    uint32_t send_flag;
    char name[MAX_NAME] = {0};
    if (recv_metadata(fd, &remote) < 0) return -1;
    remote.name[MAX_NAME - 1] = '\0';
    if (safe_name(name, remote.name) < 0) return -1;
    if (get_file_metadata(name, &local) < 0) {
        memset(&local, 0, sizeof(FileMeta));
        strncpy(local.name, name, MAX_NAME - 1);
    }
    if (send_metadata(fd, &local) < 0) return -1;
    if (recv_u32(fd, &send_flag) < 0) return -1;
    if (send_flag == 0) {
        printf("sync %s: unchanged\n", name);
        fflush(stdout);
        return 0;
    }
    return recv_content(fd, name, remote.mtime);
}

int handle_client(int fd) {
    uint32_t type;
    if (recv_u32(fd, &type) < 0) return -1;
    if (type == TYPE_FILE) return handle_file(fd);
    if (type == TYPE_META) return handle_meta(fd);
    if (type == TYPE_SYNC) return handle_sync(fd);
    return -1;
}

int main(int argc, char const* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int PORT = atoi(argv[1]);
    if (PORT <= 0 || PORT > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
    int opt = 1;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    printf("listening on port %d\n", PORT);
    fflush(stdout);

    while (1) {
        int new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }
        uint32_t status = 0;
        if (handle_client(new_socket) < 0) {
            status = 1;
            fprintf(stderr, "request failed\n");
        }
        send_u32(new_socket, status);
        close(new_socket);
    }

    close(server_fd);
    return 0;
}
