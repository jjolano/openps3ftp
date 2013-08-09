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

Note to users:
- There are two CEX builds: "cex", and "ncex". Use the "ncex" build if running
a firmware version higher than 3.55.
- The DEX build should work on any DEX firmware. It will be installed at a
separate location from the CEX build.

Implemented error codes:
0x1337BEEF - Socket binding failed. Just restart. The only reason I think this would happen is if you launch OpenPS3FTP right after exiting it.
0x13371010 - Socket polling failed. Probably only happens if the system decides to screw up.
0x1337ABCD - Error on listener socket. Probably only happens if you're screwing around with the server.

Note to developers:
Do what you like with the source code - however when releasing something that
makes use of this source code, I would appreciate the attribution.

Compiling OpenPS3FTP from source:
- I recommend that you have scetool. Make sure the path to scetool is correct in the Makefile.
- `make dist` to create a cex (old-keyrev) and dex build.
- `make dist2` to additionally create a ncex (new-keyrev) build.
- At the time of writing, pkg files are being built with pkg.py and package_finalize (for cex). This doesn't seem to work with later firmwares (atreyu187) - so when something new comes out to make valid pkg files, this will be updated.

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
