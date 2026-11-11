#!/bin/bash
set -e
cd "$(dirname "$0")"
CFLAGS="-Wall -Wextra -std=c99 -O2 -Iinclude"
OUT=build
BIN=.
if [ "$1" = "asan" ]; then
    CFLAGS="-Wall -Wextra -std=c99 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Iinclude"
    OUT=build/asan
    BIN=build/asan
fi
mkdir -p $OUT
for f in net protocol hash delta fsutil manifest log discovery server client; do
    gcc $CFLAGS -c src/$f.c -o $OUT/$f.o
done
rm -f $OUT/libFsynC.a
ar rcs $OUT/libFsynC.a $OUT/*.o
gcc $CFLAGS examples/FsynC_server.c $OUT/libFsynC.a -o $BIN/FsynC_server
gcc $CFLAGS examples/FsynC_client.c $OUT/libFsynC.a -o $BIN/FsynC_client
for t in tests/test_*.c; do
    gcc $CFLAGS "$t" $OUT/libFsynC.a -o "$OUT/$(basename "$t" .c)"
done
gcc $CFLAGS tests/evil_client.c $OUT/libFsynC.a -o $OUT/evil_client
echo "build ok in $OUT"
