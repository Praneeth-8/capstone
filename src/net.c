#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int send_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t sent = 0;

    while (sent < n) {
        ssize_t r = send(fd, p + sent, n - sent, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return -1;
        sent += (size_t)r;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;

    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return -1;
        got += (size_t)r;
    }
    return 0;
}

int my_send_msg(int fd, uint8_t op, const char *name, const void *data, uint64_t len)
{
    struct header h;
    uint8_t buf[HEADER_SIZE];
    size_t nl = name ? strlen(name) : 0;

    if (nl > MAX_NAME_LEN || len > MAX_PAYLOAD)
        return MY_ERR_ARG;
    h.op = op;
    h.name_len = (uint16_t)nl;
    h.file_size = len;
    encode_header(&h, buf);
    if (send_all(fd, buf, HEADER_SIZE) < 0)
        return MY_ERR_NET;
    if (nl && send_all(fd, name, nl) < 0)
        return MY_ERR_NET;
    if (len && send_all(fd, data, (size_t)len) < 0)
        return MY_ERR_NET;
    return MY_OK;
}

int my_recv_msg(int fd, struct my_msg *m)
{
    uint8_t buf[HEADER_SIZE];
    struct header h;

    memset(m, 0, sizeof *m);
    if (recv_all(fd, buf, HEADER_SIZE) < 0)
        return MY_ERR_NET;
    if (decode_header(buf, &h) < 0)
        return MY_ERR_PROTO;
    m->op = h.op;
    m->len = h.file_size;
    m->name = calloc(1, (size_t)h.name_len + 1);
    m->data = malloc(h.file_size ? (size_t)h.file_size : 1);
    if (!m->name || !m->data) {
        my_msg_free(m);
        return MY_ERR_NOMEM;
    }
    /* read body */
    if (recv_all(fd, m->name, h.name_len) < 0 ||
        recv_all(fd, m->data, (size_t)h.file_size) < 0) {
        my_msg_free(m);
        return MY_ERR_NET;
    }
    if (strlen(m->name) != h.name_len) {
        my_msg_free(m);
        return MY_ERR_PROTO;
    }
    return MY_OK;
}

void my_msg_free(struct my_msg *m)
{
    free(m->name);
    free(m->data);
    m->name = NULL;
    m->data = NULL;
    m->len = 0;
}

int my_send_ack(int fd, uint8_t code, uint64_t version)
{
    uint8_t p[9];

    p[0] = code;
    put_u64(p + 1, version);
    return my_send_msg(fd, code == RES_ERROR ? OP_ERROR : OP_ACK, NULL, p, sizeof p);
}
