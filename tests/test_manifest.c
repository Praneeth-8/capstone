#define _DEFAULT_SOURCE
#include "FsynC.h"
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put_file(const char *root, const char *rel, const char *text)
{
    char *p = my_join(root, rel);
    my_mkdirs(p, 0);
    my_write_atomic(p, text, strlen(text));
    free(p);
}

static int has(const struct my_change *ch, size_t n, const char *path, int kind)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (strcmp(ch[i].entry->path, path) == 0 && ch[i].kind == kind)
            return 1;
    return 0;
}

int main(void)
{
    char tmpl[] = "/tmp/FsynC_man_XXXXXX", cmd[256], *root = mkdtemp(tmpl), *p;
    struct my_manifest a, b, c;
    struct my_change *ch;
    struct my_entry e;
    uint8_t *buf;
    size_t len, n;

    /* scan tree */
    put_file(root, "a.txt", "one");
    put_file(root, "d/b.txt", "two");
    put_file(root, "d/e/empty", "");
    put_file(root, ".FsynC/state", "ignored");
    put_file(root, "skip.FsynC-tmp", "ignored");
    p = my_join(root, "emptydir");
    mkdir(p, 0755);
    free(p);
    CHECK(my_manifest_scan(root, NULL, &a) == MY_OK);
    CHECK(a.n == 6);
    CHECK(my_manifest_find(&a, "d/e/empty") && my_manifest_find(&a, "d/e/empty")->size == 0);
    CHECK(my_manifest_find(&a, "emptydir") && my_manifest_find(&a, "emptydir")->type == MY_DIR);
    CHECK(!my_manifest_find(&a, ".FsynC/state"));
    CHECK(!my_manifest_find(&a, "skip.FsynC-tmp"));
    /* encode decode */
    CHECK(my_manifest_encode(&a, &buf, &len) == MY_OK);
    CHECK(my_manifest_decode(buf, len, &b) == MY_OK && b.n == a.n);
    CHECK(memcmp(my_manifest_find(&b, "a.txt")->sha, my_manifest_find(&a, "a.txt")->sha, MY_SHA_LEN) == 0);
    free(buf);
    my_manifest_free(&b);
    CHECK(my_manifest_decode((const uint8_t *)"F 1 2 3 xx ../a\n", 16, &b) == MY_ERR_PROTO);
    CHECK(my_manifest_decode((const uint8_t *)"garbage", 7, &b) == MY_ERR_PROTO);
    CHECK(my_manifest_decode((const uint8_t *)"", 0, &b) == MY_OK && b.n == 0);
    /* diff */
    put_file(root, "a.txt", "one changed");
    put_file(root, "new.txt", "new");
    p = my_join(root, "d/b.txt");
    unlink(p);
    free(p);
    CHECK(my_manifest_scan(root, &a, &c) == MY_OK);
    CHECK(my_manifest_diff(&a, &c, &ch, &n) == MY_OK);
    CHECK(n == 3);
    CHECK(has(ch, n, "a.txt", MY_MODIFIED));
    CHECK(has(ch, n, "new.txt", MY_ADDED));
    CHECK(has(ch, n, "d/b.txt", MY_REMOVED));
    free(ch);
    CHECK(my_manifest_diff(&c, &c, &ch, &n) == MY_OK && n == 0);
    free(ch);
    /* put find remove */
    memset(&e, 0, sizeof e);
    e.path = "zzz";
    e.type = MY_FILE;
    e.version = 7;
    CHECK(my_manifest_put(&c, &e) == MY_OK && my_manifest_find(&c, "zzz")->version == 7);
    e.version = 8;
    CHECK(my_manifest_put(&c, &e) == MY_OK && my_manifest_find(&c, "zzz")->version == 8);
    CHECK(my_manifest_remove(&c, "zzz") == MY_OK && !my_manifest_find(&c, "zzz"));
    CHECK(my_manifest_remove(&c, "zzz") == MY_ERR_NOTFOUND);
    /* save load */
    p = my_join(root, "state");
    CHECK(my_manifest_save(p, &c) == MY_OK);
    CHECK(my_manifest_load(p, &b) == MY_OK && b.n == c.n);
    my_manifest_free(&b);
    free(p);
    my_manifest_free(&a);
    my_manifest_free(&c);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
    return DONE("test_manifest");
}
