# Host harness for the vsh-plugin web console (Linux).
# Verifies httpd.c + the shared UI model against the real FTP server.
cmake_minimum_required(VERSION 3.16)
project(openps3ftp-host C)

set(CMAKE_C_STANDARD 11)

set(CORE_SRCS
  ../../src/thread.c
  ../../src/pollset.c
  ../../src/resolve.c
  ../../src/listing.c
  ../../src/utf8.c
  ../../src/fs_mem.c
  ../../src/fs_rooted.c
  ../../src/fs_posix.c
  ../../src/client.c
  ../../src/dispatch.c
  ../../src/transfer.c
  ../../src/datachan.c
  ../../src/commands.c
  ../../src/server.c
  ../../src/tls.c)

add_executable(openps3ftp-host
  host_main.c
  ../httpd.c
  ../../app/ui.c
  ${CORE_SRCS})

target_include_directories(openps3ftp-host PRIVATE
  ../../include ../../src ../../app ..)
# OPFTP_PS3 must stay UNDEFINED (code uses #ifdef, not #if) to get
# the host paths; OPFTP_TLS likewise undefined -> tls.c stubs.
target_compile_definitions(openps3ftp-host PRIVATE)

find_package(Threads REQUIRED)
target_link_libraries(openps3ftp-host PRIVATE Threads::Threads m)
