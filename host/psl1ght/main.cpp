#include <stdbool.h>
#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sysutil/msg.h>
#include <sysmodule/sysmodule.h>
#include <NoRSX.h>
#include <NoRSX/NoRSXutil.h>

#include "server/server.h"
#include "server/client.h"
#include "server/cmd.h"

#include "commands/base.h"
#include "commands/ext.h"
#include "commands/feat.h"
#include "commands/site.h"

#define MOUNT_POINT	"/dev_blind"

using namespace std;

void load_sysmodules(void)
{
	sysModuleLoad(SYSMODULE_FS);
	sysModuleLoad(SYSMODULE_NET);
	sysModuleLoad(SYSMODULE_NETCTL);
	sysModuleLoad(SYSMODULE_SYSUTIL);
	sysModuleLoad(SYSMODULE_FREETYPE_TT);
	
	netInitialize();
	netCtlInit();
}

void unload_sysmodules(void)
{
	netCtlTerm();
	netDeinitialize();

	sysModuleUnload(SYSMODULE_FREETYPE_TT);
	sysModuleUnload(SYSMODULE_SYSUTIL);
	sysModuleUnload(SYSMODULE_NETCTL);
	sysModuleUnload(SYSMODULE_NET);
	sysModuleUnload(SYSMODULE_FS);
}

#ifdef __cplusplus
extern "C" {
#endif

LV2_SYSCALL sysLv2FsMount(const char* name, const char* fs, const char* path, int readonly)
{
	lv2syscall8(837,
		(u64)name,
		(u64)fs,
		(u64)path,
		0, readonly, 0, 0, 0);
	return_to_user_prog(s32);
}

LV2_SYSCALL sysLv2FsUnmount(const char* path)
{
	lv2syscall1(838, (u64)path);
	return_to_user_prog(s32);
}

#ifdef __cplusplus
}
#endif

void server_run_ex(void* arg)
{
	struct FTPServer* ftp_server = (struct FTPServer*) arg;
	sysThreadExit(ftpserv_run(ftp_server));
}

int main(void)
{
	// Initialize required sysmodules.
	load_sysmodules();

	// Initialize framebuffer.
	NoRSX* gfx = new (nothrow) NoRSX();

	if(!gfx)
	{
		// memory allocation error

		// Unload sysmodules.
		unload_sysmodules();

		return -2;
	}

	Background bg(gfx);
	Font text(LATIN2, gfx);

	// netctl variables
	s32 netctl_state;
	net_ctl_info netctl_info;

	// Check network connection status.
	netCtlGetState(&netctl_state);

	if(netctl_state != NET_CTL_STATE_IPObtained)
	{
		// Unload sysmodules.
		unload_sysmodules();

		// Free up graphics.
		gfx->NoRSX_Exit();

		return -1;
	}

	// Obtain network connection IP address.
	netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &netctl_info);

	// Mount dev_blind.
	sysLv2FsMount("CELL_FS_IOS:BUILTIN_FLSH1", "CELL_FS_FAT", MOUNT_POINT, 0);

	// Create the server thread.
	struct FTPCommand ftp_command;

	// initialize command struct
	ftpcmd_create(&ftp_command);

	// import commands...
	// base
	register_cmd(&ftp_command, "ABOR", ssftpCmdAbor);
	register_cmd(&ftp_command, "ACCT", ssftpCmdAcct);
	register_cmd(&ftp_command, "ALLO", ssftpCmdAllo);
	register_cmd(&ftp_command, "APPE", ssftpCmdStor);
	register_cmd(&ftp_command, "CDUP", ssftpCmdCdup);
	register_cmd(&ftp_command, "CWD", ssftpCmdCwd);
	register_cmd(&ftp_command, "DELE", ssftpCmdDele);
	register_cmd(&ftp_command, "HELP", ssftpCmdHelp);
	register_cmd(&ftp_command, "LIST", ssftpCmdList);
	register_cmd(&ftp_command, "MKD", ssftpCmdMkd);
	register_cmd(&ftp_command, "MODE", ssftpCmdMode);
	register_cmd(&ftp_command, "NLST", ssftpCmdNlst);
	register_cmd(&ftp_command, "NOOP", ssftpCmdNoop);
	register_cmd(&ftp_command, "PASS", ssftpCmdPass);
	register_cmd(&ftp_command, "PASV", ssftpCmdPasv);
	register_cmd(&ftp_command, "PORT", ssftpCmdPort);
	register_cmd(&ftp_command, "PWD", ssftpCmdPwd);
	register_cmd(&ftp_command, "QUIT", ssftpCmdQuit);
	register_cmd(&ftp_command, "REST", ssftpCmdRest);
	register_cmd(&ftp_command, "RETR", ssftpCmdRetr);
	register_cmd(&ftp_command, "RMD", ssftpCmdRmd);
	register_cmd(&ftp_command, "RNFR", ssftpCmdRnfr);
	register_cmd(&ftp_command, "RNTO", ssftpCmdRnto);
	register_cmd(&ftp_command, "SITE", ssftpCmdSite);
	register_cmd(&ftp_command, "STAT", ssftpCmdStat);
	register_cmd(&ftp_command, "STOR", ssftpCmdStor);
	register_cmd(&ftp_command, "STRU", ssftpCmdStru);
	register_cmd(&ftp_command, "SYST", ssftpCmdSyst);
	register_cmd(&ftp_command, "TYPE", ssftpCmdType);
	register_cmd(&ftp_command, "USER", ssftpCmdUser);

	// ext
	register_cmd(&ftp_command, "SIZE", ssftpCmdSize);
	register_cmd(&ftp_command, "MDTM", ssftpCmdMdtm);

	// feat
	register_cmd(&ftp_command, "FEAT", ssftpCmdFeat);

	// site
	register_cmd2(&ftp_command, "CHMOD", ssftpCmdChmod);
	register_cmd2(&ftp_command, "STOP", ssftpCmdStop);

	struct FTPServer ftp_server;
	
	// initialize server struct
	ftpserv_create(&ftp_server, 21, &ftp_command);

	sys_ppu_thread_t server_tid;
	sysThreadCreate(&server_tid, server_run_ex, (void*) &ftp_server, 1001, 0x4000, THREAD_JOINABLE, (char*) "ftpd");
	sysThreadYield();

	// Start application loop.
	gfx->AppStart();

	int flipped = 0;
	int last_num_connections = 0;

	while(gfx->GetAppStatus() && ftp_server.run)
	{
		int num_connections = ftp_server.nfds - 1;

		if(gfx->GetXMBStatus() == XMB_CLOSE && flipped < 2)
		{
			bg.Mono(COLOR_BLACK);

			text.Printf(100, 100, COLOR_WHITE, "OpenPS3FTP v4.4");
			text.Printf(100, 150, COLOR_WHITE, "IP Address: %s", netctl_info.ip_address);
			text.Printf(100, 200, COLOR_WHITE, "Connections: %d", num_connections);

			flipped++;
			gfx->Flip();
		}
		else
		{
			if(gfx->GetXMBStatus() == XMB_OPEN)
			{
				// flip an extra 4 frames
				flipped = -4;
			}

			if(last_num_connections != num_connections)
			{
				flipped = -2;
				last_num_connections = num_connections;
			}

			waitFlip();
			flip(gfx->context, gfx->currentBuffer);
			gfx->currentBuffer = !gfx->currentBuffer;
			setRenderTarget(gfx->context, &gfx->buffers[gfx->currentBuffer]);
			sysUtilCheckCallback();
		}
	}

	gfx->Flip();

	// Join server thread and wait for exit...
	ftpserv_stop(&ftp_server);

	u64 thread_exit;
	sysThreadJoin(server_tid, &thread_exit);

	ftpserv_destroy(&ftp_server);
	ftpcmd_destroy(&ftp_command);

	// Unmount dev_blind.
	sysLv2FsUnmount(MOUNT_POINT);

	// Unload sysmodules.
	unload_sysmodules();

	// Free up graphics.
	gfx->NoRSX_Exit();
	
	return 0;
}
