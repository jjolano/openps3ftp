#include <iostream>
#include <cell.h>

#include "server.h"

using namespace std;

void load_sysmodules(void)
{
	cellSysmoduleInitialize();
	cellSysmoduleLoadModule(CELL_SYSMODULE_FS);
	cellSysmoduleLoadModule(CELL_SYSMODULE_NET);
	cellSysmoduleLoadModule(CELL_SYSMODULE_NETCTL);
}

void unload_sysmodules(void)
{
	cellSysmoduleUnloadModule(CELL_SYSMODULE_FS);
	cellSysmoduleUnloadModule(CELL_SYSMODULE_NET);
	cellSysmoduleUnloadModule(CELL_SYSMODULE_NETCTL);
	cellSysmoduleFinalize();
}

int main(void)
{
	load_sysmodules();

	// Mount dev_blind.
	{
		system_call_8(837,
			(uint64_t)"CELL_FS_IOS:BUILTIN_FLSH1",
			(uint64_t)"CELL_FS_FAT",
			(uint64_t)"/dev_blind",
			0, 0, 0, 0, 0);
	}

	// Prepare Async IO.
	cellFsAioInit("/dev_hdd0");

	// Create server thread.
	app_status status;
	status.is_running = 1;

	sys_ppu_thread_t server_tid;
	sys_ppu_thread_create(&server_tid, server_start_ex, (uint64_t)&status, 1000, 0x10000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*)"ftpd");

	// Application loop
	

	// Join server thread and wait for exit...
	uint64_t thread_exit;
	sys_ppu_thread_join(server_tid, &thread_exit);

	// Finish Async IO.
	cellFsAioFinish("/dev_hdd0");

	// Unmount dev_blind.
	{
		system_call_1(838, (uint64_t)"/dev_blind");
	}
	
	unload_sysmodules();
	return 0;
}
