#include "FsynC.h"

#include <stdio.h>
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* sha256 block */
static void sha_block(struct my_sha256 *c, const uint8_t *p)
{
    uint32_t w[64], s[8], t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = get_u32(p + i * 4);
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(s, c->h, sizeof s);
    for (i = 0; i < 64; i++) {
        t1 = s[7] + (ROR(s[4], 6) ^ ROR(s[4], 11) ^ ROR(s[4], 25)) +
             ((s[4] & s[5]) ^ (~s[4] & s[6])) + K[i] + w[i];
        t2 = (ROR(s[0], 2) ^ ROR(s[0], 13) ^ ROR(s[0], 22)) +
             ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        memmove(s + 1, s, 7 * sizeof s[0]);
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++)
        c->h[i] += s[i];
}

void my_sha256_init(struct my_sha256 *c)
{
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(c->h, iv, sizeof iv);
    c->len = 0;
    c->used = 0;
}

void my_sha256_update(struct my_sha256 *c, const void *data, size_t n)
{
    const uint8_t *p = data;

    c->len += n;
    while (n > 0) {
        size_t take = 64 - c->used;
        if (take > n)
            take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += take;
        p += take;
        n -= take;
        if (c->used == 64) {
            sha_block(c, c->buf);
            c->used = 0;
        }
    }
}

void my_sha256_final(struct my_sha256 *c, uint8_t out[MY_SHA_LEN])
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, zero = 0, lenbuf[8];
    int i;

    my_sha256_update(c, &pad, 1);
    while (c->used != 56)
        my_sha256_update(c, &zero, 1);
    put_u64(lenbuf, bits);
    my_sha256_update(c, lenbuf, 8);
    for (i = 0; i < 8; i++)
        put_u32(out + i * 4, c->h[i]);
}

void my_sha256(const void *data, size_t n, uint8_t out[MY_SHA_LEN])
{
    struct my_sha256 c;

    my_sha256_init(&c);
    my_sha256_update(&c, data, n);
    my_sha256_final(&c, out);
}

void my_sha_hex(const uint8_t sha[MY_SHA_LEN], char out[65])
{
    int i;

    for (i = 0; i < MY_SHA_LEN; i++)
        snprintf(out + i * 2, 3, "%02x", sha[i]);
}

int my_sha_unhex(const char *hex, uint8_t sha[MY_SHA_LEN])
{
    int i;

    for (i = 0; i < MY_SHA_LEN; i++) {
        unsigned v;
        if (sscanf(hex + i * 2, "%2x", &v) != 1)
            return MY_ERR_PROTO;
        sha[i] = (uint8_t)v;
    }
    return MY_OK;
}

/* rolling checksum */
uint32_t my_weak_sum(const uint8_t *p, size_t n)
{
    uint32_t a = 0, b = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        a += p[i];
        b += (uint32_t)(n - i) * p[i];
    }
    return (a & 0xffff) | (b << 16);
}

uint32_t my_weak_roll(uint32_t sum, uint8_t out, uint8_t in, size_t n)
{
    uint32_t a = sum & 0xffff, b = sum >> 16;

    a = (a - out + in) & 0xffff;
    b = (b - (uint32_t)n * out + a) & 0xffff;
    return a | (b << 16);
}
