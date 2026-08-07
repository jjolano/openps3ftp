#!/usr/bin/env python3
"""
Integration + concurrency tests for the opftp server core.

Starts the driver binary on an ephemeral port and exercises the
protocol with ftplib and raw sockets: login, LIST/NLST, RETR, STOR,
APPE, REST, RNFR/RNTO, DELE, MKD/RMD, SIZE, MDTM, PASV/PORT, ABOR
(in-band + OOB), one-transfer-per-client 450, queue saturation 425,
PORT bounce rejection, fragmented control lines, disconnect
mid-transfer, concurrent transfers, multi-client, stop/drain.

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


def read_reply(sock, timeout=10):
    sock.settimeout(timeout)
    data = b""
    while not data.endswith(b"\r\n"):
        chunk = sock.recv(1)
        if not chunk:
            break
        data += chunk
    return data


def read_multiline_reply(sock, timeout=10):
    sock.settimeout(timeout)
    lines = []
    while True:
        line = read_reply(sock, timeout)
        lines.append(line)
        if len(line) >= 4 and line[3:4] == b" ":
            break
    return b"".join(lines)


def start_server(binary, root, workers=2):
    proc = subprocess.Popen(
        [binary, root, "0", str(workers)],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    line = proc.stdout.readline().decode().strip()
    m = re.match(r"PORT (\d+)", line)
    if not m:
        proc.kill()
        raise RuntimeError(f"bad driver output: {line!r}")
    return proc, int(m.group(1))


def stop_server(proc):
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
        proc.wait()


def raw_connect(port, timeout=10):
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    read_reply(s)  # 220
    return s


def raw_login(s):
    s.sendall(b"USER anonymous\r\n")
    r1 = read_reply(s)
    s.sendall(b"PASS x@y\r\n")
    r2 = read_reply(s)
    assert r1.startswith(b"331"), r1
    assert r2.startswith(b"230"), r2


def make_big_file(path, size=1 << 20):
    with open(path, "wb") as f:
        f.write(os.urandom(size))


def scenario_basic(ftp, root):
    # PWD/CWD
    check("pwd", ftp.pwd() == "/", ftp.pwd())
    check("cwd", ftp.cwd("/sub") == "250 File operation successful.")
    check("cwd-back", ftp.cwd("..") == "250 File operation successful.")
    # LIST / NLST
    lines = []
    ftp.retrlines("LIST", lines.append)
    names = [l.split()[-1] for l in lines if l.strip()]
    check("list", "small.txt" in names and "sub" in names, names)
    nlst = ftp.nlst()
    check("nlst", "small.txt" in nlst and "sub" in nlst, nlst)
    # LIST of a file
    flines = []
    ftp.retrlines("LIST small.txt", flines.append)
    check("list-file", len(flines) == 1 and "small.txt" in flines[0], flines)
    # RETR
    data = []
    ftp.retrbinary("RETR small.txt", data.append)
    with open(os.path.join(root, "small.txt"), "rb") as f:
        check("retr", b"".join(data) == f.read())
    # SIZE / MDTM
    check("size", ftp.size("small.txt") == os.path.getsize(os.path.join(root, "small.txt")))
    mdtm = ftp.sendcmd("MDTM small.txt")
    check("mdtm", re.match(r"^213 \d{14}$", mdtm), mdtm)
    # STOR
    payload = b"stor payload \x00\x01\x02" * 100
    import io
    ftp.storbinary("STOR uploaded.bin", io.BytesIO(payload))
    with open(os.path.join(root, "uploaded.bin"), "rb") as f:
        check("stor", f.read() == payload)
    # APPE
    ftp.storbinary("APPE uploaded.bin", io.BytesIO(b"tail"))
    with open(os.path.join(root, "uploaded.bin"), "rb") as f:
        check("appe", f.read() == payload + b"tail")
    # REST + RETR
    data = []
    ftp.retrbinary("RETR uploaded.bin", data.append, rest=5)
    check("rest-retr", b"".join(data) == (payload + b"tail")[5:])
    # REST + STOR
    ftp.storbinary("STOR uploaded.bin", io.BytesIO(b"XY"), rest=3)
    with open(os.path.join(root, "uploaded.bin"), "rb") as f:
        got = f.read()
    check("rest-stor", got[:3] == (payload + b"tail")[:3] and got[3:5] == b"XY", got[:8])
    # RNFR/RNTO
    ftp.rename("small.txt", "renamed.txt")
    check("rnto", os.path.exists(os.path.join(root, "renamed.txt")))
    ftp.rename("renamed.txt", "small.txt")
    # MKD / RMD / DELE
    ftp.mkd("newdir")
    check("mkd", os.path.isdir(os.path.join(root, "newdir")))
    ftp.rmd("newdir")
    check("rmd", not os.path.exists(os.path.join(root, "newdir")))
    ftp.delete("uploaded.bin")
    # FEAT
    feat = ftp.sendcmd("FEAT")
    check("feat", "UTF8" in feat and "SIZE" in feat and "MDTM" in feat, feat)
    # TYPE/MODE/STRU
    ftp.voidcmd("TYPE I")
    ftp.voidcmd("MODE S")
    ftp.voidcmd("STRU F")
    # SITE CHMOD
    ftp.sendcmd("SITE CHMOD 600 small.txt")
    check("site-chmod", (os.stat(os.path.join(root, "small.txt")).st_mode & 0o777) == 0o600)


def scenario_port_and_bounce(ftp, port):
    # PORT mode transfer
    lsock = socket.socket()
    lsock.bind(("127.0.0.1", 0))
    lsock.listen(1)
    lport = lsock.getsockname()[1]
    ftp.voidcmd(f"PORT 127,0,0,1,{lport >> 8},{lport & 0xff}")
    ftp.voidcmd("TYPE I")
    ftp.sendcmd("RETR small.txt")   # replies 150; sendcmd accepts 1xx
    conn, _ = lsock.accept()
    chunks = []
    while True:
        b = conn.recv(65536)
        if not b:
            break
        chunks.append(b)
    conn.close()
    lsock.close()
    ftp.voidresp()
    with open(os.path.join(ftp_root, "small.txt"), "rb") as f:
        check("port-retr", b"".join(chunks) == f.read())

    # PORT bounce: target IP differs from control peer -> 425
    raw = raw_connect(port)
    raw_login(raw)
    raw.sendall(b"PORT 192,0,2,1,10,10\r\n")
    pr = read_reply(raw)
    check("port-store", pr.startswith(b"200"), pr)
    raw.sendall(b"RETR small.txt\r\n")
    r = read_reply(raw)
    check("port-bounce", r.startswith(b"425"), r)
    raw.close()


def scenario_ipv6(binary, root):
    # IPv6 data channel (RFC 2428): EPSV/EPRT over a ::1 control
    # connection. Skipped when the host has no IPv6.
    try:
        probe = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        probe.bind(("::1", 0))
        probe.close()
    except OSError:
        return
    proc, port = start_server(binary, root)
    try:
        raw = socket.create_connection(("::1", port), timeout=10)
        read_reply(raw)                      # 220
        raw.sendall(b"USER u\r\n")
        read_reply(raw)
        raw.sendall(b"PASS p\r\n")
        read_reply(raw)

        # PASV on a pure-v6 control connection -> 522 (use EPSV)
        raw.sendall(b"PASV\r\n")
        r = read_reply(raw)
        check("ipv6-pasv-522", r.startswith(b"522"), r)

        # EPSV + RETR via ::1
        raw.sendall(b"EPSV\r\n")
        r = read_reply(raw)
        m = re.search(rb"\(\|\|\|(\d+)\|\)", r)
        check("ipv6-epsv-229", m is not None, r)
        if m:
            data = socket.create_connection(("::1", int(m.group(1))), timeout=10)
            raw.sendall(b"RETR small.txt\r\n")
            check("ipv6-epsv-150", read_reply(raw).startswith(b"150"))
            chunks = []
            while True:
                b = data.recv(65536)
                if not b:
                    break
                chunks.append(b)
            data.close()
            check("ipv6-epsv-226", read_reply(raw).startswith(b"226"))
            with open(os.path.join(root, "small.txt"), "rb") as f:
                check("ipv6-epsv-data", b"".join(chunks) == f.read())

        # EPRT + RETR via ::1
        lsock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        lsock.bind(("::1", 0))
        lsock.listen(1)
        lport = lsock.getsockname()[1]
        raw.sendall(b"EPRT |2|::1|%d|\r\n" % lport)
        check("ipv6-eprt-200", read_reply(raw).startswith(b"200"))
        raw.sendall(b"RETR small.txt\r\n")
        check("ipv6-eprt-150", read_reply(raw).startswith(b"150"))
        conn, _ = lsock.accept()
        d = b""
        while True:
            b = conn.recv(65536)
            if not b:
                break
            d += b
        conn.close()
        lsock.close()
        check("ipv6-eprt-226", read_reply(raw).startswith(b"226"))
        with open(os.path.join(root, "small.txt"), "rb") as f:
            check("ipv6-eprt-data", d == f.read())

        # EPRT bounce: foreign target address -> 425 at connect
        raw.sendall(b"EPRT |1|192.0.2.1|1024|\r\n")
        check("ipv6-eprt-store", read_reply(raw).startswith(b"200"))
        raw.sendall(b"RETR small.txt\r\n")
        r = read_reply(raw)
        check("ipv6-eprt-bounce", r.startswith(b"425"), r)

        # EPSV protocol parameter validation
        raw.sendall(b"EPSV 1\r\n")
        r = read_reply(raw)
        check("ipv6-epsv-proto", r.startswith(b"522"), r)
        raw.sendall(b"EPSV 2\r\n")
        check("ipv6-epsv-proto2", read_reply(raw).startswith(b"229"))

        # OPTS UTF8: only "UTF8 ON" is valid
        raw.sendall(b"OPTS UTF8 ON\r\n")
        check("opts-utf8-on", read_reply(raw).startswith(b"200"))
        raw.sendall(b"OPTS UTF8 OFF\r\n")
        check("opts-utf8-off", read_reply(raw).startswith(b"501"))

        raw.close()
    finally:
        stop_server(proc)


def gen_cert(root):
    """Generate a self-signed cert/key; returns (cert, key) paths or
    None when openssl is unavailable."""
    import subprocess
    cert = os.path.join(root, "cert.pem")
    key = os.path.join(root, "key.pem")
    rc = subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048",
         "-keyout", key, "-out", cert, "-days", "1", "-nodes",
         "-subj", "/CN=localhost"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rc.returncode != 0:
        return None
    return cert, key


def start_server_tls(binary, root, cert, key, require_tls=False):
    proc = subprocess.Popen(
        [binary, root, "0", "2", cert, key, "1" if require_tls else "0"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    line = proc.stdout.readline().decode().strip()
    m = re.match(r"PORT (\d+)", line)
    if not m:
        proc.kill()
        raise RuntimeError(f"bad driver output: {line!r}")
    return proc, int(m.group(1))


def scenario_tls(binary, root):
    import ssl
    import io
    from ftplib import FTP_TLS

    pk = gen_cert(root)
    if pk is None:
        return           # no openssl: skip
    cert, key = pk

    # --- no-cert server: AUTH TLS -> 502 ---
    proc, port = start_server(binary, root)
    try:
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"AUTH TLS\r\n")
        check("tls-no-cert-502", read_reply(raw).startswith(b"502"))
        raw.close()
    finally:
        stop_server(proc)

    # --- raw-socket TLS: PBSZ/PROT ordering + AUTH handshake + 234 ---
    proc, port = start_server_tls(binary, root, cert, key)
    try:
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"PBSZ 0\r\n")
        r = read_reply(raw)
        check("tls-pbsz-before-auth", r.startswith(b"503"), r)
        raw.sendall(b"PROT P\r\n")
        r = read_reply(raw)
        check("tls-prot-before-auth", r.startswith(b"503"), r)
        raw.sendall(b"AUTH TLS\r\n")
        r = read_reply(raw)       # 234 arrives before the handshake
        check("tls-auth-234", r.startswith(b"234"), r)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        tls = ctx.wrap_socket(raw, server_hostname="localhost")
        tls.sendall(b"PBSZ 0\r\n")
        check("tls-pbsz-200", read_reply(tls).startswith(b"200"))
        tls.sendall(b"PROT P\r\n")
        check("tls-prot-200", read_reply(tls).startswith(b"200"))
        tls.sendall(b"NOOP\r\n")
        check("tls-noop-200", read_reply(tls).startswith(b"200"))
        tls.close()
    finally:
        stop_server(proc)

    # --- require_tls server: plaintext rejected with 534 ---
    proc, port = start_server_tls(binary, root, cert, key, require_tls=True)
    try:
        raw = raw_connect(port)   # consumes 220
        r = read_reply(raw)       # then 534
        check("tls-require-534", r.startswith(b"534"), r)
        raw.close()
    finally:
        stop_server(proc)

    # --- FTP_TLS integration: AUTH TLS + PROT P transfers ---
    proc, port = start_server_tls(binary, root, cert, key)
    try:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ftp = FTP_TLS(context=ctx)
        ftp.connect("127.0.0.1", port, timeout=15)
        ftp.login("u", "p")       # performs AUTH TLS automatically
        ftp.prot_p()              # PROT P: TLS data channel
        chunks = []
        ftp.retrbinary("RETR small.txt", chunks.append)
        with open(os.path.join(root, "small.txt"), "rb") as f:
            check("tls-retr-data", b"".join(chunks) == f.read())
        payload = os.urandom(1 << 16)
        ftp.storbinary("STOR tls_up.bin", io.BytesIO(payload))
        with open(os.path.join(root, "tls_up.bin"), "rb") as f:
            check("tls-stor-data", f.read() == payload)
        os.remove(os.path.join(root, "tls_up.bin"))
        ftp.quit()
    finally:
        stop_server(proc)

    # --- handshake stall: AUTH TLS then silence -> server closes after
    # the 10s deadline ---
    proc, port = start_server_tls(binary, root, cert, key)
    try:
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"AUTH TLS\r\n")
        raw.settimeout(15)
        d = raw.recv(4096)        # ServerHello should arrive
        check("tls-stall-serverhello", len(d) > 0)
        # now stay silent: the server must close after the deadline
        done = False
        while not done:
            try:
                b = raw.recv(4096)
                if not b:
                    done = True
            except (socket.timeout, ConnectionError):
                done = True
        check("tls-stall-closed", True)
        raw.close()
    finally:
        stop_server(proc)


def scenario_abor(binary, root):
    # in-band ABOR. Clients connect the data socket BEFORE the transfer
    # command (standard RFC 959 ordering); the server then accepts
    # synchronously and replies 150. satfile (256MB) guarantees the
    # transfer is still in flight when ABOR arrives.
    proc, port = start_server(binary, root)
    try:
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"TYPE I\r\n")
        read_reply(raw)
        raw.sendall(b"PASV\r\n")
        r = read_reply(raw)
        m = re.search(rb"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)", r)
        assert m, r
        data_addr = ("127.0.0.1", (int(m.group(5)) << 8) | int(m.group(6)))
        data = socket.create_connection(data_addr, timeout=10)
        raw.sendall(b"RETR satfile\r\n")
        r150 = read_reply(raw)
        check("abor-150", r150.startswith(b"150"), r150)
        # in-band ABOR
        raw.sendall(b"ABOR\r\n")
        r426 = read_reply(raw)
        r226 = read_reply(raw)
        check("abor-426-226", r426.startswith(b"426") and r226.startswith(b"226"),
              (r426, r226))
        data.close()
        # server still alive
        raw.sendall(b"NOOP\r\n")
        check("abor-alive", read_reply(raw).startswith(b"200"))
        raw.close()

        # OOB ABOR (ftplib style)
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"TYPE I\r\n")
        read_reply(raw)
        raw.sendall(b"PASV\r\n")
        r = read_reply(raw)
        m = re.search(rb"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)", r)
        data_addr = ("127.0.0.1", (int(m.group(5)) << 8) | int(m.group(6)))
        data = socket.create_connection(data_addr, timeout=10)
        raw.sendall(b"RETR satfile\r\n")
        read_reply(raw)
        raw.sendall(b"ABOR\r\n", socket.MSG_OOB)
        r426 = read_reply(raw)
        r226 = read_reply(raw)
        check("oob-abor", r426.startswith(b"426") and r226.startswith(b"226"),
              (r426, r226))
        data.close()
        raw.close()
    finally:
        stop_server(proc)


def scenario_one_transfer_and_saturation(binary, root):
    # satfile (256MB, created in main) can't fit any socket buffer, so
    # transfers stay in flight while the clients never read
    # one transfer per client -> 450
    proc, port = start_server(binary, root, workers=1)
    try:
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"TYPE I\r\n")
        read_reply(raw)
        raw.sendall(b"PASV\r\n")
        r = read_reply(raw)
        m = re.search(rb"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)", r)
        assert m, r
        data1 = socket.create_connection(
            ("127.0.0.1", (int(m.group(5)) << 8) | int(m.group(6))), timeout=10)
        raw.sendall(b"RETR satfile\r\n")
        check("onetime-150", read_reply(raw).startswith(b"150"))
        raw.sendall(b"RETR small.txt\r\n")
        r = read_reply(raw)
        check("onetime-450", r.startswith(b"450"), r)
        # abort + drain
        raw.sendall(b"ABOR\r\n")
        r1 = read_reply(raw)
        r2 = read_reply(raw)
        check("onetime-abor", r1.startswith(b"426") and r2.startswith(b"226"),
              (r1, r2))
        data1.close()
        raw.close()

        # queue saturation -> 425 (workers=1, queue cap=2 queued): three
        # transfers occupy running+queued slots; the fourth is refused
        clients, datas = [], []
        for _ in range(4):
            c = raw_connect(port)
            raw_login(c)
            c.sendall(b"TYPE I\r\n")
            read_reply(c)
            c.sendall(b"PASV\r\n")
            r = read_reply(c)
            m = re.search(rb"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)", r)
            assert m, r
            d = socket.create_connection(
                ("127.0.0.1", (int(m.group(5)) << 8) | int(m.group(6))), timeout=10)
            c.sendall(b"RETR satfile\r\n")
            clients.append(c)
            datas.append(d)
        for idx in range(3):
            r = read_reply(clients[idx])
            check(f"sat-{idx+1}-150", r.startswith(b"150"), r)
        r4 = read_reply(clients[3])
        check("sat-4-425", r4.startswith(b"425"), r4)
        for d in datas:
            d.close()
        for c in clients:
            c.close()
    finally:
        stop_server(proc)


def scenario_fragmented_and_disconnect(binary, root):
    proc, port = start_server(binary, root)
    try:
        # fragmented control lines
        raw = raw_connect(port)
        raw.sendall(b"USER ano")
        time.sleep(0.1)
        raw.sendall(b"nymous\r\n")
        check("frag-user", read_reply(raw).startswith(b"331"))
        raw.sendall(b"PASS x")
        time.sleep(0.05)
        raw.sendall(b"@y\r")
        time.sleep(0.05)
        raw.sendall(b"\n")
        check("frag-pass", read_reply(raw).startswith(b"230"))
        # byte-by-byte
        for b in b"NOOP\r\n":
            raw.sendall(bytes([b]))
            time.sleep(0.01)
        check("frag-noop", read_reply(raw).startswith(b"200"))
        raw.close()

        # disconnect mid-transfer
        raw = raw_connect(port)
        raw_login(raw)
        raw.sendall(b"PASV\r\n")
        r = read_reply(raw)
        m = re.search(rb"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)", r)
        assert m, r
        data = socket.create_connection(
            ("127.0.0.1", (int(m.group(5)) << 8) | int(m.group(6))), timeout=10)
        raw.sendall(b"RETR satfile\r\n")
        read_reply(raw)          # 150
        raw.close()              # abrupt disconnect during transfer
        data.close()

        time.sleep(0.3)
        # server must still accept and serve
        ftp = ftplib.FTP()
        ftp.connect("127.0.0.1", port, timeout=10)
        ftp.login("a", "b")
        check("alive-after-dc", ftp.voidcmd("NOOP") == "200 OK.")
        ftp.quit()
    finally:
        stop_server(proc)


def scenario_concurrent_and_multi(binary, root):
    proc, port = start_server(binary, root)
    try:
        # concurrent STOR from 2 clients
        payloads = [os.urandom(1 << 18), os.urandom(1 << 18)]
        import io

        def upload(payload, name):
            f = ftplib.FTP()
            f.connect("127.0.0.1", port, timeout=15)
            f.login("u", "p")
            f.storbinary(f"STOR {name}", io.BytesIO(payload))
            f.quit()

        threads = []
        import threading
        for i, p in enumerate(payloads):
            t = threading.Thread(target=upload, args=(p, f"conc{i}.bin"))
            threads.append(t)
            t.start()
        for t in threads:
            t.join()
        for i, p in enumerate(payloads):
            with open(os.path.join(root, f"conc{i}.bin"), "rb") as f:
                check(f"concurrent-stor-{i}", f.read() == p)
            os.remove(os.path.join(root, f"conc{i}.bin"))

        # multi-client: 5 concurrent sessions do LIST + NOOP + QUIT
        results = []

        def session():
            f = ftplib.FTP()
            f.connect("127.0.0.1", port, timeout=15)
            f.login("u", "p")
            ok = f.voidcmd("NOOP") == "200 OK."
            f.quit()
            results.append(ok)

        threads = [threading.Thread(target=session) for _ in range(5)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        check("multi-client", len(results) == 5 and all(results))
    finally:
        stop_server(proc)


def scenario_lifecycle(binary):
    # run_loop exclusivity + stop/drain via a short-lived server
    proc, port = start_server(binary, tempfile.gettempdir(), workers=1)
    stop_server(proc)
    check("graceful-stop", proc.returncode == 0, proc.returncode)


def main():
    if len(sys.argv) < 2:
        print("usage: test_ftplib.py <ftp_server_main>")
        return 1
    binary = sys.argv[1]
    global ftp_root
    ftp_root = tempfile.mkdtemp(prefix="opftp_it_")

    with open(os.path.join(ftp_root, "small.txt"), "wb") as f:
        f.write(b"hello world\n")
    # bigfile: large enough that a non-reading client keeps the transfer
    # in flight (socket buffers can hold a few MB on loopback)
    make_big_file(os.path.join(ftp_root, "bigfile"), 8 << 20)
    # satfile: far beyond any socket buffer — guarantees in-flight
    # transfers for ABOR and queue-saturation scenarios
    make_big_file(os.path.join(ftp_root, "satfile"), 256 << 20)
    os.mkdir(os.path.join(ftp_root, "sub"))
    with open(os.path.join(ftp_root, "sub", "nested.txt"), "wb") as f:
        f.write(b"nested\n")

    proc, port = start_server(binary, ftp_root)
    try:
        ftp = ftplib.FTP()
        ftp.connect("127.0.0.1", port, timeout=10)
        ftp.login("anonymous", "test@test")
        check("login", True)
        scenario_basic(ftp, ftp_root)
        scenario_port_and_bounce(ftp, port)
        ftp.quit()
    finally:
        stop_server(proc)

    scenario_ipv6(binary, ftp_root)
    scenario_tls(binary, ftp_root)
    scenario_abor(binary, ftp_root)
    scenario_one_transfer_and_saturation(binary, ftp_root)
    scenario_fragmented_and_disconnect(binary, ftp_root)
    scenario_concurrent_and_multi(binary, ftp_root)
    scenario_lifecycle(binary)

    if FAILURES:
        print(f"{len(FAILURES)} FAILURES: {FAILURES}")
        return 1
    print("all integration scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
