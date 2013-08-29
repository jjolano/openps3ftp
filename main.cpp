/*
 * main.cpp: screen+input handling and ftp server bootstrap
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <NoRSX.h>
#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <io/pad.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sysmodule/sysmodule.h>

#include "defs.h"

using namespace std;

msgType MSG_OK = (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON);
msgType MSG_YESNO = (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_YESNO);

// FTP server starter
void ftpInitialize(void *arg);

int main(s32 argc, char* argv[])
{
	// Initialize graphics
	NoRSX* GFX = new NoRSX();
	MsgDialog MSG(GFX);
	
	// Initialize networking
	netInitialize();
	netCtlInit();

	GFX->AppStart();

	// Verify connection state
	s32 state;
	netCtlGetState(&state);

	while(state != NET_CTL_STATE_IPObtained)
	{
		MSG.Dialog(MSG_YESNO, ERR_NOCONN_Q);

		if(MSG.GetResponse(MSG_DIALOG_BTN_YES) != 1)
		{
			GFX->AppExit();
			netCtlTerm();
			netDeinitialize();
			GFX->NoRSX_Exit();
			return 1;
		}

		netCtlGetState(&state);
	}

	// Initialize sysmodules
	ioPadInit(7);
	sysModuleLoad(SYSMODULE_FS);
	sysModuleLoad(SYSMODULE_MUSIC2); // for in-game background music?

	// Create thread for server
	sys_ppu_thread_t id;
	sysThreadCreate(&id, ftpInitialize, GFX, 1500, 0x4000, THREAD_JOINABLE, const_cast<char*>("oftp"));

	// Retrieve detailed connection information (ip address)
	net_ctl_info info;
	netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info);

	// Initialize bitmap
	Font F1(LATIN2, GFX);
	Background BG(GFX);
	Bitmap BMap(GFX);

	NoRSX_Bitmap PCL;
	BMap.GenerateBitmap(&PCL);
	BG.MonoBitmap(COLOR_BLACK, &PCL);

	F1.PrintfToBitmap(65, 75, &PCL, COLOR_WHITE, APP_NAME " version " APP_VERSION);
	F1.PrintfToBitmap(65, 125, &PCL, COLOR_WHITE, "by " APP_AUTHOR " (compiled " __DATE__ " " __TIME__")");

	F1.PrintfToBitmap(65, 225, &PCL, COLOR_WHITE, "IP Address: %s (port 21)", info.ip_address);

	F1.PrintfToBitmap(65, 325, &PCL, COLOR_WHITE, "SELECT: dev_blind mounter");
	F1.PrintfToBitmap(65, 375, &PCL, COLOR_WHITE, "START: Exit " APP_NAME);
	F1.PrintfToBitmap(65, 425, &PCL, COLOR_WHITE, "L1: Credits/Acknowledgements");

	// drawing
	int x = 0;

	// Pad IO variables
	padInfo padinfo;
	padData paddata;

	// dev_blind
	sysFSStat stat;

	// Main thread loop
	while(GFX->GetAppStatus())
	{
		if(GFX->GetXMBStatus() == XMB_CLOSE)
		{
			// Get Pad Status
			ioPadGetInfo(&padinfo);

			// Handle only for first pad
			if(padinfo.status[0])
			{
				ioPadGetData(0, &paddata);

				if(paddata.BTN_L1)
				{
					// Credits
					MSG.Dialog(MSG_OK, CREDITS);
				}

				if(paddata.BTN_SELECT)
				{
					// dev_blind stuff
					if(sysLv2FsStat(DB_MOUNTPOINT, &stat) == 0)
					{
						// dev_blind exists - ask to unmount
						MSG.Dialog(MSG_YESNO, DB_UNMOUNT_Q);

						if(MSG.GetResponse(MSG_DIALOG_BTN_YES) == 1)
						{
							// syscall unmount
							lv2syscall1(838, (u64)DB_MOUNTPOINT);

							// display success
							MSG.Dialog(MSG_OK, DB_UNMOUNT_S);
						}
					}
					else
					{
						// dev_blind does not exist - ask to mount
						MSG.Dialog(MSG_YESNO, DB_MOUNT_Q);

						if(MSG.GetResponse(MSG_DIALOG_BTN_YES) == 1)
						{
							// syscall mount
							lv2syscall8(837, (u64)"CELL_FS_IOS:BUILTIN_FLSH1",
										(u64)"CELL_FS_FAT",
										(u64)DB_MOUNTPOINT,
										0, 0 /* readonly */, 0, 0, 0);

							// display success with info
							MSG.Dialog(MSG_OK, DB_MOUNT_S);
						}
					}
				}

				if(paddata.BTN_START)
				{
					// Exit application
					GFX->AppExit();
				}
			}
			
			// Draw frame
			BMap.DrawBitmap(&PCL);
			GFX->Flip();
		}
		else
		{
			waitFlip();
			flip(GFX->context, x);
			x = !x;
			sysUtilCheckCallback();
		}
	}

	BMap.ClearBitmap(&PCL);

	// Wait for server thread
	u64 ret_val = 0;
	sysThreadJoin(id, &ret_val);

	if(GFX->ExitSignalStatus() == NO_SIGNAL && ret_val > 0)
	{
		MSG.ErrorDialog((u32)ret_val);
	}

	// Unload sysmodules
	ioPadEnd();
	sysModuleUnload(SYSMODULE_FS);
	sysModuleUnload(SYSMODULE_MUSIC2);

	// Unload networking
	netCtlTerm();
	netDeinitialize();

	GFX->NoRSX_Exit();
	return 0;
}
