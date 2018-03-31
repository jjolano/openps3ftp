#include "prx.h"

SYS_MODULE_INFO(FTPD, 0, 4, 4);
SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);

struct Command* ftp_command = NULL;
struct Server* ftp_server = NULL;

sys_ppu_thread_t prx_tid;

volatile bool prx_running;

inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

inline int usleep(int usec)
{
	system_call_1(141, (uint64_t) usec);
	return_to_user_prog(int);
}

void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = sys_prx_get_my_module_id();

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uintptr_t) meminfo);
}

void ftp_stop(void)
{
	prx_running = false;

	if(ftp_server != NULL)
	{
		server_stop(ftp_server);
	}

	uint64_t prx_exitcode;
	sys_ppu_thread_join(prx_tid, &prx_exitcode);
}

int prx_stop(void)
{
	ftp_stop();
	finalize_module();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

void ftp_main(uint64_t ptr)
{
	uint32_t ret = 0;

	ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...
	feat_command_import(ftp_command);
	base_command_import(ftp_command);
	ext_command_import(ftp_command);

	while(prx_running)
	{
		sys_timer_sleep(5);

		ftp_server = (struct Server*) malloc(sizeof(struct Server));

		server_init(ftp_server, ftp_command, 21);
		ret = server_run(ftp_server);
		server_free(ftp_server);

		free(ftp_server);
	}

	command_free(ftp_command);
	free(ftp_command);

	ftp_server = NULL;
	ftp_command = NULL;

	sys_ppu_thread_exit(ret);
}

void prx_main(uint64_t ptr)
{
	#ifdef _NTFS_SUPPORT_
	#ifndef _PS3NTFS_PRX_
	sys_ppu_thread_t ntfs_tid;
	sys_ppu_thread_create(&ntfs_tid, ps3ntfs_automount, 0, 1001, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-NTFS");
	#endif
	#endif

	// initialize server
	sys_ppu_thread_t ftp_tid;
	sys_ppu_thread_create(&ftp_tid, ftp_main, 0, 1000, 0x10000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-FTPD");

	while(prx_running)
	{
		sys_timer_sleep(1);
	}

	prx_running = false;

	if(ftp_server != NULL)
	{
		server_stop(ftp_server);
	}

	uint64_t ftp_exitcode;
	sys_ppu_thread_join(ftp_tid, &ftp_exitcode);

	#ifdef _NTFS_SUPPORT_
	#ifndef _PS3NTFS_PRX_
	uint64_t ntfs_exitcode;
	sys_ppu_thread_join(ntfs_tid, &ntfs_exitcode);
	#endif
	#endif
	
	sys_ppu_thread_exit(0);
}

int prx_start(size_t args, void* argv)
{
	prx_running = true;

	sys_ppu_thread_create(&prx_tid, prx_main, 0, 1001, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP");

	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}
