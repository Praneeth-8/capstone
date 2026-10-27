# Changes: src_b compared to src

`src` is the original code and was not modified. `src_b` contains the fixed and extended version.

## 1. New files in src_b (not in src)

| File | Description |
|---|---|
| `receive_b.c`, `receive_b` | New server source and binary. It replaces `receive.c`, and handles file, metadata and sync requests. |
| `send_b.c`, `send_b` | New basic file client source and binary. It replaces `send.c`. |
| `README.md` | Full project documentation for `src_b`. |
| `changes.md` | This file. |

The `metadata_transfer`, `file_modification` and `metadata_comparison` programs exist in both folders, but the `src_b` versions are rewritten (see section 4).

## 2. New folders in src_b

| Folder | Description |
|---|---|
| `test_run/` | Output of the test session. It holds `client/` (test files such as `testfile.txt`, `big.bin`, `other.txt`), `server/` (files the server stored, including `.meta` records), `server.log`, `server.err` and `server.pid`. It can be deleted. |

## 3. Files in src that are not in src_b

These were deliberately not carried over:

- `receive.c` and `send.c`: replaced by `receive_b.c` and `send_b.c`.
- `test_net.c`: the original network helper test, not part of this work.
- `prtocl/`: the original protocol folder (`protocol.c`, `protocol.h`, `protocol_test.c`), not used by `src_b`.
- Old binaries (`receive` and others built in `src`).

## 4. Rewritten source files

### receive_b.c (replaces receive.c)

- New wire protocol: a 4-byte request type (file, metadata or sync), with every length and size as a 4-byte network-order integer.
- Handles three request types instead of only plain text. For metadata it stores `<name>.meta`. For sync it replies with its own `FileMeta` and optionally receives the content.
- Fixes the filename problem: the name length is received first, checked (not 0, below 256) and then read into a 256-byte buffer. The original read 1023 bytes into a 30-byte buffer.
- Sanitizes names: keeps only the part after the last `/` and rejects empty names, `.` and `..`. A name such as `../../evil.txt` is stored as `evil.txt` in the server folder.
- Reads content in 4096-byte chunks, limited to the declared size, with a 100 MB cap. The original read with a fixed buffer size and added a newline after every chunk.
- Deletes partially written files if a transfer fails.
- Sets the stored file's mtime to the client's after a sync, so repeat syncs report no change.
- Opens files in `wb` (overwrite). The original used `a` (append).
- Checks `argc`, the port, and the return values of `socket`, `bind`, `listen`, `accept` and `fopen`. The original ignored several of these.
- Loops and keeps serving requests. The original accepted one connection and exited.
- Sends a 4-byte status (0 success, 1 failure) to the client after each request.

### send_b.c (replaces send.c)

- Sends the new protocol: type, name length, name, size, then the content.
- Sends only the basename, so directory paths are not leaked or rejected.
- Sends the content in 4096-byte chunks with `fread` and `send_all`, instead of line by line with `getline`, so binary files and empty files work.
- Checks `argc` (the original read `argv[3]` without checking), the port, the file open, the address, `connect`, and every send.
- Reads the server's status and returns exit 1 on failure with `transfer failed`.

### metadata_transfer.c

- `argc` check fixed: `argc < 4` (the original tested `argc < 3` but used `argv[3]`).
- Sends the request type before the `FileMeta`, so the new server can recognize it.
- Waits for the server's status and fails if it is not 0. The original closed the socket right after sending and never knew whether it worked.
- Validates the port and the address (`inet_pton` result).
- `compute_checksum` now returns an error if the file cannot be opened. The original returned 0, which looks like a valid checksum.
- `get_file_metadata` rejects non-regular files and stores only the basename in `name`.

### file_modification.c

- Sends a sync request type, then the local `FileMeta`, then reads the server's `FileMeta`.
- Sends an explicit 0 or 1 flag telling the server whether content follows. The original compared and then either sent content or not, so the server could not tell.
- `transfer_file_content` now sends the size first, then reads exactly that many bytes. Failed reads and sends are checked.
- Reads the server's final status and prints `TRANSFERRED` or `UNCHANGED`.
- Same fixes as `metadata_transfer.c`: checked `connect`, address and port validation, checksum errors reported, regular files only, basename only.
- The `compare_metadata` rule is unchanged: checksum or mtime differs.

### metadata_comparison.c

- Interface changed: `<local_file> <remote_meta_file>`. The original took two raw `FileMeta` files, which nothing in the project could create.
- Computes the local metadata with the same `get_file_metadata` as the other programs.
- Reads the stored `FileMeta` with a helper that closes the file on every path. The original leaked file handles on early returns and exited 1 without any message.
- Null-terminates the name read from the file before comparing, so a damaged file cannot cause an over-read.
- Exit codes: 0 `SAME`, 1 `DIFFERENT`, 2 for errors (usage, unreadable file, truncated metadata). The original used 1 for both `DIFFERENT` and errors.
- The comparison rule is unchanged: checksum, mtime and name.

## 5. Files copied unchanged

- `net.c` and `net.h`: byte-identical to the originals in `src` (checked with `cmp`). They provide `send_all` and `recv_all`.

## 6. Kept the same

- The `FileMeta` struct (`name[256]`, `checksum`, `mtime`, `mode`) and its raw-struct transfer.
- The checksum algorithm (`sum * 31 + byte`).
- The coding style: plain C, simple functions, same function and variable names. The new sources contain no comments.

## 7. Documentation and test artifacts

- `README.md`: overview, protocol, file-by-file explanation, build and run instructions, test results and limitations.
- `test_run/`: the artifacts from the test session described in the README (see section 2).
- All five programs were compiled with `gcc -Wall -Wextra ... net.c` with no warnings, and tested end to end against the new server on port 8090.
