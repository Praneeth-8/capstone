# Testing

```
./build.sh && for t in build/test_*; do $t; done && ./tests/run_all.sh
./build.sh asan && for t in build/asan/test_*; do $t; done && BIN=build/asan TOOLS=build/asan ./tests/run_all.sh
```
or `make test` and `make asan`. `run_all.sh` prints PASS or FAIL per scenario and a final
count, exits non-zero on failure, uses temp folders and a random port, and stops every process
it started.

## Unit tests

- `test_protocol`: header round trip; invalid op, empty name, oversized name and payload; integer
  helpers; framed message over a socket pair, empty payload, garbage header, truncated body.
- `test_hash`: SHA-256 of "", "abc", the 448-bit NIST message and one million "a"; incremental
  hashing; hex round trip; rolling checksum equals a direct sum at every offset.
- `test_delta`: identical file (13 bytes of delta), 1-byte edit, insert at start, delete at start,
  append, truncate, empty base, empty result, both empty, tiny files, unrelated data, corrupt
  deltas, 60 MB file with a small edit.
- `test_path`: accepted names; rejected NULL, empty, absolute, `..`, `.`, `//`, trailing `/`,
  backslash, newline, tab, `.FsynC`, temp suffix, over-long; mkdir -p, atomic write, empty
  file, missing file.
- `test_manifest`: scan with nested and empty dirs and ignored entries; encode/decode; invalid
  manifests; diff added/modified/removed; put/find/remove; save/load.
- `test_discovery`: request round trip; wrong magic, short, empty and trailing-byte requests;
  reply round trip including the maximum name and folder; buffer too small; empty labels, a
  control character, an over-long label, a truncated reply, a trailing byte, a `name_len` that
  runs past the datagram, and TCP port 0.

## Integration test (`tests/run_all.sh`)

One server, three `watch` clients (A, B, C) and two `once` clients (X, Y):

- UDP discovery: `discover` lists the server started by this script (name, source IP, TCP port)
  and prints a connect command; the server log contains `DISCOVER`; a datagram that is not the
  magic leaves the server running
- propagation of create, modify, delete for files and dirs, empty file, empty dir, binary file
- no sync ping-pong once changes settle, all copies equal the server
- offline client catches up with new, modified and deleted entries and uploads its own file
- server restart keeps versions (no false conflicts)
- concurrent edits: first writer kept, loser saved as conflict copy, synced, logged
- identical concurrent edits are not a conflict; delete vs edit keeps the edit; rmdir of a dir
  that gained a file elsewhere is refused
- delta byte counts for 1-byte edit, insert at start, append, truncation, touched-only file
  (0 bytes), pull-side delta, 50 MB file with a small edit
- 100 MB cap on client and server, path traversal, garbage, truncated header, giant length,
  request before HELLO, bad SHA-256, corrupt delta, client dying mid-transfer; the server stays up
  and normal clients keep working
- no sanitizer report in server or client output (meaningful for the ASAN run)
