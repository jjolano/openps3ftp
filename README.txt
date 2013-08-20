OpenPS3FTP version 3.0 README
=============================

OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in and out of their console.

OpenPS3FTP is built using libraries from the PSL1GHT SDK (commit 534e589).

Big thanks to atreyu187 from PSX-SCENE for helping me test out this release.

=============================
What makes OpenPS3FTP stand out from the rest?

- Theoretically unlimited simultaneous transfers (depends on storage device)
- Open Source
- Greater stability when transferring multiple files
- Supports all FTP clients
- Ability to change file permissions (CHMOD)
- Supports transfer resuming
=============================

Implemented error codes (shouldn't happen under typical use anyway):
0x1337BEEF - Server initialization failed.
0x1337DEAD - Connection polling failed.
0x1337CAFE - Out of memory.

Any other error code is unknown and should be reported to me. Please include
the FTP connection log.

Note to developers:
Do what you like with the source code - however when releasing something that
makes use of this source code, I would appreciate the attribution.

Compiling OpenPS3FTP from source:
- I recommend that you have scetool. Make sure the path to scetool is correct in the Makefile.
- `make dist` to create a cex (old-keyrev, pseudo-retail), rex (new-keyrev) and dex build.

=============================
GitHub (v3.0+): https://github.com/jjolano/openps3ftp
GitHub (v2.x): https://github.com/jjolano/openps3ftp-v2
GitHub (v1.x): https://github.com/jjolano/openps3ftp-v1
License: Beerware (as of version 3.0), GNU GPL (versions <=2.3)
=============================

Thank you for using OpenPS3FTP!

John Olano (jjolano)
Twitter: @jjolano
Donations: http://bit.ly/gmzGcI
