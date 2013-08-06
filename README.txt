OpenPS3FTP version 3.0 README
=============================

OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in and out of their console.

OpenPS3FTP is built using libraries from the PSL1GHT SDK (commit 534e589).

=============================
What makes OpenPS3FTP stand out from the rest?

- Theoretically unlimited simultaneous transfers (depends on storage device)
- Open Source
- Greater stability when transferring multiple files
- Supports all FTP clients
- Ability to change file permissions (CHMOD)
=============================

Note to users:
- The retail pkg is signed for 3.55 CEX - if you are running anything higher,
you need to re-sign the EBOOT and re-pkg it.
- The debug pkg should work on any DEX firmware. Also, the pkg will be installed
at a separate location from the CEX build.

Error codes:
0x1337BEEF - Socket binding failed. This should never be seen. Just restart OpenPS3FTP, or the whole system if it keeps occurring.
0x13371010 - Socket polling failed. Probably only occurs if there's some sort of hardware problem.
0x1337ABCD - Error on listener socket. This should never happen at all. Restart your system.

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
