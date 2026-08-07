#!/usr/bin/env python3
"""
Legacy API smoke test driver.

Starts the legacy smoke app (which uses the OLD OpenPS3FTP API:
command_init + base/ext/site/feat imports + server_init + server_run
on a thread) and drives it with ftplib — login, PWD, LIST/NLST,
RETR, STOR, PASV/PORT, SIZE, MDTM, FEAT, ABOR, QUIT.

Exit code 0 = all scenarios passed.
"""
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time

import ftplib

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS {name}")
    else:
        print(f"FAIL {name} {detail}")
        FAILURES.append(name)


def start_server(binary, root):
    proc = subprocess.Popen(
        [binary, "0"],
        cwd=root,
        env=dict(os.environ, OPFTP_LEGACY_ROOT=root),
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    # wait for "PORT n"
    for _ in range(200):
        line = proc.stdout.readline()
        if not line:
            break
        m = re.match(r"PORT (\d+)", line)
        if m:
            return proc, int(m.group(1))
        time.sleep(0.05)
    proc.terminate()
    raise RuntimeError("server did not report a port")


def stop_server(proc):
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()


def scenario_legacy(binary, root):
    proc, port = start_server(binary, root)
    try:
        ftp = ftplib.FTP()
        ftp.connect("127.0.0.1", port, timeout=10)
        ftp.login("test", "test")          # base.c accepts any user/pass
        check("login", ftp.sock is not None)

        check("pwd", ftp.pwd() == "/")

        # FEAT via legacy feat.c handler
        feat = []
        ftp.voidcmd("TYPE I")
        try:
            feat = ftp.sendcmd("FEAT").splitlines()
        except ftplib.error_perm as e:
            print("FEAT perm:", e)
        check("feat", any("SIZE" in l for l in feat))

        # LIST via the legacy data callback executor (PASV)
        listing = ftp.nlst("/")
        check("nlst_root", isinstance(listing, list))

        # write + RETR round trip: the legacy server roots at "/" == the
        # smoke app's cwd (the temp root), since base.c resolves paths
        # against "/" with ftpio on the host backend.
        testfile = os.path.join(root, "legacy_test.bin")
        data = os.urandom(256 * 1024)
        with open(testfile, "wb") as f:
            f.write(data)
        try:
            chunks = []
            ftp.retrbinary("RETR /legacy_test.bin", chunks.append)
            got = b"".join(chunks)
            check("retr_pasv", got == data, f"{len(got)} != {len(data)}")

            ftp.storbinary("STOR /legacy_test_up.bin", __import__("io").BytesIO(data))
            with open(os.path.join(root, "legacy_test_up.bin"), "rb") as f:
                check("stor_pasv", f.read() == data)

            # PORT (active) mode round trip
            ftp.set_pasv(False)
            chunks2 = []
            ftp.retrbinary("RETR /legacy_test.bin", chunks2.append)
            got2 = b"".join(chunks2)
            check("retr_port", got2 == data)
            ftp.set_pasv(True)

            # SIZE / MDTM via legacy ext.c
            sz = ftp.size("/legacy_test.bin")
            check("size", sz == len(data), f"{sz} != {len(data)}")
            try:
                mdtm = ftp.sendcmd("MDTM /legacy_test.bin")
                check("mdtm", mdtm.startswith("213 "))
            except ftplib.error_perm:
                check("mdtm", False, "perm denied")

            # REST + RETR (base.c supports REST)
            ftp.sendcmd("REST 100")
            chunks3 = []
            ftp.retrbinary("RETR /legacy_test.bin", chunks3.append)
            got3 = b"".join(chunks3)
            check("rest_retr", got3 == data[100:])
            ftp.sendcmd("REST 0")
        finally:
            for p in ("legacy_test.bin", "legacy_test_up.bin"):
                try:
                    os.unlink(os.path.join(root, p))
                except OSError:
                    pass

        # ABOR without an active transfer: legacy cmd_abor replies 226
        resp = ftp.sendcmd("ABOR")
        check("abor", resp.startswith("226"))

        # SITE + STOP are registered by site.c; SITE CHMOD on a bogus
        # path should 550 (handled, not 502)
        try:
            ftp.sendcmd("SITE CHMOD 755 /no_such_file")
            check("site", False, "expected error")
        except ftplib.error_perm as e:
            check("site", str(e).startswith("550"))

        ftp.quit()
        check("quit", True)
    finally:
        try:
            ftp.close()
        except Exception:
            pass
        proc.terminate()
        try:
            err = proc.stderr.read()
            if err:
                print("server stderr:", err[-2000:])
        except Exception:
            pass
        stop_server(proc)


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory() as root:
        scenario_legacy(binary, root)
    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        sys.exit(1)
    print("all legacy scenarios passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
