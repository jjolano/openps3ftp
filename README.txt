OpenPS3FTP version 3.0 README
=============================

OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in and out of their console.

OpenPS3FTP is built using libraries from the PSL1GHT SDK.

=============================
What makes OpenPS3FTP stand out from the rest?

- Theoretically unlimited simultaneous transfers (depends on storage device)
- Open Source
- Greater stability when transferring multiple files
- Supports virtually all FTP clients
- Ability to change file permissions (SITE CHMOD, used by FileZilla)
- Supports transfer resuming
=============================
What build should I use?

All builds contain identical program code, but each are signed differently.
Thus, you will have to install the build that is suitable for your firmware.

- If you are on firmware 3.55 or older, use the CEX (pkg-cex) build.
- If you are on firmware 3.60 or newer, use the REX (pkg-rex) build.
- If you are on a true DEX firmware, use the DEX (pkg-dex) build.
=============================

Implemented error codes (shouldn't happen under typical use anyway):
0x1337BEEF - Server initialization failed.
0x1337DEAD - Connection polling failed.
0x1337CAFE - Failed to allocate required memory block.

Any other error code is unknown and should be reported to me. Please include
the FTP connection log.

Note to developers:
Do what you like with the source code - however when releasing something that
makes use of this source code, I would appreciate the attribution.

With this version, it should be much easier to integrate into your program
compared to v2.3 and older.

Required Compilation Tools:
- scetool: github.com/naehrwert/scetool
- make_gpkg: github.com/jjolano/make_gpkg
- make_fself: github.com/jjolano/make_fself
- make_his: github.com/jjolano/make_his

=============================
GitHub (v3.0+): https://github.com/jjolano/openps3ftp
GitHub (v2.x): https://github.com/jjolano/openps3ftp-v2
GitHub (v1.x): https://github.com/jjolano/openps3ftp-v1
License: Beerware (as of version 3.0), GNU GPL (versions <=2.3)
=============================

Thank you for using OpenPS3FTP!

John Olano (jjolano)
Twitter: @jjolano
Donations/Beer: http://bit.ly/gmzGcI
