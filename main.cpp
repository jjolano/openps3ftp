#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sysutil/msg.h>
#include <sysmodule/sysmodule.h>
#include <NoRSX.h>

#include "const.h"
#include "server.h"

#define MSG_OK (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON)

using namespace std;

void load_sysmodules(void)
{
	netInitialize();
	netCtlInit();
	sysModuleLoad(SYSMODULE_FS);
}

void unload_sysmodules(void)
{
	sysModuleUnload(SYSMODULE_FS);
	netCtlTerm();
	netDeinitialize();
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
	NoRSX* gfx = new NoRSX();
	MsgDialog md(gfx);

	// netctl variables
	s32 netctl_state;
	net_ctl_info netctl_info;

	// Check network connection status.
	netCtlGetState(&netctl_state);

	if(netctl_state != NET_CTL_STATE_IPObtained)
	{
		md.Dialog(MSG_OK, "No network connection detected. Please connect to a network before starting OpenPS3FTP.");
		goto unload;
	}

	// Obtain network connection IP address.
	netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &netctl_info);

	// Mount dev_blind.
	sysLv2FsMount("CELL_FS_IOS:BUILTIN_FLSH1", "CELL_FS_FAT", MOUNT_POINT, 0);

	// Prepare Async IO.
	sysFsAioInit("/dev_hdd0");
	sysFsAioInit(MOUNT_POINT);

	// Create the server thread.
	app_status status;
	status.is_running = 1;

	sys_ppu_thread_t server_tid;
	sysThreadCreate(&server_tid, server_start, (void*)&status, 1000, 0x10000, THREAD_JOINABLE, (char*)"ftpd");

	// Start application loop.
	gfx->AppStart();
	md.Dialog(MSG_OK, netctl_info.ip_address);
	gfx->AppExit();

	// Join server thread and wait for exit...
	status.is_running = 0;

	u64 thread_exit;
	sysThreadJoin(server_tid, &thread_exit);
	
	// Unmount dev_blind.
	sysLv2FsUnmount(MOUNT_POINT);

	// Finish Async IO.
	sysFsAioFinish("/dev_hdd0");
	sysFsAioFinish(MOUNT_POINT);

unload:
	// Unload sysmodules.
	unload_sysmodules();

	// Free up graphics.
	gfx->NoRSX_Exit();
	return 0;
}
