#include "prx.h"
#include "openps3ftp.h"

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_stop);
SYS_MODULE_INFO(FTPD, 0, 4, 2);

SYS_LIB_DECLARE_WITH_STUB(FTPD, SYS_LIB_AUTO_EXPORT, libopenps3ftp_prx);

SYS_LIB_EXPORT(ext_command_register_connect, FTPD);
SYS_LIB_EXPORT(ext_command_register_disconnect, FTPD);
SYS_LIB_EXPORT(ext_command_register, FTPD);

SYS_LIB_EXPORT(client_get_cvar, FTPD);
SYS_LIB_EXPORT(client_set_cvar, FTPD);

SYS_LIB_EXPORT(client_send_message, FTPD);
SYS_LIB_EXPORT(client_send_code, FTPD);
SYS_LIB_EXPORT(client_send_multicode, FTPD);
SYS_LIB_EXPORT(client_send_multimessage, FTPD);

struct Server* ftp_server;
struct Command* ftp_command;

sys_ppu_thread_t prx_tid;
bool prx_running = false;

void ext_command_register_connect(connect_callback callback)
{
	command_register_connect(ftp_command, callback);
}

void ext_command_register_disconnect(disconnect_callback callback)
{
	command_register_disconnect(ftp_command, callback);
}

void ext_command_register(const char name[32], command_callback callback)
{
	command_register(ftp_command, name, callback);
}

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

	ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...
	feat_command_import(ftp_command);
	base_command_import(ftp_command);
	ext_command_import(ftp_command);

	ftp_server = (struct Server*) malloc(sizeof(struct Server));
	server_init(ftp_server, ftp_command, 21);

	// let ftp library take over thread
	uint32_t ret = server_run(ftp_server);

	// server stopped, free resources
	server_free(ftp_server);
	command_free(ftp_command);

	free(ftp_server);
	free(ftp_command);

	// exited by server error or command
	if(prx_running)
	{
		finalize_module();
	}
	
	sys_ppu_thread_exit(ret);
}

int prx_start(size_t args, void* argv)
{
	sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP");
	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}
