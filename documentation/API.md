# API

Everything is declared in `include/FsynC.h`. Functions return `MY_OK` (0) or a negative
`enum my_err`. `FsynC_strerror(code)` returns a static string.

| code | meaning |
|---|---|
| MY_ERR_ARG | invalid argument |
| MY_ERR_NOMEM | out of memory |
| MY_ERR_IO | file system error |
| MY_ERR_NET | network error |
| MY_ERR_PROTO | malformed message or data |
| MY_ERR_PATH | unsafe path |
| MY_ERR_TOOBIG | over 100 MB |
| MY_ERR_HASH | SHA-256 mismatch |
| MY_ERR_CONFLICT | conflict |
| MY_ERR_NOTFOUND | not found |

## Server

```c
struct FsynC_server *FsynC_server_create(int port, const char *dir, const char *logfile, int *err);
int  FsynC_server_port(const struct FsynC_server *s);
int  FsynC_server_set_discovery(struct FsynC_server *s, int udp_port, const char *name);
int  FsynC_server_run(struct FsynC_server *s, volatile sig_atomic_t *stop);
void FsynC_server_destroy(struct FsynC_server *s);
```
`port` 0 picks a free port. `logfile` may be NULL (stdout only). `run` returns when `*stop` is set.
`run` also binds the discovery socket. `set_discovery` is optional and must be called before `run`.
`udp_port` 0 keeps 9999. `name` NULL keeps the hostname. A bad name or a discovery port that is
already taken makes `run` fail with `MY_ERR_NET` (bind) or `set_discovery` return `MY_ERR_ARG`.

## Discovery

```c
int FsynC_discover_label_ok(const char *s, size_t max);
int FsynC_discover_encode_request(uint8_t *buf, size_t cap);
int FsynC_discover_check_request(const uint8_t *buf, size_t len);
int FsynC_discover_encode_reply(uint8_t *buf, size_t cap, const char *name,
                                const char *folder, unsigned tcp_port);
int FsynC_discover_parse_reply(const uint8_t *buf, size_t len, char *name, size_t name_sz,
                               char *folder, size_t folder_sz, unsigned *tcp_port);
int FsynC_discover(int udp_port, int timeout_ms, struct FsynC_found *out, size_t cap,
                   size_t *count);
```

`encode_request` writes the 17-byte magic and returns that length. `check_request` returns 1 only
for an exact match. `encode_reply` returns the reply length. `parse_reply` fills the caller
buffers. `udp_port` 0 and `timeout_ms` 0 in `FsynC_discover` mean 9999 and 1000 ms. `*count` is
how many unique servers were stored, at most `cap`. `struct FsynC_found` holds `name`, `folder`,
`ip` and `tcp_port`.

The `FsynC_client` program calls this:

```
FsynC_client discover [udp_port] [timeout_ms]
FsynC_client discover-connect <name> <dir> once|watch
```

## Client

```c
struct FsynC_client *FsynC_client_create(const char *host, int port, const char *dir, int *err);
int  FsynC_client_sync(struct FsynC_client *c);
int  FsynC_client_watch(struct FsynC_client *c, volatile sig_atomic_t *stop);
const struct FsynC_stats *FsynC_client_stats(const struct FsynC_client *c);
const char *FsynC_client_id(const struct FsynC_client *c);
void FsynC_client_destroy(struct FsynC_client *c);
```
`sync` connects if needed and does one full reconcile. `watch` loops until `*stop` is set.

## Building blocks

- Protocol: `encode_header`, `decode_header`, `put_u32/u64`, `get_u32/u64`.
- Network: `send_all`, `recv_all`, `my_send_msg`, `my_recv_msg`, `my_msg_free`, `my_send_ack`.
- Hash: `my_sha256_init/update/final`, `my_sha256`, `my_sha_hex`, `my_sha_unhex`, `my_weak_sum`, `my_weak_roll`.
- Delta: `my_sig_build`, `my_delta_build`, `my_delta_apply`.
- Files: `my_path_valid` (1 if safe), `my_join`, `my_mkdirs`, `my_read_file`, `my_write_atomic`, `my_is_dir`.
- Manifest: `my_manifest_init/free/put/find/remove`, `my_entry_stat`, `my_manifest_scan`,
  `my_manifest_encode/decode`, `my_manifest_load/save`, `my_manifest_diff`.
- Log: `my_log_open`, `my_log_event`, `my_log_close`.

## Example

```c
volatile sig_atomic_t stop = 0;
int err;
struct FsynC_client *c = FsynC_client_create("127.0.0.1", 9000, "./mine", &err);
if (!c) { fprintf(stderr, "%s\n", FsynC_strerror(err)); return 1; }
err = FsynC_client_sync(c);
FsynC_client_destroy(c);
```
