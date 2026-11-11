#define _DEFAULT_SOURCE
#include "FsynC.h"
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char longname[2000], tmpl[] = "/tmp/FsynC_path_XXXXXX", *root, *p;
    uint8_t *buf;
    size_t len;

    /* accepted */
    CHECK(my_path_valid("a.txt"));
    CHECK(my_path_valid("dir/sub/file.bin"));
    CHECK(my_path_valid("with space.txt"));
    CHECK(my_path_valid(".hidden"));
    CHECK(my_path_valid("...dots"));
    CHECK(my_path_valid("dir/.FsynC"));
    /* rejected */
    CHECK(!my_path_valid(NULL));
    CHECK(!my_path_valid(""));
    CHECK(!my_path_valid("/etc/passwd"));
    CHECK(!my_path_valid("../x"));
    CHECK(!my_path_valid("a/../../x"));
    CHECK(!my_path_valid("a/.."));
    CHECK(!my_path_valid("./a"));
    CHECK(!my_path_valid("a//b"));
    CHECK(!my_path_valid("a/"));
    CHECK(!my_path_valid("a\\b"));
    CHECK(!my_path_valid("a\nb"));
    CHECK(!my_path_valid("a\tb"));
    CHECK(!my_path_valid(".FsynC/state"));
    CHECK(!my_path_valid(".FsynC"));
    CHECK(!my_path_valid("x.FsynC-tmp"));
    memset(longname, 'a', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    CHECK(!my_path_valid(longname));
    longname[300] = '\0';
    CHECK(!my_path_valid(longname));
    /* fs helpers */
    root = mkdtemp(tmpl);
    CHECK(root != NULL);
    p = my_join(root, "x/y/z/f.txt");
    CHECK(my_mkdirs(p, 0) == MY_OK);
    CHECK(my_write_atomic(p, "hello", 5) == MY_OK);
    CHECK(my_read_file(p, &buf, &len) == MY_OK && len == 5 && memcmp(buf, "hello", 5) == 0);
    free(buf);
    CHECK(my_write_atomic(p, "", 0) == MY_OK);
    CHECK(my_read_file(p, &buf, &len) == MY_OK && len == 0);
    free(buf);
    CHECK(my_read_file("/nonexistent/zz", &buf, &len) == MY_ERR_NOTFOUND);
    CHECK(my_write_atomic("/nonexistent/zz", "a", 1) == MY_ERR_IO);
    unlink(p);
    free(p);
    p = my_join(root, "x/y/z");
    CHECK(my_is_dir(p));
    rmdir(p);
    free(p);
    p = my_join(root, "x/y");
    rmdir(p);
    free(p);
    p = my_join(root, "x");
    rmdir(p);
    free(p);
    CHECK(rmdir(root) == 0);
    return DONE("test_path");
}
