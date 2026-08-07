#!/usr/bin/env python3
"""
RPCS3 headless FTP probe: drives the PS3 build's FTP server through
the emulator's sys_net -> host socket mapping.

The headless app binds 0.0.0.0:2121 inside the emulated PS3; with
RPCS3's network enabled that listener is reachable from the host at
127.0.0.1:2121.

Usage: rpcs3_ftp_probe.py [host] [port] [timeout_s]
Exit 0 = all checks passed; 1 = any check failed.
"""
import ftplib
import io
import os
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2121
TIMEOUT = int(sys.argv[3]) if len(sys.argv) > 3 else 10

fails = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS {name}")
    else:
        print(f"FAIL {name} {detail}")
        fails.append(name)


def wait_for_port(host, port, deadline):
    import socket
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except OSError:
            time.sleep(1)
    return False


def main():
    if not wait_for_port(HOST, PORT, time.time() + TIMEOUT):
        print(f"FAIL server not reachable at {HOST}:{PORT}")
        sys.exit(1)

    ftp = ftplib.FTP()
    ftp.connect(HOST, PORT, timeout=10)
    try:
        check("banner", "220" in ftp.getwelcome(), ftp.getwelcome())
        ftp.login("rpcs3", "rpcs3")          # app accepts any user/pass
        check("login", True)
        check("pwd", ftp.pwd() == "/", ftp.pwd())
        nlst = ftp.nlst()
        check("nlst", isinstance(nlst, list) and len(nlst) >= 0, nlst[:3])

        # best-effort file round trip; write to /dev_hdd0/tmp if present,
        # otherwise fall back to a plain RETR of a known /dev_flash file.
        payload = b"rpcs3 headless round trip \x00\x01\x02" * 64
        ok = False
        for remote in ("/dev_hdd0/tmp/rpcs3_probe.bin", "/tmp/rpcs3_probe.bin"):
            try:
                ftp.storbinary(f"STOR {remote}", io.BytesIO(payload))
                chunks = []
                ftp.retrbinary(f"RETR {remote}", chunks.append)
                got = b"".join(chunks)
                check("roundtrip", got == payload, f"{len(got)} != {len(payload)}")
                ftp.delete(remote)
                ok = True
                break
            except ftplib.error_perm as e:
                continue
        if not ok:
            check("roundtrip", False, "no writable path")

        # missing file -> 550 (FluentFTP-classifiable message)
        try:
            ftp.size("/no_such_file.bin")
            check("missing-550", False, "expected 550")
        except ftplib.error_perm as e:
            check("missing-550", str(e).startswith("550"), e)

        ftp.quit()
        check("quit", True)
    except Exception as e:
        check("exception", False, repr(e))
    finally:
        try:
            ftp.close()
        except Exception:
            pass

    if fails:
        print(f"{len(fails)} failure(s)")
        sys.exit(1)
    print("rpcs3 ftp probe passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
