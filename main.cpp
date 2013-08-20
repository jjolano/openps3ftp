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

#include "defs.h"

using namespace std;

msgType MSG_OK = (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON);
msgType MSG_YESNO = (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_YESNO);

// FTP server starter
void ftpInitialize(void *arg);

int main(s32 argc, char* argv[])
{
	// Initialize graphics
	NoRSX *GFX = new NoRSX();
	MsgDialog MSG(GFX);
	
	// Initialize networking and pad-input
	netInitialize();
	netCtlInit();
	ioPadInit(7);

	// Verify connection state
	s32 state;
	netCtlGetState(&state);

	while(state != NET_CTL_STATE_IPObtained)
	{
		MSG.Dialog(MSG_YESNO, ERR_NOCONN_Q);

		if(MSG.GetResponse(MSG_DIALOG_BTN_YES) != 1)
		{
			netCtlTerm();
			netDeinitialize();
			GFX->NoRSX_Exit();
			ioPadEnd();
			return 1;
		}

		netCtlGetState(&state);
	}

	// Set application running state
	GFX->AppStart();

	// Create thread for server
	sys_ppu_thread_t id;
	sysThreadCreate(&id, ftpInitialize, GFX, 1500, 0x1000, THREAD_JOINABLE, const_cast<char*>("oftp"));

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

	// Pad IO variables
	padInfo padinfo;
	padData paddata;

	// Main thread loop
	while(GFX->GetAppStatus())
	{
		// Get Pad Status
		ioPadGetInfo(&padinfo);

		// Handle only for first pad
		if(padinfo.status[0])
		{
			ioPadGetData(0, &paddata);

			if(paddata.BTN_START)
			{
				// Exit application
				GFX->AppExit();
				break;
			}

			if(paddata.BTN_L1)
			{
				// Credits
				MSG.Dialog(MSG_OK, CREDITS);
			}

			if(paddata.BTN_SELECT)
			{
				// dev_blind stuff
				sysFSStat stat;
				if(sysFsStat(DB_MOUNTPOINT, &stat) == 0)
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
		}

		// Draw frame
		BMap.DrawBitmap(&PCL);
		GFX->Flip();
	}

	BMap.ClearBitmap(&PCL);

	// Wait for server thread to complete
	u64 retval;
	sysThreadJoin(id, &retval);

	// Unload networking
	netCtlTerm();
	netDeinitialize();

	// Parse thread return value if application is not exiting
	if(!GFX->ExitSignalStatus() && retval > 0)
	{
		// Error - see ftp.cpp
		MSG.ErrorDialog((u32)retval);

		GFX->NoRSX_Exit();
		ioPadEnd();
		return 1;
	}

	GFX->NoRSX_Exit();
	ioPadEnd();
	return 0;
}
