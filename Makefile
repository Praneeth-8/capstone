CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -Iinclude
ASAN = -Wall -Wextra -std=c99 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Iinclude
SRC = src/net.c src/protocol.c src/hash.c src/delta.c src/fsutil.c src/manifest.c src/log.c src/discovery.c src/server.c src/client.c
OBJ = $(SRC:src/%.c=build/%.o)
TESTS = test_protocol test_hash test_delta test_path test_manifest test_discovery

all: lib examples tests

lib: build/libFsynC.a

build/%.o: src/%.c include/FsynC.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/libFsynC.a: $(OBJ)
	ar rcs $@ $(OBJ)

examples: FsynC_server FsynC_client

FsynC_server: examples/FsynC_server.c build/libFsynC.a
	$(CC) $(CFLAGS) $< build/libFsynC.a -o $@

FsynC_client: examples/FsynC_client.c build/libFsynC.a
	$(CC) $(CFLAGS) $< build/libFsynC.a -o $@

tests: $(TESTS:%=build/%) build/evil_client

build/%: tests/%.c build/libFsynC.a
	$(CC) $(CFLAGS) $< build/libFsynC.a -o $@

test: all
	@for t in $(TESTS); do ./build/$$t || exit 1; done
	./tests/run_all.sh

asan:
	./build.sh asan
	@for t in $(TESTS); do ./build/asan/$$t || exit 1; done
	BIN=build/asan TOOLS=build/asan ./tests/run_all.sh

clean:
	rm -rf build FsynC_server FsynC_client

.PHONY: all lib examples tests test asan clean
