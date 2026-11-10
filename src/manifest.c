#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void my_manifest_init(struct my_manifest *m)
{
    m->e = NULL;
    m->n = 0;
    m->cap = 0;
}

void my_manifest_free(struct my_manifest *m)
{
    size_t i;

    for (i = 0; i < m->n; i++)
        free(m->e[i].path);
    free(m->e);
    my_manifest_init(m);
}

/* binary search */
static size_t lower_bound(const struct my_manifest *m, const char *path)
{
    size_t lo = 0, hi = m->n;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (strcmp(m->e[mid].path, path) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

struct my_entry *my_manifest_find(const struct my_manifest *m, const char *path)
{
    size_t i = lower_bound(m, path);

    if (i < m->n && strcmp(m->e[i].path, path) == 0)
        return &m->e[i];
    return NULL;
}

int my_manifest_put(struct my_manifest *m, const struct my_entry *e)
{
    size_t i = lower_bound(m, e->path);
    char *path;

    if (i < m->n && strcmp(m->e[i].path, e->path) == 0) {
        path = m->e[i].path;
        m->e[i] = *e;
        m->e[i].path = path;
        return MY_OK;
    }
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 16;
        struct my_entry *p = realloc(m->e, cap * sizeof *p);
        if (!p)
            return MY_ERR_NOMEM;
        m->e = p;
        m->cap = cap;
    }
    path = strdup(e->path);
    if (!path)
        return MY_ERR_NOMEM;
    memmove(m->e + i + 1, m->e + i, (m->n - i) * sizeof *m->e);
    m->e[i] = *e;
    m->e[i].path = path;
    m->n++;
    return MY_OK;
}

int my_manifest_remove(struct my_manifest *m, const char *path)
{
    struct my_entry *e = my_manifest_find(m, path);
    size_t i;

    if (!e)
        return MY_ERR_NOTFOUND;
    i = (size_t)(e - m->e);
    free(e->path);
    memmove(m->e + i, m->e + i + 1, (m->n - i - 1) * sizeof *m->e);
    m->n--;
    return MY_OK;
}

static int sha_file(const char *path, uint8_t out[MY_SHA_LEN])
{
    uint8_t buf[65536];
    struct my_sha256 c;
    FILE *f = fopen(path, "rb");
    size_t n;
    int err;

    if (!f)
        return MY_ERR_IO;
    my_sha256_init(&c);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        my_sha256_update(&c, buf, n);
    err = ferror(f);
    fclose(f);
    if (err)
        return MY_ERR_IO;
    my_sha256_final(&c, out);
    return MY_OK;
}

/* stat one entry */
static int scan_entry(const char *full, const char *rel, const struct stat *st,
                      const struct my_manifest *old, struct my_manifest *out)
{
    struct my_entry e, *prev = old ? my_manifest_find(old, rel) : NULL;

    memset(&e, 0, sizeof e);
    e.path = (char *)rel;
    e.type = S_ISDIR(st->st_mode) ? MY_DIR : MY_FILE;
    if (e.type == MY_FILE) {
        e.size = (uint64_t)st->st_size;
        e.mtime = (uint64_t)st->st_mtim.tv_sec * 1000000000ULL + (uint64_t)st->st_mtim.tv_nsec;
    }
    if (prev && prev->type == e.type && prev->size == e.size && prev->mtime == e.mtime) {
        memcpy(e.sha, prev->sha, MY_SHA_LEN);
        e.version = prev->version;
    } else if (e.type == MY_FILE && sha_file(full, e.sha) < 0) {
        return MY_OK;
    }
    return my_manifest_put(out, &e);
}

int my_entry_stat(const char *root, const char *rel, struct my_entry *out)
{
    struct my_manifest one;
    struct stat st;
    char *full = my_join(root, rel);
    int rc;

    if (!full)
        return MY_ERR_NOMEM;
    my_manifest_init(&one);
    rc = lstat(full, &st) == 0 && (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) ? MY_OK : MY_ERR_NOTFOUND;
    if (rc == MY_OK)
        rc = scan_entry(full, rel, &st, NULL, &one);
    free(full);
    if (rc == MY_OK && one.n != 1)
        rc = MY_ERR_IO;
    if (rc == MY_OK) {
        *out = one.e[0];
        out->path = (char *)rel;
    }
    my_manifest_free(&one);
    return rc;
}

/* recursive scan */
static int scan_dir(const char *root, const char *rel, const struct my_manifest *old,
                    struct my_manifest *out)
{
    char *dir = rel[0] ? my_join(root, rel) : strdup(root);
    DIR *d;
    struct dirent *de;
    int rc = MY_OK;

    if (!dir)
        return MY_ERR_NOMEM;
    d = opendir(dir);
    if (!d) {
        free(dir);
        return rel[0] ? MY_OK : MY_ERR_IO;
    }
    while (rc == MY_OK && (de = readdir(d)) != NULL) {
        struct stat st;
        char *r, *full;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        r = rel[0] ? my_join(rel, de->d_name) : strdup(de->d_name);
        full = r ? my_join(root, r) : NULL;
        if (!r || !full) {
            rc = MY_ERR_NOMEM;
        } else if (my_path_valid(r) && lstat(full, &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                rc = scan_entry(full, r, &st, old, out);
            } else if (S_ISDIR(st.st_mode)) {
                rc = scan_entry(full, r, &st, old, out);
                if (rc == MY_OK)
                    rc = scan_dir(root, r, old, out);
            }
        }
        free(r);
        free(full);
    }
    closedir(d);
    free(dir);
    return rc;
}

int my_manifest_scan(const char *root, const struct my_manifest *old, struct my_manifest *out)
{
    int rc;

    if (!root || !out)
        return MY_ERR_ARG;
    my_manifest_init(out);
    rc = scan_dir(root, "", old, out);
    if (rc < 0)
        my_manifest_free(out);
    return rc;
}

/* text encoding */
int my_manifest_encode(const struct my_manifest *m, uint8_t **buf, size_t *len)
{
    size_t cap = 1, i, used = 0;
    char *p, hex[65];

    for (i = 0; i < m->n; i++)
        cap += strlen(m->e[i].path) + 140;
    p = malloc(cap);
    if (!p)
        return MY_ERR_NOMEM;
    for (i = 0; i < m->n; i++) {
        const struct my_entry *e = &m->e[i];
        my_sha_hex(e->sha, hex);
        used += (size_t)snprintf(p + used, cap - used, "%c %llu %llu %llu %s %s\n", e->type,
                                 (unsigned long long)e->size, (unsigned long long)e->mtime,
                                 (unsigned long long)e->version, hex, e->path);
    }
    *buf = (uint8_t *)p;
    *len = used;
    return MY_OK;
}

static int parse_line(char *line, struct my_entry *e)
{
    unsigned long long size, mtime, version;
    char type, hex[65];
    int off = 0;

    if (sscanf(line, "%c %llu %llu %llu %64s %n", &type, &size, &mtime, &version, hex, &off) != 5 ||
        off == 0)
        return MY_ERR_PROTO;
    if ((type != MY_FILE && type != MY_DIR) || strlen(hex) != 64 || my_sha_unhex(hex, e->sha) < 0)
        return MY_ERR_PROTO;
    e->path = line + off;
    if (!my_path_valid(e->path) || size > MAX_FILE_SIZE)
        return MY_ERR_PROTO;
    e->type = type;
    e->size = size;
    e->mtime = mtime;
    e->version = version;
    return MY_OK;
}

int my_manifest_decode(const uint8_t *buf, size_t len, struct my_manifest *m)
{
    char *text = malloc(len + 1), *line, *nl;
    int rc = MY_OK;

    my_manifest_init(m);
    if (!text)
        return MY_ERR_NOMEM;
    memcpy(text, buf, len);
    text[len] = '\0';
    if (strlen(text) != len)
        rc = MY_ERR_PROTO;
    for (line = text; rc == MY_OK && *line; line = nl + 1) {
        struct my_entry e;
        nl = strchr(line, '\n');
        if (!nl) {
            rc = MY_ERR_PROTO;
            break;
        }
        *nl = '\0';
        rc = parse_line(line, &e);
        if (rc == MY_OK)
            rc = my_manifest_put(m, &e);
    }
    free(text);
    if (rc < 0)
        my_manifest_free(m);
    return rc;
}

int my_manifest_load(const char *file, struct my_manifest *m)
{
    uint8_t *buf;
    size_t len;
    int rc = my_read_file(file, &buf, &len);

    my_manifest_init(m);
    if (rc == MY_ERR_NOTFOUND)
        return MY_OK;
    if (rc < 0)
        return rc;
    rc = my_manifest_decode(buf, len, m);
    free(buf);
    return rc;
}

int my_manifest_save(const char *file, const struct my_manifest *m)
{
    uint8_t *buf;
    size_t len;
    int rc = my_manifest_encode(m, &buf, &len);

    if (rc < 0)
        return rc;
    rc = my_write_atomic(file, buf, len);
    free(buf);
    return rc;
}

/* compare manifests */
static int add_change(struct my_change **out, size_t *n, const struct my_entry *e, int kind)
{
    struct my_change *p = realloc(*out, (*n + 1) * sizeof *p);

    if (!p)
        return MY_ERR_NOMEM;
    p[*n].entry = e;
    p[*n].kind = kind;
    *out = p;
    (*n)++;
    return MY_OK;
}

int my_manifest_diff(const struct my_manifest *old, const struct my_manifest *cur,
                     struct my_change **out, size_t *count)
{
    size_t i;
    int rc = MY_OK;

    *out = NULL;
    *count = 0;
/* first pass:report added or modified entries from the current tree */
		for (i = 0; rc == MY_OK && i < cur->n; i++) {
        const struct my_entry *o = my_manifest_find(old, cur->e[i].path);
        if (!o)
            rc = add_change(out, count, &cur->e[i], MY_ADDED);
        else if (o->type != cur->e[i].type ||
                 memcmp(o->sha, cur->e[i].sha, MY_SHA_LEN) != 0)
            rc = add_change(out, count, &cur->e[i], MY_MODIFIED);
    }
/*second pass: report entries that disappeared*/
		for (i = 0; rc == MY_OK && i < old->n; i++) {
        if (!my_manifest_find(cur, old->e[i].path))
            rc = add_change(out, count, &old->e[i], MY_REMOVED);
    }
    if (rc < 0) {
        free(*out);
        *out = NULL;
        *count = 0;
    }
    return rc;
}
