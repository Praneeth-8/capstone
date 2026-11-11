#define _DEFAULT_SOURCE
#include "FsynC.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct my_log {
    FILE *f;
};

struct my_log *my_log_open(const char *file)
{
    struct my_log *log = calloc(1, sizeof *log);

    if (!log)
        return NULL;
    if (file) {
        log->f = fopen(file, "a");
        if (!log->f) {
            free(log);
            return NULL;
        }
    }
    return log;
}

void my_log_close(struct my_log *log)
{
    if (!log)
        return;
    if (log->f)
        fclose(log->f);
    free(log);
}

/* write one event */
void my_log_event(struct my_log *log, const char *ip, const char *client, const char *action,
                  const char *file, const char *result, uint64_t size, uint64_t sent)
{
    char ts[32] = "unknown-time";
    time_t now = time(NULL);
    struct tm tm_now;

    if (now != (time_t)-1 && localtime_r(&now, &tm_now))
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm_now);
    printf("%s | IP: %s | Client: %s | Action: %s | File: %s | Result: %s | Size: %llu bytes | Sent: %llu bytes\n",
           ts, ip, client, action, file, result, (unsigned long long)size, (unsigned long long)sent);
    fflush(stdout);
    if (log && log->f) {
        fprintf(log->f, "%s | IP: %s | Client: %s | Action: %s | File: %s | Result: %s | Size: %llu bytes | Sent: %llu bytes\n",
                ts, ip, client, action, file, result, (unsigned long long)size, (unsigned long long)sent);
        fflush(log->f);
    }
}
