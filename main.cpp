#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sysutil/msg.h>
#include <sysmodule/sysmodule.h>
#include <NoRSX.h>
#include <NoRSX/NoRSXutil.h>

#include "common.h"
#include "ftp/server.h"

#define MOUNT_POINT	"/dev_blind"
#define MSG_OK		(msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON)

using namespace std;

void load_sysmodules(void)
{
	sysModuleLoad(SYSMODULE_FS);
	sysModuleLoad(SYSMODULE_SYSUTIL);
	netInitialize();
	netCtlInit();
}

void unload_sysmodules(void)
{
	netCtlTerm();
	netDeinitialize();
	sysModuleUnload(SYSMODULE_SYSUTIL);
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

void dialog_callback(msgButton button, void* usrData)
{
	if(button == MSG_DIALOG_BTN_OK)
	{
		app_status* status = (app_status*)usrData;
		status->is_running = 0;
	}
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
	app_status status;
	status.is_running = 1;

	sys_ppu_thread_t server_tid;
	sysThreadCreate(&server_tid, server_start, (void*)&status, 1000, 0x10000, THREAD_JOINABLE, (char*)"ftpd");

	// Start application loop.
	gfx->AppStart();
	msgDialogOpen2(MSG_OK, netctl_info.ip_address, dialog_callback, (void*)&status, NULL);

	int buffer;
	buffer = 0;

	while(gfx->GetAppStatus() && status.is_running)
	{
		flip(gfx->context, buffer);

		sysUtilCheckCallback();
		buffer = !buffer;
		
		waitFlip();
	}

	// Join server thread and wait for exit...
	gfx->AppExit();
	msgDialogAbort();
	status.is_running = 0;

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
