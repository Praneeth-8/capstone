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
#include <errno.h>
#include "net.h"
#include "protocol.h"
#include <time.h>

#define BUF_SIZE 4096
#define MAX_FILE_SIZE 104857600
#define RES_ERROR -1



static void log_action(const char *ip,
                       const char *action,
                       const char *name,
                       const char *result,
                       uint64_t size)
{
    FILE *log = fopen("server.log", "a");

    if (log == NULL) {
        perror("server.log");
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    char timestamp[32] = "unknown-time";

    if (now != (time_t)-1 &&
        localtime_r(&now, &tm_now) != NULL) {
        strftime(timestamp, sizeof timestamp,
                 "%Y-%m-%d %H:%M:%S", &tm_now);
    }

    fprintf(log,
            "%s | IP: %s | Action: %s | File: %s | "
            "Result: %s | Size: %llu bytes\n",
            timestamp, ip, action, name, result,
            (unsigned long long)size);

    fclose(log);
}
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
 
    if (size > MAX_FILE_SIZE) return RES_ERROR;
    snprintf(tmp_name, sizeof tmp_name, "%s.part", name);
 
    FILE *f = fopen(tmp_name, "wb");
    if (!f) return RES_ERROR;
 
    uint64_t remaining = size;
    while (remaining > 0) {
        size_t chunk = remaining < BUF_SIZE ? (size_t)remaining : BUF_SIZE;
        if (recv_all(fd, buffer, chunk) < 0 ||
            fwrite(buffer, 1, chunk, f) != chunk) {
            fclose(f);
            remove(tmp_name);
            return RES_ERROR;
        }
        remaining -= chunk;
    }
    if (fclose(f) != 0) {
        remove(tmp_name);
        return RES_ERROR;
    }
 
    /* already have the same file? then drop the upload */
    if (access(name, F_OK) == 0 && files_identical(tmp_name, name)) {
        remove(tmp_name);
        return RES_UNCHANGED;
    }
 
    if (rename(tmp_name, name) < 0) {
        remove(tmp_name);
        return RES_ERROR;
    }
    return RES_STORED;
}

static int handle_delete(int fd, const struct header *h,const char *client_ip){
	char raw[MAX_NAME_LEN + 1];
	char name[MAX_NAME_LEN + 1];
	if(h->name_len == 0||h->name_len>MAX_NAME_LEN) return RES_ERROR;
	if(recv_all(fd, raw, h->name_len)<0) return RES_ERROR;
	raw[h->name_len] = '\0';

	if(safe_name(name,raw)<0) return RES_ERROR;
	
	if(remove(name)== 0){
		printf("File %s deleted successfully. \n", name);
		log_action(client_ip, "DELETE", name, "DELETED", 0);
		return RES_DELETED;
	}
	else if (errno == ENOENT){
		printf("File %s NOT FOUND.\n", name);
		log_action(client_ip, "DELETE", name, "NOT_FOUND", 0);
		return RES_NOT_FOUND;
	}
	else{
		perror("Error deleting file");
		return RES_ERROR;
	}

}


static int handle_file(int fd, const struct header *h,const char *client_ip)
{
    char raw[MAX_NAME_LEN + 1];    /* +1 for the '\0' we add ourselves */
    char name[MAX_NAME_LEN + 1];
 
    if (h->name_len == 0 || h->name_len > MAX_NAME_LEN) return RES_ERROR;
    if (recv_all(fd, raw, h->name_len) < 0) return RES_ERROR;
    raw[h->name_len] = '\0';       /* recv_all gives raw bytes, no terminator */
    if (safe_name(name, raw) < 0) return RES_ERROR;
 
    int r = recv_content(fd, name, h->file_size);
    if (r == RES_STORED){
        printf("stored %s (%llu bytes)\n", name, (unsigned long long)h->file_size);
				log_action(client_ip, "UPLOAD", name,
               "STORED", h->file_size);}
    else if (r == RES_UNCHANGED){
        printf("unchanged %s (already up to date)\n", name);
			log_action(client_ip, "UPLOAD", name,
               "UNCHANGED", h->file_size);
		}
				else{log_action(client_ip, "UPLOAD", name,
               "ERROR", h->file_size);}
    fflush(stdout);
    return r;
}


int handle_client(int fd,const char *client_ip) {
	uint8_t hdr[HEADER_SIZE];
	struct header h;
	if( recv_all(fd,hdr,HEADER_SIZE)<0){return RES_ERROR;}
	if(decode_header(hdr,&h)<0){return RES_ERROR;}

	switch(h.op){
	 case OP_SEND_FILE:
		return handle_file(fd,&h,client_ip);
	 case OP_DELETE:
		return handle_delete(fd,&h,client_ip);
	 default:
		return RES_ERROR;



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
			struct sockaddr_in client_addr;
			socklen_t client_len = sizeof(client_addr);
			
			int conn = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
			if (conn < 0) { perror("accept"); continue; }
			
			char client_ip[INET_ADDRSTRLEN];

			if (inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip)) == NULL) {
        snprintf(client_ip, sizeof(client_ip), "unknown");
			}

			int result = handle_client(conn,client_ip);
			if (result == RES_STORED){send_reply(conn, OP_ACK, 0);}
			else if (result == RES_UNCHANGED){ send_reply(conn, OP_ACK, 1);}
			else if(result == RES_DELETED){send_reply(conn,OP_ACK,2);}
			else {
            fprintf(stderr, "request failed\n");
            send_reply(conn, OP_ERROR, 0);
			}
      close(conn);               
    }
}

