# OpenPS3FTP

Open source FTP server for the PlayStation 3 — full rewrite (C11) with a
compatibility shim for the old API.

- **New core** (`src/`): reactor + worker pool, explicit FTPS (AUTH TLS),
  IPv6 (EPSV/EPRT), UTF-8 (RFC 2640), one `opftp_server_t` owns everything.
- **Legacy shim** (`legacy/`): the old OpenPS3FTP API (`server_init`,
  `client_send_code`, `command_register`, …) runs on the new core, so
  consumers like multiMAN / webMAN / IRISMAN can link the new library
  unchanged. The shim exports the old archive name
  `libopenps3ftp_psl1ght.a`.
- **Legacy consumers** (`feat/`): the original command handlers
  (base/ext/site/feat) compile unchanged against the shim — verified by the
  `legacy_smoke` host test.

## Layout

| Path | What it is |
|------|------------|
| `src/` | New core library (reactor, transfers, commands, TLS, fs backends) |
| `include/openps3ftp/` | New public API (`openps3ftp.h`) |
| `legacy/` | Compatibility shim: old headers + facades over the new core |
| `feat/` | Unchanged legacy command handlers (base/ext/site/feat) |
| `app/` | PS3 app (EBOOT), new API |
| `tests/` | Host unit tests, ftplib integration drivers, legacy smoke app |
| `third_party/mbedtls/` | Vendored mbedtls 2.28 (TLS), same source for host and PS3 |
| `DESIGN.md` | Full design: architecture, concurrency model, phases |

## Building (host)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build          # unit + lifecycle + ftplib + legacy smoke
```

Sanitizer variants:

```sh
cmake -S . -B build-asan -DOPFTP_SANITIZE=address
cmake --build build-asan && ctest --test-dir build-asan

cmake -S . -B build-tsan -DOPFTP_SANITIZE=thread
cmake --build build-tsan && ctest --test-dir build-tsan
```

## Building (PS3, ps3dk/PSL1GHT)

Prereqs: ps3dk toolchain (`PS3DEV`, `PSL1GHT`, `PORTLIBS` env vars set).

```sh
cmake -S . -B build-ps3 -DOPFTP_PS3=1 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ps3dk.cmake
cmake --build build-ps3         # libopenps3ftp.a + compat archive
cmake --install build-ps3       # into $PORTLIBS (ps3dev convention)

make -C app EBOOT.BIN           # PS3 app (new API): ftp.elf -> EBOOT.BIN
```

The old Makefile build tree (`lib/`, `bin/`, `external/ps3ntfs`) was removed
in the P6 cleanup; NTFS (`/dev_ntfs`) support is compiled out of the new core.

## Options

| CMake option | Default | Meaning |
|---|---|---|
| `OPFTP_TLS` | `ON` | FTPS support (AUTH TLS) via vendored mbedtls |
| `OPFTP_SANITIZE` | `off` | `address` or `thread` sanitizer for host builds |
| `OPFTP_PS3` | — | PS3 (ps3dk) build; use the toolchain file |

## Users

There are two builds included in a standard distribution: CEX and REX.
CEX is usable on (O/C)FW 3.40 to 3.55, and REX is for any CFW 3.56+.

Bug reports are welcome at the GitHub repository.
