#include "net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define BIG_SIZE (5u * 1024 * 1024)
#define SLOW_SIZE 64
#define SHORT_SENT 100
#define SHORT_WANT 200

static void fill_pattern(unsigned char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        b[i] = (unsigned char)(i % 251);
}

static int check_pattern(const unsigned char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (b[i] != (unsigned char)(i % 251))
            return -1;
    return 0;
}

/* Client behaviours */
static void client_big(int fd)
{
    unsigned char *b = malloc(BIG_SIZE);
    if (!b)
        return;
    fill_pattern(b, BIG_SIZE);
    if (send_all(fd, b, BIG_SIZE) < 0)
        perror("client_big send_all");
    free(b);
}

static void client_slow(int fd)
{
    for (size_t i = 0; i < SLOW_SIZE; i++) {
        unsigned char byte = (unsigned char)(i % 251);
        if (send_all(fd, &byte, 1) < 0) {
            perror("client_slow send_all");
            return;
        }
        usleep(20000); /* force short reads on the receiver */
    }
}

static void client_short(int fd)
{
    unsigned char b[SHORT_SENT];
    fill_pattern(b, sizeof b);
    send_all(fd, b, sizeof b);
    /* returns, then the caller closes the socket early */
}

/* Runs one test: forks a client, receives `want` bytes in the parent. */
static int run(const char *name, void (*client)(int), size_t want, int expect_ok)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* let the OS pick a free port */

    if (lfd < 0 || bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(lfd, 1) < 0) {
        perror("setup");
        return 1;
    }
    socklen_t alen = sizeof addr;
    getsockname(lfd, (struct sockaddr *)&addr, &alen);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        close(lfd);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
            perror("client connect");
            _exit(1);
        }
        client(fd);
        close(fd);
        _exit(0);
    }

    int conn = accept(lfd, NULL, NULL);
    close(lfd);
    if (conn < 0) {
        perror("accept");
        return 1;
    }

    unsigned char *buf = malloc(want);
    if (!buf)
        return 1;
    int r = recv_all(conn, buf, want);
    int ok = expect_ok ? (r == 0 && check_pattern(buf, want) == 0) : (r == -1);

    close(conn);
    free(buf);
    waitpid(pid, NULL, 0);

    printf("%-28s %s\n", name, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    int failures = 0;
    failures += run("big transfer (5 MB)", client_big, BIG_SIZE, 1);
    failures += run("slow sender (1 byte at a time)", client_slow, SLOW_SIZE, 1);
    failures += run("peer closes early", client_short, SHORT_WANT, 0);
    return failures ? 1 : 0;
}
