#define _DEFAULT_SOURCE
#include "FsynC.h"
#include "check.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_header(void)
{
    struct header h = {OP_SEND_FILE, 32, 1000000}, d;
    uint8_t buf[HEADER_SIZE];

    /* round trip */
    encode_header(&h, buf);
    CHECK(buf[0] == 1 && buf[1] == 0 && buf[2] == 32);
    CHECK(decode_header(buf, &d) == 0);
    CHECK(d.op == h.op && d.name_len == 32 && d.file_size == 1000000);
    /* invalid */
    h.op = 0;
    encode_header(&h, buf);
    CHECK(decode_header(buf, &d) < 0);
    h.op = 99;
    encode_header(&h, buf);
    CHECK(decode_header(buf, &d) < 0);
    h.op = OP_SEND_FILE;
    h.name_len = 0;
    encode_header(&h, buf);
    CHECK(decode_header(buf, &d) < 0);
    h.name_len = MAX_NAME_LEN + 1;
    encode_header(&h, buf);
    CHECK(decode_header(buf, &d) < 0);
    h.name_len = 4;
    h.file_size = MAX_PAYLOAD + 1;
    encode_header(&h, buf);
    CHECK(decode_header(buf, &d) < 0);
    h.op = OP_LIST;
    h.name_len = 0;
    h.file_size = 0;
    encode_header(&h, buf);
    CHECK(decode_header(buf, &d) == 0);
}

static void test_ints(void)
{
    uint8_t b[8];

    put_u64(b, 0x0102030405060708ULL);
    CHECK(b[0] == 1 && b[7] == 8);
    CHECK(get_u64(b) == 0x0102030405060708ULL);
    put_u32(b, 0xdeadbeef);
    CHECK(get_u32(b) == 0xdeadbeef);
}

static void test_messages(void)
{
    int sv[2];
    struct my_msg m;
    uint8_t junk[HEADER_SIZE] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    /* normal message */
    CHECK(my_send_msg(sv[0], OP_GET, "dir/a.txt", "abc", 3) == MY_OK);
    CHECK(my_recv_msg(sv[1], &m) == MY_OK);
    CHECK(m.op == OP_GET && strcmp(m.name, "dir/a.txt") == 0 && m.len == 3);
    CHECK(memcmp(m.data, "abc", 3) == 0);
    my_msg_free(&m);
    /* empty payload */
    CHECK(my_send_msg(sv[0], OP_LIST, NULL, NULL, 0) == MY_OK);
    CHECK(my_recv_msg(sv[1], &m) == MY_OK && m.len == 0);
    my_msg_free(&m);
    /* garbage header */
    CHECK(send_all(sv[0], junk, sizeof junk) == 0);
    CHECK(my_recv_msg(sv[1], &m) == MY_ERR_PROTO);
    my_msg_free(&m);
    /* truncated body */
    CHECK(my_send_msg(sv[0], OP_HELLO, "x", NULL, 0) == MY_OK);
    CHECK(my_recv_msg(sv[1], &m) == MY_OK);
    my_msg_free(&m);
    {
        struct header h = {OP_SEND_FILE, 3, 100};
        uint8_t buf[HEADER_SIZE];
        encode_header(&h, buf);
        send_all(sv[0], buf, HEADER_SIZE);
        send_all(sv[0], "abc", 3);
        close(sv[0]);
        CHECK(my_recv_msg(sv[1], &m) == MY_ERR_NET);
        my_msg_free(&m);
    }
    close(sv[1]);
    CHECK(strcmp(FsynC_strerror(MY_ERR_PATH), "unsafe path") == 0);
    CHECK(strcmp(FsynC_strerror(12345), "unknown error") == 0);
}

int main(void)
{
    test_header();
    test_ints();
    test_messages();
    return DONE("test_protocol");
}
