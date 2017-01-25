OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in and out of their console.

Developers:
- For PSL1GHT, a static library can be built by using make lib. To install
  to the standard PSL1GHT libraries, use make install.
- For cellsdk, the source *should* be easily portable by changing up a few
  function names and swapping the PSL1GHT includes for the cellsdk equivalents.
- The only include file needed in your app is <ftp/server.h> which exposes
  the function server_start and C struct app_status. These are designed to be
  used in a separate PPU thread. See main.cpp for example usage.

Known issues:
- Occasionally, file transfers do not register as complete. Seems to be more
  of an I/O problem with the PS3. Activating Async IO may help stabilize this.
- There is a limit to how many times a file or folder object can be created
  over the runtime of the app. Might be a kernel limit and thus not fixable.
  The workaround is to relaunch the app.
