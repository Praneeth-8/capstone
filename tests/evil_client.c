#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int dial(int port)
{
    struct sockaddr_in a;
    struct timeval tv = {15, 0};
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("connect");
        exit(2);
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

static int reply_code(int fd)
{
    struct my_msg m;
    int code = -1;

    if (my_recv_msg(fd, &m) < 0)
        return -1;
    if ((m.op == OP_ACK || m.op == OP_ERROR) && m.len == 9)
        code = m.data[0];
    my_msg_free(&m);
    return code;
}

static int hello(int fd)
{
    my_send_msg(fd, OP_HELLO, "evil", NULL, 0);
    return reply_code(fd);
}

/* upload helper */
static int put(int fd, const char *name, const uint8_t *data, size_t len)
{
    uint8_t *p = malloc(40 + len);
    int rc;

    put_u64(p, 0);
    my_sha256(data, len, p + 8);
    memcpy(p + 40, data, len);
    rc = my_send_msg(fd, OP_SEND_FILE, name, p, 40 + len);
    free(p);
    return rc < 0 ? -1 : reply_code(fd);
}

int main(int argc, char *argv[])
{
    const char *mode;
    int fd, port;

    if (argc < 3)
        return 2;
    port = atoi(argv[1]);
    mode = argv[2];
    fd = dial(port);
    if (!strcmp(mode, "traversal")) {
        const char *names[] = {"../escape.txt", "/tmp/abs.txt", "a/../../b", "a\\b", ".FsynC/state", "a//b"};
        size_t i;
        int bad = 0;
        hello(fd);
        for (i = 0; i < 6; i++)
            if (put(fd, names[i], (const uint8_t *)"x", 1) != RES_ERROR)
                bad++;
        printf("%s\n", bad ? "ACCEPTED" : "REJECTED");
    } else if (!strcmp(mode, "garbage")) {
        send_all(fd, "\xff\x00\x10garbage-garbage-garbage", 26);
    } else if (!strcmp(mode, "truncated")) {
        send_all(fd, "\x01\x00", 2);
    } else if (!strcmp(mode, "giant")) {
        uint8_t h[HEADER_SIZE] = {OP_SEND_FILE, 0, 4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        send_all(fd, h, sizeof h);
        printf("%s\n", reply_code(fd) == -1 ? "DROPPED" : "ANSWERED");
    } else if (!strcmp(mode, "midtransfer")) {
        uint8_t h[HEADER_SIZE];
        struct header hd = {OP_SEND_FILE, 7, 1000000};
        hello(fd);
        encode_header(&hd, h);
        send_all(fd, h, sizeof h);
        send_all(fd, "mid.bin", 7);
        send_all(fd, "only a few bytes", 16);
    } else if (!strcmp(mode, "nohello")) {
        my_send_msg(fd, OP_LIST, NULL, NULL, 0);
        printf("%s\n", reply_code(fd) == -1 ? "DROPPED" : "ANSWERED");
    } else if (!strcmp(mode, "toobig")) {
        size_t n = MAX_FILE_SIZE + 1;
        uint8_t *d = calloc(1, n);
        hello(fd);
        printf("%s\n", put(fd, "toobig.bin", d, n) == RES_ERROR ? "REJECTED" : "ACCEPTED");
        free(d);
    } else if (!strcmp(mode, "badhash")) {
        uint8_t p[45] = {0};
        hello(fd);
        memcpy(p + 40, "hello", 5);
        my_send_msg(fd, OP_SEND_FILE, "badhash.txt", p, 45);
        printf("%s\n", reply_code(fd) == RES_ERROR ? "REJECTED" : "ACCEPTED");
    } else if (!strcmp(mode, "baddelta")) {
        uint8_t p[60] = {0};
        hello(fd);
        put(fd, "dbase.txt", (const uint8_t *)"base", 4);
        put_u32(p + 40, 2048);
        p[44] = 'C';
        put_u32(p + 45, 999);
        put_u32(p + 49, 1);
        my_send_msg(fd, OP_DELTA, "dbase.txt", p, 53);
        printf("%s\n", reply_code(fd) == RES_ERROR ? "REJECTED" : "ACCEPTED");
    } else if (!strcmp(mode, "ping")) {
        printf("%s\n", hello(fd) == RES_STORED ? "ALIVE" : "DEAD");
    }
    close(fd);
    return 0;
}
