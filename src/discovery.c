#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* request is exactly FSYNC_DISCOVER v1.
   reply is name_len, name, folder_len, folder, tcp port u16 BE. */

int FsynC_discover_label_ok(const char *s, size_t max)
{
    size_t n, i;

    if (!s)
        return 0;
    n = strlen(s);
    if (n == 0 || n > max)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7e)
            return 0;
    }
    return 1;
}

int FsynC_discover_encode_request(uint8_t *buf, size_t cap)
{
    if (!buf || cap < FSYNC_DISCOVER_MAGIC_LEN)
        return MY_ERR_ARG;
    memcpy(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN);
    return FSYNC_DISCOVER_MAGIC_LEN;
}

int FsynC_discover_check_request(const uint8_t *buf, size_t len)
{
    if (!buf || len != FSYNC_DISCOVER_MAGIC_LEN)
        return 0;
    return memcmp(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN) == 0;
}

static void put_u16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static unsigned get_u16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

int FsynC_discover_encode_reply(uint8_t *buf, size_t cap, const char *name,
                                const char *folder, unsigned tcp_port)
{
    size_t nl, fl, total;
    size_t off;

    if (!buf || !name || !folder)
        return MY_ERR_ARG;
    if (tcp_port == 0 || tcp_port > 65535)
        return MY_ERR_ARG;
    if (!FsynC_discover_label_ok(name, FSYNC_DISCOVER_NAME_MAX) ||
        !FsynC_discover_label_ok(folder, FSYNC_DISCOVER_FOLDER_MAX))
        return MY_ERR_ARG;
    nl = strlen(name);
    fl = strlen(folder);
    total = FSYNC_DISCOVER_MAGIC_LEN + 1 + nl + 1 + fl + 2;
    if (cap < total)
        return MY_ERR_ARG;
    memcpy(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN);
    off = FSYNC_DISCOVER_MAGIC_LEN;
    buf[off++] = (uint8_t)nl;
    memcpy(buf + off, name, nl);
    off += nl;
    buf[off++] = (uint8_t)fl;
    memcpy(buf + off, folder, fl);
    off += fl;
    put_u16(buf + off, tcp_port);
    return (int)total;
}

int FsynC_discover_parse_reply(const uint8_t *buf, size_t len, char *name, size_t name_sz,
                               char *folder, size_t folder_sz, unsigned *tcp_port)
{
    char nbuf[FSYNC_DISCOVER_NAME_MAX + 1];
    char fbuf[FSYNC_DISCOVER_FOLDER_MAX + 1];
    size_t off, nl, fl;
    unsigned port;

    if (!buf || !name || !folder || !tcp_port)
        return MY_ERR_ARG;
    if (len < FSYNC_DISCOVER_MAGIC_LEN + 1 + 1 + 2)
        return MY_ERR_PROTO;
    if (memcmp(buf, FSYNC_DISCOVER_MAGIC, FSYNC_DISCOVER_MAGIC_LEN) != 0)
        return MY_ERR_PROTO;
    off = FSYNC_DISCOVER_MAGIC_LEN;
    nl = buf[off++];
    if (nl == 0 || nl > FSYNC_DISCOVER_NAME_MAX)
        return MY_ERR_PROTO;
    if (len < off + nl + 1)
        return MY_ERR_PROTO;
    memcpy(nbuf, buf + off, nl);
    nbuf[nl] = 0;
    off += nl;
    if (!FsynC_discover_label_ok(nbuf, FSYNC_DISCOVER_NAME_MAX))
        return MY_ERR_PROTO;
    fl = buf[off++];
    if (fl == 0 || fl > FSYNC_DISCOVER_FOLDER_MAX)
        return MY_ERR_PROTO;
    if (len < off + fl + 2)
        return MY_ERR_PROTO;
    memcpy(fbuf, buf + off, fl);
    fbuf[fl] = 0;
    off += fl;
    if (!FsynC_discover_label_ok(fbuf, FSYNC_DISCOVER_FOLDER_MAX))
        return MY_ERR_PROTO;
    if (len != off + 2)
        return MY_ERR_PROTO;
    port = get_u16(buf + off);
    if (port == 0 || port > 65535)
        return MY_ERR_PROTO;
    if (name_sz < nl + 1 || folder_sz < fl + 1)
        return MY_ERR_ARG;
    memcpy(name, nbuf, nl + 1);
    memcpy(folder, fbuf, fl + 1);
    *tcp_port = port;
    return MY_OK;
}

static int send_to(int fd, struct in_addr addr, int port, const void *buf, size_t len)
{
    struct sockaddr_in to;

    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    to.sin_addr = addr;
    if (sendto(fd, buf, len, 0, (struct sockaddr *)&to, sizeof to) < 0)
        return -1;
    return 0;
}

static int ms_since(const struct timeval *start)
{
    struct timeval now;

    gettimeofday(&now, NULL);
    return (int)((now.tv_sec - start->tv_sec) * 1000 + (now.tv_usec - start->tv_usec) / 1000);
}

static int seen_addr(struct in_addr *seen, int n, struct in_addr addr)
{
    int i;

    for (i = 0; i < n; i++)
        if (seen[i].s_addr == addr.s_addr)
            return 1;
    return 0;
}

static void remember(struct in_addr *seen, int *n, struct in_addr addr)
{
    if (*n >= 64 || seen_addr(seen, *n, addr))
        return;
    seen[(*n)++] = addr;
}

static int same_peer(const struct FsynC_found *a, const char *ip, int port)
{
    return a->tcp_port == port && strcmp(a->ip, ip) == 0;
}

int FsynC_discover(int udp_port, int timeout_ms, struct FsynC_found *out, size_t cap,
                   size_t *count)
{
    int fd = -1, opt = 1, req_len;
    uint8_t req[FSYNC_DISCOVER_MAGIC_LEN];
    struct sockaddr_in me;
    struct in_addr seen[64];
    struct in_addr dest;
    struct ifaddrs *ifs = NULL, *ifa;
    struct timeval start;
    int nseen = 0;
    int rc = MY_ERR_NET;

    if (!count || udp_port < 0 || udp_port > 65535 || timeout_ms < 0 || (cap && !out))
        return MY_ERR_ARG;
    *count = 0;
    if (udp_port == 0)
        udp_port = FSYNC_DISCOVER_PORT;
    if (timeout_ms == 0)
        timeout_ms = 1000;
    req_len = FsynC_discover_encode_request(req, sizeof req);
    if (req_len < 0)
        return req_len;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return MY_ERR_NET;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof opt) < 0)
        goto done;
    memset(&me, 0, sizeof me);
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&me, sizeof me) < 0)
        goto done;

    dest.s_addr = htonl(INADDR_BROADCAST);
    send_to(fd, dest, udp_port, req, (size_t)req_len);
    remember(seen, &nseen, dest);
    dest.s_addr = htonl(INADDR_LOOPBACK);
    send_to(fd, dest, udp_port, req, (size_t)req_len);
    remember(seen, &nseen, dest);
    if (getifaddrs(&ifs) == 0) {
        for (ifa = ifs; ifa; ifa = ifa->ifa_next) {
            struct sockaddr_in *b;
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_BROADCAST))
                continue;
            if (!ifa->ifa_broadaddr || ifa->ifa_broadaddr->sa_family != AF_INET)
                continue;
            b = (struct sockaddr_in *)ifa->ifa_broadaddr;
            if (b->sin_addr.s_addr == 0 || seen_addr(seen, nseen, b->sin_addr))
                continue;
            send_to(fd, b->sin_addr, udp_port, req, (size_t)req_len);
            remember(seen, &nseen, b->sin_addr);
        }
    }

    gettimeofday(&start, NULL);
    for (;;) {
        struct pollfd p;
        int left = timeout_ms - ms_since(&start);
        int pr;

        if (left <= 0)
            break;
        p.fd = fd;
        p.events = POLLIN;
        pr = poll(&p, 1, left);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            goto done;
        }
        if (pr == 0)
            break;
        for (;;) {
            uint8_t buf[2048];
            struct sockaddr_storage ss;
            socklen_t sl = sizeof ss;
            ssize_t n = recvfrom(fd, buf, sizeof buf, MSG_DONTWAIT, (struct sockaddr *)&ss, &sl);
            struct sockaddr_in *from;
            char ip[16];
            char name[FSYNC_DISCOVER_NAME_MAX + 1];
            char folder[FSYNC_DISCOVER_FOLDER_MAX + 1];
            unsigned tcp = 0;
            size_t i;

            if (n < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if ((size_t)n >= sizeof buf || ss.ss_family != AF_INET)
                continue;
            from = (struct sockaddr_in *)&ss;
            if (!inet_ntop(AF_INET, &from->sin_addr, ip, sizeof ip))
                continue;
            if (FsynC_discover_parse_reply(buf, (size_t)n, name, sizeof name, folder, sizeof folder,
                                           &tcp) != MY_OK)
                continue;
            for (i = 0; i < *count; i++)
                if (same_peer(&out[i], ip, (int)tcp))
                    break;
            if (i < *count || *count >= cap)
                continue;
            memset(&out[*count], 0, sizeof out[*count]);
            memcpy(out[*count].name, name, strlen(name) + 1);
            memcpy(out[*count].folder, folder, strlen(folder) + 1);
            memcpy(out[*count].ip, ip, strlen(ip) + 1);
            out[*count].tcp_port = (int)tcp;
            (*count)++;
        }
    }
    rc = MY_OK;
done:
    if (ifs)
        freeifaddrs(ifs);
    if (fd >= 0)
        close(fd);
    return rc;
}
