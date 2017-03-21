#include "prx.h"

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_exit);
SYS_MODULE_INFO(FTPD, SYS_MODULE_ATTR_EXCLUSIVE_LOAD | SYS_MODULE_ATTR_EXCLUSIVE_START, 4, 2);

struct Server* ftp_server;
struct Command* ftp_command;

sys_ppu_thread_t prx_tid;
bool prx_running = false;

inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
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

void prx_unload(void)
{
	sys_prx_id_t prx = sys_prx_get_my_module_id();
	system_call_3(483, prx, 0, NULL);
}

int prx_stop(void)
{
	if(prx_running)
	{
		prx_running = false;
		server_stop(ftp_server);

		uint64_t prx_exitcode;
		sys_ppu_thread_join(prx_tid, &prx_exitcode);
	}
	
	finalize_module();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

int prx_exit(void)
{
	if(prx_running)
	{
		prx_running = false;
		server_stop(ftp_server);

		uint64_t prx_exitcode;
		sys_ppu_thread_join(prx_tid, &prx_exitcode);
	}

	prx_unload();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

void prx_main(uint64_t ptr)
{
	prx_running = true;

	#ifdef _NTFS_SUPPORT_
	#ifndef _PS3NTFS_PRX_
	sys_ppu_thread_t ntfs_tid;
	sys_ppu_thread_create(&ntfs_tid, ps3ntfs_automount, 0, 1001, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-NTFS");
	#endif
	#endif

	ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...
	feat_command_import(ftp_command);
	base_command_import(ftp_command);
	ext_command_import(ftp_command);

	// wait for a bit for other plugins...
	sys_timer_sleep(11);

	if(prx_running)
	{
		// show startup msg
		vshtask_notify("OpenPS3FTP " APP_VERSION);

		ftp_server = (struct Server*) malloc(sizeof(struct Server));

		// let ftp library take over thread
		while(prx_running)
		{
			sys_timer_sleep(1);
			
			// initialize server struct
			server_init(ftp_server, ftp_command, 21);
			server_run(ftp_server);
			server_free(ftp_server);
		}
		
		free(ftp_server);
	}

	// server stopped, free resources
	command_free(ftp_command);
	free(ftp_command);

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
	if(sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x4000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP") != 0)
	{
		finalize_module();
		_sys_ppu_thread_exit(SYS_PRX_NO_RESIDENT);
		return SYS_PRX_NO_RESIDENT;
	}

	_sys_ppu_thread_exit(SYS_PRX_START_OK);
	return SYS_PRX_START_OK;
}
