#define _DEFAULT_SOURCE
#include "FsynC.h"
#include "check.h"

#include <string.h>

static void fill(char *dst, size_t n, char c)
{
    memset(dst, c, n);
    dst[n] = 0;
}

static void test_request(void)
{
    uint8_t buf[64];
    int n;

    CHECK(strlen(FSYNC_DISCOVER_MAGIC) == FSYNC_DISCOVER_MAGIC_LEN);
    CHECK(FsynC_discover_encode_request(NULL, 32) == MY_ERR_ARG);
    CHECK(FsynC_discover_encode_request(buf, FSYNC_DISCOVER_MAGIC_LEN - 1) == MY_ERR_ARG);
    n = FsynC_discover_encode_request(buf, sizeof buf);
    CHECK(n == FSYNC_DISCOVER_MAGIC_LEN);
    CHECK(memcmp(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN) == 0);
    CHECK(FsynC_discover_check_request(buf, (size_t)n) == 1);
    /* malformed */
    CHECK(FsynC_discover_check_request(NULL, FSYNC_DISCOVER_MAGIC_LEN) == 0);
    CHECK(FsynC_discover_check_request(buf, 0) == 0);
    CHECK(FsynC_discover_check_request(buf, FSYNC_DISCOVER_MAGIC_LEN - 1) == 0);
    CHECK(FsynC_discover_check_request(buf, FSYNC_DISCOVER_MAGIC_LEN + 1) == 0);
    buf[0] = 'f';
    CHECK(FsynC_discover_check_request(buf, FSYNC_DISCOVER_MAGIC_LEN) == 0);
    memcpy(buf, "FSYNC_DISCOVER v2", FSYNC_DISCOVER_MAGIC_LEN);
    CHECK(FsynC_discover_check_request(buf, FSYNC_DISCOVER_MAGIC_LEN) == 0);
    memcpy(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN);
    buf[FSYNC_DISCOVER_MAGIC_LEN] = 'X';
    CHECK(FsynC_discover_check_request(buf, FSYNC_DISCOVER_MAGIC_LEN + 1) == 0);
}

static void test_reply_roundtrip(void)
{
    uint8_t buf[FSYNC_DISCOVER_MAX_REPLY + 8];
    char name[FSYNC_DISCOVER_NAME_MAX + 1];
    char folder[FSYNC_DISCOVER_FOLDER_MAX + 1];
    char long_name[80], long_folder[300];
    unsigned port = 0;
    int n;

    n = FsynC_discover_encode_reply(buf, sizeof buf, "laptop", "FsynC_shared", 9000);
    CHECK(n > 0);
    CHECK(buf[n - 2] == (uint8_t)(9000 >> 8) && buf[n - 1] == (uint8_t)(9000 & 0xff));
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_OK);
    CHECK(strcmp(name, "laptop") == 0);
    CHECK(strcmp(folder, "FsynC_shared") == 0);
    CHECK(port == 9000);

    fill(long_name, FSYNC_DISCOVER_NAME_MAX, 'a');
    fill(long_folder, FSYNC_DISCOVER_FOLDER_MAX, 'b');
    n = FsynC_discover_encode_reply(buf, sizeof buf, long_name, long_folder, 65535);
    CHECK(n == FSYNC_DISCOVER_MAX_REPLY);
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_OK);
    CHECK(strcmp(name, long_name) == 0 && strcmp(folder, long_folder) == 0 && port == 65535);
    CHECK(FsynC_discover_encode_reply(buf, 8, "laptop", "dir", 9000) == MY_ERR_ARG);
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, 4, folder, sizeof folder, &port) ==
          MY_ERR_ARG);
}

static void test_reply_rejects(void)
{
    uint8_t buf[FSYNC_DISCOVER_MAX_REPLY + 8];
    char name[FSYNC_DISCOVER_NAME_MAX + 1];
    char folder[FSYNC_DISCOVER_FOLDER_MAX + 1];
    char big[FSYNC_DISCOVER_FOLDER_MAX + 2];
    unsigned port = 1;
    int n;

    CHECK(FsynC_discover_encode_reply(NULL, 32, "a", "b", 1) == MY_ERR_ARG);
    CHECK(FsynC_discover_encode_reply(buf, sizeof buf, "", "b", 1) == MY_ERR_ARG);
    CHECK(FsynC_discover_encode_reply(buf, sizeof buf, "a", "", 1) == MY_ERR_ARG);
    CHECK(FsynC_discover_encode_reply(buf, sizeof buf, "a", "b", 0) == MY_ERR_ARG);
    CHECK(FsynC_discover_encode_reply(buf, sizeof buf, "bad\nname", "b", 1) == MY_ERR_ARG);
    CHECK(FsynC_discover_label_ok("ok.name-1", 63) == 1);
    CHECK(FsynC_discover_label_ok("has space", 63) == 1);
    CHECK(FsynC_discover_label_ok("", 63) == 0);
    CHECK(FsynC_discover_label_ok(NULL, 63) == 0);
    fill(big, 64, 'z');
    CHECK(FsynC_discover_encode_reply(buf, sizeof buf, big, "dir", 9) == MY_ERR_ARG);
    fill(big, FSYNC_DISCOVER_FOLDER_MAX + 1, 'z');
    CHECK(FsynC_discover_encode_reply(buf, sizeof buf, "n", big, 9) == MY_ERR_ARG);

    n = FsynC_discover_encode_reply(buf, sizeof buf, "laptop", "docs", 9000);
    CHECK(n > 0);
    CHECK(FsynC_discover_parse_reply(NULL, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_ARG);
    CHECK(FsynC_discover_parse_reply(buf, 0, name, sizeof name, folder, sizeof folder, &port) ==
          MY_ERR_PROTO);
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n - 1, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
    buf[n] = 0x01;
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n + 1, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
    /* wrong magic, same shape */
    n = FsynC_discover_encode_reply(buf, sizeof buf, "laptop", "docs", 9000);
    buf[0] = 'X';
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
    /* name_len claims more bytes than the datagram holds */
    memset(buf, 'A', sizeof buf);
    memcpy(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN);
    buf[FSYNC_DISCOVER_MAGIC_LEN] = 200;
    CHECK(FsynC_discover_parse_reply(buf, 30, name, sizeof name, folder, sizeof folder, &port) ==
          MY_ERR_PROTO);
    /* name_len 0 */
    n = FsynC_discover_encode_reply(buf, sizeof buf, "laptop", "docs", 9000);
    buf[FSYNC_DISCOVER_MAGIC_LEN] = 0;
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
    /* control byte inside the name */
    n = FsynC_discover_encode_reply(buf, sizeof buf, "laptop", "docs", 9000);
    buf[FSYNC_DISCOVER_MAGIC_LEN + 1] = 0x01;
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
    /* tcp port 0 */
    n = FsynC_discover_encode_reply(buf, sizeof buf, "laptop", "docs", 9000);
    buf[n - 2] = 0;
    buf[n - 1] = 0;
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
    /* folder_len past the end */
    n = FsynC_discover_encode_reply(buf, sizeof buf, "ab", "cd", 9000);
    buf[FSYNC_DISCOVER_MAGIC_LEN + 1 + 2] = 50;
    CHECK(FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                     &port) == MY_ERR_PROTO);
}

int main(void)
{
    test_request();
    test_reply_roundtrip();
    test_reply_rejects();
    return DONE("test_discovery");
}
