#!/bin/bash
cd "$(dirname "$0")/.."
BIN=${BIN:-.}
TOOLS=${TOOLS:-build}
PORT=${PORT:-$((20000 + RANDOM % 20000))}
UDPP=${UDPP:-$((40000 + RANDOM % 20000))}
DNAME=${DNAME:-fsyncit$$}
W=$(mktemp -d /tmp/FsynC_it.XXXXXX)
PASS=0
FAIL=0
PIDS=""

# helpers
ok() { echo "PASS  $1"; PASS=$((PASS + 1)); }
bad() { echo "FAIL  $1"; FAIL=$((FAIL + 1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
cleanup() { for p in $PIDS; do kill $p 2>/dev/null; done; wait 2>/dev/null; }
trap 'cleanup; rm -rf "$W"' EXIT

start_server() { "$BIN/FsynC_server" $PORT "$W/srv" "$W/server.log" $UDPP "$DNAME" >> "$W/server.out" 2>&1 & SRV=$!; PIDS="$PIDS $SRV"; sleep 0.5; }
stop_server() { kill $SRV 2>/dev/null; wait $SRV 2>/dev/null; }
watch() { "$BIN/FsynC_client" 127.0.0.1 $PORT "$W/$1" watch >> "$W/$1.out" 2>&1 & eval "PID_$1=$!"; PIDS="$PIDS $!"; }
stop_watch() { local p; eval "p=\$PID_$1"; kill $p 2>/dev/null; wait $p 2>/dev/null; }
once() { "$BIN/FsynC_client" 127.0.0.1 $PORT "$W/$1" once >> "$W/$1.out" 2>&1; }
same() { diff -r -x .FsynC "$W/$1" "$W/$2" > /dev/null 2>&1; }
wait_for() { local i; for i in $(seq 1 60); do eval "$1" && return 0; sleep 0.1; done; return 1; }
sent_for() { grep "UPLOAD $2:" "$W/$1.out" | tail -1 | sed 's/.*sent \([0-9]*\) of.*/\1/'; }

mkdir -p "$W/srv" "$W/a" "$W/b" "$W/c" "$W/x" "$W/y"
start_server

echo "== discovery"
"$BIN/FsynC_client" discover $UDPP 2000 > "$W/disc.out" 2>&1
check "discover finds this server" "grep -E -q '^[0-9]+ \\| ${DNAME} \\| ([0-9]+\\.){3}[0-9]+ \\| ${PORT}\$' $W/disc.out"
check "discover prints a connect command" "grep -F -q \"$PORT <dir> once|watch\" $W/disc.out"
check "discovery request is logged" "grep -q 'Action: DISCOVER' $W/server.log"
check "bad discovery datagram leaves the server up" "echo -n not-a-discovery-packet > /dev/udp/127.0.0.1/$UDPP && sleep 0.2 && kill -0 $SRV"

watch a
watch b
watch c
sleep 1

echo "== propagation (3 clients in watch mode)"
echo "hello" > "$W/a/hello.txt"
mkdir -p "$W/a/docs/notes"
echo "deep" > "$W/a/docs/notes/n.txt"
check "file and nested dir created on A reach B and C" "wait_for 'same a b && same a c && [ -f $W/c/docs/notes/n.txt ]'"
echo "hello again" > "$W/b/hello.txt"
check "modify on B reaches A and C" "wait_for 'grep -q again $W/a/hello.txt && grep -q again $W/c/hello.txt'"
: > "$W/c/empty.txt"
mkdir "$W/c/emptydir"
check "empty file and empty dir from C reach A and B" "wait_for '[ -f $W/a/empty.txt ] && [ -d $W/b/emptydir ] && [ ! -s $W/b/empty.txt ]'"
rm "$W/a/hello.txt"
rm -r "$W/a/docs"
check "file delete and dir delete on A reach B and C" "wait_for '[ ! -e $W/b/hello.txt ] && [ ! -e $W/c/docs ] && [ ! -e $W/srv/docs ]'"
head -c 300000 /dev/urandom > "$W/a/bin.dat"
check "binary file reaches B and C intact" "wait_for 'cmp -s $W/a/bin.dat $W/b/bin.dat && cmp -s $W/a/bin.dat $W/c/bin.dat'"
sleep 3
N1=$(grep -c "Action: UPLOAD\|Action: DELTA" "$W/server.log")
sleep 3
N2=$(grep -c "Action: UPLOAD\|Action: DELTA" "$W/server.log")
check "no sync ping-pong after changes settle" "[ $N1 -eq $N2 ]"
check "all three copies equal the server" "same a srv && same b srv && same c srv"

echo "== offline client catches up"
stop_watch c
echo "while offline" > "$W/a/offline_new.txt"
rm "$W/a/empty.txt"
rmdir "$W/a/emptydir"
echo "changed while offline" > "$W/a/bin.dat"
sleep 2
rm "$W/c/offline_gone.txt" 2>/dev/null
echo "c offline edit" > "$W/c/from_c.txt"
once c
check "offline client gets new, modified and deleted entries" "same a c || (sleep 2; same a c)"
check "offline client's own new file reaches others" "wait_for '[ -f $W/a/from_c.txt ]'"
watch c
sleep 1

echo "== server restart keeps versions"
stop_server
start_server
echo "after restart" > "$W/b/restart.txt"
check "edit after server restart propagates without conflict" "wait_for '[ -f $W/a/restart.txt ] && [ -f $W/c/restart.txt ]' && ! ls $W/srv | grep -q conflict"

echo "== conflicts (once-mode clients X and Y)"
stop_watch a
stop_watch b
stop_watch c
once x
once y
echo "base" > "$W/x/shared.txt"
once x
once y
echo "edit from X" > "$W/x/shared.txt"
echo "edit from Y" > "$W/y/shared.txt"
once x
once y
check "server keeps first writer's copy" "grep -q 'edit from X' $W/srv/shared.txt"
check "loser saved as conflict copy, no data lost" "grep -l 'edit from Y' $W/srv/shared.conflict-*.txt > /dev/null 2>&1"
once x
check "conflict copy synced to other client" "ls $W/x | grep -q 'shared.conflict-'"
check "conflict logged on server" "grep -q 'Action: CONFLICT' $W/server.log"
echo "same text" > "$W/x/same.txt"
once x
once y
echo "identical edit" > "$W/x/same.txt"
echo "identical edit" > "$W/y/same.txt"
once x
once y
check "identical concurrent edits are not a conflict" "! ls $W/srv | grep -q 'same.conflict'"
echo "victim" > "$W/x/dv.txt"
once x
once y
rm "$W/x/dv.txt"
echo "edited by Y" > "$W/y/dv.txt"
once y
once x
check "delete vs edit: edit wins and file comes back" "grep -q 'edited by Y' $W/x/dv.txt && grep -q 'edited by Y' $W/srv/dv.txt"
mkdir -p "$W/x/proj"
echo 1 > "$W/x/proj/one.txt"
once x
once y
rm -r "$W/x/proj"
echo 2 > "$W/y/proj/two.txt"
once y
once x
check "rmdir of a dir that gained a file elsewhere is refused, files kept" "[ -f $W/srv/proj/two.txt ] && [ -f $W/x/proj/two.txt ]"

echo "== delta transfer byte counts"
head -c 5242880 /dev/urandom > "$W/x/a.bin"
once x
once y
printf 'Z' | dd of="$W/x/a.bin" bs=1 seek=1000000 conv=notrunc 2> /dev/null
once x
S=$(sent_for x a.bin); check "1 byte edit in 5 MB sends $S bytes (< 3000)" "[ -n '$S' ] && [ $S -lt 3000 ]"
(printf 'INSERTED'; cat "$W/x/a.bin") > "$W/x/t" && mv "$W/x/t" "$W/x/a.bin"
once x
S=$(sent_for x a.bin); check "insert at start sends $S bytes (< 3000)" "[ -n '$S' ] && [ $S -lt 3000 ]"
printf 'appended tail' >> "$W/x/a.bin"
once x
S=$(sent_for x a.bin); check "append sends $S bytes (< 3000)" "[ -n '$S' ] && [ $S -lt 3000 ]"
truncate -s 3000000 "$W/x/a.bin"
once x
S=$(sent_for x a.bin); check "truncation sends $S bytes (< 3000)" "[ -n '$S' ] && [ $S -lt 3000 ]"
check "server copy matches after deltas" "cmp -s $W/x/a.bin $W/srv/a.bin"
L1=$(grep -c "UPLOAD a.bin" "$W/x.out")
touch "$W/x/a.bin"
once x
L2=$(grep -c "UPLOAD a.bin" "$W/x.out")
check "identical file (touched only) sends 0 bytes" "[ $L1 -eq $L2 ]"
once y
check "pull side also uses delta" "grep -q 'DOWNLOAD a.bin.*(delta)' $W/y.out"
head -c 52428800 /dev/urandom > "$W/x/big.bin"
once x
printf 'small edit' | dd of="$W/x/big.bin" bs=1 seek=30000000 conv=notrunc 2> /dev/null
once x
S=$(sent_for x big.bin); check "50 MB file with small edit sends $S bytes (< 5000)" "[ -n '$S' ] && [ $S -lt 5000 ] && cmp -s $W/x/big.bin $W/srv/big.bin"
rm -f "$W/x/big.bin" "$W/srv/big.bin"
once x
once y

echo "== limits and malicious input"
head -c 104857601 /dev/zero > "$W/x/huge.bin"
once x
check "client skips file over 100 MB cap" "[ ! -e $W/srv/huge.bin ] && grep -q 'SKIP huge.bin' $W/x.out"
rm -f "$W/x/huge.bin"
check "server rejects 100 MB + 1 upload" "[ \"\$($TOOLS/evil_client $PORT toobig)\" = REJECTED ] && [ ! -e $W/srv/toobig.bin ]"
check "path traversal names rejected" "[ \"\$($TOOLS/evil_client $PORT traversal)\" = REJECTED ] && [ ! -e $W/escape.txt ] && [ ! -e /tmp/abs.txt ]"
$TOOLS/evil_client $PORT garbage
$TOOLS/evil_client $PORT truncated
check "giant length field drops connection" "[ \"\$($TOOLS/evil_client $PORT giant)\" = DROPPED ]"
check "request before HELLO drops connection" "[ \"\$($TOOLS/evil_client $PORT nohello)\" = DROPPED ]"
check "bad SHA-256 upload rejected" "[ \"\$($TOOLS/evil_client $PORT badhash)\" = REJECTED ] && [ ! -e $W/srv/badhash.txt ]"
check "corrupt delta rejected" "[ \"\$($TOOLS/evil_client $PORT baddelta)\" = REJECTED ] && grep -q base $W/srv/dbase.txt"
$TOOLS/evil_client $PORT midtransfer
check "client dying mid-transfer leaves no partial file" "[ ! -e $W/srv/mid.bin ] && ! find $W/srv -name '*.FsynC-tmp' | grep -q ."
check "server still alive after all attacks" "[ \"\$($TOOLS/evil_client $PORT ping)\" = ALIVE ] && kill -0 $SRV"
once x
check "normal client still syncs after attacks" "same x srv"

echo "== sanitizer and leak reports"
cleanup
PIDS=""
check "no AddressSanitizer / UBSan reports in server or clients" "! grep -q 'AddressSanitizer\|LeakSanitizer\|runtime error' $W/*.out"

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
