OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in to and out of their console.

Homebrews that use modified OpenPS3FTP code to provide FTP services include:
- multiMAN
- webMAN
- IRISMAN
- Rebug Toolbox

Consider donating $1 (or $0.01) to show your appreciation. Thank you!

PayPal: http://bit.ly/gmzGcI

Source code: https://github.com/jjolano/openps3ftp
Releases: https://github.com/jjolano/openps3ftp/releases

Developers:
- For PSL1GHT, a static library can be built by using make lib. To install
  to the standard PSL1GHT libraries, use make install.
- For cellsdk, try using make with SDK=CELL.
- Check "common.h" for compile-time options. Apply changes to the Makefile.
- The only include file needed in your app is <ftp/server.h> which exposes
  the function server_start and C struct app_status. These are designed to be
  used in a separate PPU thread. See main.cpp for example usage.
- This uses an older commit of NoRSX, since the supposed performance patch
  the latest commit brings actually made things slower. A lot slower.
- Questions/need support? Create an "Issue" on the GitHub repo.

Users:
- For best results, use 2 or 3 simultaneous connections at a time. The console
  cannot reliably handle a high number of IO requests and ends up crashing.
  Some adjustments to help alleviate this may come in future updates.
- There are two builds included in a standard distribution: CEX and REX.
  CEX is usable on (O/C)FW 3.40 to 3.55, and REX is for any CFW 3.56+.

Future Plans:
- Performance/stability tuning, as usual.
- SPRX/VSH plugin?

I do not participate in the "Scene" websites anymore, so the only way to
contact me (preferably) is through GitHub.

- jjolano
