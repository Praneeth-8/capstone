#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include "net.h"
#include "protocol.h"

#define BUF_SIZE 4096

static int send_content(int fd, FILE *f, uint64_t size)
{
    char buffer[BUF_SIZE];
    uint64_t remaining = size;
    while (remaining > 0) {
        size_t want = remaining < BUF_SIZE ? (size_t)remaining : BUF_SIZE;
        size_t got = fread(buffer, 1, want, f);
        if (got == 0) return -1;               /* file shrank or read error */
        if (send_all(fd, buffer, got) < 0) return -1;
        remaining -= got;
    }
    return 0;
}

static int send_file(const char *ip, int port, const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) { perror(path); return -1; }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s is not a regular file\n", path);
        return -1;
    }

    /* send only the base name, never the directory part */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t name_len = strlen(base);
    if (name_len == 0 || name_len > MAX_NAME_LEN) {
        fprintf(stderr, "bad file name length\n");
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return -1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); fclose(f); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid address\n");
        close(fd); fclose(f); return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        close(fd); fclose(f); return -1;
    }

    /* header, then name, then file data */
    struct header h = { .op = OP_SEND_FILE,
                        .name_len = (uint16_t)name_len,
                        .file_size = (uint64_t)st.st_size };
    uint8_t hdr[HEADER_SIZE];
    encode_header(&h, hdr);

    if (send_all(fd, hdr, HEADER_SIZE) < 0 ||
        send_all(fd, base, name_len) < 0 ||
        send_content(fd, f, h.file_size) < 0) {
        fprintf(stderr, "send failed\n");
        close(fd); fclose(f); return -1;
    }
    fclose(f);

    /* reply: a header; ACK's file_size is the result code */
    uint8_t rbuf[HEADER_SIZE];
    struct header reply;
    if (recv_all(fd, rbuf, HEADER_SIZE) < 0 || decode_header(rbuf, &reply) < 0) {
        fprintf(stderr, "no valid reply from server\n");
        close(fd); return -1;
    }
    close(fd);

    if (reply.op == OP_ACK) {
        if (reply.file_size == 0)
            printf("STORED\n");
        else
            printf("UNCHANGED (server already has this version)\n");
        return 0;
    }
    fprintf(stderr, "server reported an error\n");
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <file>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
    return send_file(argv[1], port, argv[3]) < 0 ? 1 : 0;
}
