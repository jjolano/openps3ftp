#!/usr/bin/env python3
"""
P5 symbol-surface gate (DESIGN.md, "Legacy compatibility shim").

Legacy consumers (multiMAN/webMAN/IRISMAN, the old feat/ code) link
against the old OpenPS3FTP API. The shim must therefore *define* every
function the old headers declare — if one goes missing, those consumers
fail at link time, which is exactly the breakage the rewrite promised
not to cause. A compile-only smoke test does not catch it: the smoke app
only references the handful of symbols it happens to call.

So: parse the function declarations out of the legacy headers, run `nm`
over the built archive, and require every declared name to be defined.

Usage: check_symbols.py <libopenps3ftp.a> <legacy-include-dir>
"""
import re
import subprocess
import sys
import os

# Old public headers, the ones legacy consumers include. openps3ftp/ is
# the NEW api and is checked by the normal tests, not this gate.
LEGACY_HEADERS = [
    "avlutils.h", "client.h", "command.h", "common.h", "const.h",
    "io.h", "pftutils.h", "server.h", "sys.thread.h", "thread.h",
    "types.h", "util.h",
]

# A function declaration at file scope: "<type> name(args);" — no body,
# not a call, not a macro. Deliberately conservative: this gate should
# never produce a false alarm that trains people to ignore it.
DECL = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ \t*]*?[ \t*]([A-Za-z_][A-Za-z0-9_]*)[ \t]*\([^;{]*\)[ \t]*;",
    re.MULTILINE)

# Declared in the legacy headers but supplied by the platform, not the
# shim: common.h forward-declares these because the old ps3 SDK headers
# did not. They resolve against libc / libnet at link time.
EXEMPT = {
    "main",
    "usleep",
    "closesocket",
}


def strip_comments_and_macros(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    # drop preprocessor lines: a #define with parens looks like a decl
    text = "\n".join(l for l in text.split("\n") if not l.lstrip().startswith("#"))
    # typedef'd function pointers are types, not symbols
    text = "\n".join(l for l in text.split("\n") if not l.lstrip().startswith("typedef"))
    return text


def declared_symbols(include_dir):
    names = set()
    for h in LEGACY_HEADERS:
        path = os.path.join(include_dir, h)
        if not os.path.exists(path):
            continue
        with open(path, "r", errors="replace") as f:
            for m in DECL.finditer(strip_comments_and_macros(f.read())):
                names.add(m.group(1))
    return names - EXEMPT


def defined_symbols(archive):
    nm = os.environ.get("NM", "nm")
    try:
        out = subprocess.run([nm, "-g", "--defined-only", archive],
                             capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        print(f"SKIP: {nm} not found")
        sys.exit(0)
    except subprocess.CalledProcessError as e:
        print(f"FAIL: {nm} failed: {e.stderr}")
        sys.exit(1)
    names = set()
    for line in out.splitlines():
        parts = line.split()
        # "<addr> T name" or "T name" (undefined/aliased lines have fewer)
        if len(parts) >= 2 and parts[-2] in ("T", "t", "D", "B", "R", "W"):
            names.add(parts[-1].lstrip("_") if parts[-1].startswith("__")
                      else parts[-1])
    return names


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    archive, include_dir = sys.argv[1], sys.argv[2]

    declared = declared_symbols(include_dir)
    defined = defined_symbols(archive)
    if not declared:
        print(f"FAIL: no declarations parsed from {include_dir}")
        return 1

    missing = sorted(declared - defined)
    print(f"legacy headers declare {len(declared)} functions; "
          f"archive defines {len(defined)} symbols")
    if missing:
        print(f"FAIL: {len(missing)} declared but not defined by the shim:")
        for name in missing:
            print(f"  {name}")
        return 1
    print("PASS: shim defines every function the legacy headers declare")
    return 0


if __name__ == "__main__":
    sys.exit(main())
