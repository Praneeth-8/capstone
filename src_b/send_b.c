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
#define TYPE_FILE 1

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

int Send_file(FILE* file, const char ip[], const char file_name[], int PORT) {
    struct stat st;
    char buffer[BUF_SIZE];
    if (fstat(fileno(file), &st) < 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;

    const char *base = strrchr(file_name, '/');
    base = base ? base + 1 : file_name;
    size_t name_len = strlen(base);
    if (name_len == 0 || name_len >= MAX_NAME) return -1;

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) return -1;

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) != 1) {
        close(client_fd);
        return -1;
    }
    if (connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(client_fd);
        return -1;
    }

    uint32_t size = (uint32_t)st.st_size;
    if (send_u32(client_fd, TYPE_FILE) < 0 ||
        send_u32(client_fd, (uint32_t)name_len) < 0 ||
        send_all(client_fd, base, name_len) < 0 ||
        send_u32(client_fd, size) < 0) {
        close(client_fd);
        return -1;
    }

    uint32_t remaining = size;
    while (remaining > 0) {
        size_t want = remaining < BUF_SIZE ? remaining : BUF_SIZE;
        size_t got = fread(buffer, 1, want, file);
        if (got == 0) {
            close(client_fd);
            return -1;
        }
        if (send_all(client_fd, buffer, got) < 0) {
            close(client_fd);
            return -1;
        }
        remaining -= (uint32_t)got;
    }

    uint32_t status;
    if (recv_u32(client_fd, &status) < 0) {
        close(client_fd);
        return -1;
    }
    close(client_fd);
    return status == 0 ? 0 : -1;
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
    FILE* fptr = fopen(argv[3], "rb");
    if (!fptr) {
        perror("fopen");
        return 1;
    }
    int result = Send_file(fptr, argv[1], argv[3], port);
    fclose(fptr);
    if (result < 0) {
        fprintf(stderr, "transfer failed\n");
        return 1;
    }
    return 0;
}
