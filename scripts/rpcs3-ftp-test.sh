#!/usr/bin/env bash
# rpcs3-ftp-test.sh — boot the headless PS3 FTP app in RPCS3 and probe
# it over the emulated network (sys_net -> host sockets).
#
# Usage:
#   scripts/rpcs3-ftp-test.sh [path/to/headless.fake.self] [probe_timeout_s]
#
# Defaults:
#   SELF:  app/headless.fake.self   (build: make -C app headless, then
#                                    sign with make_self_npdrm)
#   RPCS3_BIN: rpcs3 (override with env)
#
# User-local deps (no root): the AppImage needs libOpenGL.so.0 +
# libxcb-cursor0 that minimal hosts lack. Extract the Ubuntu debs into
# scripts/rpcs3-libs/ and this script adds them to LD_LIBRARY_PATH:
#   mkdir -p scripts/rpcs3-libs && cd scripts/rpcs3-libs
#   apt-get download libglvnd0 libopengl0 libxcb-cursor0
#   for d in *.deb; do dpkg-deb -x "$d" .; done
#
# Behaviour:
#   - Raises the memlock limit (RPCS3 must lock 64MB+ of VM; the default
#     ulimit -l of 8MB caused the boot crash in reference/rpcs3-log.txt)
#   - Launches `rpcs3 --no-gui <self>` in the background
#   - Waits for the FTP port (127.0.0.1:2121) to accept connections
#   - Runs tests/rpcs3_ftp_probe.py against it
#   - Kills RPCS3, reports PASS/FAIL, exit 0/1
#
# Exit: 0 = probe passed; 1 = boot failed or probe failed; 2 = env blocked.

set -u

SELF_PATH="${1:-app/headless.fake.self}"
PROBE_TIMEOUT="${2:-60}"
RPCS3_BIN="${RPCS3_BIN:-rpcs3}"
HOST=127.0.0.1
PORT=2121
LOG="$HOME/.cache/rpcs3/RPCS3.log"
PROBE="$(dirname "$0")/../tests/rpcs3_ftp_probe.py"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

[[ -f "$SELF_PATH" ]] || { echo "rpcs3-ftp-test: file not found: $SELF_PATH" >&2; exit 2; }
command -v "$RPCS3_BIN" >/dev/null || { echo "rpcs3-ftp-test: rpcs3 not found (set RPCS3_BIN)" >&2; exit 2; }

# memlock: RPCS3 locks 64MB+ of VM; default ulimit -l (8MB) crashes at boot.
# If we cannot raise it, warn loudly — boot will fail at "Failed to lock
# sudo memory" (see reference/rpcs3-log.txt).
if ! ulimit -l unlimited 2>/dev/null; then
    if [ "$(ulimit -l)" -lt 65536 ] 2>/dev/null; then
        echo "rpcs3-ftp-test: FATAL: memlock limit is $(ulimit -l) KB; RPCS3 needs 2 GiB." >&2
        echo "rpcs3-ftp-test: raise it on the host: 'ulimit -l unlimited' or docker --ulimit memlock=-1" >&2
        exit 2
    fi
fi

# User-local GLVND/Qt libs: the RPCS3 AppImage needs libOpenGL.so.0 etc.
# that may be missing on minimal hosts. If scripts/rpcs3-libs exists
# (extracted .debs), put it on LD_LIBRARY_PATH.
if [ -d "$ROOT/scripts/rpcs3-libs/usr/lib/x86_64-linux-gnu" ]; then
    export LD_LIBRARY_PATH="$ROOT/scripts/rpcs3-libs/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# kill stale instances
pgrep -x "$(basename "$RPCS3_BIN")" >/dev/null && pkill -x "$(basename "$RPCS3_BIN")" || true

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

echo "rpcs3-ftp-test: booting $SELF_PATH"
"$RPCS3_BIN" --no-gui "$SELF_PATH" >/dev/null 2>&1 &
RPCS3_PID=$!

# wait for the FTP port
echo "rpcs3-ftp-test: waiting for $HOST:$PORT (${PROBE_TIMEOUT}s)"
waited=0
while [ $waited -lt "$PROBE_TIMEOUT" ]; do
    if (echo > /dev/tcp/$HOST/$PORT) 2>/dev/null; then
        break
    fi
    # if rpcs3 died, fail fast
    kill -0 "$RPCS3_PID" 2>/dev/null || {
        echo "rpcs3-ftp-test: rpcs3 exited early (see $LOG)" >&2
        kill "$RPCS3_PID" 2>/dev/null
        exit 1
    }
    sleep 1
    waited=$((waited + 1))
done

if [ $waited -ge "$PROBE_TIMEOUT" ]; then
    echo "rpcs3-ftp-test: FTP port never came up (see $LOG)" >&2
    kill "$RPCS3_PID" 2>/dev/null
    exit 1
fi

echo "rpcs3-ftp-test: server up, running probe"
python3 "$PROBE" "$HOST" "$PORT" 10
RC=$?

kill "$RPCS3_PID" 2>/dev/null
echo "rpcs3-ftp-test: probe exit $RC"
exit $RC
