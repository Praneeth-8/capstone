#include "FsynC.h"

#include <stdlib.h>
#include <string.h>

#define SIG_HEAD 16
#define SIG_ITEM (4 + MY_STRONG_LEN)
#define HASH_BITS 16

struct buf {
    uint8_t *p;
    size_t n;
    size_t cap;
};

/* growable buffer */
static int buf_add(struct buf *b, const void *data, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        uint8_t *p;
        while (cap < b->n + n)
            cap *= 2;
        p = realloc(b->p, cap);
        if (!p)
            return MY_ERR_NOMEM;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
    return MY_OK;
}

static void strong_sum(const uint8_t *p, size_t n, uint8_t out[MY_STRONG_LEN])
{
    uint8_t sha[MY_SHA_LEN];

    my_sha256(p, n, sha);
    memcpy(out, sha, MY_STRONG_LEN);
}

/* build signatures */
int my_sig_build(const uint8_t *base, size_t n, uint8_t **sig, size_t *sig_len)
{
    size_t count = (n + MY_BLOCK_SIZE - 1) / MY_BLOCK_SIZE, i;
    uint8_t *p;

    if (!sig || !sig_len || (n && !base))
        return MY_ERR_ARG;
    p = malloc(SIG_HEAD + count * SIG_ITEM);
    if (!p)
        return MY_ERR_NOMEM;
    put_u32(p, MY_BLOCK_SIZE);
    put_u64(p + 4, n);
    put_u32(p + 12, (uint32_t)count);
    for (i = 0; i < count; i++) {
        size_t off = i * MY_BLOCK_SIZE;
        size_t len = n - off < MY_BLOCK_SIZE ? n - off : MY_BLOCK_SIZE;
        uint8_t *item = p + SIG_HEAD + i * SIG_ITEM;
        put_u32(item, my_weak_sum(base + off, len));
        strong_sum(base + off, len, item + 4);
    }
    *sig = p;
    *sig_len = SIG_HEAD + count * SIG_ITEM;
    return MY_OK;
}

struct sigs {
    size_t bs;
    size_t size;
    size_t count;
    const uint8_t *items;
    int32_t *head;
    int32_t *next;
};

static int sigs_parse(const uint8_t *sig, size_t len, struct sigs *s)
{
    size_t i;

    if (len < SIG_HEAD)
        return MY_ERR_PROTO;
    s->bs = get_u32(sig);
    s->size = (size_t)get_u64(sig + 4);
    s->count = get_u32(sig + 12);
    if (s->bs == 0 || s->bs > 65536 || s->size > MAX_FILE_SIZE)
        return MY_ERR_PROTO;
    if (s->count != (s->size + s->bs - 1) / s->bs || len != SIG_HEAD + s->count * SIG_ITEM)
        return MY_ERR_PROTO;
    s->items = sig + SIG_HEAD;
    s->head = malloc(sizeof(int32_t) << HASH_BITS);
    s->next = malloc(sizeof(int32_t) * (s->count + 1));
    if (!s->head || !s->next) {
        free(s->head);
        free(s->next);
        return MY_ERR_NOMEM;
    }
    memset(s->head, 0xff, sizeof(int32_t) << HASH_BITS);
    for (i = s->count; i-- > 0;) {
        uint32_t h = get_u32(s->items + i * SIG_ITEM) & ((1u << HASH_BITS) - 1);
        s->next[i] = s->head[h];
        s->head[h] = (int32_t)i;
    }
    return MY_OK;
}

static size_t block_len(const struct sigs *s, size_t i)
{
    size_t off = i * s->bs;
    return s->size - off < s->bs ? s->size - off : s->bs;
}

/* find matching block */
static long find_block(const struct sigs *s, uint32_t weak, const uint8_t *p, size_t len)
{
    int32_t i = s->head[weak & ((1u << HASH_BITS) - 1)];
    uint8_t strong[MY_STRONG_LEN];
    int have = 0;

    for (; i >= 0; i = s->next[i]) {
        const uint8_t *item = s->items + (size_t)i * SIG_ITEM;
        if (get_u32(item) != weak || block_len(s, (size_t)i) != len)
            continue;
        if (!have) {
            strong_sum(p, len, strong);
            have = 1;
        }
        if (memcmp(strong, item + 4, MY_STRONG_LEN) == 0)
            return i;
    }
    return -1;
}

static int emit_literal(struct buf *out, const uint8_t *p, size_t n)
{
    uint8_t h[5];

    if (n == 0)
        return MY_OK;
    h[0] = 'L';
    put_u32(h + 1, (uint32_t)n);
    if (buf_add(out, h, 5) < 0)
        return MY_ERR_NOMEM;
    return buf_add(out, p, n);
}

static int emit_copy(struct buf *out, size_t idx, size_t *last)
{
    uint8_t h[9];

    if (*last != (size_t)-1 && *last + 9 == out->n) {
        uint8_t *c = out->p + *last;
        if (get_u32(c + 1) + get_u32(c + 5) == idx) {
            put_u32(c + 5, get_u32(c + 5) + 1);
            return MY_OK;
        }
    }
    h[0] = 'C';
    put_u32(h + 1, (uint32_t)idx);
    put_u32(h + 5, 1);
    *last = out->n;
    return buf_add(out, h, 9);
}

/* scan new data */
static int delta_scan(const struct sigs *s, const uint8_t *data, size_t n, struct buf *out)
{
    size_t pos = 0, lit = 0, tail = s->count ? block_len(s, s->count - 1) : 0;
    size_t last = (size_t)-1;
    uint32_t weak = 0;
    int rolling = 0;

    while (pos < n) {
        long idx = -1;
        size_t len = 0;
        if (s->count && n - pos >= s->bs) {
            len = s->bs;
            weak = rolling ? weak : my_weak_sum(data + pos, len);
            rolling = 1;
            idx = find_block(s, weak, data + pos, len);
        } else if (tail && tail < s->bs && n - pos == tail) {
            len = tail;
            idx = find_block(s, my_weak_sum(data + pos, len), data + pos, len);
        }
        if (idx >= 0) {
            if (emit_literal(out, data + lit, pos - lit) < 0 || emit_copy(out, (size_t)idx, &last) < 0)
                return MY_ERR_NOMEM;
            pos += len;
            lit = pos;
            rolling = 0;
            continue;
        }
        if (rolling && pos + s->bs < n)
            weak = my_weak_roll(weak, data[pos], data[pos + s->bs], s->bs);
        else
            rolling = 0;
        pos++;
    }
    return emit_literal(out, data + lit, n - lit);
}

int my_delta_build(const uint8_t *sig, size_t sig_len, const uint8_t *data, size_t n,
                   uint8_t **delta, size_t *delta_len)
{
    struct sigs s;
    struct buf out = {0};
    uint8_t h[4];
    int rc;

    if (!sig || !delta || !delta_len || (n && !data) || n > MAX_FILE_SIZE)
        return MY_ERR_ARG;
    rc = sigs_parse(sig, sig_len, &s);
    if (rc < 0)
        return rc;
    put_u32(h, (uint32_t)s.bs);
    rc = buf_add(&out, h, 4);
    if (rc == MY_OK)
        rc = delta_scan(&s, data, n, &out);
    free(s.head);
    free(s.next);
    if (rc < 0) {
        free(out.p);
        return rc;
    }
    *delta = out.p;
    *delta_len = out.n;
    return MY_OK;
}

/* apply delta */
int my_delta_apply(const uint8_t *base, size_t base_len, const uint8_t *delta, size_t delta_len,
                   uint8_t **out, size_t *out_len)
{
    struct buf b = {0};
    size_t pos = 4, bs;

    if (!delta || !out || !out_len || (base_len && !base) || delta_len < 4)
        return MY_ERR_ARG;
    bs = get_u32(delta);
    if (bs == 0 || bs > 65536)
        return MY_ERR_PROTO;
    while (pos < delta_len) {
        uint8_t kind = delta[pos];
        size_t v, cnt, off, len, blocks = (base_len + bs - 1) / bs;
        const uint8_t *src;
        if (delta_len - pos < 5)
            goto bad;
        v = get_u32(delta + pos + 1);
        pos += 5;
        if (kind == 'C') {
            if (delta_len - pos < 4)
                goto bad;
            cnt = get_u32(delta + pos);
            pos += 4;
            if (cnt == 0 || v >= blocks || cnt > blocks - v)
                goto bad;
            off = v * bs;
            len = (v + cnt) * bs > base_len ? base_len - off : cnt * bs;
            src = base + off;
        } else if (kind == 'L') {
            if (v > delta_len - pos)
                goto bad;
            len = v;
            src = delta + pos;
            pos += v;
        } else {
            goto bad;
        }
        if (b.n + len > MAX_FILE_SIZE) {
            free(b.p);
            return MY_ERR_TOOBIG;
        }
        if (len && buf_add(&b, src, len) < 0) {
            free(b.p);
            return MY_ERR_NOMEM;
        }
    }
    if (!b.p && !(b.p = malloc(1)))
        return MY_ERR_NOMEM;
    *out = b.p;
    *out_len = b.n;
    return MY_OK;
bad:
    free(b.p);
    return MY_ERR_PROTO;
}
