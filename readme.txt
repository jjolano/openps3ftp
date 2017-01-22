OpenPS3FTP is an open source FTP server for the PlayStation 3. It supports
the basic FTP commands that the typical user would need to transfer files
in and out of their console.

It also provides read/write access to the console's flash partition, by
mounting /dev_blind.

Currently, this server is optimized for stability over performance.
A performance benefit is possible, but it seems libsysfs isn't cooperating.

Known issues:
- Occasionally, file transfers do not register as complete. Seems to be more
  of an I/O problem with the PS3, but maybe fixable with libsysfs.
- There is a limit to how many times a file or folder object can be created
  over the runtime of the app. Might be a kernel limit and thus not fixable.
  The workaround is to relaunch the app.
