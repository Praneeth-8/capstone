#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

static int usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s <ip> <port> <dir> [once|watch]\n", argv0);
    fprintf(stderr, "       %s discover [udp_port] [timeout_ms]\n", argv0);
    fprintf(stderr, "       %s discover-connect <name> <dir> once|watch\n", argv0);
    return 1;
}

static int run_client(const char *host, int port, const char *dir, const char *mode)
{
    struct FsynC_client *c;
    const struct FsynC_stats *st;
    int err;

    c = FsynC_client_create(host, port, dir, &err);
    if (!c) {
        fprintf(stderr, "client: %s\n", FsynC_strerror(err));
        return 1;
    }
    printf("client %s syncing %s\n", FsynC_client_id(c), dir);
    fflush(stdout);
    if (strcmp(mode, "watch") == 0)
        err = FsynC_client_watch(c, &stop_flag);
    else
        err = FsynC_client_sync(c);
    /* summary */
    st = FsynC_client_stats(c);
    printf("done: %s | pushed %llu, pulled %llu, deleted %llu, conflicts %llu | sent %llu of %llu bytes\n",
           FsynC_strerror(err), (unsigned long long)st->pushed, (unsigned long long)st->pulled,
           (unsigned long long)st->deleted, (unsigned long long)st->conflicts,
           (unsigned long long)st->bytes_sent, (unsigned long long)st->bytes_full);
    FsynC_client_destroy(c);
    return err < 0 ? 1 : 0;
}

static int do_discover(int argc, char **argv)
{
    struct FsynC_found found[32];
    size_t n = 0, i;
    int port = 0, timeout = 0, err;

    if (argc > 4)
        return usage(argv[0]);
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid discovery port\n");
            return 1;
        }
    }
    if (argc == 4) {
        timeout = atoi(argv[3]);
        if (timeout <= 0) {
            fprintf(stderr, "Invalid timeout\n");
            return 1;
        }
    }
    err = FsynC_discover(port, timeout, found, 32, &n);
    if (err < 0) {
        fprintf(stderr, "discover: %s\n", FsynC_strerror(err));
        return 1;
    }
    printf("# | name | IP | TCP port\n");
    for (i = 0; i < n; i++)
        printf("%d | %s | %s | %d\n", (int)i + 1, found[i].name, found[i].ip, found[i].tcp_port);
    for (i = 0; i < n; i++)
        printf("%s %s %d <dir> once|watch\n", argv[0], found[i].ip, found[i].tcp_port);
    if (n == 0)
        printf("no servers found\n");
    fflush(stdout);
    return n == 0 ? 1 : 0;
}

static int do_discover_connect(int argc, char **argv)
{
    struct FsynC_found found[32];
    size_t n = 0, i;
    int err;

    if (argc != 5 || (strcmp(argv[4], "once") && strcmp(argv[4], "watch")))
        return usage(argv[0]);
    err = FsynC_discover(0, 1000, found, 32, &n);
    if (err < 0) {
        fprintf(stderr, "discover: %s\n", FsynC_strerror(err));
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(found[i].name, argv[2]) == 0)
            return run_client(found[i].ip, found[i].tcp_port, argv[3], argv[4]);
    }
    fprintf(stderr, "no server named %s\n", argv[2]);
    return 1;
}

int main(int argc, char *argv[])
{
    const char *mode;
    int port;

    /* signals */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    if (argc >= 2 && strcmp(argv[1], "discover") == 0)
        return do_discover(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "discover-connect") == 0)
        return do_discover_connect(argc, argv);
    mode = argc > 4 ? argv[4] : "once";
    if (argc < 4 || (strcmp(mode, "once") && strcmp(mode, "watch")))
        return usage(argv[0]);
    port = atoi(argv[2]);
    return run_client(argv[1], port, argv[3], mode);
}
