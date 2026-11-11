# Design

## Architecture

```
client A (watch)            server (poll loop)             client B (watch)
local dir + .FsynC/state  shared dir + .FsynC/index    local dir + .FsynC/state
   | scan every 1 s            | versions per entry           |
   | push changes  ----------> | apply, bump version  ------> | NOTIFY
   |                           |                              | pull (delta)
```

The core is the static library `libFsynC.a`. `examples/` only parse arguments and call it.

The server is one process with a `poll()` event loop. Each request is handled to completion
before the next, so the shared directory and the version table need no locks. Every TCP socket has
a 10 second send/receive timeout, so a stalled client can block the loop for at most 10 seconds
and is then dropped. Up to 64 clients. The same loop also watches a UDP discovery socket.

The client works on one TCP connection. In `watch` mode it waits up to 1 second for a server
NOTIFY, then scans its folder. Either event starts a reconcile. If the connection is lost, it
reconnects with backoff of 1, 2, 4 and 8 seconds.

## Protocol

Every message is the demo header followed by a name and a payload:

```
op (u8) | name_len (u16, big-endian) | payload_len (u64, big-endian) | name | payload
```

`decode_header` rejects unknown ops, names over 4096 bytes, missing names and payloads over
100 MB + 1 MB. The server additionally validates every path.

| op | direction | name | payload |
|---|---|---|---|
| HELLO 5 | C to S | client id | none |
| LIST 6 | C to S | none | none |
| MANIFEST 7 | S to C | none | manifest text |
| SEND_FILE 1 | C to S | path | base version u64, sha256, content |
| SIGREQ 10 | C to S | path | none |
| SIGS 11 | S to C | none | block signatures |
| DELTA 12 | both | path | version u64, sha256 of the result, delta |
| GET 13 | C to S | path | signatures of the client's copy, or empty |
| FILE 14 | S to C | path | version u64, sha256, content |
| DELETE 2 / RMDIR 9 | C to S | path | base version u64 |
| MKDIR 8 | C to S | path | none |
| ACK 3 / ERROR 4 | S to C | none | result u8, version u64 |
| NOTIFY 15 | S to C | path | none |

Results: STORED, UNCHANGED, DELETED, NOT_FOUND, ERROR, CONFLICT, CREATED.

## Discovery

Finding a server does not use the TCP framing above. The server binds a UDP socket (port 9999
unless `FsynC_server_set_discovery` sets another port before `run`) and puts it in the `poll()`
set next to the listening socket. The socket is non-blocking. One wakeup reads at most 32
datagrams, so a flood of probes cannot crowd out file transfers.

A datagram is a request only when its length and bytes are exactly `FSYNC_DISCOVER v1`
(17 bytes). Any other length, a wrong magic, or a truncated buffer is ignored. The server does
not answer and does not close the socket. A valid request is logged with action `DISCOVER` and
gets one reply, sent back to the source address:

```
magic (17) | name_len (u8) | name | folder_len (u8) | folder | tcp_port (u16, big-endian)
```

The name and the folder are printable ASCII (0x20-0x7e), 1 to 63 and 1 to 255 bytes. The name
defaults to the hostname, truncated to 63 bytes, or `FsynC` if that name is unusable. The folder
is the last path component of the shared directory. `tcp_port` is the port the sync service is
already listening on, and it is never 0. Extra bytes, a zero port, an empty label, a control
character, or a length that runs past the datagram make `FsynC_discover_parse_reply` return
`MY_ERR_PROTO`.

The client enables `SO_BROADCAST` and sends that request to `255.255.255.255`, to each IPv4
interface broadcast address from `getifaddrs`, and to `127.0.0.1` so a server on the same machine
is found when the stack does not deliver a broadcast back to the sender. It collects replies until
the timeout (default 1 second). The IP it prints is the reply's source address, not a field inside
the packet. The same IP and TCP port are listed once.

## Data structures

- `struct my_entry`: path, type (`F` or `D`), size, mtime (ns), sha256, version.
- `struct my_manifest`: array of entries sorted by path, binary search lookup.
- Manifest text line: `F <size> <mtime> <version> <sha256 hex> <path>`. Used on the wire and in
  `.FsynC/state` (client) and `.FsynC/index` (server).
- Server: listening socket, UDP discovery socket, announced name and folder, manifest of the
  shared dir, global version counter, client table.
- Client: connection, saved state (last synced manifest), conflict list for one reconcile, stats.

## Change detection

A scan walks the folder (skipping `.FsynC/`, `*.FsynC-tmp`, symlinks and unsafe names). If
size and mtime match the previous manifest, the old SHA-256 is reused. Otherwise SHA-256 is
computed. Comparing the scan to the saved state gives ADDED, MODIFIED and REMOVED entries.

## Reconcile (client)

1. Scan, diff against the saved state.
2. Push local changes in order: type changes, mkdirs (parents first), files, deletes (children
   first). Each carries the version the client last saw.
3. LIST the server manifest and rescan.
4. For every server entry: same content locally means record it; local unpushed edit means keep
   it; otherwise pull it (mkdir or GET).
5. For every saved entry missing on the server: delete it locally if it is unchanged. A locally
   edited file is kept and pushed next time (edit wins).
6. Save the state.

Because the state is updated with the exact file written by a pull, the next scan sees no change,
so a file received from the network is never echoed back.

The saved state tells "deleted locally" (in state, not on disk) from "new on server" (not in state).

## Versions and conflicts (server)

Every entry has a version from a global counter that is persisted in `.FsynC/index`, so it
survives restarts and re-created files always get a new number.

- PUT or DELTA with base version equal to the server's: apply, new version, NOTIFY others.
- Same content as the server: UNCHANGED, never a conflict.
- Base version differs and content differs: the server copy stays. The upload is saved as
  `stem.conflict-<client>-<YYYYMMDD-HHMMSS>.ext`, logged as CONFLICT, and synced to everyone.
  The client then downloads the server copy.
- File deleted on the server and edited by a client: the upload is stored (edit wins).
- DELETE with an old base version: refused, the file stays, the client downloads it (edit wins).
- RMDIR only removes an empty directory. If another client added a file in it, the server
  answers CONFLICT and the directory comes back with that file. A client removes a directory
  tree by deleting its files first, then the directories deepest first.

## Delta algorithm

1. The side that has the old copy splits it into 2048-byte blocks and sends, per block, a 32-bit
   rolling checksum and the first 8 bytes of its SHA-256.
2. The side with the new copy slides a window over it. The rolling checksum updates in O(1) per
   byte (`a = sum of bytes`, `b = sum of weighted bytes`, both mod 2^16). Weak matches are looked
   up in a 65536-bucket hash table and confirmed with the strong hash.
3. Output is `C index count` (copy a run of old blocks) and `L length bytes` (literal) records.
4. The receiver rebuilds the file in memory, checks the full SHA-256 and writes it atomically.

If the delta is not smaller than the file, the whole file is sent. Pushes use SIGREQ + DELTA,
pulls send the client's signatures with GET and get DELTA back.

## Safety

- Every write goes to `path.FsynC-tmp`, is fsynced and renamed. On any failure the temp file
  is removed.
- Paths must be relative, without `.` or `..` segments, empty segments, backslashes, control
  characters, segments over 255 bytes or totals over 1024 bytes, and must not touch `.FsynC`.
- Uploads are checked against their SHA-256 before they are stored.
- `SIGPIPE` is ignored and sends use `MSG_NOSIGNAL`. The listening socket uses `SO_REUSEADDR`.
- A protocol error or broken connection drops only that client.
