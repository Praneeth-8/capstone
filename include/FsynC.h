#ifndef FsynC_H
#define FsynC_H

#include <stddef.h>
#include <stdint.h>
#include <signal.h>

/* limits */
#define HEADER_SIZE 11
#define MAX_NAME_LEN 4096
#define MAX_PATH_LEN 1024
#define MAX_FILE_SIZE 104857600ULL
#define MAX_PAYLOAD (MAX_FILE_SIZE + 1048576ULL)
#define MY_BLOCK_SIZE 2048
#define MY_STRONG_LEN 8
#define MY_SHA_LEN 32
#define MY_TMP_SUFFIX ".FsynC-tmp"
#define MY_META_DIR ".FsynC"

/* UDP discovery. A request is exactly these bytes. A reply is
   magic | name_len u8 | name | folder_len u8 | folder | tcp_port u16 BE. */
#define FSYNC_DISCOVER_MAGIC "FSYNC_DISCOVER v1"
#define FSYNC_DISCOVER_MAGIC_LEN 17
#define FSYNC_DISCOVER_PORT 9999
#define FSYNC_DISCOVER_NAME_MAX 63
#define FSYNC_DISCOVER_FOLDER_MAX 255
#define FSYNC_DISCOVER_MAX_REPLY \
    (FSYNC_DISCOVER_MAGIC_LEN + 1 + FSYNC_DISCOVER_NAME_MAX + 1 + FSYNC_DISCOVER_FOLDER_MAX + 2)

/* custom error codes */
enum my_err {
    MY_OK = 0,
    MY_ERR_ARG = -1,
    MY_ERR_NOMEM = -2,
    MY_ERR_IO = -3,
    MY_ERR_NET = -4,
    MY_ERR_PROTO = -5,
    MY_ERR_PATH = -6,
    MY_ERR_TOOBIG = -7,
    MY_ERR_HASH = -8,
    MY_ERR_CONFLICT = -9,
    MY_ERR_NOTFOUND = -10
};
const char *FsynC_strerror(int err);

/* protocol */
enum Op {
    OP_SEND_FILE = 1,
    OP_DELETE,
    OP_ACK,
    OP_ERROR,
    OP_HELLO,
    OP_LIST,
    OP_MANIFEST,
    OP_MKDIR,
    OP_RMDIR,
    OP_SIGREQ,
    OP_SIGS,
    OP_DELTA,
    OP_GET,
    OP_FILE,
    OP_NOTIFY
};
enum result {
    RES_STORED = 0,
    RES_UNCHANGED,
    RES_DELETED,
    RES_NOT_FOUND,
    RES_ERROR,
    RES_CONFLICT,
    RES_CREATED
};
struct header {
    uint8_t op;
    uint16_t name_len;
    uint64_t file_size;
};
struct my_msg {
    uint8_t op;
    char *name;
    uint8_t *data;
    uint64_t len;
};
void encode_header(const struct header *h, uint8_t H[]);
int decode_header(const uint8_t *buf, struct header *h);
void put_u32(uint8_t *p, uint32_t v);
void put_u64(uint8_t *p, uint64_t v);
uint32_t get_u32(const uint8_t *p);
uint64_t get_u64(const uint8_t *p);

/* network */
int send_all(int fd, const void *buf, size_t n);
int recv_all(int fd, void *buf, size_t n);
int my_send_msg(int fd, uint8_t op, const char *name, const void *data, uint64_t len);
int my_recv_msg(int fd, struct my_msg *m);
void my_msg_free(struct my_msg *m);
int my_send_ack(int fd, uint8_t code, uint64_t version);

/* hashing */
struct my_sha256 {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t used;
};
void my_sha256_init(struct my_sha256 *c);
void my_sha256_update(struct my_sha256 *c, const void *data, size_t n);
void my_sha256_final(struct my_sha256 *c, uint8_t out[MY_SHA_LEN]);
void my_sha256(const void *data, size_t n, uint8_t out[MY_SHA_LEN]);
void my_sha_hex(const uint8_t sha[MY_SHA_LEN], char out[65]);
int my_sha_unhex(const char *hex, uint8_t sha[MY_SHA_LEN]);
uint32_t my_weak_sum(const uint8_t *p, size_t n);
uint32_t my_weak_roll(uint32_t sum, uint8_t out, uint8_t in, size_t n);

/* delta */
int my_sig_build(const uint8_t *base, size_t n, uint8_t **sig, size_t *sig_len);
int my_delta_build(const uint8_t *sig, size_t sig_len, const uint8_t *data, size_t n,
                   uint8_t **delta, size_t *delta_len);
int my_delta_apply(const uint8_t *base, size_t base_len, const uint8_t *delta, size_t delta_len,
                   uint8_t **out, size_t *out_len);

/* filesystem */
int my_path_valid(const char *path);
char *my_join(const char *root, const char *rel);
int my_mkdirs(const char *path, int include_last);
int my_read_file(const char *path, uint8_t **buf, size_t *len);
int my_write_atomic(const char *path, const void *buf, size_t len);
int my_is_dir(const char *path);

/* manifest */
enum my_type { MY_FILE = 'F', MY_DIR = 'D' };
struct my_entry {
    char *path;
    int type;
    uint64_t size;
    uint64_t mtime;
    uint8_t sha[MY_SHA_LEN];
    uint64_t version;
};
struct my_manifest {
    struct my_entry *e;
    size_t n;
    size_t cap;
};
enum my_change_kind { MY_ADDED = 1, MY_REMOVED, MY_MODIFIED };
struct my_change {
    const struct my_entry *entry;
    int kind;
};
void my_manifest_init(struct my_manifest *m);
void my_manifest_free(struct my_manifest *m);
int my_manifest_put(struct my_manifest *m, const struct my_entry *e);
struct my_entry *my_manifest_find(const struct my_manifest *m, const char *path);
int my_manifest_remove(struct my_manifest *m, const char *path);
int my_entry_stat(const char *root, const char *rel, struct my_entry *out);
int my_manifest_scan(const char *root, const struct my_manifest *old, struct my_manifest *out);
int my_manifest_encode(const struct my_manifest *m, uint8_t **buf, size_t *len);
int my_manifest_decode(const uint8_t *buf, size_t len, struct my_manifest *m);
int my_manifest_load(const char *file, struct my_manifest *m);
int my_manifest_save(const char *file, const struct my_manifest *m);
int my_manifest_diff(const struct my_manifest *old, const struct my_manifest *cur,
                     struct my_change **out, size_t *count);

/* logging */
struct my_log;
struct my_log *my_log_open(const char *file);
void my_log_close(struct my_log *log);
void my_log_event(struct my_log *log, const char *ip, const char *client, const char *action,
                  const char *file, const char *result, uint64_t size, uint64_t sent);

/* server */
struct FsynC_server;
struct FsynC_server *FsynC_server_create(int port, const char *dir, const char *logfile, int *err);
int FsynC_server_port(const struct FsynC_server *s);
/* udp_port 0 keeps 9999 (or the last value). name NULL keeps the hostname.
   Call before run. */
int FsynC_server_set_discovery(struct FsynC_server *s, int udp_port, const char *name);
int FsynC_server_run(struct FsynC_server *s, volatile sig_atomic_t *stop);
void FsynC_server_destroy(struct FsynC_server *s);

/* discovery */
struct FsynC_found {
    char name[FSYNC_DISCOVER_NAME_MAX + 1];
    char folder[FSYNC_DISCOVER_FOLDER_MAX + 1];
    char ip[16];
    int tcp_port;
};
int FsynC_discover_label_ok(const char *s, size_t max);
int FsynC_discover_encode_request(uint8_t *buf, size_t cap);
int FsynC_discover_check_request(const uint8_t *buf, size_t len);
int FsynC_discover_encode_reply(uint8_t *buf, size_t cap, const char *name,
                                const char *folder, unsigned tcp_port);
int FsynC_discover_parse_reply(const uint8_t *buf, size_t len, char *name, size_t name_sz,
                               char *folder, size_t folder_sz, unsigned *tcp_port);
/* udp_port 0 and timeout_ms 0 select the defaults (9999 and 1000).
   Writes at most cap results and sets *count to how many were stored. */
int FsynC_discover(int udp_port, int timeout_ms, struct FsynC_found *out, size_t cap,
                   size_t *count);

/* client */
struct FsynC_stats {
    uint64_t pushed;
    uint64_t pulled;
    uint64_t deleted;
    uint64_t conflicts;
    uint64_t bytes_sent;
    uint64_t bytes_full;
};
struct FsynC_client;
struct FsynC_client *FsynC_client_create(const char *host, int port, const char *dir, int *err);
int FsynC_client_sync(struct FsynC_client *c);
int FsynC_client_watch(struct FsynC_client *c, volatile sig_atomic_t *stop);
const struct FsynC_stats *FsynC_client_stats(const struct FsynC_client *c);
const char *FsynC_client_id(const struct FsynC_client *c);
void FsynC_client_destroy(struct FsynC_client *c);

#endif
