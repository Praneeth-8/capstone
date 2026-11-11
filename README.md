# FsynC

A small Dropbox/rsync-style file synchronizer in C99, built on the capstone `demo/` code
(same `send_all`/`recv_all`, same 11-byte header, same log line style).

A server keeps a shared directory. Any number of clients keep local copies. A change made in
one client goes to the server and is pushed to every other connected client within about a second.

## What it does

- Syncs file create, modify, delete, directory create and directory delete, nested paths, empty files, empty directories and binary files up to 100 MB.
- Sends only the changed parts of a modified file (rsync-style rolling checksum delta), in both directions.
- Pushes changes live: the server notifies other clients, which pull right away.
- Catches up after being offline, including deletions made while offline.
- Never silently loses an edit: a concurrent edit is saved as `name.conflict-<client>-<time>.ext`.
- Logs every sync event on the server.
- Survives malformed, truncated, oversized and malicious requests and dead clients.
- Lists servers on the local network. A client can discover a server instead of being given its IP.

## Build

```
make            # or ./build.sh if make is not installed
make test       # unit tests + integration test
make asan       # same with AddressSanitizer + UBSan
```

Plain gcc, no external libraries. Output: `FsynC_server`, `FsynC_client`, `build/libFsynC.a`.

## Run

Terminal 1 (server):
```
./FsynC_server 9000 ~/FsynC_shared
```
Terminal 2 and 3 (clients):
```
./FsynC_client 127.0.0.1 9000 ~/client1 watch
./FsynC_client 127.0.0.1 9000 ~/client2 watch
```
`once` instead of `watch` does a single full sync and exits. On two machines, use the server
machine's IP address instead of `127.0.0.1` and open the port in its firewall.

The server log goes to `<dir>/../FsynC_server.log`, or to a path given as a third argument.

The server also listens for discovery on UDP port 9999. Optional arguments set that port and
the name announced to clients (the default name is the hostname):

```
./FsynC_server 9000 ~/FsynC_shared ~/FsynC_server.log 9999 my-server
```

From another machine on the same LAN:

```
./FsynC_client discover
./FsynC_client discover 9999 1000
./FsynC_client discover-connect my-server ~/client1 watch
```

`discover` prints a numbered table (`# | name | IP | TCP port`) and a connect command for each
server that answers. `discover-connect` finds that name and syncs. The IP in the table is the
source address of the reply.

`./examples/demo.sh` runs a scripted demo with one server and two clients on one machine.

## Layout

See `PROPOSED_STRUCTURE.md`. Documentation is in `documentation/`.
