#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>

static int fails, total;

#define CHECK(cond) do { total++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define DONE(name) (printf("%s: %d/%d passed\n", name, total - fails, total), fails ? 1 : 0)

#endif
