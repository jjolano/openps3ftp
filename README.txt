OpenPS3FTP version 3.0 README
=============================

OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
	the basic FTP commands that the typical user would need to transfer files
	in and out of their console.

OpenPS3FTP is built using libraries from the PSL1GHT SDK (commit 534e589).

As of version 3.0:
- It has been blind-rewritten from scratch, with C++. The meaning of
	"blind-rewritten" is that all the code that I have written has NOT been
	tested by me at all. The reason for this is because I no longer have
	access to homebrew functionality on my own console. That said, I trust my
	own skills in writing good code. Some of the code is based on previous
	versions, and some are based on new knowledge and pre-tested code. If
	somehow this program does not work, please contact me with much detail so
	that I can look into it for you. Also, feel free to participate in the
	development of OpenPS3FTP on the GitHub repository. :)
- The source code is now on the "Beerware" license, instead of GNU GPL. You
	can do whatever you want with the code, as long as proper credit is
	given. Think this program is awesome? You can buy me a beer. :P
- Any login detail is now accepted. I have seen this as the most common
	complaint. Don't know why I designed it like that.


Note to users:
The included pkg file is signed for firmware versions lower than 3.60. If you
are not running a firmware version lower than 3.60, then you may need to re-sign
the pkg file and EBOOT.BIN.

Error codes:
0x1337BEEF - Socket binding failed. This should never be seen. Just restart OpenPS3FTP, or the whole system if it keeps occurring.
0x13371010 - Socket polling failed. Probably only occurs if there's some sort of hardware problem.
0x1337ABCD - Error on listener socket. This should never happen at all. Restart your system.

Note to PS3 news websites:
I never released version 3.0 officially, not sure who tipped that. That build
wasn't even finished at all, just a testing build for new code. This will be
the official release of version 3.0 of OpenPS3FTP. Please note the new source
code links and new changes. Keep updated on my Twitter feed and GitHub - this
particular build is still not a final release.

Also, that "version 3.1"? That is essentially this, just re-signed for 4.xx. You 
know who you are - don't claim my work as your own. Give credit where it's due.

This may be my last release for the PS3. Thanks everyone!

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
