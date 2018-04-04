#include <sdk_version.h>
#include <cellstatus.h>
#include <sys/prx.h>

#include "prx.h"
#include "mod.h"

SYS_MODULE_INFO(prxmb_ftp_plugin, 0, 1, 0);
SYS_MODULE_START(_start);
SYS_MODULE_STOP(_stop);

int _start(size_t args, void* argp);
int _stop(void);

void prxmb_if_action(const char* action)
{
	vshtask_notify(action);

	/*
	if(strcmp(action, "ftp_start") == 0)
	{
		// initialize server
		sys_ppu_thread_t ftp_tid;
		sys_ppu_thread_create(&ftp_tid, ftp_main, 0, 1000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-FTPD");
	}

	if(strcmp(action, "ftp_stop") == 0)
	{
		
	}*/
}

int _start(size_t args, void* argp)
{
	void* if_proxy_func[4] = { if_init, if_start, if_stop, if_exit };
	plugin_setInterface2(*(unsigned int*) argp, 1, if_proxy_func);
	return SYS_PRX_RESIDENT;
}

int _stop(void)
{
	return SYS_PRX_STOP_OK;
}
