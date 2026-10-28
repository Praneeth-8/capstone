#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "net.h"
#include "protocol.h"

//#define MAX_NAME 256
#define BUF_SIZE 4096
#define MAX_FILE_SIZE 104857600
//#define TYPE_META 2
//#define TYPE_SYNC 3

/** typedef struct {
    char name[MAX_NAME];
    uint32_t checksum;
    int64_t mtime;
    uint32_t mode;
} FileMeta;**/

#define RESULT_STORED 0
#define RESULT_UNCHANGED 1
#define RESULT_ERROR -1

int compute_checksum(const char *path, uint32_t *out) {
		unsigned char buf[BUF_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t sum = 0;
    size_t n;
    while ((n = fread(buf,1,sizeof buf,f)) >0) {
        for (size_t i =0; i<n;i++){
			sum = sum*31 +buf[i];
		}
		}
		int err = ferror(f);
    
    fclose(f);
		if(err) return -1;
    *out = sum;
    return 0;
}

static int files_identical(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) < 0 || stat(b, &sb) < 0) return 0;
    if (sa.st_size != sb.st_size) return 0;
    uint32_t ca, cb;
    if (compute_checksum(a, &ca) < 0 || compute_checksum(b, &cb) < 0) return 0;
    return ca == cb;
}

/**int send_metadata(int fd, const FileMeta *meta) {
    return send_all(fd, meta, sizeof(FileMeta));
}

int recv_metadata(int fd, FileMeta *meta) {
    return recv_all(fd, meta, sizeof(FileMeta));
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
}**/

int send_u32(int fd, uint32_t value) {
    uint32_t net_value = htonl(value);
    return send_all(fd, &net_value, sizeof(net_value));
}

int recv_u32(int fd, uint32_t *value) {
    uint32_t net_value;
    if (recv_all(fd, &net_value, sizeof(net_value)) < 0) return -1;
    *value = ntohl(net_value);
    return 0;
}

int safe_name(char *dst, const char *src) {
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    if (base[0] == '\0') return -1;
    if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0) return -1;
    if (strlen(base) >= MAX_NAME_LEN) return -1;
    strcpy(dst, base);
    return 0;
}

static int recv_content(int fd, const char *name, uint64_t size)
{
    char tmp_name[MAX_NAME_LEN + 8];
    char buffer[BUF_SIZE];
 
    if (size > MAX_FILE_SIZE) return RESULT_ERROR;
    snprintf(tmp_name, sizeof tmp_name, "%s.part", name);
 
    FILE *f = fopen(tmp_name, "wb");
    if (!f) return RESULT_ERROR;
 
    uint64_t remaining = size;
    while (remaining > 0) {
        size_t chunk = remaining < BUF_SIZE ? (size_t)remaining : BUF_SIZE;
        if (recv_all(fd, buffer, chunk) < 0 ||
            fwrite(buffer, 1, chunk, f) != chunk) {
            fclose(f);
            remove(tmp_name);
            return RESULT_ERROR;
        }
        remaining -= chunk;
    }
    if (fclose(f) != 0) {
        remove(tmp_name);
        return RESULT_ERROR;
    }
 
    /* already have the same file? then drop the upload */
    if (access(name, F_OK) == 0 && files_identical(tmp_name, name)) {
        remove(tmp_name);
        return RESULT_UNCHANGED;
    }
 
    if (rename(tmp_name, name) < 0) {
        remove(tmp_name);
        return RESULT_ERROR;
    }
    return RESULT_STORED;
}

/**int save_metadata(const FileMeta *meta, const char *name) {
    char meta_path[MAX_NAME + 8];
    snprintf(meta_path, sizeof(meta_path), "%s.meta", name);
    FILE *fptr = fopen(meta_path, "wb");
    if (!fptr) return -1;
    if (fwrite(meta, sizeof(FileMeta), 1, fptr) != 1) {
        fclose(fptr);
        return -1;
    }
    if (fclose(fptr) != 0) return -1;
    printf("stored metadata %s (checksum %u, mtime %lld)\n", meta_path, meta->checksum, (long long)meta->mtime);
    fflush(stdout);
    return 0;
}**/

/**int handle_meta(int fd) {
    FileMeta meta;
    char name[MAX_NAME] = {0};
    if (recv_metadata(fd, &meta) < 0) return -1;
    meta.name[MAX_NAME - 1] = '\0';
    if (safe_name(name, meta.name) < 0) return -1;
    return save_metadata(&meta, name);
}**/

/**int handle_sync(int fd) {
    FileMeta remote, local;
    uint32_t send_flag;
    char name[MAX_NAME] = {0};
    if (recv_metadata(fd, &remote) < 0) return -1;
    remote.name[MAX_NAME - 1] = '\0';
    if (safe_name(name, remote.name) < 0) return -1;
    if (get_file_metadata(name, &local) < 0) {
        memset(&local, 0, sizeof(FileMeta));
        strncpy(local.name, name, MAX_NAME - 1);
    }
    if (send_metadata(fd, &local) < 0) return -1;
    if (recv_u32(fd, &send_flag) < 0) return -1;
    if (send_flag == 0) {
        printf("sync %s: unchanged\n", name);
        fflush(stdout);
    } else if (recv_content(fd, name, remote.mtime) < 0) {
        return -1;
    }
    return save_metadata(&remote, name);
}**/

static int handle_file(int fd, const struct header *h)
{
    char raw[MAX_NAME_LEN + 1];    /* +1 for the '\0' we add ourselves */
    char name[MAX_NAME_LEN + 1];
 
    if (h->name_len == 0 || h->name_len > MAX_NAME_LEN) return RESULT_ERROR;
    if (recv_all(fd, raw, h->name_len) < 0) return RESULT_ERROR;
    raw[h->name_len] = '\0';       /* recv_all gives raw bytes, no terminator */
    if (safe_name(name, raw) < 0) return RESULT_ERROR;
 
    int r = recv_content(fd, name, h->file_size);
    if (r == RESULT_STORED)
        printf("stored %s (%llu bytes)\n", name, (unsigned long long)h->file_size);
    else if (r == RESULT_UNCHANGED)
        printf("unchanged %s (already up to date)\n", name);
    fflush(stdout);
    return r;
}


int handle_client(int fd) {
	uint8_t hdr[HEADER_SIZE];
	struct header h;
	if( recv_all(fd,hdr,HEADER_SIZE)<0){return RESULT_ERROR;}
	if(decode_header(hdr,&h)<0){return RESULT_ERROR;}

	switch(h.op){
	 case OP_SEND_FILE:
		return handle_file(fd,&h);
	 default:
		return RESULT_ERROR;



	}
}
static int send_reply(int fd, uint8_t op, uint64_t code)
{
    struct header h = { .op = op, .name_len = 0, .file_size = code };
    uint8_t buf[HEADER_SIZE];
    encode_header(&h, buf);
    return send_all(fd, buf, HEADER_SIZE);
}



int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
 
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }
 
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
 
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
 
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen"); close(server_fd); return 1;
    }
    printf("listening on port %d\n", port);
    fflush(stdout);
 
    for (;;) {
        int conn = accept(server_fd, NULL, NULL);
        if (conn < 0) { perror("accept"); continue; }
 
        int result = handle_client(conn);
        if (result == RESULT_STORED)
            send_reply(conn, OP_ACK, 0);
        else if (result == RESULT_UNCHANGED)
            send_reply(conn, OP_ACK, 1);
        else {
            fprintf(stderr, "request failed\n");
            send_reply(conn, OP_ERROR, 0);
        }
        close(conn);               
    }
}

