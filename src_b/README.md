# Capstone File Synchronization (src_b)

## 1. Project overview

`src_b` is a corrected version of the capstone file synchronization system in `../src`. It is a small client-server system written in C that can:

- send a file from a client to a server,
- upload a file's metadata (name, checksum, modification time, mode) to the server,
- synchronize a file conditionally: the content is only transferred if the server's copy differs from the client's,
- compare a local file against a stored metadata record.

The original code in `../src` had several problems, which this version fixes:

| Problem in the original | Fix in `src_b` |
|---|---|
| `send.c` sent the filename and the file content back to back with no separator, and `receive.c` tried to read a fixed 1023 bytes for the name into a 30-byte buffer. The name and content got mixed and the buffer could overflow. | Every message uses length prefixes. The filename length is sent first and checked against `MAX_NAME` before anything is read. |
| `receive.c` only understood plain text files and had no way to receive `FileMeta`. | `receive_b.c` handles three request types: file, metadata and sync. |
| `metadata_transfer.c` checked `argc < 3` but read `argv[3]`. | All programs check `argc` correctly. |
| No program checked return values for `socket`, `bind`, `listen`, `connect`, `fopen` and so on. | Every call is checked and failures give an error message and a non-zero exit code. |
| The client never learned whether the server succeeded. | The server answers every request with a 4-byte status code. |

The `FileMeta` struct is unchanged from the original. The code keeps the original style: plain C, simple functions, the same variable names, and no comments in the source files.

## 2. Folder structure

```
src_b/
├── README.md
├── net.c                  send_all / recv_all helpers (copied unchanged from ../src)
├── net.h                  declarations for the helpers
├── receive_b.c            the server
├── send_b.c               basic file transfer client
├── metadata_transfer.c    client: uploads a file's FileMeta
├── file_modification.c    client: sends content only if the server's copy differs
├── metadata_comparison.c  local tool: compares a file against a stored FileMeta
├── receive_b              compiled binaries
├── send_b
├── metadata_transfer
├── file_modification
├── metadata_comparison
└── test_run/              output of the test session (can be deleted)
    ├── client/            test files used by the clients
    ├── server/            files the server stored
    ├── server.log         server stdout
    └── server.err         server stderr
```

## 3. The protocol

All communication happens over one TCP connection per request. Every request starts with a 4-byte request type. Every integer that is not part of `FileMeta` is a 4-byte unsigned integer in network byte order (big endian), written with `htonl` and read with `ntohl`.

| Type value | Name | Used by |
|---|---|---|
| 1 | `TYPE_FILE` | `send_b` |
| 2 | `TYPE_META` | `metadata_transfer` |
| 3 | `TYPE_SYNC` | `file_modification` |

### The FileMeta struct

```c
typedef struct {
    char name[MAX_NAME];   /* 256 bytes: basename of the file, null terminated */
    uint32_t checksum;     /* simple rolling checksum of the content          */
    int64_t mtime;         /* modification time, seconds since the epoch      */
    uint32_t mode;         /* st_mode of the file                             */
} FileMeta;
```

On a 64-bit Linux build `sizeof(FileMeta)` is 280 bytes (the compiler adds padding). The struct is sent and stored as raw bytes. Padding bytes are zeroed because `get_file_metadata` clears the struct with `memset` before filling it.

The checksum is computed byte by byte as `sum = sum * 31 + byte` with a 32-bit unsigned sum.

### TYPE_FILE (basic file transfer)

```
client -> server:  type (4 bytes) = 1
                   name_len (4 bytes)
                   name (name_len bytes, no terminator)
                   size (4 bytes)
                   content (size bytes)
server -> client:  status (4 bytes), 0 = success, 1 = failure
```

### TYPE_META (metadata upload)

```
client -> server:  type (4 bytes) = 2
                   FileMeta (sizeof(FileMeta) bytes)
server -> client:  status (4 bytes)
```

The server saves the received struct to a file called `<name>.meta` in its working directory.

### TYPE_SYNC (conditional transfer)

```
client -> server:  type (4 bytes) = 3
                   FileMeta of the client's file
server -> client:  FileMeta of the server's copy (all zero except the name if the server has no such file)
client -> server:  send_flag (4 bytes): 1 if checksum or mtime differ, otherwise 0
                   if send_flag is 1:
                       size (4 bytes)
                       content (size bytes)
server -> client:  status (4 bytes)
```

When the server stores content received through a sync, it sets the file's modification time to the client's `mtime`. This makes the next sync of the same unchanged file report no difference.

## 4. Source files in detail

### receive_b.c (server)

Usage: `./receive_b <port>`

The server creates a TCP socket with `SO_REUSEADDR`, binds to all interfaces on the given port, listens, and then loops forever accepting one connection at a time. For every connection it calls `handle_client`, sends a 4-byte status (0 on success, 1 on failure) and closes the connection. It prints one line per stored file or metadata record to stdout and `request failed` to stderr on errors.

Main functions:

- `send_u32` and `recv_u32` send and receive a 4-byte integer in network byte order.
- `safe_name` takes a name from the network, keeps only the part after the last `/`, and rejects empty names, `.`, `..` and names of `MAX_NAME` (256) characters or more. This stops path traversal such as `../../evil.txt`, which gets stored as `evil.txt`.
- `recv_content` reads the 4-byte size (rejecting anything above `MAX_FILE_SIZE`, 100 MB), then reads the content in chunks of at most `BUF_SIZE` (4096) bytes and writes it to the file. If anything fails midway it deletes the partial file. If a non-zero `mtime` is given it sets the file's modification time with `utimes`.
- `handle_file` handles `TYPE_FILE`: reads the name length and name, sanitizes the name, then calls `recv_content`.
- `handle_meta` handles `TYPE_META`: reads a `FileMeta`, sanitizes its name and writes the struct to `<name>.meta`.
- `handle_sync` handles `TYPE_SYNC`: reads the client's `FileMeta`, builds the metadata of its own copy of the file (or a zeroed one if there is none), sends it back, reads the send flag, and if the flag is 1 calls `recv_content`.
- `handle_client` reads the request type and dispatches. An unknown type is a failure.

Files are stored in the directory the server was started from, always overwriting any existing file of the same name.

### send_b.c (basic file client)

Usage: `./send_b <ip> <port> <file>`

`Send_file` opens the file, checks it is a regular file with `fstat`, takes the basename as the name, connects, sends the type, name length, name and size, then streams the content in 4096-byte chunks with `fread` and `send_all`. Finally it reads the server's status. `main` checks `argc`, validates the port and returns 1 on any failure, printing `transfer failed`.

### metadata_transfer.c

Usage: `./metadata_transfer <ip> <port> <file>`

Builds the `FileMeta` of the given file with `get_file_metadata` (basename, checksum, mtime, mode), connects, sends `TYPE_META` followed by the struct, and waits for the status. It exits 0 only if the server confirmed the upload.

### file_modification.c

Usage: `./file_modification <ip> <port> <file>`

Builds the local `FileMeta`, sends `TYPE_SYNC` with it, and receives the server's `FileMeta`. `compare_metadata` returns 1 if the checksums or the modification times differ. The client sends the result as the send flag. If it is 1, `transfer_file_content` sends the size and content. At the end it reads the status and prints `TRANSFERRED` or `UNCHANGED`.

Note that a file that was only touched (same content, new mtime) is transferred again, because the comparison looks at both checksum and mtime.

### metadata_comparison.c

Usage: `./metadata_comparison <local_file> <remote_meta_file>`

This is a local tool and uses no network. It computes the metadata of `<local_file>`, reads a stored `FileMeta` from `<remote_meta_file>` (for example a `.meta` file the server created), and compares checksum, mtime and name. It prints `SAME` (exit 0) or `DIFFERENT` (exit 1). Errors such as an unreadable or truncated metadata file give a message and exit 2.

The original program took two metadata files. This version takes a real file for the first argument because nothing in the project writes a local metadata file.

### net.c and net.h

Copied unchanged from `../src`. `send_all(fd, buf, n)` keeps calling `send` (with `MSG_NOSIGNAL`) until all `n` bytes are sent, retrying on `EINTR`. `recv_all(fd, buf, n)` keeps calling `recv` until exactly `n` bytes have arrived. Both return 0 on success and -1 on an error or if the peer closes early. Because `recv_all` waits for exactly `n` bytes, all message sizes in the protocol are known in advance, which is why every field is length prefixed.

## 5. Compiling

From inside `src_b`:

```
gcc -Wall -Wextra receive_b.c net.c -o receive_b
gcc -Wall -Wextra send_b.c net.c -o send_b
gcc -Wall -Wextra metadata_transfer.c net.c -o metadata_transfer
gcc -Wall -Wextra file_modification.c net.c -o file_modification
gcc -Wall -Wextra metadata_comparison.c net.c -o metadata_comparison
```

All five compile without warnings.

## 6. Running

Start the server in the folder where received files should be stored:

```
mkdir -p server_dir && cd server_dir
../receive_b 8090 &
```

From another folder (the clients and server can be on the same machine using `127.0.0.1`):

```
echo "hello test" > testfile.txt

./send_b 127.0.0.1 8090 testfile.txt
./metadata_transfer 127.0.0.1 8090 testfile.txt
./file_modification 127.0.0.1 8090 testfile.txt
./metadata_comparison testfile.txt server_dir/testfile.txt.meta
```

Typical results:

- `send_b` prints nothing on success; `server_dir/testfile.txt` now exists with the same content.
- `metadata_transfer` prints nothing on success; `server_dir/testfile.txt.meta` (280 bytes) now exists.
- `file_modification` prints `TRANSFERRED` the first time for a file the server does not have (or if it changed), then `UNCHANGED` on repeat runs.
- `metadata_comparison` prints `SAME` or `DIFFERENT`.

Stop the server with `kill` (or `Ctrl+C` if it runs in the foreground).

## 7. What each program does and how it was tested

The tests ran on one machine with the server on port 8090, from `src_b/test_run/`.

| Program | Expected behavior | Test and result |
|---|---|---|
| `send_b` | Server stores the file under the right name with identical content | `testfile.txt` (`hello test`) was stored correctly. A 20,000-byte random file was byte-identical (`cmp`). An empty file gave a 0-byte file. `sub/nested.txt` was stored as `nested.txt`. |
| `metadata_transfer` | Server stores the client's `FileMeta` | `testfile.txt.meta` was created with 280 bytes. The server log showed checksum 1791038698 and the file's mtime. |
| `file_modification` | Content is sent only when checksum or mtime differ | A new file gave `TRANSFERRED`. A repeat run gave `UNCHANGED`. After editing the content it gave `TRANSFERRED` and the server copy updated. After `touch` only, it gave `TRANSFERRED`. |
| `metadata_comparison` | `SAME` for matching metadata, `DIFFERENT` otherwise | The unchanged file gave `SAME` (exit 0). After editing the file it gave `DIFFERENT` (exit 1). A truncated `.meta` file gave an error and exit 2. |
| Argument checks | Usage message, non-zero exit | Every client printed a usage message with no arguments, and `metadata_transfer` did so with only two. |
| Error handling | Clear message, exit 1 | A missing file and a port with no server both gave an error message and exit 1. |
| Server robustness | Bad requests are rejected and the server keeps running | A 300-character name, an empty name, `..` and an unknown request type each returned status 1. `../../evil.txt` was stored as `evil.txt` in the server folder, not outside it. The server stayed up through all of them. |

## 8. Limitations and notes

- `FileMeta` is sent and stored as a raw struct. This only works when both sides use the same architecture, compiler and struct layout (here, 280 bytes with the same padding and byte order). Across different machines it would need field-by-field serialization in network byte order.
- Content sizes are 32-bit, so files over 4 GB are not supported. The server also refuses files over 100 MB (`MAX_FILE_SIZE`).
- The server handles one connection at a time and has no timeouts, so a client that connects and stalls blocks other clients.
- There is no authentication or encryption. Anyone who can reach the port can create, overwrite or update files in the server's directory.
- The checksum is a simple rolling hash, not a cryptographic one. Different content can have the same checksum.
- `file_modification` compares checksum and mtime only. A file that was only touched is sent again.
- The server stores every file under its basename in one flat directory, so two files with the same name from different folders overwrite each other. A sync also overwrites a changed server copy even if the server's copy is newer.
- `FileMeta.name` holds only the basename, not the full path.
- `metadata_comparison` differs from the original by taking a local file and a stored metadata file.
- `.meta` files live in the same folder as the stored files, so a stored file named `x.meta` could collide with the metadata record for `x`.
