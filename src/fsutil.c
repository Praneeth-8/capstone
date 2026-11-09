#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

/* path safety */
int my_path_valid(const char *path)
{
    const char *seg = path, *p;
    size_t len;

    if (!path || !*path || path[0] == '/')
        return 0;
    len = strlen(path);
    if (len > MAX_PATH_LEN || ends_with(path, MY_TMP_SUFFIX))
        return 0;
    for (p = path; *p; p++) {
        if ((unsigned char)*p < 32 || *p == '\\' || *p == 127)
            return 0;
    }
    for (p = path;; p++) {
        if (*p == '/' || *p == '\0') {
            size_t n = (size_t)(p - seg);
            if (n == 0 || n > 255)
                return 0;
            if ((n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.'))
                return 0;
            if (seg == path && n == strlen(MY_META_DIR) && strncmp(seg, MY_META_DIR, n) == 0)
                return 0;
            if (*p == '\0')
                break;
            seg = p + 1;
        }
    }
    return 1;
}

char *my_join(const char *root, const char *rel)
{
    size_t n = strlen(root) + strlen(rel) + 2;
    char *p = malloc(n);

    if (p)
        snprintf(p, n, "%s/%s", root, rel);
    return p;
}

int my_is_dir(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* mkdir -p */
int my_mkdirs(const char *path, int include_last)
{
    char *tmp = strdup(path);
    char *p;
    int rc = MY_OK;

    if (!tmp)
        return MY_ERR_NOMEM;
    for (p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
            rc = MY_ERR_IO;
        *p = '/';
    }
    if (include_last && mkdir(tmp, 0755) < 0 && errno != EEXIST)
        rc = MY_ERR_IO;
    free(tmp);
    return rc;
}

int my_read_file(const char *path, uint8_t **buf, size_t *len)
{
    struct stat st;
    FILE *f;
    uint8_t *p;

    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode))
        return MY_ERR_NOTFOUND;
    if ((uint64_t)st.st_size > MAX_FILE_SIZE)
        return MY_ERR_TOOBIG;
    f = fopen(path, "rb");
    if (!f)
        return MY_ERR_IO;
    p = malloc(st.st_size ? (size_t)st.st_size : 1);
    if (!p) {
        fclose(f);
        return MY_ERR_NOMEM;
    }
    if (fread(p, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
        free(p);
        fclose(f);
        return MY_ERR_IO;
    }
    fclose(f);
    *buf = p;
    *len = (size_t)st.st_size;
    return MY_OK;
}

/* temp file + rename */
int my_write_atomic(const char *path, const void *buf, size_t len)
{
    size_t n = strlen(path) + sizeof MY_TMP_SUFFIX;
    char *tmp = malloc(n);
    FILE *f;
    int ok;

    if (!tmp)
        return MY_ERR_NOMEM;
    snprintf(tmp, n, "%s%s", path, MY_TMP_SUFFIX);
    f = fopen(tmp, "wb");
    if (!f) {
        free(tmp);
        return MY_ERR_IO;
    }
    ok = fwrite(buf, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) < 0) {
        remove(tmp);
        free(tmp);
        return MY_ERR_IO;
    }
    free(tmp);
    return MY_OK;
}
