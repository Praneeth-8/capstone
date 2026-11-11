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

int main(int argc, char *argv[])
{
    struct FsynC_server *s;
    char logfile[4096];
    int err, port;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <dir> [logfile] [udp_port] [name]\n", argv[0]);
        return 1;
    }
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
    if (argc > 3)
        snprintf(logfile, sizeof logfile, "%s", argv[3]);
    else
        snprintf(logfile, sizeof logfile, "%s/../FsynC_server.log", argv[2]);
    /* signals */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    s = FsynC_server_create(port, argv[2], logfile, &err);
    if (!s) {
        fprintf(stderr, "server: %s\n", FsynC_strerror(err));
        return 1;
    }
    if (argc > 4) {
        int udp = atoi(argv[4]);
        const char *name = argc > 5 ? argv[5] : NULL;
        if (udp <= 0 || udp > 65535) {
            fprintf(stderr, "Invalid discovery port\n");
            FsynC_server_destroy(s);
            return 1;
        }
        err = FsynC_server_set_discovery(s, udp, name);
        if (err < 0) {
            fprintf(stderr, "server: %s\n", FsynC_strerror(err));
            FsynC_server_destroy(s);
            return 1;
        }
    }
    err = FsynC_server_run(s, &stop_flag);
    if (err < 0)
        fprintf(stderr, "server: %s\n", FsynC_strerror(err));
    FsynC_server_destroy(s);
    printf("server stopped\n");
    return err < 0 ? 1 : 0;
}
