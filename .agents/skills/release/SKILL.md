---
name: release
description: Release OpenPS3FTP — build the PS3 binaries and publish a tagged GitHub release. Use when the user asks to release, cut a release, tag a version, build the release assets, or mentions PKG/EBOOT/SPRX shipping. Requires the PS3 toolchain at ~/PS3DK; the release assets cannot be built by CI.
---

# OpenPS3FTP release

Ship a version like the v5.x line: annotated tag, two binary assets
(`openps3ftp-vX.Y.zip` = PKG dist, `openps3ftp.sprx` = vsh plugin),
uploaded to the GitHub release with v5.1's exact file names.

## Workflow

1. **Prereqs.** `source ~/PS3DK/scripts/env.sh` (sets PS3DEV/PSL1GHT/PATH).
   Verify `powerpc64-ps3-elf-gcc`, `scetool`, `sprx` resolve. scetool's
   NPDRM keys come from the `PS3` env var — `env.sh` exports it.

2. **Stage freshness gate.** The toolchain aborts configure when the
   installed stage is stale: `ps3-self: stale SDK install. source
   vX.Y installed vX.Y+dirty — Run: make -C ~/PS3DK/sdk install`.
   If that error appears, run it, then re-run the configure.

3. **Bump the version.** Add a `[Version X.Y - date]` entry to the top
   of `changelog.txt` summarising the release, and commit it. Version
   comes from `git describe --always` for both the zip name and the
   in-app suffix, so the tag must exist before the dist build (see
   step 7 ordering).

4. **Tag + push.** `git tag -a vX.Y -m "Version X.Y - <one-liner>"`
   then `git push origin vX.Y`. The ZIP is named from this tag — do
   not build `make dist` before it exists, or you get
   `openps3ftp-<commithash>.zip`.

5. **PS3 library** (all refactor gates live here; CI runs this step):
   ```
   cmake -B build-ps3 -S . -DCMAKE_TOOLCHAIN_FILE=cmake/ps3dk.cmake -DOPFTP_TLS=ON
   cmake --build build-ps3 -j$(nproc)
   cmake --install build-ps3        # -> $PS3DEV/portlibs/ppu
   ```
   Done when `$PS3DEV/portlibs/ppu/lib/libopenps3ftp.a` and
   `include/openps3ftp/openps3ftp.h` are fresh.

6. **App EBOOT:** `cd app && make EBOOT.BIN`. Done when scetool prints
   `EBOOT.BIN written` and `app/EBOOT.BIN` is new. Build notes:
   - The toolchain you just installed is the modern unified one
     (`~/PS3DK/cmake/...`), but the app/plugin still build against the
     PSL1GHT stage layout; the repo `cmake/ps3dk.cmake` is the library
     path, NOT the app path.
   - `app/Makefile` LIBS must include `-lfs_stub`: the `cellFs*` API
     used by `src/fs_ps3.c` lives in `libfs_stub.a`, not `libsysfs.a`.
     Missing it = undefined `cellFsOpen`/`cellFsStat`/... link errors.
   - The `crtend.o .eh_frame` linker note is benign.

7. **PKG dist zip:** `cp app/EBOOT.BIN pkg/ && cd pkg && make dist`, then
   move `pkg/openps3ftp-vX.Y.zip` to a release-staging dir. Rebuild the
   zip as `openps3ftp-vX.Y.zip` if `git describe` named it with the hash.
   Done when an `openps3ftp-vX.Y.zip` with both `dist/rex` and `dist/cex`
   PKGs exists.

8. **Plugin SPRX:** the plugin builds with the **toolchain's** cmake
   files, not the repo's:
   ```
   cmake -S plugin -B plugin/build \
     -DCMAKE_TOOLCHAIN_FILE=~/PS3DK/cmake/ps3-ppu-toolchain.cmake
   cmake --build plugin/build -j$(nproc)
   ```
   `ps3-self.cmake` writes final artifacts next to the SOURCE dir, so
   the result is `plugin/openps3ftp-plugin.sprx`. The
   `cannot find entry symbol _start` warning is expected for a plugin.
   The web-console assets (index.html/style.css/app.js) are embedded by
   `ps3_bin2s`; changing them requires this rebuild.

9. **Publish:**
   ```
   gh release create vX.Y --title "Version X.Y" --notes <release notes>
   gh release upload vX.Y pkg/openps3ftp-vX.Y.zip plugin/openps3ftp-plugin.sprx
   ```
   Asset names must match v5.1 EXACTLY (`openps3ftp.sprx`, not
   `openps3ftp-plugin.sprx` — rename on disk before upload; `gh release
   upload` has no --rename flag). Done when `gh release view vX.Y`
   lists both assets.

## Ordering traps

- Tag BEFORE `make dist` (zip name) — step 4 before step 7.
- Fresh EBOOT into `pkg/` before `make dist` — the pkg build copies
  whatever `pkg/EBOOT.BIN` holds; stale EBOOT = stale signed binary.
- The rex/cex pair: `make dist` builds RETAIL_PKG=0 rex and =1 cex
  (finalize step needs the SDK stage's keys; a failed finalize still
  ships rex, so verify BOTH PKGs exist in the zip).