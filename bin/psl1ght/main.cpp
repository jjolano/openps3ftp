#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sysutil/msg.h>
#include <sysmodule/sysmodule.h>
#include <NoRSX.h>
#include <NoRSX/NoRSXutil.h>

#include "const.hpp"
#include "ftphelper.hpp"
#include "server.hpp"

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

#ifdef _USE_SYSFS_
	// Prepare Async IO.
	sysFsAioInit("/dev_hdd0");
#endif

	// Create the server thread.
	FTP::Command command;
	FTP::Server server(&command, 21);

	sys_ppu_thread_t server_tid;
	sysThreadCreate(&server_tid, ftp_server_start, (void*) &server, 1001, 0x10000, THREAD_JOINABLE, (char*)"ftpd");
	sysThreadYield();

	// Start application loop.
	gfx->AppStart();

	int flipped = 0;
	int last_num_connections = 0;

	while(gfx->GetAppStatus() && server.is_running())
	{
		int num_connections = server.get_num_connections();

		if(gfx->GetXMBStatus() == XMB_CLOSE && flipped < 2)
		{
			bg.Mono(COLOR_BLACK);

			text.Printf(100, 100, COLOR_WHITE, "OpenPS3FTP " APP_VERSION);
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
	server.stop();

	u64 thread_exit;
	sysThreadJoin(server_tid, &thread_exit);

#ifdef _USE_SYSFS_
	// Finish Async IO.
	sysFsAioFinish("/dev_hdd0");
#endif

	// Unmount dev_blind.
	sysLv2FsUnmount(MOUNT_POINT);

	// Unload sysmodules.
	unload_sysmodules();

	// Free up graphics.
	gfx->NoRSX_Exit();
	
	return 0;
}
