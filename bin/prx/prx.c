#include <stdbool.h>
#include <inttypes.h>

#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>

#include "server.h"
#include "command.h"
#include "vsh_exports.h"

inline void _sys_ppu_thread_exit(uint64_t val);
inline sys_prx_id_t prx_get_module_id_by_address(void* addr);
void finalize_module(void);
int prx_stop(void);
void prx_main(uint64_t ptr);
int prx_start(size_t args, void* argv);

struct Server* ftp_server;
sys_ppu_thread_t prx_tid;
bool prx_running = false;

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_stop);
SYS_MODULE_INFO(FTPD, 0, 4, 2);

inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

inline sys_prx_id_t prx_get_module_id_by_address(void* addr)
{
	system_call_1(461, (uint64_t)(uint32_t)addr);
	return (int)p1;
}

void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address(finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uint64_t)(uint32_t)meminfo);
}

int prx_stop(void)
{
	if(prx_running)
	{
		server_stop(ftp_server);

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

	struct Command* ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...

	ftp_server = (struct Server*) malloc(sizeof(struct Server));
	server_init(ftp_server, ftp_command, 21);

	// let ftp library take over thread
	uint32_t ret = server_run(ftp_server);

	// server stopped, free resources
	server_free(ftp_server);
	command_free(ftp_command);
	
	sys_ppu_thread_exit(ret);
}

int prx_start(size_t args, void* argv)
{
	sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP");
	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}
