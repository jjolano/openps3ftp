# OpenPS3FTP ps3dk cross-compile toolchain.
# Usage: cmake -B build-ps3 -S . -DCMAKE_TOOLCHAIN_FILE=cmake/ps3dk.cmake
#
# Requires PS3DEV pointing at a ps3dev/ps3dk stage
# (e.g. /home/coder/PS3DK/stage/ps3dev with ps3dk/ + ppu/ + portlibs/).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)

if(NOT DEFINED ENV{PS3DEV})
    set(ENV{PS3DEV} /home/coder/PS3DK/stage/ps3dev)
endif()
set(PS3DEV $ENV{PS3DEV})

set(CMAKE_C_COMPILER ${PS3DEV}/ppu/bin/powerpc64-ps3-elf-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS "-mcpu=cell -mhard-float -fmodulo-sched -D__PPU__" CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH
    ${PS3DEV}/ps3dk
    ${PS3DEV}/ppu
    ${PS3DEV}/portlibs/ppu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# The ps3 port defines
add_definitions(-DOPFTP_PS3=1)
set(OPFTP_PS3 ON CACHE BOOL "PS3 build" FORCE)
