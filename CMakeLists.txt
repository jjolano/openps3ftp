cmake_minimum_required(VERSION 3.16)
project(openps3ftp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

set(OPFTP_SRCS
  src/thread.c
  src/pollset.c
  src/resolve.c
  src/listing.c
  src/utf8.c
  src/fs_mem.c
  src/fs_rooted.c
  src/client.c
  src/dispatch.c
  src/transfer.c
  src/datachan.c
  src/commands.c
  src/server.c
  src/tls.c
)

# ---- P5: legacy compatibility shim (old OpenPS3FTP API on the new core) ----
set(OPFTP_LEGACY_SRCS
  legacy/src/ftpserver.c
  legacy/src/ftpclient.c
  legacy/src/ftpcommand.c
  legacy/src/avlutils.c
  legacy/src/pftutils.c
  legacy/src/sys.thread.c
  legacy/src/thread.c
  legacy/src/ftputil.c
)

if(OPFTP_PS3)
  list(APPEND OPFTP_SRCS src/fs_ps3.c)
  add_compile_definitions(OPFTP_PS3=1)
  list(APPEND OPFTP_LEGACY_SRCS legacy/src/ftpio.c)
  # legacy code expects the old SDK macros (types.h/common.h)
  set_source_files_properties(${OPFTP_LEGACY_SRCS}
    PROPERTIES COMPILE_DEFINITIONS "PSL1GHT_SDK=1;PS3DK_SDK=1")
else()
  list(APPEND OPFTP_SRCS src/fs_posix.c)
  list(APPEND OPFTP_LEGACY_SRCS legacy/src/ftpio_linux.c)
  set_source_files_properties(${OPFTP_LEGACY_SRCS}
    PROPERTIES COMPILE_DEFINITIONS "LINUX=1")
endif()

add_library(openps3ftp STATIC ${OPFTP_SRCS} ${OPFTP_LEGACY_SRCS})
target_include_directories(openps3ftp PUBLIC include PRIVATE src legacy/src)
target_link_libraries(openps3ftp PUBLIC Threads::Threads)

# P5: legacy consumers link -lopenps3ftp_psl1ght; emit the same objects
# under the old archive name.
add_custom_command(TARGET openps3ftp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:openps3ftp>
    ${CMAKE_BINARY_DIR}/libopenps3ftp_psl1ght.a
  COMMENT "Creating legacy archive libopenps3ftp_psl1ght.a")

# ---- TLS: vendored mbedtls 2.28.10 (same source for host and ps3) ----
option(OPFTP_TLS "Build with FTPS support (vendored mbedtls)" ON)
if(OPFTP_TLS)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/mbedtls)
  set_target_properties(mbedtls mbedx509 mbedcrypto PROPERTIES FOLDER third_party)
  # pinned config: stock 2.28 config minus MBEDTLS_NET_C (custom BIO)
  target_compile_definitions(mbedtls PUBLIC
    "MBEDTLS_CONFIG_FILE=\"opftp_mbedtls_config.h\"")
  target_compile_definitions(mbedx509 PUBLIC
    "MBEDTLS_CONFIG_FILE=\"opftp_mbedtls_config.h\"")
  target_compile_definitions(mbedcrypto PUBLIC
    "MBEDTLS_CONFIG_FILE=\"opftp_mbedtls_config.h\"")
  foreach(tgt mbedtls mbedx509 mbedcrypto)
    target_include_directories(${tgt}
      PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    if(OPFTP_PS3)
      # entropy_poll.c pulls <lv2/system.h> under __PPU__
      target_include_directories(${tgt} SYSTEM PUBLIC
        ${PS3DEV}/ps3dk/ppu/include)
    endif()
  endforeach()
  target_link_libraries(openps3ftp PUBLIC mbedtls mbedx509 mbedcrypto)
  target_compile_definitions(openps3ftp PUBLIC OPFTP_TLS=1)
else()
  target_compile_definitions(openps3ftp PUBLIC OPFTP_TLS=0)
endif()

# OPFTP_SANITIZE=address|thread|off
set(OPFTP_SANITIZE "off" CACHE STRING "Sanitizer: address, thread, or off")
if(OPFTP_SANITIZE STREQUAL "address")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address -static-libasan)
elseif(OPFTP_SANITIZE STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread -static-libtsan)
endif()

enable_testing()

if(OPFTP_PS3)
  # ps3 sysfs/net syscall stubs
  target_link_libraries(openps3ftp PUBLIC
    ${PS3DEV}/ps3dk/ppu/lib/libsysfs.a
    ${PS3DEV}/ps3dk/ppu/lib/libnet.a
    ${PS3DEV}/ps3dk/ppu/lib/libnetctl.a
    ${PS3DEV}/ps3dk/ppu/lib/liblv2.a
    ${PS3DEV}/ps3dk/ppu/lib/librt.a)
  # install into portlibs (ps3dev convention), like the PS3Libraries ports
  install(TARGETS openps3ftp mbedtls mbedx509 mbedcrypto
    ARCHIVE DESTINATION ${PS3DEV}/portlibs/ppu/lib)
  install(FILES ${CMAKE_BINARY_DIR}/libopenps3ftp_psl1ght.a
    DESTINATION ${PS3DEV}/portlibs/ppu/lib
    RENAME libopenps3ftp_psl1ght.a)
  install(FILES include/openps3ftp/openps3ftp.h
    DESTINATION ${PS3DEV}/portlibs/ppu/include/openps3ftp)
  # mbedtls headers (we disable INSTALL_MBEDTLS_HEADERS to keep the
  # subdir's install out of /usr/local)
  install(DIRECTORY third_party/mbedtls/include/mbedtls
    DESTINATION ${PS3DEV}/portlibs/ppu/include)
  install(DIRECTORY third_party/mbedtls/include/psa
    DESTINATION ${PS3DEV}/portlibs/ppu/include)
  # the host test harness is not built for ps3
  return()
endif()

add_executable(test_units tests/test_units.c)
target_link_libraries(test_units openps3ftp)
add_test(NAME units COMMAND test_units)

add_executable(test_lifecycle tests/test_lifecycle.c)
target_link_libraries(test_lifecycle openps3ftp)
add_test(NAME lifecycle COMMAND test_lifecycle)

add_executable(ftp_server_main tests/ftp_server_main.c)
target_link_libraries(ftp_server_main openps3ftp)
add_test(NAME ftplib COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_ftplib.py $<TARGET_FILE:ftp_server_main>)

# ---- P5: legacy API smoke test (old consumer code, unchanged) ----
# The feat/ sources are the unchanged legacy consumers (base.c etc.).
add_executable(legacy_smoke tests/legacy_smoke.c
  feat/base/base.c
  feat/ext/ext.c
  feat/site/site.c
  feat/feat/feat.c)
target_compile_definitions(legacy_smoke PRIVATE LINUX=1)
target_include_directories(legacy_smoke PRIVATE include feat)
target_link_libraries(legacy_smoke openps3ftp)
add_test(NAME legacy_ftplib COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_legacy_ftplib.py $<TARGET_FILE:legacy_smoke>)

# P5 gate: the shim must define every function the old headers declare,
# or legacy consumers fail at link time (DESIGN.md, "Symbol surface").
add_test(NAME symbols COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/check_symbols.py
  $<TARGET_FILE:openps3ftp> ${CMAKE_SOURCE_DIR}/include)
