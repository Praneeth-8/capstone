#include "FsynC.h"

#include <arpa/inet.h>
#include <string.h>

void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void put_u64(uint8_t *p, uint64_t v)
{
    put_u32(p, (uint32_t)(v >> 32));
    put_u32(p + 4, (uint32_t)v);
}

uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint64_t get_u64(const uint8_t *p)
{
    return ((uint64_t)get_u32(p) << 32) | get_u32(p + 4);
}

void encode_header(const struct header *h, uint8_t H[])
{
    uint16_t nl = htons(h->name_len);

    H[0] = h->op;
    memcpy(H + 1, &nl, sizeof nl);
    put_u64(H + 3, h->file_size);
}

static int needs_name(uint8_t op)
{
    return op == OP_SEND_FILE || op == OP_DELETE || op == OP_HELLO || op == OP_MKDIR ||
           op == OP_RMDIR || op == OP_SIGREQ || op == OP_DELTA || op == OP_GET;
}

int decode_header(const uint8_t *buf, struct header *h)
{
    uint16_t nl;

    memcpy(&nl, buf + 1, sizeof nl);
    h->op = buf[0];
    h->name_len = ntohs(nl);
    h->file_size = get_u64(buf + 3);

    /* validate */
    if (h->op < OP_SEND_FILE || h->op > OP_NOTIFY)
        return -1;
    if (h->name_len > MAX_NAME_LEN)
        return -1;
    if (needs_name(h->op) && h->name_len == 0)
        return -1;
    if (h->file_size > MAX_PAYLOAD)
        return -1;
    return 0;
}

const char *FsynC_strerror(int err)
{
    switch (err) {
    case MY_OK: return "ok";
    case MY_ERR_ARG: return "invalid argument";
    case MY_ERR_NOMEM: return "out of memory";
    case MY_ERR_IO: return "file system error";
    case MY_ERR_NET: return "network error";
    case MY_ERR_PROTO: return "protocol error";
    case MY_ERR_PATH: return "unsafe path";
    case MY_ERR_TOOBIG: return "file too large";
    case MY_ERR_HASH: return "checksum mismatch";
    case MY_ERR_CONFLICT: return "conflict";
    case MY_ERR_NOTFOUND: return "not found";
    default: return "unknown error";
    }
}
