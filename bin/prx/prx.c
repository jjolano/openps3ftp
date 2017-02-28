#include "prx.h"
#include "openps3ftp.h"

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_stop);
SYS_MODULE_INFO(FTPD, 0, 4, 2);

SYS_LIB_DECLARE_WITH_STUB(FTPD, SYS_LIB_AUTO_EXPORT, libopenps3ftp_prx);

SYS_LIB_EXPORT(prx_command_register_connect, FTPD);
SYS_LIB_EXPORT(prx_command_register_disconnect, FTPD);
SYS_LIB_EXPORT(prx_command_register, FTPD);
SYS_LIB_EXPORT(prx_command_import, FTPD);

SYS_LIB_EXPORT(prx_command_override, FTPD);

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

int (*vshtask_notify)(int, const char *) = NULL;

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

void prx_command_import(struct Command* ext_command)
{
	command_import(ftp_command, ext_command);
}

void prx_command_override(struct Command* ext_command)
{
	command_free(ftp_command);
	free(ftp_command);

	ftp_command = ext_command;
}

void* getNIDfunc(const char* vsh_module, uint32_t fnid, int32_t offset)
{
	// 0x10000 = ELF
	// 0x10080 = segment 2 start
	// 0x10200 = code start

	uint32_t table = (*(uint32_t*)0x1008C) + 0x984; // vsh table address

	while(((uint32_t)*(uint32_t*)table) != 0)
	{
		uint32_t* export_stru_ptr = (uint32_t*)*(uint32_t*)table; // ptr to export stub, size 2C, "sys_io" usually... Exports:0000000000635BC0 stru_635BC0:    ExportStub_s <0x1C00, 1, 9, 0x39, 0, 0x2000000, aSys_io, ExportFNIDTable_sys_io, ExportStubTable_sys_io>

		const char* lib_name_ptr =  (const char*)*(uint32_t*)((char*)export_stru_ptr + 0x10);

		if(strncmp(vsh_module, lib_name_ptr, strlen(lib_name_ptr)) == 0)
		{
			// we got the proper export struct
			uint32_t lib_fnid_ptr = *(uint32_t*)((char*)export_stru_ptr + 0x14);
			uint32_t lib_func_ptr = *(uint32_t*)((char*)export_stru_ptr + 0x18);
			uint16_t count = *(uint16_t*)((char*)export_stru_ptr + 6); // number of exports
			for(int i = 0; i < count; i++)
			{
				if(fnid == *(uint32_t*)((char*)lib_fnid_ptr + i*4))
				{
					// take address from OPD
					return (void**)*((uint32_t*)(lib_func_ptr) + i) + offset;
				}
			}
		}
		table += 4;
	}

	return 0;
}

void show_msg(const char* msg)
{
	if(!vshtask_notify)
		vshtask_notify = getNIDfunc("vshtask", 0xA02D46E7, 0);

	if(vshtask_notify)
		vshtask_notify(0, msg);
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

	// wait for a bit...
	sys_timer_sleep(15);

	ftp_server = (struct Server*) malloc(sizeof(struct Server));

	// initialize server struct
	server_init(ftp_server, ftp_command, 21);

	// show startup msg
	show_msg("FTP server (v" APP_VERSION ") started.");

	// let ftp library take over thread
	uint32_t ret = server_run(ftp_server);

	// show shutdown msg
	show_msg("FTP server stopped.");

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
	ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...
	feat_command_import(ftp_command);
	base_command_import(ftp_command);
	ext_command_import(ftp_command);

	if(sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP") != 0)
	{
		free(ftp_command);
		_sys_ppu_thread_exit(0);
		return SYS_PRX_NO_RESIDENT;
	}

	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}
