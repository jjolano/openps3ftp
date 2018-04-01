#include <sdk_version.h>
#include <cellstatus.h>
#include <sys/prx.h>

#include "prx.h"
#include "mod.h"

SYS_MODULE_INFO(prxmb_ftp_plugin, 0, 1, 0);
SYS_MODULE_START(_start);
SYS_MODULE_STOP(_stop);

struct Command* ftp_command = NULL;
struct Server* ftp_server = NULL;

sys_ppu_thread_t ftp_tid;

int _start(size_t args, void* argp);
int _stop(void);
void ftp_main(uint64_t ptr);

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

	vshtask_notify("Starting FTP server on port 21.");

	ftp_server = (struct Server*) malloc(sizeof(struct Server));

	server_init(ftp_server, ftp_command, 21);
	ret = server_run(ftp_server);
	
	server_free(ftp_server);
	free(ftp_server);

	vshtask_notify("Stopped FTP server.");

	command_free(ftp_command);
	free(ftp_command);

	ftp_server = NULL;
	ftp_command = NULL;

	sys_ppu_thread_exit(ret);
}

void prxmb_if_action(const char* action)
{
	if(strcmp(action, "ftp_start") == 0)
	{
		if(ftp_server == NULL)
		{
			// initialize server
			sys_ppu_thread_create(&ftp_tid, ftp_main, 0, 1000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-FTPD");
		}
		else
		{
			vshtask_notify("FTP server already running.");
		}
	}

	if(strcmp(action, "ftp_stop") == 0)
	{
		if(ftp_server != NULL)
		{
			server_stop(ftp_server);

			uint64_t ftp_exitcode;
			sys_ppu_thread_join(ftp_tid, &ftp_exitcode);
		}
		else
		{
			vshtask_notify("FTP server not running.");
		}
	}
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
