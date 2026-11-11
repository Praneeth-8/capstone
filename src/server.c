#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 64
#define IO_TIMEOUT 10

struct conn {
    int fd;
    char ip[INET_ADDRSTRLEN];
    char id[40];
};

struct FsynC_server {
    int lfd;
    int ufd;
    int port;
    int udp_port;
    char name[FSYNC_DISCOVER_NAME_MAX + 1];
    char folder[FSYNC_DISCOVER_FOLDER_MAX + 1];
    char *dir;
    char *index;
    struct my_log *log;
    struct my_manifest m;
    uint64_t seq;
    struct conn c[MAX_CLIENTS];
    int nc;
};

/* rescan shared dir */
static int refresh(struct FsynC_server *s)
{
    struct my_manifest cur;
    size_t i;
    int rc = my_manifest_scan(s->dir, &s->m, &cur);

    if (rc < 0)
        return rc;
    for (i = 0; i < cur.n; i++) {
        struct my_entry *e = &cur.e[i], *o;
        if (e->version)
            continue;
        o = my_manifest_find(&s->m, e->path);
        if (o && o->type == e->type && memcmp(o->sha, e->sha, MY_SHA_LEN) == 0)
            e->version = o->version;
        else
            e->version = ++s->seq;
    }
    my_manifest_free(&s->m);
    s->m = cur;
    my_manifest_save(s->index, &s->m);
    return MY_OK;
}

static int listen_on(int port, int *bound)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0), opt = 1;

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 16) < 0 ||
        getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        close(fd);
        return -1;
    }
    *bound = ntohs(addr.sin_port);
    return fd;
}

static void set_folder(struct FsynC_server *s)
{
    const char *dir = s->dir, *base, *p, *end;
    size_t n;

    end = dir + strlen(dir);
    while (end > dir && end[-1] == '/')
        end--;
    base = dir;
    for (p = dir; p < end; p++)
        if (*p == '/' && p + 1 < end)
            base = p + 1;
    n = (size_t)(end - base);
    if (n == 0 || n > FSYNC_DISCOVER_FOLDER_MAX) {
        snprintf(s->folder, sizeof s->folder, "shared");
        return;
    }
    memcpy(s->folder, base, n);
    s->folder[n] = 0;
    if (!FsynC_discover_label_ok(s->folder, FSYNC_DISCOVER_FOLDER_MAX))
        snprintf(s->folder, sizeof s->folder, "shared");
}

static void set_default_name(struct FsynC_server *s)
{
    char host[256];

    host[0] = 0;
    if (gethostname(host, sizeof host - 1) != 0)
        host[0] = 0;
    host[sizeof host - 1] = 0;
    if (strlen(host) > FSYNC_DISCOVER_NAME_MAX)
        host[FSYNC_DISCOVER_NAME_MAX] = 0;
    if (!FsynC_discover_label_ok(host, FSYNC_DISCOVER_NAME_MAX)) {
        memcpy(s->name, "FsynC", 6);
    } else {
        size_t n = strlen(host);
        memcpy(s->name, host, n + 1);
    }
}

struct FsynC_server *FsynC_server_create(int port, const char *dir, const char *logfile, int *err)
{
    struct FsynC_server *s = calloc(1, sizeof *s);
    struct my_manifest old;
    char *meta = NULL;
    size_t i;
    int rc = MY_ERR_NOMEM;

    if (!s)
        goto fail;
    s->lfd = -1;
    s->ufd = -1;
    s->udp_port = FSYNC_DISCOVER_PORT;
    rc = MY_ERR_ARG;
    if (!dir || port < 0 || port > 65535)
        goto fail;
    rc = MY_ERR_NOMEM;
    s->dir = strdup(dir);
    meta = my_join(dir, MY_META_DIR);
    s->index = meta ? my_join(meta, "index") : NULL;
    s->log = my_log_open(logfile);
    if (!s->dir || !s->index || !s->log)
        goto fail;
    set_folder(s);
    set_default_name(s);
    rc = MY_ERR_IO;
    if (my_mkdirs(meta, 1) < 0)
        goto fail;
    /* load versions */
    rc = my_manifest_load(s->index, &old);
    if (rc < 0)
        goto fail;
    for (i = 0; i < old.n; i++)
        if (old.e[i].version > s->seq)
            s->seq = old.e[i].version;
    s->m = old;
    rc = refresh(s);
    if (rc < 0)
        goto fail;
    rc = MY_ERR_NET;
    s->lfd = listen_on(port, &s->port);
    if (s->lfd < 0)
        goto fail;
    free(meta);
    if (err)
        *err = MY_OK;
    return s;
fail:
    free(meta);
    FsynC_server_destroy(s);
    if (err)
        *err = rc;
    return NULL;
}

int FsynC_server_port(const struct FsynC_server *s)
{
    return s ? s->port : -1;
}

int FsynC_server_set_discovery(struct FsynC_server *s, int udp_port, const char *name)
{
    int port;
    char nbuf[FSYNC_DISCOVER_NAME_MAX + 1];

    if (!s || udp_port < 0 || udp_port > 65535 || s->ufd >= 0)
        return MY_ERR_ARG;
    port = s->udp_port;
    memcpy(nbuf, s->name, sizeof nbuf);
    if (udp_port > 0)
        port = udp_port;
    if (name) {
        if (!FsynC_discover_label_ok(name, FSYNC_DISCOVER_NAME_MAX))
            return MY_ERR_ARG;
        memcpy(nbuf, name, strlen(name) + 1);
    }
    s->udp_port = port;
    memcpy(s->name, nbuf, sizeof nbuf);
    return MY_OK;
}

void FsynC_server_destroy(struct FsynC_server *s)
{
    int i;

    if (!s)
        return;
    for (i = 0; i < s->nc; i++)
        close(s->c[i].fd);
    if (s->lfd >= 0)
        close(s->lfd);
    if (s->ufd >= 0)
        close(s->ufd);
    my_manifest_free(&s->m);
    my_log_close(s->log);
    free(s->dir);
    free(s->index);
    free(s);
}

/* push notify */
static void notify_others(struct FsynC_server *s, struct conn *from, const char *path)
{
    int i;

    for (i = 0; i < s->nc; i++) {
        struct conn *c = &s->c[i];
        if (c == from || !c->id[0])
            continue;
        if (my_send_msg(c->fd, OP_NOTIFY, path, NULL, 0) < 0) {
            shutdown(c->fd, SHUT_RDWR);
            continue;
        }
        my_log_event(s->log, c->ip, c->id, "PUSH", path, "NOTIFIED", 0, 0);
    }
}

static void event(struct FsynC_server *s, struct conn *c, const char *action, const char *path,
                  const char *result, uint64_t size, uint64_t sent)
{
    my_log_event(s->log, c->ip, c->id[0] ? c->id : "-", action, path, result, size, sent);
}

/* conflict copy name */
static char *conflict_name(struct FsynC_server *s, const char *path, const char *id)
{
    const char *base = strrchr(path, '/'), *dot;
    char stamp[32], *full;
    size_t n = strlen(path) + strlen(id) + 64;
    char *out = malloc(n);
    time_t now = time(NULL);
    struct tm tm_now;
    int k;

    if (!out)
        return NULL;
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    if (!dot || dot == base)
        dot = path + strlen(path);
    localtime_r(&now, &tm_now);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm_now);
    for (k = 1; k < 100; k++) {
        int len = (int)(dot - path);
        if (k == 1)
            snprintf(out, n, "%.*s.conflict-%s-%s%s", len, path, id, stamp, dot);
        else
            snprintf(out, n, "%.*s.conflict-%s-%s-%d%s", len, path, id, stamp, k, dot);
        full = my_join(s->dir, out);
        if (!full)
            break;
        if (access(full, F_OK) != 0) {
            free(full);
            return out;
        }
        free(full);
    }
    free(out);
    return NULL;
}

static int store(struct FsynC_server *s, const char *path, const uint8_t *data, size_t len)
{
    char *full = my_join(s->dir, path);
    int rc;

    if (!full)
        return MY_ERR_NOMEM;
    rc = my_mkdirs(full, 0);
    if (rc == MY_OK)
        rc = my_write_atomic(full, data, len);
    free(full);
    if (rc == MY_OK)
        rc = refresh(s);
    return rc;
}

/* apply upload */
static int apply_put(struct FsynC_server *s, struct conn *c, const char *path, uint64_t base,
                     const uint8_t *data, size_t len, const char *action, uint64_t sent)
{
    struct my_entry *e;
    uint8_t sha[MY_SHA_LEN];
    char *alt;
    uint64_t v;

    refresh(s);
    e = my_manifest_find(&s->m, path);
    my_sha256(data, len, sha);
    if (e && e->type == MY_DIR) {
        event(s, c, action, path, "ERROR", len, sent);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    if (e && memcmp(e->sha, sha, MY_SHA_LEN) == 0) {
        event(s, c, action, path, "UNCHANGED", len, sent);
        return my_send_ack(c->fd, RES_UNCHANGED, e->version);
    }
    if (e && e->version != base) {
        v = e->version;
        alt = conflict_name(s, path, c->id[0] ? c->id : "anon");
        if (!alt || store(s, alt, data, len) < 0) {
            free(alt);
            event(s, c, "CONFLICT", path, "ERROR", len, sent);
            return my_send_ack(c->fd, RES_ERROR, 0);
        }
        event(s, c, "CONFLICT", alt, "KEPT_BOTH", len, sent);
        notify_others(s, NULL, alt);
        free(alt);
        return my_send_ack(c->fd, RES_CONFLICT, v);
    }
    if (store(s, path, data, len) < 0) {
        event(s, c, action, path, "ERROR", len, sent);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    e = my_manifest_find(&s->m, path);
    v = e ? e->version : 0;
    event(s, c, action, path, "STORED", len, sent);
    notify_others(s, c, path);
    return my_send_ack(c->fd, RES_STORED, v);
}

static int on_put(struct FsynC_server *s, struct conn *c, struct my_msg *m)
{
    uint8_t sha[MY_SHA_LEN];
    uint64_t len = m->len - 40;

    if (m->len < 40)
        return my_send_ack(c->fd, RES_ERROR, 0);
    if (len > MAX_FILE_SIZE) {
        event(s, c, "UPLOAD", m->name, "TOO_BIG", len, m->len);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    my_sha256(m->data + 40, (size_t)len, sha);
    if (memcmp(sha, m->data + 8, MY_SHA_LEN) != 0) {
        event(s, c, "UPLOAD", m->name, "BAD_HASH", len, m->len);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    return apply_put(s, c, m->name, get_u64(m->data), m->data + 40, (size_t)len, "UPLOAD", len);
}

static int on_delta(struct FsynC_server *s, struct conn *c, struct my_msg *m)
{
    uint8_t *base = NULL, *out = NULL, sha[MY_SHA_LEN];
    size_t base_len = 0, out_len = 0;
    char *full = my_join(s->dir, m->name);
    int rc;

    if (!full || m->len < 40) {
        free(full);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    rc = my_read_file(full, &base, &base_len);
    free(full);
    if (rc == MY_OK)
        rc = my_delta_apply(base, base_len, m->data + 40, (size_t)m->len - 40, &out, &out_len);
    free(base);
    if (rc == MY_OK) {
        my_sha256(out, out_len, sha);
        if (memcmp(sha, m->data + 8, MY_SHA_LEN) != 0)
            rc = MY_ERR_HASH;
    }
    if (rc < 0) {
        free(out);
        event(s, c, "DELTA", m->name, FsynC_strerror(rc), 0, m->len);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    rc = apply_put(s, c, m->name, get_u64(m->data), out, out_len, "DELTA", m->len - 40);
    free(out);
    return rc;
}

/* delete file or dir */
static int on_remove(struct FsynC_server *s, struct conn *c, struct my_msg *m, int is_dir)
{
    const char *action = is_dir ? "RMDIR" : "DELETE";
    struct my_entry *e;
    char *full;
    int r;

    if (m->len != 8)
        return my_send_ack(c->fd, RES_ERROR, 0);
    refresh(s);
    e = my_manifest_find(&s->m, m->name);
    if (!e) {
        event(s, c, action, m->name, "NOT_FOUND", 0, 0);
        return my_send_ack(c->fd, RES_NOT_FOUND, 0);
    }
    if ((e->type == MY_DIR) != is_dir) {
        event(s, c, action, m->name, "ERROR", 0, 0);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    if (!is_dir && e->version != get_u64(m->data)) {
        event(s, c, "CONFLICT", m->name, "EDIT_WINS", e->size, 0);
        return my_send_ack(c->fd, RES_CONFLICT, e->version);
    }
    full = my_join(s->dir, m->name);
    if (!full)
        return my_send_ack(c->fd, RES_ERROR, 0);
    r = is_dir ? rmdir(full) : unlink(full);
    free(full);
    if (r < 0) {
        r = (is_dir && (errno == ENOTEMPTY || errno == EEXIST)) ? RES_CONFLICT : RES_ERROR;
        event(s, c, action, m->name, r == RES_CONFLICT ? "NOT_EMPTY" : "ERROR", 0, 0);
        return my_send_ack(c->fd, (uint8_t)r, 0);
    }
    refresh(s);
    event(s, c, action, m->name, "DELETED", 0, 0);
    notify_others(s, c, m->name);
    return my_send_ack(c->fd, RES_DELETED, 0);
}

static int on_mkdir(struct FsynC_server *s, struct conn *c, struct my_msg *m)
{
    struct my_entry *e;
    char *full;
    int rc;

    refresh(s);
    e = my_manifest_find(&s->m, m->name);
    if (e) {
        event(s, c, "MKDIR", m->name, e->type == MY_DIR ? "UNCHANGED" : "ERROR", 0, 0);
        return my_send_ack(c->fd, e->type == MY_DIR ? RES_UNCHANGED : RES_ERROR, e->version);
    }
    full = my_join(s->dir, m->name);
    rc = full ? my_mkdirs(full, 1) : MY_ERR_NOMEM;
    free(full);
    if (rc < 0) {
        event(s, c, "MKDIR", m->name, "ERROR", 0, 0);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    refresh(s);
    e = my_manifest_find(&s->m, m->name);
    event(s, c, "MKDIR", m->name, "CREATED", 0, 0);
    notify_others(s, c, m->name);
    return my_send_ack(c->fd, RES_CREATED, e ? e->version : 0);
}

static int on_sigreq(struct FsynC_server *s, struct conn *c, struct my_msg *m)
{
    uint8_t *data, *sig;
    size_t len, sig_len;
    char *full = my_join(s->dir, m->name);
    int rc = full ? my_read_file(full, &data, &len) : MY_ERR_NOMEM;

    free(full);
    if (rc < 0)
        return my_send_ack(c->fd, RES_NOT_FOUND, 0);
    rc = my_sig_build(data, len, &sig, &sig_len);
    free(data);
    if (rc < 0)
        return my_send_ack(c->fd, RES_ERROR, 0);
    rc = my_send_msg(c->fd, OP_SIGS, NULL, sig, sig_len);
    free(sig);
    return rc;
}

/* send file to client */
static int on_get(struct FsynC_server *s, struct conn *c, struct my_msg *m)
{
    struct my_entry *e;
    uint8_t *data = NULL, *delta = NULL, *out;
    size_t len = 0, dlen = 0;
    char *full;
    int rc, op = OP_FILE;

    refresh(s);
    e = my_manifest_find(&s->m, m->name);
    if (!e || e->type != MY_FILE)
        return my_send_ack(c->fd, RES_NOT_FOUND, 0);
    full = my_join(s->dir, m->name);
    rc = full ? my_read_file(full, &data, &len) : MY_ERR_NOMEM;
    free(full);
    if (rc < 0)
        return my_send_ack(c->fd, RES_ERROR, 0);
    if (m->len > 0 && my_delta_build(m->data, (size_t)m->len, data, len, &delta, &dlen) == MY_OK &&
        dlen < len)
        op = OP_DELTA;
    out = malloc(40 + (op == OP_DELTA ? dlen : len));
    if (!out) {
        free(data);
        free(delta);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    put_u64(out, e->version);
    memcpy(out + 8, e->sha, MY_SHA_LEN);
    memcpy(out + 40, op == OP_DELTA ? delta : data, op == OP_DELTA ? dlen : len);
    rc = my_send_msg(c->fd, (uint8_t)op, m->name, out, 40 + (op == OP_DELTA ? dlen : len));
    event(s, c, "PUSH", m->name, op == OP_DELTA ? "SENT_DELTA" : "SENT_FILE", len,
          op == OP_DELTA ? dlen : len);
    free(out);
    free(data);
    free(delta);
    return rc;
}

static int on_list(struct FsynC_server *s, struct conn *c)
{
    uint8_t *buf;
    size_t len;
    int rc;

    refresh(s);
    rc = my_manifest_encode(&s->m, &buf, &len);
    if (rc < 0)
        return my_send_ack(c->fd, RES_ERROR, 0);
    rc = my_send_msg(c->fd, OP_MANIFEST, NULL, buf, len);
    free(buf);
    return rc;
}

static int valid_id(const char *id)
{
    size_t n = strlen(id), i;

    if (n == 0 || n >= sizeof(((struct conn *)0)->id))
        return 0;
    for (i = 0; i < n; i++)
        if (!(id[i] >= 'a' && id[i] <= 'z') && !(id[i] >= 'A' && id[i] <= 'Z') &&
            !(id[i] >= '0' && id[i] <= '9') && id[i] != '-' && id[i] != '_')
            return 0;
    return 1;
}

/* dispatch one request */
static int handle(struct FsynC_server *s, struct conn *c, struct my_msg *m)
{
    if (m->op == OP_HELLO) {
        if (!valid_id(m->name))
            return my_send_ack(c->fd, RES_ERROR, 0);
        snprintf(c->id, sizeof c->id, "%s", m->name);
        event(s, c, "HELLO", "-", "CONNECTED", 0, 0);
        return my_send_ack(c->fd, RES_STORED, 0);
    }
    if (!c->id[0])
        return MY_ERR_PROTO;
    if (m->op == OP_LIST)
        return on_list(s, c);
    if (!my_path_valid(m->name)) {
        event(s, c, "REQUEST", "(unsafe path)", "REJECTED", 0, 0);
        return my_send_ack(c->fd, RES_ERROR, 0);
    }
    switch (m->op) {
    case OP_SEND_FILE: return on_put(s, c, m);
    case OP_DELTA: return on_delta(s, c, m);
    case OP_DELETE: return on_remove(s, c, m, 0);
    case OP_RMDIR: return on_remove(s, c, m, 1);
    case OP_MKDIR: return on_mkdir(s, c, m);
    case OP_SIGREQ: return on_sigreq(s, c, m);
    case OP_GET: return on_get(s, c, m);
    default: return MY_ERR_PROTO;
    }
}

static void drop(struct FsynC_server *s, int i)
{
    event(s, &s->c[i], "BYE", "-", "DISCONNECTED", 0, 0);
    close(s->c[i].fd);
    s->c[i] = s->c[--s->nc];
}

static void accept_one(struct FsynC_server *s)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    struct timeval tv = {IO_TIMEOUT, 0};
    int fd = accept(s->lfd, (struct sockaddr *)&addr, &len);
    struct conn *c;

    if (fd < 0)
        return;
    if (s->nc == MAX_CLIENTS) {
        close(fd);
        return;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    c = &s->c[s->nc++];
    memset(c, 0, sizeof *c);
    c->fd = fd;
    if (!inet_ntop(AF_INET, &addr.sin_addr, c->ip, sizeof c->ip))
        snprintf(c->ip, sizeof c->ip, "unknown");
}

static int bind_udp(struct FsynC_server *s)
{
    struct sockaddr_in addr;
    int fd, flags;

    if (s->udp_port <= 0 || s->udp_port > 65535)
        return -1;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)s->udp_port);
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    s->ufd = fd;
    return 0;
}

/* one wakeup, bounded so a datagram flood cannot stall TCP clients */
static void on_discover(struct FsynC_server *s)
{
    int k;

    for (k = 0; k < 32; k++) {
        uint8_t buf[512];
        uint8_t reply[FSYNC_DISCOVER_MAX_REPLY];
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        ssize_t n;
        int rlen;
        char ip[INET_ADDRSTRLEN];

        n = recvfrom(s->ufd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if ((size_t)n >= sizeof buf || flen < sizeof from || from.sin_family != AF_INET)
            continue;
        if (!FsynC_discover_check_request((const uint8_t *)buf, (size_t)n))
            continue;
        rlen = FsynC_discover_encode_reply(reply, sizeof reply, s->name, s->folder,
                                           (unsigned)s->port);
        if (rlen < 0)
            continue;
        if (sendto(s->ufd, reply, (size_t)rlen, 0, (struct sockaddr *)&from, flen) < 0)
            continue;
        if (!inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip))
            snprintf(ip, sizeof ip, "unknown");
        my_log_event(s->log, ip, "-", "DISCOVER", s->folder, "REPLIED", 0, (uint64_t)rlen);
    }
}

/* event loop */
int FsynC_server_run(struct FsynC_server *s, volatile sig_atomic_t *stop)
{
    struct pollfd p[MAX_CLIENTS + 2];
    int i, n;

    if (!s)
        return MY_ERR_ARG;
    if (s->ufd < 0 && bind_udp(s) < 0)
        return MY_ERR_NET;
    printf("listening on port %d, sharing %s\n", s->port, s->dir);
    printf("discovery on udp %d name %s\n", s->udp_port, s->name);
    fflush(stdout);
    while (!stop || !*stop) {
        p[0].fd = s->lfd;
        p[0].events = POLLIN;
        p[1].fd = s->ufd;
        p[1].events = POLLIN;
        for (i = 0; i < s->nc; i++) {
            p[i + 2].fd = s->c[i].fd;
            p[i + 2].events = POLLIN;
        }
        n = s->nc;
        if (poll(p, (nfds_t)n + 2, 500) < 0) {
            if (errno == EINTR)
                continue;
            return MY_ERR_NET;
        }
        for (i = n - 1; i >= 0; i--) {
            struct my_msg m;
            if (!(p[i + 2].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            if (my_recv_msg(s->c[i].fd, &m) < 0 || handle(s, &s->c[i], &m) < 0) {
                my_msg_free(&m);
                drop(s, i);
                continue;
            }
            my_msg_free(&m);
        }
        if (p[0].revents & POLLIN)
            accept_one(s);
        if (p[1].revents & POLLIN)
            on_discover(s);
    }
    return MY_OK;
}
