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

	//#include <pthread_types.h> // for timespec
	#include <time.h>
	#include "../source/endians.h"
	#include <sys/sys_time.h> // for sys_time_get_current_time

#include "ntfs.h"

SYS_PROCESS_PARAM(1001, 0x10000)

char message[] ="This is a NTFS file test writing";
char buffer[1024];
static char buff[4096];
int LOG;

void log_printf(const char *format, ...);

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

int main(void)
{
	int ret;
	int k;
	int fd;
	char temp[128];
	ntfs_md *mounts;
	
	ret = cellSysmoduleLoadModule(CELL_SYSMODULE_FS);
	if (ret != CELL_OK) return ret;
	
	ret = cellFsOpen("/dev_hdd0/libntfs_sample_log.txt",
                     CELL_FS_O_RDWR|CELL_FS_O_CREAT, &LOG, NULL, 0);
	if(ret) return ret;
		
	log_printf("*** LOG ***\n");
	
	log_printf("\n*** PS3_NTFS_IsInserted ***\n\n");
	for(k = 0; k < 8; k++) {
		if(PS3_NTFS_IsInserted(k)) log_printf("- PS3_NTFS_IsInserted(%d) = true\n", k);
		else log_printf("- PS3_NTFS_IsInserted(%d) = false\n", k);
	}
	
	log_printf("\n*** ntfsFindPartitions ***\n");
	sec_t *partitions = NULL;
	int partition_number = ntfsFindPartitions(disc_ntfs[0], &partitions);
	log_printf("- ntfsFindPartitions = %d\n", partition_number);
	
	log_printf("\n*** ntfsMount ***\n");
	
	if(ntfsMount("ntfs0", disc_ntfs[0], partitions[0], CACHE_DEFAULT_PAGE_COUNT, CACHE_DEFAULT_PAGE_SIZE, NTFS_DEFAULT | NTFS_RECOVER))
		log_printf("- ntfsMount = true\n");
	else
		log_printf("- ntfsMount = false\n");
	
	if(partitions) free(partitions);
	
	log_printf("\n*** ntfsUnmount ***\n");
	ntfsUnmount("ntfs0", 1);
	
	log_printf("\n*** ntfsMountDevice ***\n");	
	ret = ntfsMountDevice(disc_ntfs[0], &mounts, NTFS_DEFAULT | NTFS_RECOVER);
	log_printf("- ntfsMountDevice = %d\n", ret);
	log_printf("- mount->name = %s\n", mounts->name);
	
	log_printf("\n*** ntfsUnmount ***\n");
	ntfsUnmount(mounts->name, 1);
	
	log_printf("\n*** ntfsMountAll ***\n");
	int mountCount = ntfsMountAll(&mounts, NTFS_DEFAULT | NTFS_RECOVER );
	log_printf("- ntfsMountAll = %d\n", mountCount);
	log_printf("- mount[0].name = %s\n", mounts[0].name);
	
	log_printf("\n*** ntfsGetVolumeName ***\n");
	const char *OldName = ntfsGetVolumeName(mounts[0].name);
	if(OldName) log_printf("- Old name : '%s'\n", OldName);
	else log_printf("- Error %d\n", ps3ntfs_errno());
	
	/* need to re-mount the device after SetVolumeName to 'update' the value of GetVolumeName
	log_printf("\n*** ntfsSetVolumeName ***\n");
	if(ntfsSetVolumeName(mounts[0].name, "NTFS_VOLUME"))
		log_printf("- ntfsSetVolumeName = true\n");
	else
		log_printf("- ntfsSetVolumeName = false - %d \n", ps3ntfs_errno());
	
	const char *NewName = ntfsGetVolumeName(mounts[0].name);
	if(NewName) log_printf("- New name : '%s'\n", NewName);
	else log_printf("- Error %d\n", ps3ntfs_errno());
	*/
	
	log_printf("\n*** ps3ntfs_mkdir ***\n");
	
	sprintf(temp, "%s:/viper6", mounts[0].name);
	if(ps3ntfs_mkdir(temp, 0777) == 0)
		log_printf("- ps3ntfs_mkdir = true\n");
	else 
		log_printf("- ps3ntfs_mkdir = false\n");
		
	log_printf("\n*** ps3ntfs_open ***\n");
	strcat(temp, (char*) "/ntfs.txt");
	
	for(k=0; k<5000; k++) { // force
		fd = ps3ntfs_open(temp, O_CREAT | O_WRONLY | O_TRUNC, 0777);
		if(fd>0) break;
	}
	
	if(fd > 0) {
		log_printf("- ps3ntfs_open = success\n");
		
		log_printf("\n*** ps3ntfs_write ***\n");
		ret = ps3ntfs_write(fd, message, strlen(message));
		log_printf("- ps3ntfs_write = %d\n", ret);
		if(ret != (int) strlen(message)) log_printf("- Error writing the file!\n");
		
		log_printf("\n*** ps3ntfs_close ***\n");
		ret = ps3ntfs_close(fd);
		log_printf("- ps3ntfs_close = %d\n", ret);
	} else log_printf("- ps3ntfs_open = failed - %s\n", temp);
	
	struct stat st;
	
	log_printf("\n*** ps3ntfs_stat ***\n");
	
	ret = ps3ntfs_stat(temp, &st);
	log_printf("- ps3ntfs_stat = %d\n", ret);
	log_printf("- SIZE = %d\n", st.st_size);
	log_printf("- last_access_time = %d\n", st.st_atime);
	log_printf("- last_mft_change_time = %d\n", st.st_ctime);
	log_printf("- last_data_change_time = %d\n", st.st_mtime);
	log_printf("- st_dev (id) = %d\n", st.st_dev);
	log_printf("- st uid = %d\n", st.st_uid);
	log_printf("- st gid = %d\n", st.st_gid);
	log_printf("- st ino = %d\n", st.st_ino);

	log_printf("\n*** ps3ntfs_open ***\n");
	for(k=0; k<5000; k++) { // force
		fd = ps3ntfs_open(temp, O_RDONLY, 0);
		if(fd > 0) break;
	}
	log_printf("- ps3ntfs_open = %d\n", fd);
	if(fd > 0) {
	
		log_printf("- ps3ntfs_open = success\n");
		
		log_printf("\n*** ps3ntfs_fstat ***\n");
	
		ret = ps3ntfs_fstat(fd, &st);
		log_printf("- ps3ntfs_fstat = %d\n", ret);
		log_printf("- SIZE = %d\n", st.st_size);
		log_printf("- last_access_time = %d\n", st.st_atime);
		log_printf("- last_mft_change_time = %d\n", st.st_ctime);
		log_printf("- last_data_change_time = %d\n", st.st_mtime);
		log_printf("- st_dev (id) = %d\n", st.st_dev);
		log_printf("- st uid = %d\n", st.st_uid);
		log_printf("- st gid = %d\n", st.st_gid);
		log_printf("- st ino = %d\n", st.st_ino);
		
		log_printf("\n*** ps3ntfs_seek ***\n");
		
		int size = ps3ntfs_seek(fd, 0, SEEK_END);

		log_printf("- ps3ntfs_seek - size = %d\n", size);

		ps3ntfs_seek(fd, 0, SEEK_SET);
		
		log_printf("\n*** ps3ntfs_read ***\n");
		ret = ps3ntfs_read(fd, buffer, size);
		log_printf("- ps3ntfs_read : '%s'\n", buffer);
		if(ret != size) log_printf("Error reading the file!\n"); 
		
		log_printf("\n*** ps3ntfs_close ***\n");
		ret = ps3ntfs_close(fd);
		log_printf("- ps3ntfs_close = %d\n", ret);

	} else log_printf("- ps3ntfs_open = failed - %s\n", temp);
		
	sprintf(buffer, "%s:/viper6/ntfs_newname.txt", mounts[0].name);
	log_printf("\n*** ps3ntfs_rename ***\n");
	for(k=0; k<5000; k++) { // force
		ret = ps3ntfs_rename(temp, buffer);
		if(ret==0) break;
	}
	if(ret==0) log_printf("- ps3ntfs_rename = %d\n", ret);
	else log_printf("- ps3ntfs_rename = %d - %s > %s\n", ret, temp, buffer);
	
	DIR_ITER *pdir;
	char filename[255];
	
	log_printf("\n*** ps3ntfs_diropen ***\n");
	sprintf(temp, "%s:/viper6", mounts[0].name);
	
	for(k=0; k<5000; k++) { // force
		pdir = ps3ntfs_diropen(temp);
		if (pdir) break;
	}
	if (pdir) {
		log_printf("- ps3ntfs_diropen = success\n");
		
		log_printf("\n*** ps3ntfs_dirnext ***\n");
		
		while (ps3ntfs_dirnext(pdir, filename, &st) == 0) {
		  
			if ((strcmp(filename, ".") == 0) || (strcmp(filename, "..") == 0)) continue;
			
			log_printf("- ps3ntfs_dirnext = File : %s/\n", filename);
			log_printf("- last_access_time = %d\n", st.st_atime);
			log_printf("- last_mft_change_time = %d\n", st.st_ctime);
			log_printf("- last_data_change_time = %d\n", st.st_mtime);

		}
		
		log_printf("\n*** ps3ntfs_dirreset ***\n");
		ret = ps3ntfs_dirreset(pdir);
		log_printf("- ps3ntfs_dirreset = %d\n", ret);
		
		log_printf("\n*** ps3ntfs_dirclose ***\n");
		ret = ps3ntfs_dirclose(pdir);
		log_printf("- ps3ntfs_dirclose = %d\n", ret);

	} else log_printf("- ps3ntfs_diropen = failed\n");
	
	log_printf("\n*** ps3ntfs_open ***\n");
	strcat(temp, (char*)"/unlink.txt");
	for(k=0; k<5000; k++) { // force
		fd = ps3ntfs_open(temp, O_CREAT | O_WRONLY | O_TRUNC, 0777);
		if(fd > 0) break;
	}
	if(fd > 0) {
		log_printf("- ps3ntfs_open = success\n");
		
		log_printf("\n*** ps3ntfs_close ***\n");
		ret = ps3ntfs_close(fd);
		log_printf("- ps3ntfs_close = %d\n", ret);
		
	} else log_printf("- ps3ntfs_open = failed\n");
	
	log_printf("\n*** ps3ntfs_unlink ***\n");
	for(k=0; k<5000; k++) { // force
		ret = ps3ntfs_unlink(temp);
		if(ret==0) break;
	}
	log_printf("- ps3ntfs_unlink = %d\n", ret);
	
	log_printf("\n*** ntfsUnmount ***\n");
	for (k = 0; k < mountCount; k++) ntfsUnmount(mounts[k].name, 1);
	
	log_printf("\n*** PS3_NTFS_Shutdown ***\n\n");
	for(k = 0; k < 8; k++) {
		if(PS3_NTFS_Shutdown(k)) log_printf("- PS3_NTFS_Shutdown(%d) = true\n", k);
		else log_printf("- PS3_NTFS_Shutdown(%d) = false\n", k);
	}
	
	/* // Debugging for time related functions in ntfstime.h
	struct timespec {
		time_t tv_sec;
		long tv_nsec;
	} ;

	struct timespec now;
	typedef uint64_t u64;
	typedef u64 sle64;
	typedef sle64 ntfs_time;
	
	//sys_time_sec_t  time_s;
	//sys_time_nsec_t time_n_s;
	//sys_time_get_current_time(&time_s, &time_n_s);
	//now.tv_sec = time_s;
	//now.tv_nsec = time_n_s;
	
	now.tv_sec = time((time_t*)NULL);
	now.tv_nsec = 0;
	
	log_printf("- test now.tv_sec = %d\n", now.tv_sec);
	log_printf("- test now.tv_nsec = %d\n", now.tv_nsec);

	ntfs_time ntfstime;
	struct timespec unixtime;

	#define NTFS_TIME_OFFSET ((s64)(369 * 365 + 89) * 24 * 3600 * 10000000)

	s64 units;

	units = (s64)now.tv_sec * 10000000 + NTFS_TIME_OFFSET + now.tv_nsec/100;
	
	ntfstime = (cpu_to_sle64(units));
	
	log_printf("- timespec2ntfs = %d\n", ntfstime);
	
	struct timespec spec;
	s64 cputime;

	cputime = sle64_to_cpu(ntfstime);
	spec.tv_sec = (cputime - (NTFS_TIME_OFFSET)) / 10000000;
	spec.tv_nsec = (cputime - (NTFS_TIME_OFFSET)
			- (s64)spec.tv_sec*10000000)*100;
		//force zero nsec for overflowing dates
	if ((spec.tv_nsec < 0) || (spec.tv_nsec > 999999999))
		spec.tv_nsec = 0;
		
	unixtime = spec;
	
	log_printf("- ntfs2timespec = %d\n", unixtime);
	*/	// End debugging

	/*
	TODO
	int ps3ntfs_file_to_sectors(const char *path, uint32_t *sec_out, uint32_t *size_out, int max, int phys);
	int ps3ntfs_get_fd_from_FILE(FILE *fp);
	s64 ps3ntfs_seek64(int fd, s64 pos, int dir);
	int ps3ntfs_link(const char *existing, const char  *newLink);
	
	int ps3ntfs_statvfs(const char *path, struct statvfs *buf);
	int ps3ntfs_ftruncate(int fd, off_t len);
	int ps3ntfs_fsync(int fd);
	
	void NTFS_init_system_io(void);
	void NTFS_deinit_system_io(void);
	
	Standard functions supported:
	
	open_r -> for stdio.h fopen()...
	close_r -> for stdio.h fclose()...
	read_r -> for stdio.h fread()...
	write_r -> for stdio.h fwrite()...
	lseek_r -> for stdio.h fseek()...
	lseek64_r -> for using with large files (see ps3_example_stdio for this)
	fstat_r -> for stat.h fstat()
	stat_r -> for stat.h stat()

	ftruncate_r -> for unistd.h ftruncate()
	truncate_r -> for unistd.h truncate()
	fsync_r -> for stdio.h fflush()
	link_r -> for unistd.h link()
	unlink_r -> for unistd.h unlink()
	rename_r -> for stdio.h rename()
	mkdir_r -> for stat.h mkdir()
	rmdir_r -> for unistd.h rmdir()
	*/
	
	cellFsClose(LOG);
	
	return 0;
}
