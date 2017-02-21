#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>
#include <sys/syscall.h>

#include "const.hpp"
#include "ftphelper.hpp"
#include "server.hpp"

#include "feat.hpp"

#ifdef PRX
#include "prx/vsh_exports.h"
#endif

static sys_ppu_thread_t prx_tid;
static bool prx_running;

extern "C" static inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

extern "C" static inline sys_prx_id_t prx_get_module_id_by_address(void (*addr)())
{
	system_call_1(461, (uint64_t)(uint32_t)addr);
	return (int)p1;
}

extern "C" static void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address(finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uint64_t)(uint32_t)meminfo);
}

extern "C" int prx_unload(void)
{
	if(prx_running)
	{
		prx_running = false;

		uint64_t prx_exit;
		sys_ppu_thread_join(prx_tid, &prx_exit);
	}

	finalize_module();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

void prx_main(uint64_t ptr)
{
	prx_running = true;
	
	FTP::Command command;
	
	FTP::Command base_command = feat::base::get_commands();
	FTP::Command app_command = feat::app::get_commands();
	FTP::Command ext_command = feat::ext::get_commands();
	FTP::Command feat_command = feat::get_commands();

	command.import(&base_command);
	command.import(&app_command);
	command.import(&ext_command);
	command.import(&feat_command);

	FTP::Server server(&command, 21);

	sys_ppu_thread_t server_tid;
	if(sys_ppu_thread_create(&server_tid, ftp_server_start_ex, (uint64_t)&server, 1000, 0x10000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "ftpd") != CELL_OK)
	{
		prx_unload();
		sys_ppu_thread_exit(0);
	}

	while(server.is_running() && prx_running)
	{
		sys_timer_sleep(1);
	}

	server.stop();

	uint64_t thread_exit;
	sys_ppu_thread_join(server_tid, &thread_exit);

	sys_ppu_thread_exit(thread_exit);
}

extern "C" int prx_start(size_t args, void* argv)
{
	sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x10000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP");
	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_unload);
SYS_MODULE_EXIT(prx_unload);
SYS_MODULE_INFO(FTP, 0, 4, 2);
