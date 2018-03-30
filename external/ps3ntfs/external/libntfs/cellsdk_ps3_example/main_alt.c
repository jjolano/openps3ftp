#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <sys/fs.h>
#include <sys/process.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/synchronization.h>

#include <cell/sysmodule.h>
#include <cell/cell_fs.h>

#include "ntfs.h"

#define PATH_MAX 255
#define USB_MASS_STORAGE_1(n)	(0x10300000000000AULL+(n)) /* For 0-5 */
#define USB_MASS_STORAGE_2(n)	(0x10300000000001FULL+((n)-6)) /* For 6-127 */
#define USB_MASS_STORAGE(n)	(((n) < 6) ? USB_MASS_STORAGE_1(n) : USB_MASS_STORAGE_2(n))

SYS_PROCESS_PARAM(1001, 0x10000)
static char buff[4096];

int LOG;

void log_printf(const char *format, ...);
void list(const char *path, int depth);

void log_printf(const char *format, ...)
{
	char *str = (char *) buff;
	va_list	opt;

	va_start(opt, format);
	vsprintf( (void *) buff, format, opt);
	va_end(opt);

	uint64_t sw;
	cellFsWrite(LOG, (const void *) str, (uint64_t)strlen(str), &sw);
}

const DISC_INTERFACE *disc_ntfs[8]= {
	&__io_ntfs_usb000,
	&__io_ntfs_usb001,
	&__io_ntfs_usb002,
	&__io_ntfs_usb003,
	&__io_ntfs_usb004,
	&__io_ntfs_usb005,
	&__io_ntfs_usb006,
	&__io_ntfs_usb007
};

ntfs_md *mounts;
int mountCount;

char message[] ="This is a NTFS file test writing";
char buffer[1024];

int main(int32_t argc, const char* argv[])
{
	int ret;

	ret = cellSysmoduleLoadModule(CELL_SYSMODULE_FS);
	if (ret != CELL_OK) return ret;

	ret = cellFsOpen("/dev_hdd0/libntfs_sample_log.txt",
                     CELL_FS_O_RDWR|CELL_FS_O_CREAT, &LOG, NULL, 0);
	if(ret) return ret;

	log_printf("\n\nntfsMountAll");
	mountCount = ntfsMountAll(&mounts, NTFS_DEFAULT | NTFS_RECOVER /* | NTFS_READ_ONLY */ );
	if (mountCount <= 0) 	log_printf("\n\nCan't mount");

	int r;

	// NTFS operation to write
	log_printf("\n\nWriting to ntfs0");
	r = ps3ntfs_open("ntfs0:/text.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
	log_printf(" : %d", r);

	// FAT operation  to write (from internal device)
	log_printf("\n\nWriting to usb000");
	r = ps3ntfs_open("/dev_usb000/text.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
	log_printf(" : %d", r);

	// FAT failed operation usb000
	log_printf("\n\nWriting to wrong path in usb000");
	r = ps3ntfs_open("/dev_usb000/blurps/text.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
	log_printf(" : %d", r);

	// FAT operation  to write (from internal device)
	log_printf("\n\nWriting to hdd0");
	r = ps3ntfs_open("/dev_hdd0/text.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
	log_printf(" : %d", r);

	// NTFS failed operation
	log_printf("\n\nWriting to wrong path in ntfs0");
	r = ps3ntfs_open("ntfs0:/blurps/text.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
	log_printf(" : %d", r);

	log_printf("\n\nUnmounting...");
	for (uint8_t u = 0; u < mountCount; u++) ntfsUnmount(mounts[u].name, 1);

	return 0;
}
