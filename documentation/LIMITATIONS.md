# Limitations

- No authentication or encryption. Anyone who can reach the port can read and change the shared
  folder. Use it on a trusted network or through an SSH tunnel.
- Change detection is a 1 second polling scan, not inotify. Cost grows with the number of files.
- The server is single-threaded: a large transfer or a stalled client (up to the 10 s timeout)
  delays everyone else. Max 64 clients.
- Files are held in memory during transfer (about 2 to 3 times the file size at peak). Max file
  size is 100 MB.
- Server side change detection rescans the shared dir on each request. Fine for thousands of
  files, slow for very large trees.
- The client id is a random value in `.FsynC/id`; there is no user identity.
- Symlinks, devices and names with control characters or backslashes are skipped.
- Delete vs edit: the edit always wins. A file over the cap is skipped with a `SKIP` line, and a
  `watch` client retries it every second.
- Conflicts are detected per file. Two clients editing different files in the same dir never conflict.
- Directory rmdir only works on empty directories; trees are removed file by file.
- Only IPv4 on the server.
- Discovery is UDP broadcast on the local LAN. It does not cross routers. Wi-Fi client isolation,
  guest networks and many firewalls drop broadcast, so a server that is up can still be invisible
  to `discover`. The reply is not authenticated: anyone on the LAN learns the server name, the
  shared folder's name and the TCP port. One process can bind a given discovery port; a second
  server on that port fails at startup and must be given another `udp_port`.
- mtime is not copied: a pulled file gets the local time of the pull.
