# OpenPS3FTP vsh plugin (SPRX loaded by the XMB on CFW/HEN).
#
# Build:
#   cmake -S plugin -B plugin/build \
#     -DCMAKE_TOOLCHAIN_FILE=<PS3DK>/cmake/ps3-ppu-toolchain.cmake
#   cmake --build plugin/build
# Produces: plugin/openps3ftp-plugin.elf  +  openps3ftp-plugin.sprx
#
# A plugin has no main()/crt1: the loader calls module_start through
# the .lib.ent export table emitted by the SYS_MODULE_* macros in
# <sys/prx.h>.  Link freestanding, add the lv2-sprx.o prologue so
# .sys_proc_prx_param + the lib table prefixes are present, then run
# the ps3_add_sprx post-build chain (strip -> sprxlinker -> make_self).
#
# Unlike the hello-plugin sample, this links the FTP core, so it also
# needs libc: newlib's libc.a + libsysbase.a (the newlib syscall glue
# for ps3: _sbrk/_write/_exit/...) + libsysfs.a (sysFs* calls) + libgcc
# (64-bit div helpers on the 32-bit target).

cmake_minimum_required(VERSION 3.20)
project(openps3ftp-plugin C ASM)

include(ps3-self)

set(OPFTP_CORE_SRCS
  ../src/thread.c
  ../src/pollset.c
  ../src/resolve.c
  ../src/listing.c
  ../src/utf8.c
  ../src/fs_mem.c
  ../src/fs_rooted.c
  ../src/fs_ps3.c
  ../src/client.c
  ../src/dispatch.c
  ../src/transfer.c
  ../src/datachan.c
  ../src/commands.c
  ../src/server.c
  ../src/tls.c)

add_executable(openps3ftp-plugin
    main.c httpd.c
    ../app/ui.c           # shared UI model (third renderer: web)
    ${OPFTP_CORE_SRCS})
target_compile_features(openps3ftp-plugin PRIVATE c_std_11)

# Freestanding: no crt1, no libc, no default libraries.
target_compile_options(openps3ftp-plugin PRIVATE
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-PIC)

target_link_options(openps3ftp-plugin PRIVATE
    -nostartfiles -nodefaultlibs)

# Prologue object: .sys_proc_prx_param + .lib.ent.top/.lib.stub.top.
# Installed by build-runtime-lv2.sh next to lv2.ld.
target_link_libraries(openps3ftp-plugin PRIVATE
    "${PS3DEV}/ppu/powerpc64-ps3-elf/lib/lv2-sprx.o")

# libc + syscall glue + sysfs + lv2 + libgcc (start-group: circular
# refs between libc and libsysbase).  Mirrors the toolchain's default
# LIB_LV2_SPEC group (-lsysbase -lc -lrt -llv2 -llv2_stub -lc_stub)
# plus the sysfs archive; lv2_stub provides newlib's sys_lwmutex_*
# import pointers.  libfs/libnet are the real syscall-backed cellFs /
# sys_net implementations from the FirebirdTA01 PS3DK; libfs_stub and
# the old module-import libnet would not resolve cleanly in the vsh
# process, which is why netbridge.c (raw lv2 calls) existed before.
# All live in $PS3DK/ppu/lib.
target_link_libraries(openps3ftp-plugin PRIVATE
    -L"${PS3DK}/ppu/lib"
    -Wl,--start-group -lsysfs -lfs -lfs_stub -lnet -lsysbase -lc -lrt -llv2 -llv2_stub -lc_stub
                     -lm -lgcc -Wl,--end-group)

target_include_directories(openps3ftp-plugin PRIVATE
    ../include ../src ../app)
# OPFTP_TLS must stay UNDEFINED (tls.c uses #ifdef, not #if): undefined
# selects the tls.c stub path and avoids the mbedtls dependency.
target_compile_definitions(openps3ftp-plugin PRIVATE OPFTP_PS3=1)

# Embed the web console assets (index.html / style.css / app.js).
ps3_bin2s(openps3ftp-plugin ../app/webui/index.html)
ps3_bin2s(openps3ftp-plugin ../app/webui/style.css)
ps3_bin2s(openps3ftp-plugin ../app/webui/app.js)

ps3_add_sprx(openps3ftp-plugin)
