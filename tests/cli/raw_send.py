#!/usr/bin/env python3
"""Send deliberately malformed requests to the file server.

Usage: python3 raw_send.py <port> <case>

Cases:
  junk         random bytes, not a valid header
  badop        header with an unknown op (9)
  ackop        valid header, but op = ACK (not a request)
  noname       upload with name length 0
  longname     name length 5000 (limit is 4096)
  shortheader  only 6 of the 11 header bytes, then the connection closes
  truncated    upload promises 1000 bytes, sends 10, then closes
  huge         upload claims 200 MB (server cap is 100 MB)

Header layout (big-endian): op (1 byte), name_len (2 bytes), file_size (8 bytes).
"""
import socket
import struct
import sys

HOST = "127.0.0.1"
OPS = {3: "ACK", 4: "ERROR"}


def header(op, name_len, size):
    return struct.pack(">BHQ", op, name_len, size)


def talk(port, payload, half_close=False):
    s = socket.create_connection((HOST, port), timeout=5)
    s.sendall(payload)
    if half_close:
        s.shutdown(socket.SHUT_WR)      # tell the server "no more data"
    try:
        reply = s.recv(64)
    except OSError:
        reply = b""
    s.close()
    if len(reply) >= 11:
        code = struct.unpack(">Q", reply[3:11])[0]
        print(f"  server replied: {OPS.get(reply[0], reply[0])} (code {code})")
    else:
        print("  server replied: nothing (connection closed)")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    port, case = int(sys.argv[1]), sys.argv[2]

    if case == "junk":
        talk(port, b"GARBAGE-GARBAGE-GARBAGE")
    elif case == "badop":
        talk(port, header(9, 5, 5) + b"a.txt12345")
    elif case == "ackop":
        talk(port, header(3, 0, 0))
    elif case == "noname":
        talk(port, header(1, 0, 5) + b"12345")
    elif case == "longname":
        talk(port, header(1, 5000, 5))
    elif case == "shortheader":
        talk(port, header(1, 4, 5)[:6], half_close=True)
    elif case == "truncated":
        name = b"cut.txt"
        talk(port, header(1, len(name), 1000) + name + b"0123456789", half_close=True)
    elif case == "huge":
        name = b"huge.bin"
        talk(port, header(1, len(name), 200 * 1024 * 1024) + name)
    else:
        print(f"unknown case: {case}")
        sys.exit(2)


if __name__ == "__main__":
    main()
