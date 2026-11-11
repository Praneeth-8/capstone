#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define REPLY_TIMEOUT 30

struct FsynC_client {
    char *host;
    int port;
    char *dir;
    char *state_path;
    char id[40];
    int fd;
    int notified;
    struct my_manifest state;
    struct my_manifest conflicts;
    struct FsynC_stats st;
};

/* client id */
static int load_id(struct FsynC_client *c, const char *meta)
{
    char *path = my_join(meta, "id");
    uint8_t *buf;
    size_t len, i;
    int rc;

    if (!path)
        return MY_ERR_NOMEM;
    rc = my_read_file(path, &buf, &len);
    if (rc == MY_OK && len > 0 && len < sizeof c->id) {
        memcpy(c->id, buf, len);
        c->id[len] = '\0';
        for (i = 0; i < len; i++)
            if (c->id[i] == '\n')
                c->id[i] = '\0';
        free(buf);
        free(path);
        return MY_OK;
    }
    if (rc == MY_OK)
        free(buf);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    snprintf(c->id, sizeof c->id, "c%06x", (unsigned)rand() & 0xffffff);
    rc = my_write_atomic(path, c->id, strlen(c->id));
    free(path);
    return rc;
}

struct FsynC_client *FsynC_client_create(const char *host, int port, const char *dir, int *err)
{
    struct FsynC_client *c = calloc(1, sizeof *c);
    char *meta = NULL;
    int rc = MY_ERR_NOMEM;

    if (!c)
        goto fail;
    c->fd = -1;
    rc = MY_ERR_ARG;
    if (!host || !dir || port <= 0 || port > 65535)
        goto fail;
    rc = MY_ERR_NOMEM;
    c->port = port;
    c->host = strdup(host);
    c->dir = strdup(dir);
    meta = my_join(dir, MY_META_DIR);
    c->state_path = meta ? my_join(meta, "state") : NULL;
    if (!c->host || !c->dir || !c->state_path)
        goto fail;
    rc = my_mkdirs(meta, 1);
    if (rc == MY_OK)
        rc = load_id(c, meta);
    if (rc == MY_OK)
        rc = my_manifest_load(c->state_path, &c->state);
    if (rc < 0)
        goto fail;
    free(meta);
    if (err)
        *err = MY_OK;
    return c;
fail:
    free(meta);
    FsynC_client_destroy(c);
    if (err)
        *err = rc;
    return NULL;
}

void FsynC_client_destroy(struct FsynC_client *c)
{
    if (!c)
        return;
    if (c->fd >= 0)
        close(c->fd);
    my_manifest_free(&c->state);
    my_manifest_free(&c->conflicts);
    free(c->host);
    free(c->dir);
    free(c->state_path);
    free(c);
}

const struct FsynC_stats *FsynC_client_stats(const struct FsynC_client *c)
{
    return &c->st;
}

const char *FsynC_client_id(const struct FsynC_client *c)
{
    return c->id;
}

/* request and reply */
static int request(struct FsynC_client *c, uint8_t op, const char *name, const void *data,
                   size_t len, struct my_msg *reply)
{
    int rc = my_send_msg(c->fd, op, name, data, len);

    if (rc < 0)
        return rc;
    for (;;) {
        rc = my_recv_msg(c->fd, reply);
        if (rc < 0)
            return rc;
        if (reply->op != OP_NOTIFY)
            return MY_OK;
        c->notified = 1;
        my_msg_free(reply);
    }
}

static int ack_code(const struct my_msg *r, uint64_t *version)
{
    if ((r->op != OP_ACK && r->op != OP_ERROR) || r->len != 9)
        return -1;
    if (version)
        *version = get_u64(r->data + 1);
    return r->data[0];
}

static int connect_server(struct FsynC_client *c)
{
    struct addrinfo hints, *res, *ai;
    struct timeval tv = {REPLY_TIMEOUT, 0};
    struct my_msg r;
    char port[16];
    int fd = -1, code;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof port, "%d", c->port);
    if (getaddrinfo(c->host, port, &hints, &res) != 0)
        return MY_ERR_NET;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        if (fd >= 0)
            close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        return MY_ERR_NET;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    c->fd = fd;
    if (request(c, OP_HELLO, c->id, NULL, 0, &r) < 0) {
        close(fd);
        c->fd = -1;
        return MY_ERR_NET;
    }
    code = ack_code(&r, NULL);
    my_msg_free(&r);
    if (code != RES_STORED) {
        close(fd);
        c->fd = -1;
        return MY_ERR_PROTO;
    }
    return MY_OK;
}

static void disconnect(struct FsynC_client *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
}

static int local_entry(struct FsynC_client *c, const char *path, uint64_t version)
{
    struct my_entry e;
    int rc = my_entry_stat(c->dir, path, &e);

    if (rc < 0)
        return rc == MY_ERR_NOTFOUND ? MY_OK : rc;
    e.version = version;
    return my_manifest_put(&c->state, &e);
}

/* push one file */
static int push_file(struct FsynC_client *c, const struct my_entry *e, uint64_t base)
{
    uint8_t *data, *delta = NULL, *out;
    size_t len, dlen = 0, sent;
    struct my_msg r;
    char *full = my_join(c->dir, e->path);
    int rc, code, op = OP_SEND_FILE;
    uint64_t v;

    rc = full ? my_read_file(full, &data, &len) : MY_ERR_NOMEM;
    free(full);
    if (rc < 0) {
        printf("SKIP %s: %s\n", e->path, FsynC_strerror(rc));
        return rc == MY_ERR_NOMEM ? rc : MY_OK;
    }
    /* try delta */
    if (base && request(c, OP_SIGREQ, e->path, NULL, 0, &r) == MY_OK) {
        if (r.op == OP_SIGS && my_delta_build(r.data, (size_t)r.len, data, len, &delta, &dlen) == MY_OK &&
            dlen < len)
            op = OP_DELTA;
        my_msg_free(&r);
    }
    sent = op == OP_DELTA ? dlen : len;
    out = malloc(40 + sent);
    if (!out) {
        free(data);
        free(delta);
        return MY_ERR_NOMEM;
    }
    put_u64(out, base);
    my_sha256(data, len, out + 8);
    memcpy(out + 40, op == OP_DELTA ? delta : data, sent);
    free(data);
    free(delta);
    rc = request(c, (uint8_t)op, e->path, out, 40 + sent, &r);
    free(out);
    if (rc < 0)
        return rc;
    code = ack_code(&r, &v);
    my_msg_free(&r);
    if (code == RES_STORED || code == RES_UNCHANGED) {
        c->st.pushed++;
        c->st.bytes_sent += sent;
        c->st.bytes_full += len;
        printf("UPLOAD %s: sent %zu of %zu bytes%s\n", e->path, sent, len,
               op == OP_DELTA ? " (delta)" : "");
        return local_entry(c, e->path, v);
    }
    if (code == RES_CONFLICT) {
        c->st.conflicts++;
        printf("CONFLICT %s: server kept its copy, yours was saved as a conflict copy\n", e->path);
        return my_manifest_put(&c->conflicts, e);
    }
    printf("ERROR %s: upload rejected\n", e->path);
    return MY_OK;
}

static int push_simple(struct FsynC_client *c, uint8_t op, const struct my_entry *e, uint64_t base)
{
    uint8_t p[8];
    struct my_msg r;
    uint64_t v;
    int code, rc;

    put_u64(p, base);
    rc = request(c, op, e->path, p, op == OP_MKDIR ? 0 : 8, &r);
    if (rc < 0)
        return rc;
    code = ack_code(&r, &v);
    my_msg_free(&r);
    if (op == OP_MKDIR) {
        if (code == RES_CREATED || code == RES_UNCHANGED) {
            printf("MKDIR %s\n", e->path);
            return local_entry(c, e->path, v);
        }
    } else if (code == RES_DELETED || code == RES_NOT_FOUND) {
        printf("%s %s\n", op == OP_RMDIR ? "RMDIR" : "DELETE", e->path);
        c->st.deleted++;
        my_manifest_remove(&c->state, e->path);
        return MY_OK;
    } else if (code == RES_CONFLICT) {
        c->st.conflicts++;
        printf("CONFLICT %s: changed on server, keeping it\n", e->path);
    }
    return MY_OK;
}

/* send local changes */
static int push_changes(struct FsynC_client *c, const struct my_manifest *local)
{
    struct my_change *ch;
    size_t n, i;
    int rc = my_manifest_diff(&c->state, local, &ch, &n), pass;

    if (rc < 0)
        return rc;
    for (pass = 0; rc >= 0 && pass < 4; pass++) {
        for (i = 0; rc >= 0 && i < n; i++) {
            size_t k = pass == 3 ? n - 1 - i : i;
            const struct my_entry *e = ch[k].entry;
            struct my_entry *s = my_manifest_find(&c->state, e->path);
            uint64_t base = s ? s->version : 0;
            int removed = ch[k].kind == MY_REMOVED;
            if (pass == 0 && ch[k].kind == MY_MODIFIED && s && s->type != e->type)
                rc = push_simple(c, s->type == MY_DIR ? OP_RMDIR : OP_DELETE, s, base);
            else if (pass == 1 && !removed && e->type == MY_DIR)
                rc = push_simple(c, OP_MKDIR, e, 0);
            else if (pass == 2 && !removed && e->type == MY_FILE)
                rc = push_file(c, e, s && s->type == MY_FILE ? base : 0);
            else if (pass == 3 && removed)
                rc = push_simple(c, e->type == MY_DIR ? OP_RMDIR : OP_DELETE, e, base);
        }
    }
    free(ch);
    return rc;
}

/* fetch one file */
static int pull_file(struct FsynC_client *c, const struct my_entry *r, const struct my_entry *l)
{
    uint8_t *old = NULL, *sig = NULL, *data = NULL, sha[MY_SHA_LEN];
    size_t old_len = 0, sig_len = 0, len = 0;
    char *full = my_join(c->dir, r->path);
    struct my_msg m = {0};
    int rc;

    if (!full)
        return MY_ERR_NOMEM;
    if (l && l->type == MY_FILE && my_read_file(full, &old, &old_len) == MY_OK)
        my_sig_build(old, old_len, &sig, &sig_len);
    rc = request(c, OP_GET, r->path, sig, sig_len, &m);
    free(sig);
    if (rc == MY_OK && (m.op == OP_FILE || m.op == OP_DELTA) && m.len >= 40) {
        if (m.op == OP_DELTA)
            rc = my_delta_apply(old, old_len, m.data + 40, (size_t)m.len - 40, &data, &len);
        else if ((data = malloc(m.len - 40 + 1)) != NULL)
            memcpy(data, m.data + 40, (len = (size_t)m.len - 40));
        else
            rc = MY_ERR_NOMEM;
        if (rc == MY_OK) {
            my_sha256(data, len, sha);
            rc = memcmp(sha, m.data + 8, MY_SHA_LEN) ? MY_ERR_HASH : my_mkdirs(full, 0);
        }
        if (rc == MY_OK)
            rc = my_write_atomic(full, data, len);
        if (rc == MY_OK) {
            c->st.pulled++;
            printf("DOWNLOAD %s: received %llu of %zu bytes%s\n", r->path,
                   (unsigned long long)m.len - 40, len, m.op == OP_DELTA ? " (delta)" : "");
            rc = local_entry(c, r->path, get_u64(m.data));
        }
    } else if (rc == MY_OK) {
        rc = MY_ERR_NOTFOUND;
    }
    my_msg_free(&m);
    free(old);
    free(data);
    free(full);
    return rc == MY_ERR_NET ? rc : MY_OK;
}

static int local_changed(const struct FsynC_client *c, const struct my_entry *l)
{
    const struct my_entry *s = my_manifest_find(&c->state, l->path);

    if (my_manifest_find(&c->conflicts, l->path))
        return 0;
    return !s || s->type != l->type || memcmp(s->sha, l->sha, MY_SHA_LEN) != 0;
}

/* apply server state */
static int pull_changes(struct FsynC_client *c, const struct my_manifest *remote,
                        const struct my_manifest *local)
{
    size_t i;
    int rc = MY_OK;

    for (i = 0; rc >= 0 && i < remote->n; i++) {
        const struct my_entry *r = &remote->e[i], *l = my_manifest_find(local, r->path);
        char *full;
        if (l && l->type == r->type && (r->type == MY_DIR || !memcmp(l->sha, r->sha, MY_SHA_LEN))) {
            struct my_entry e = *l;
            e.version = r->version;
            rc = my_manifest_put(&c->state, &e);
        } else if (l && local_changed(c, l)) {
            continue;
        } else if (r->type == MY_DIR) {
            full = my_join(c->dir, r->path);
            rc = full ? my_mkdirs(full, 1) : MY_ERR_NOMEM;
            free(full);
            if (rc == MY_OK)
                rc = local_entry(c, r->path, r->version);
        } else if (!l || l->type == MY_FILE) {
            rc = pull_file(c, r, l);
        }
    }
    /* remove deleted entries */
    for (i = c->state.n; rc >= 0 && i-- > 0;) {
        struct my_entry *s = &c->state.e[i];
        const struct my_entry *l = my_manifest_find(local, s->path);
        char *full;
        if (my_manifest_find(remote, s->path))
            continue;
        if (l && l->type == MY_FILE && s->type == MY_FILE && !memcmp(l->sha, s->sha, MY_SHA_LEN)) {
            full = my_join(c->dir, s->path);
            if (full && unlink(full) == 0) {
                printf("REMOVED %s\n", s->path);
                c->st.deleted++;
            }
            free(full);
        } else if (l && l->type == MY_DIR && s->type == MY_DIR) {
            full = my_join(c->dir, s->path);
            if (full && rmdir(full) == 0)
                printf("REMOVED %s/\n", s->path);
            free(full);
        }
        rc = my_manifest_remove(&c->state, s->path);
    }
    return rc;
}

static int fetch_remote(struct FsynC_client *c, struct my_manifest *remote)
{
    struct my_msg m;
    int rc = request(c, OP_LIST, NULL, NULL, 0, &m);

    if (rc < 0)
        return rc;
    rc = m.op == OP_MANIFEST ? my_manifest_decode(m.data, (size_t)m.len, remote) : MY_ERR_PROTO;
    my_msg_free(&m);
    return rc;
}

/* full reconcile */
static int reconcile(struct FsynC_client *c)
{
    struct my_manifest local, remote;
    int rc;

    c->notified = 0;
    my_manifest_free(&c->conflicts);
    rc = my_manifest_scan(c->dir, &c->state, &local);
    if (rc < 0)
        return rc;
    rc = push_changes(c, &local);
    my_manifest_free(&local);
    if (rc < 0)
        return rc;
    rc = fetch_remote(c, &remote);
    if (rc < 0)
        return rc;
    rc = my_manifest_scan(c->dir, &c->state, &local);
    if (rc == MY_OK) {
        rc = pull_changes(c, &remote, &local);
        my_manifest_free(&local);
    }
    my_manifest_free(&remote);
    my_manifest_free(&c->conflicts);
    if (my_manifest_save(c->state_path, &c->state) < 0 && rc == MY_OK)
        rc = MY_ERR_IO;
    fflush(stdout);
    return rc;
}

int FsynC_client_sync(struct FsynC_client *c)
{
    int rc;

    if (!c)
        return MY_ERR_ARG;
    if (c->fd < 0 && (rc = connect_server(c)) < 0)
        return rc;
    rc = reconcile(c);
    if (rc == MY_ERR_NET)
        disconnect(c);
    return rc;
}

static int local_dirty(struct FsynC_client *c)
{
    struct my_manifest local;
    struct my_change *ch;
    size_t n = 0;

    if (my_manifest_scan(c->dir, &c->state, &local) < 0)
        return 0;
    if (my_manifest_diff(&c->state, &local, &ch, &n) == MY_OK)
        free(ch);
    my_manifest_free(&local);
    return n > 0;
}

static void wait_backoff(int secs, volatile sig_atomic_t *stop)
{
    int i;

    for (i = 0; i < secs * 10 && !(stop && *stop); i++)
        usleep(100000);
}

/* watch loop */
int FsynC_client_watch(struct FsynC_client *c, volatile sig_atomic_t *stop)
{
    int backoff = 1;

    if (!c)
        return MY_ERR_ARG;
    while (!(stop && *stop)) {
        struct pollfd p;
        struct my_msg m;
        int run = 0, r;
        if (c->fd < 0) {
            if (FsynC_client_sync(c) < 0) {
                disconnect(c);
                printf("server unreachable, retry in %d s\n", backoff);
                fflush(stdout);
                wait_backoff(backoff, stop);
                backoff = backoff < 8 ? backoff * 2 : 8;
                continue;
            }
            backoff = 1;
        }
        p.fd = c->fd;
        p.events = POLLIN;
        r = poll(&p, 1, 1000);
        if (r < 0 && errno != EINTR)
            return MY_ERR_NET;
        if (r > 0) {
            if (my_recv_msg(c->fd, &m) < 0) {
                disconnect(c);
                continue;
            }
            run = m.op == OP_NOTIFY;
            my_msg_free(&m);
        }
        if (run || c->notified || local_dirty(c)) {
            if (FsynC_client_sync(c) < 0)
                disconnect(c);
        }
    }
    return MY_OK;
}
