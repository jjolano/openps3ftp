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
    # stderr to a file, not DEVNULL: a sanitizer report is the whole
    # reason to run this suite under ASan/TSan, and discarding it turns
    # "the server died" into an unexplained connection refusal.
    errf = tempfile.NamedTemporaryFile(prefix="opftp_srv_err_", suffix=".log",
                                       delete=False, mode="w+")
    proc = subprocess.Popen(
        [binary, "0"],
        cwd=root,
        env=dict(os.environ, OPFTP_LEGACY_ROOT=root),
        stdout=subprocess.PIPE,
        stderr=errf,
        text=True,
    )
    proc._err_path = errf.name  # read by server_stderr() on failure
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


def server_stderr(proc, limit=6000):
    path = getattr(proc, "_err_path", None)
    if not path or not os.path.exists(path):
        return ""
    with open(path, "r", errors="replace") as f:
        return f.read()[-limit:]


def require_alive(proc, where):
    """Fail loudly, with the server's own output, if it died."""
    if proc.poll() is None:
        return True
    err = server_stderr(proc)
    check(f"server-alive-{where}", False,
          f"exited rc={proc.returncode}; stderr:\n{err}")
    return False


def stop_server(proc):
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
    path = getattr(proc, "_err_path", None)
    if path:
        try:
            os.unlink(path)
        except OSError:
            pass


class LineSock:
    """Control socket with proper CRLF framing (recv boundaries are not
    reply boundaries, so a naive recv() mixes replies together)."""

    def __init__(self, sock):
        self.s = sock
        self.buf = b""
        self.lines = []

    def _pump(self, timeout):
        self.s.settimeout(timeout)
        try:
            b = self.s.recv(65536)
        except socket.timeout:
            return False
        except OSError:
            return False
        if not b:
            return False
        self.buf += b
        while b"\r\n" in self.buf:
            line, self.buf = self.buf.split(b"\r\n", 1)
            self.lines.append(line.decode("latin-1"))
        return True

    def readline(self, timeout=10):
        deadline = time.time() + timeout
        while not self.lines and time.time() < deadline:
            self._pump(0.2)
        return self.lines.pop(0) if self.lines else None

    def readreply(self, timeout=10):
        """One complete reply: skips `NNN-` continuation lines (the
        legacy banner is multi-line)."""
        while True:
            line = self.readline(timeout)
            if line is None:
                return None
            if re.match(r"^\d{3} ", line):
                return line

    def drain(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            self._pump(0.05)

    def sendall(self, data):
        self.s.sendall(data)

    def close(self):
        self.s.close()


def scenario_concurrent_control(binary, root):
    """Control-channel traffic *during* a legacy data transfer.

    The legacy data_callback runs on a ThreadPool worker and replies 226
    from there, while the reactor answers NOOP on the same socket. That
    is two threads writing one fd — the race DESIGN.md's "only the
    reactor writes to control sockets" rule exists to prevent, and the
    one TSan is meant to catch. ftplib alone never triggers it because
    it never sends a command mid-transfer.
    """
    payload = os.urandom(4 << 20)
    with open(os.path.join(root, "race.bin"), "wb") as f:
        f.write(payload)

    proc, port = start_server(binary, root)
    try:
        ctl = LineSock(socket.create_connection(("127.0.0.1", port), timeout=10))
        ctl.readreply()                                 # 220 (multi-line)
        ctl.sendall(b"USER test\r\n"); ctl.readreply()
        ctl.sendall(b"PASS test\r\n"); ctl.readreply()
        ctl.sendall(b"TYPE I\r\n");    ctl.readreply()

        ctl.sendall(b"PASV\r\n")
        pasv = ctl.readreply() or ""
        m = re.search(r"\((\d+,\d+,\d+,\d+,\d+,\d+)\)", pasv)
        check("race-pasv", m is not None, pasv)
        if not m:
            return
        nums = [int(x) for x in m.group(1).split(",")]
        dport = nums[4] * 256 + nums[5]

        data = socket.create_connection(("127.0.0.1", dport), timeout=10)
        ctl.sendall(b"RETR /race.bin\r\n")

        # Read the payload slowly while hammering the control channel, so
        # the executor's 226 lands in the middle of reactor NOOP replies.
        got = 0
        noops = 0
        deadline = time.time() + 30
        while got < len(payload) and time.time() < deadline:
            ctl.sendall(b"NOOP\r\n")
            noops += 1
            ctl.drain(0.02)
            data.settimeout(0.5)
            try:
                b = data.recv(65536)
            except socket.timeout:
                continue
            if not b:
                break
            got += len(b)
        data.close()
        ctl.drain(2)

        check("race-transfer-complete", got == len(payload),
              f"{got} != {len(payload)}")
        check("race-noops-sent", noops > 10, noops)
        # Every reply must still be a well-formed FTP line: interleaved
        # writes from two threads show up as garbled or truncated codes.
        bad = [l for l in ctl.lines if not re.match(r"^\d{3}[ -]", l)]
        check("race-replies-well-formed", not bad, bad[:3])
        check("race-got-226", any(l.startswith("226") for l in ctl.lines),
              ctl.lines[-3:])
        ctl.close()
    finally:
        stop_server(proc)
        try:
            os.unlink(os.path.join(root, "race.bin"))
        except OSError:
            pass


def scenario_disconnect_mid_transfer(binary, root):
    """Drop the control connection while a legacy transfer is running.

    shim_on_disconnect frees the legacy Client and destroys its mutex on
    the reactor thread, while the data executor is still holding both —
    a use-after-free unless the disconnect waits for the executor. The
    server must survive and stay serving.
    """
    payload = os.urandom(8 << 20)
    with open(os.path.join(root, "drop.bin"), "wb") as f:
        f.write(payload)

    proc, port = start_server(binary, root)
    try:
        for i in range(5):
            if not require_alive(proc, f"drop-{i}"):
                break
            ctl = LineSock(socket.create_connection(("127.0.0.1", port), timeout=10))
            ctl.readreply()
            ctl.sendall(b"USER test\r\n"); ctl.readreply()
            ctl.sendall(b"PASS test\r\n"); ctl.readreply()
            ctl.sendall(b"TYPE I\r\n");    ctl.readreply()
            ctl.sendall(b"PASV\r\n")
            pasv = ctl.readreply() or ""
            m = re.search(r"\((\d+,\d+,\d+,\d+,\d+,\d+)\)", pasv)
            if not m:
                check(f"drop-pasv-{i}", False, pasv)
                break
            nums = [int(x) for x in m.group(1).split(",")]
            data = socket.create_connection(
                ("127.0.0.1", nums[4] * 256 + nums[5]), timeout=10)
            ctl.sendall(b"RETR /drop.bin\r\n")
            # let the transfer get going, then vanish without QUIT
            data.settimeout(2)
            try:
                data.recv(65536)
            except socket.timeout:
                pass
            ctl.close()
            data.close()
            time.sleep(0.2)

        # the server must still be alive and answering
        check("drop-server-alive", proc.poll() is None, proc.poll())
        ctl = LineSock(socket.create_connection(("127.0.0.1", port), timeout=10))
        ctl.readreply()
        ctl.sendall(b"USER test\r\n"); ctl.readreply()
        ctl.sendall(b"PASS test\r\n")
        r = ctl.readreply()
        check("drop-still-serving", r is not None and r.startswith("230"), r)
        ctl.close()
    finally:
        stop_server(proc)
        try:
            os.unlink(os.path.join(root, "drop.bin"))
        except OSError:
            pass


def scenario_many_sequential_clients(binary, root):
    """More sequential connections than the shim's client table has slots.

    The table holds SHIM_MAX_CLIENTS (64) entries; if disconnect does not
    actually free a slot, the 65th connection gets no shim state and the
    server stops working — even though only one client is ever connected.
    """
    proc, port = start_server(binary, root)
    try:
        ok = 0
        for _ in range(70):
            try:
                ctl = LineSock(socket.create_connection(("127.0.0.1", port),
                                                        timeout=10))
                ctl.readreply()
                ctl.sendall(b"USER test\r\n"); ctl.readreply()
                ctl.sendall(b"PASS test\r\n")
                r = ctl.readreply()
                ctl.sendall(b"QUIT\r\n"); ctl.readreply()
                ctl.close()
                if r and r.startswith("230"):
                    ok += 1
            except OSError:
                break
        check("many-clients", ok == 70, f"{ok}/70 logins succeeded")
    finally:
        stop_server(proc)


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
        scenario_concurrent_control(binary, root)
        scenario_disconnect_mid_transfer(binary, root)
        scenario_many_sequential_clients(binary, root)
    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        sys.exit(1)
    print("all legacy scenarios passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
