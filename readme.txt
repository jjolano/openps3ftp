OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in and out of their console.

Developers:
- For PSL1GHT, a static library can be built by using make lib. To install
  to the standard PSL1GHT libraries, use make install.
- For cellsdk, try using make with SDK=CELL.
- Check "common.h" for compile-time options. Apply changes to the Makefile.
- The only include file needed in your app is <ftp/server.h> which exposes
  the function server_start and C struct app_status. These are designed to be
  used in a separate PPU thread. See main.cpp for example usage.
- A modified libsysutil for PSL1GHT is included, and is required when compiling
  with the _USE_IOBUFFERS_ option.

Users:
- There are two builds included in a standard distribution: CEX and REX.
  CEX is usable on (O/C)FW 3.40 to 3.55, and REX is for any CFW 3.56+.
- CellPS3FTP and OpenPS3FTP are the same codebase for the actual FTP server.
  The only difference is the SDK used to compile the executable.
