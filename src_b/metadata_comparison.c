#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "net.h"

#define MAX_NAME 256

typedef struct {
    char name[MAX_NAME];
    uint32_t checksum;
    int64_t mtime;
    uint32_t mode;
} FileMeta;

int compute_checksum(const char *path, uint32_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t sum = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        sum = sum * 31 + (unsigned char)c;
    }
    fclose(f);
    *out = sum;
    return 0;
}

int get_file_metadata(const char *path, FileMeta *meta) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;
    memset(meta, 0, sizeof(FileMeta));
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(meta->name, base, MAX_NAME - 1);
    if (compute_checksum(path, &meta->checksum) < 0) return -1;
    meta->mtime = (int64_t)st.st_mtime;
    meta->mode = (uint32_t)st.st_mode;
    return 0;
}

int read_metadata_file(const char *path, FileMeta *meta) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(meta, sizeof(FileMeta), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    meta->name[MAX_NAME - 1] = '\0';
    return 0;
}

int compare_metadata(const FileMeta *a, const FileMeta *b) {
    if (a->checksum != b->checksum) return 1;
    if (a->mtime != b->mtime) return 1;
    if (strcmp(a->name, b->name) != 0) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <local_file> <remote_meta_file>\n", argv[0]);
        return 2;
    }
    FileMeta local, remote;
    if (get_file_metadata(argv[1], &local) < 0) {
        perror(argv[1]);
        return 2;
    }
    if (read_metadata_file(argv[2], &remote) < 0) {
        fprintf(stderr, "cannot read metadata file %s\n", argv[2]);
        return 2;
    }
    int diff = compare_metadata(&local, &remote);
    printf("%s\n", diff ? "DIFFERENT" : "SAME");
    return diff ? 1 : 0;
}
