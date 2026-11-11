#define _DEFAULT_SOURCE
#include "FsynC.h"
#include "check.h"

#include <stdlib.h>
#include <string.h>

static int sha_is(const char *msg, size_t n, const char *hex)
{
    uint8_t sha[MY_SHA_LEN];
    char out[65];

    my_sha256(msg, n, sha);
    my_sha_hex(sha, out);
    return strcmp(out, hex) == 0;
}

int main(void)
{
    uint8_t sha[MY_SHA_LEN], back[MY_SHA_LEN], data[300];
    char hex[65];
    struct my_sha256 c;
    char *million;
    uint32_t w;
    size_t i;

    /* known vectors */
    CHECK(sha_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(sha_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(sha_is("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    million = malloc(1000000);
    memset(million, 'a', 1000000);
    CHECK(sha_is(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    /* incremental equals one shot */
    my_sha256_init(&c);
    for (i = 0; i < 1000000; i += 777)
        my_sha256_update(&c, million + i, 1000000 - i < 777 ? 1000000 - i : 777);
    my_sha256_final(&c, sha);
    my_sha_hex(sha, hex);
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    free(million);
    /* hex round trip */
    CHECK(my_sha_unhex(hex, back) == MY_OK && memcmp(sha, back, MY_SHA_LEN) == 0);
    CHECK(my_sha_unhex("zz", back) < 0);
    /* rolling equals direct */
    for (i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)(i * 7 + 3);
    w = my_weak_sum(data, 64);
    for (i = 0; i + 64 < sizeof data; i++) {
        w = my_weak_roll(w, data[i], data[i + 64], 64);
        if (w != my_weak_sum(data + i + 1, 64))
            break;
    }
    CHECK(i + 64 == sizeof data);
    CHECK(my_weak_sum(data, 0) == 0);
    return DONE("test_hash");
}
