#define _DEFAULT_SOURCE
#include "FsynC.h"
#include "check.h"

#include <stdlib.h>
#include <string.h>

static size_t round_trip(const uint8_t *base, size_t bn, const uint8_t *data, size_t n, int *ok)
{
    uint8_t *sig, *delta, *out;
    size_t sl, dl, on;

    *ok = 0;
    if (my_sig_build(base, bn, &sig, &sl) < 0)
        return 0;
    if (my_delta_build(sig, sl, data, n, &delta, &dl) < 0) {
        free(sig);
        return 0;
    }
    if (my_delta_apply(base, bn, delta, dl, &out, &on) == MY_OK) {
        *ok = on == n && (n == 0 || memcmp(out, data, n) == 0);
        free(out);
    }
    free(sig);
    free(delta);
    return dl;
}

static uint8_t *random_bytes(size_t n, unsigned seed)
{
    uint8_t *p = malloc(n ? n : 1);
    size_t i;

    srand(seed);
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)rand();
    return p;
}

int main(void)
{
    size_t n = 1000000, d;
    uint8_t *a = random_bytes(n, 1), *b = malloc(n + 100), *out;
    uint8_t bad[16] = {0, 0, 8, 0, 'C', 0, 0, 0, 9, 0, 0, 0, 1};
    int ok;

    /* identical */
    d = round_trip(a, n, a, n, &ok);
    CHECK(ok && d == 13);
    /* one byte edit */
    memcpy(b, a, n);
    b[n / 2] ^= 0x55;
    d = round_trip(a, n, b, n, &ok);
    CHECK(ok && d < MY_BLOCK_SIZE + 40);
    /* insert at start */
    memcpy(b, "HELLO", 5);
    memcpy(b + 5, a, n);
    d = round_trip(a, n, b, n + 5, &ok);
    CHECK(ok && d < 40);
    /* delete at start */
    d = round_trip(a, n, a + 10, n - 10, &ok);
    CHECK(ok && d < MY_BLOCK_SIZE + 40);
    /* append */
    memcpy(b, a, n);
    memcpy(b + n, "tail-data", 9);
    d = round_trip(a, n, b, n + 9, &ok);
    CHECK(ok && d < 700);
    /* truncate */
    d = round_trip(a, n, a, n / 3, &ok);
    CHECK(ok && d < MY_BLOCK_SIZE + 40);
    /* empty cases */
    d = round_trip(a, 0, a, 100, &ok);
    CHECK(ok && d == 4 + 5 + 100);
    d = round_trip(a, 100, a, 0, &ok);
    CHECK(ok && d == 4);
    d = round_trip(a, 0, a, 0, &ok);
    CHECK(ok);
    /* small files */
    d = round_trip((const uint8_t *)"abc", 3, (const uint8_t *)"abd", 3, &ok);
    CHECK(ok);
    d = round_trip((const uint8_t *)"abc", 3, (const uint8_t *)"abc", 3, &ok);
    CHECK(ok && d == 13);
    /* unrelated data */
    free(b);
    b = random_bytes(n, 2);
    d = round_trip(a, n, b, n, &ok);
    CHECK(ok && d > n);
    /* invalid delta */
    CHECK(my_delta_apply(a, 10, bad, 13, &out, &d) == MY_ERR_PROTO);
    bad[8] = 0;
    bad[12] = 0;
    CHECK(my_delta_apply(a, 10, bad, 13, &out, &d) == MY_ERR_PROTO);
    bad[4] = 'L';
    bad[8] = 9;
    CHECK(my_delta_apply(a, 10, bad, 9, &out, &d) == MY_ERR_PROTO);
    bad[4] = 'X';
    CHECK(my_delta_apply(a, 10, bad, 9, &out, &d) == MY_ERR_PROTO);
    CHECK(my_delta_apply(a, 10, bad, 2, &out, &d) == MY_ERR_ARG);
    CHECK(my_delta_build(bad, 3, a, 10, &out, &d) == MY_ERR_PROTO);
    free(a);
    free(b);
    /* large file 60 MB */
    n = 60u * 1024 * 1024;
    a = random_bytes(n, 3);
    b = malloc(n);
    memcpy(b, a, n);
    memcpy(b + 12345678, "small edit", 10);
    d = round_trip(a, n, b, n, &ok);
    CHECK(ok && d < MY_BLOCK_SIZE + 60);
    free(a);
    free(b);
    return DONE("test_delta");
}
