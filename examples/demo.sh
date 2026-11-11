#!/bin/bash
cd "$(dirname "$0")/.."
PORT=${1:-9000}
D=$(mktemp -d /tmp/FsynC_demo.XXXXXX)
mkdir -p "$D/server" "$D/client1" "$D/client2"
trap 'kill $S $C1 $C2 2>/dev/null; wait 2>/dev/null' EXIT

# start server and two clients
./FsynC_server $PORT "$D/server" "$D/server.log" > "$D/server.out" &
S=$!
sleep 0.5
./FsynC_client 127.0.0.1 $PORT "$D/client1" watch > "$D/client1.out" &
C1=$!
./FsynC_client 127.0.0.1 $PORT "$D/client2" watch > "$D/client2.out" &
C2=$!
sleep 1

step() { echo; echo ">> $1"; sleep 2; }

echo "hello from client1" > "$D/client1/hello.txt"
mkdir -p "$D/client1/docs"
step "client1 created hello.txt and docs/"
ls -R "$D/client2" | grep -v '^$'

echo "client2 was here" >> "$D/client2/hello.txt"
step "client2 edited hello.txt"
cat "$D/client1/hello.txt"

head -c 2000000 /dev/urandom > "$D/client1/photo.bin"
step "client1 added a 2 MB file"
printf 'X' | dd of="$D/client1/photo.bin" bs=1 seek=500000 conv=notrunc 2> /dev/null
step "client1 changed one byte in it"
grep "photo.bin" "$D/client1.out"

rm "$D/client2/hello.txt"
rmdir "$D/client2/docs"
step "client2 deleted hello.txt and docs/"
ls "$D/client1"

echo
echo ">> server log"
cat "$D/server.log" | grep -v "PUSH\|HELLO\|BYE"
echo
echo "demo files kept in $D"
