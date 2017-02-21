#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include "const.hpp"
#include "ftphelper.hpp"
#include "server.hpp"

#include "feat.hpp"

sys_ppu_thread_t prx_tid;
bool prx_running;

void prx_main(uint64_t ptr)
{
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
	sys_ppu_thread_create(&server_tid, ftp_server_start_ex, (uint64_t)&server, 1000, 0x10000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*)"ftpd");

	while(server.is_running() && prx_running)
	{
		sys_timer_sleep(1);
	}

	server.stop();

	uint64_t thread_exit;
	sys_ppu_thread_join(server_tid, &thread_exit);

	sys_ppu_thread_exit(thread_exit);
}

extern "C" int EntryPoint(size_t args, void* argv)
{
	if(sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 2048, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-PRX") == CELL_OK)
	{
		prx_running = true;
		return SYS_PRX_RESIDENT;
	}

	return SYS_PRX_NO_RESIDENT;
}

extern "C" int UnloadModule(void)
{
	if(prx_running)
	{
		prx_running = false;

		uint64_t prx_exit;
		sys_ppu_thread_join(prx_tid, &prx_exit);
	}

	return SYS_PRX_STOP_OK;
}

SYS_MODULE_START(EntryPoint);
SYS_MODULE_STOP(UnloadModule);
SYS_MODULE_EXIT(UnloadModule);
SYS_MODULE_INFO("OpenPS3FTP", 0, 4, 2);
