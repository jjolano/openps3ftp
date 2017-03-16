#include "prx.h"
#include "openps3ftp.h"

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_exit);
SYS_MODULE_INFO(FTPD, 0, 4, 2);

SYS_LIB_DECLARE_WITH_STUB(FTPD, SYS_LIB_AUTO_EXPORT, libopenps3ftp_prx);

SYS_LIB_EXPORT(prx_command_register_connect, FTPD);
SYS_LIB_EXPORT(prx_command_register_disconnect, FTPD);
SYS_LIB_EXPORT(prx_command_register, FTPD);

SYS_LIB_EXPORT(prx_command_unregister_connect, FTPD);
SYS_LIB_EXPORT(prx_command_unregister_disconnect, FTPD);
SYS_LIB_EXPORT(prx_command_unregister, FTPD);

SYS_LIB_EXPORT(prx_command_import, FTPD);

SYS_LIB_EXPORT(prx_command_override, FTPD);

SYS_LIB_EXPORT(prx_server_stop, FTPD);

SYS_LIB_EXPORT(client_get_cvar, FTPD);
SYS_LIB_EXPORT(client_set_cvar, FTPD);

SYS_LIB_EXPORT(client_send_message, FTPD);
SYS_LIB_EXPORT(client_send_code, FTPD);
SYS_LIB_EXPORT(client_send_multicode, FTPD);
SYS_LIB_EXPORT(client_send_multimessage, FTPD);

SYS_LIB_EXPORT(command_call, FTPD);
SYS_LIB_EXPORT(command_register, FTPD);
SYS_LIB_EXPORT(command_import, FTPD);

SYS_LIB_EXPORT(command_init, FTPD);
SYS_LIB_EXPORT(command_free, FTPD);

SYS_LIB_EXPORT(get_working_directory, FTPD);
SYS_LIB_EXPORT(set_working_directory, FTPD);
SYS_LIB_EXPORT(get_absolute_path, FTPD);

SYS_LIB_EXPORT(parse_command_string, FTPD);

struct Server* ftp_server;
struct Command* ftp_command;

sys_ppu_thread_t prx_tid;
bool prx_running = false;

void prx_command_register_connect(connect_callback callback)
{
	command_register_connect(ftp_command, callback);
}

void prx_command_register_disconnect(disconnect_callback callback)
{
	command_register_disconnect(ftp_command, callback);
}

void prx_command_register(const char name[32], command_callback callback)
{
	command_register(ftp_command, name, callback);
}

void prx_command_unregister_connect(connect_callback callback)
{
	int i;

	for(i = 0; i < ftp_command->num_connect_callbacks; ++i)
	{
		struct ConnectCallback* cb = &ftp_command->connect_callbacks[i];

		if(cb->callback == callback)
		{
			cb->callback = NULL;
			break;
		}
	}
}

void prx_command_unregister_disconnect(disconnect_callback callback)
{
	int i;

	for(i = 0; i < ftp_command->num_disconnect_callbacks; ++i)
	{
		struct DisconnectCallback* cb = &ftp_command->disconnect_callbacks[i];

		if(cb->callback == callback)
		{
			cb->callback = NULL;
			break;
		}
	}
}

void prx_command_unregister(command_callback callback)
{
	int i;

	for(i = 0; i < ftp_command->num_command_callbacks; ++i)
	{
		struct CommandCallback* cb = &ftp_command->command_callbacks[i];

		if(cb->callback == callback)
		{
			cb->callback = NULL;
			break;
		}
	}
}

void prx_command_import(struct Command* ext_command)
{
	command_import(ftp_command, ext_command);
}

void prx_command_override(const char name[32], command_callback callback)
{
	int i;
	for(i = 0; i < ftp_command->num_command_callbacks; ++i)
	{
		struct CommandCallback* cb = &ftp_command->command_callbacks[i];

		if(strcmp(cb->name, name) == 0)
		{
			cb->callback = callback;
			break;
		}
	}
}

void prx_server_stop(void)
{
	server_stop(ftp_server);

	uint64_t prx_exitcode;
	sys_ppu_thread_join(prx_tid, &prx_exitcode);
}

inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = sys_prx_get_module_id_by_address(finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uintptr_t) meminfo);
}

void prx_unload(void)
{
	sys_prx_id_t prx = sys_prx_get_module_id_by_address(prx_unload);
	
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

	ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...
	feat_command_import(ftp_command);
	base_command_import(ftp_command);
	ext_command_import(ftp_command);

	// wait for a bit for other plugins...
	sys_timer_sleep(12);

	if(prx_running)
	{
		ftp_server = (struct Server*) malloc(sizeof(struct Server));

		// show startup msg
		vshtask_notify("OpenPS3FTP " APP_VERSION);

		// let ftp library take over thread
		while(prx_running)
		{
			// initialize server struct
			server_init(ftp_server, ftp_command, 21);

			uint32_t ret = server_run(ftp_server);

			switch(ret)
			{
				case 1:
				vshtask_notify("FTP Error: Another FTP server is using port 21. Trying again in 30 seconds.");
				sys_timer_sleep(30);
				break;
				case 2:
				case 3:
				vshtask_notify("FTP Error: Network library error. Trying again in 5 seconds.");
				sys_timer_sleep(5);
				break;
				default:
				sys_timer_sleep(1);
			}

			server_free(ftp_server);
		}
		
		free(ftp_server);
	}

	// server stopped, free resources
	command_free(ftp_command);
	free(ftp_command);

	// exited by server error or command
	if(prx_running)
	{
		prx_running = false;
		finalize_module();
	}
	
	sys_ppu_thread_exit(0);
}

int prx_start(size_t args, void* argv)
{
	if(sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP") != 0)
	{
		finalize_module();
		_sys_ppu_thread_exit(SYS_PRX_NO_RESIDENT);
		return SYS_PRX_NO_RESIDENT;
	}

	_sys_ppu_thread_exit(SYS_PRX_START_OK);
	return SYS_PRX_START_OK;
}
