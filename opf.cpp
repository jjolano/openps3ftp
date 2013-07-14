/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <iostream>
#include <cstdlib>

#include <NoRSX.h>
#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <io/pad.h>
#include <sys/thread.h>
#include <sysutil/msg.h>
#include <sys/file.h>

#include "ftp.h"
#include "opf.h"

#define Pad_onPress(paddata, paddata_old, button) (paddata.button == 1 && paddata_old.button == 0)

msgType MSG_OK = (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON);

msgType MSG_YESNO = (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_YESNO);

NoRSX* GFX;

void _unload(void)
{
	ioPadEnd();
	GFX->NoRSX_Exit();
	netCtlTerm();
	netDeinitialize();
}

int main(s32 argc, char* argv[])
{
	atexit(_unload);

	// Initialize graphics
	GFX = new NoRSX();
	MsgDialog MSG(GFX);

	// Release message
	MSG.Dialog(MSG_OK, "This build of OpenPS3FTP has not been tested by the author. Please report any issues found to the OpenPS3FTP GitHub repository. See README.txt for more details.");

	// Initialize required libraries: net, netctl, io
	netInitialize();
	netCtlInit();
	ioPadInit(7);

	// Verify connection state
	s32 state;
	netCtlGetState(&state);

	if(state != NET_CTL_STATE_IPObtained)
	{
		// not connected to network - terminate program
		MSG.Dialog(MSG_OK, "Could not verify connection status. OpenPS3FTP will now exit.");
		exit(EXIT_FAILURE);
	}

	// Set application running state
	GFX->AppStart();

	// Create thread for server
	sys_ppu_thread_t id;
	sysThreadCreate(&id, ftp_main, GFX, 1001, 0x800, THREAD_JOINABLE, const_cast<char*>("opf_ftp_main"));

	// Set up graphics
	Font F1(LATIN2, GFX);
	Background BG(GFX);
	Bitmap BM(GFX);

	NoRSX_Bitmap PCL;
	BM.GenerateBitmap(&PCL);
	BG.MonoBitmap(COLOR_BLACK, &PCL);

	// Retrieve detailed connection information (ip address)
	net_ctl_info info;
	netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info);

	// Draw bitmap layer
	// Not sure how this will actually look.
	F1.PrintfToBitmap(50, 50, &PCL, COLOR_WHITE, "OpenPS3FTP version %s", OFTP_VERSION);
	F1.PrintfToBitmap(50, 100, &PCL, COLOR_WHITE, "Written by John Olano (twitter: @jjolano)");

	F1.PrintfToBitmap(50, 200, &PCL, COLOR_WHITE, "IP Address: %s (port 21)", info.ip_address);

	F1.PrintfToBitmap(50, 300, &PCL, COLOR_WHITE, "L1+R1: Mount /dev_blind");
	F1.PrintfToBitmap(50, 350, &PCL, COLOR_WHITE, "SELECT+START: Exit OpenPS3FTP");

	// Pad IO variables
	padInfo padinfo;
	padData paddata;
	padData paddata_old;

	// Main thread loop
	while(GFX->GetAppStatus() != APP_EXIT)
	{
		// Get Pad Status
		ioPadGetInfo(&padinfo);

		for(unsigned int i = 0; i < MAX_PADS; i++)
		{
			if(padinfo.status[i])
			{
				// Get Pad Data
				ioPadGetData(i, &paddata);

				// Parse Pad Data
				if(Pad_onPress(paddata, paddata_old, BTN_L1)
				&& Pad_onPress(paddata, paddata_old, BTN_R1))
				{
					// dev_blind stuff
					sysFSStat stat;
					int ret = sysFsStat("/dev_blind", &stat);

					if(ret == 0)
					{
						// dev_blind exists - ask to unmount
						MSG.Dialog(MSG_YESNO, "Do you want to unmount dev_blind?");

						if(MSG.GetResponse(MSG_DIALOG_BTN_YES) == 1)
						{
							// syscall unmount
							lv2syscall1(838, (u64)"/dev_blind");

							// display success
							MSG.Dialog(MSG_OK, "dev_blind was successfully unmounted.");
						}
					}
					else
					{
						// dev_blind does not exist - ask to mount
						MSG.Dialog(MSG_YESNO, "Do you want to mount dev_blind?");

						if(MSG.GetResponse(MSG_DIALOG_BTN_YES) == 1)
						{
							// syscall mount
							lv2syscall8(837, (u64)"CELL_FS_IOS:BUILTIN_FLSH1", (u64)"CELL_FS_FAT", (u64)"/dev_blind", 0, 0 /* readonly */, 0, 0, 0);

							// display success with info
							MSG.Dialog(MSG_OK, "dev_blind was successfully mounted. Please note that dev_blind will not automatically unmount upon exiting OpenPS3FTP.");
						}
					}
				}

				if(Pad_onPress(paddata, paddata_old, BTN_SELECT)
				&& Pad_onPress(paddata, paddata_old, BTN_START))
				{
					// Exit application
					GFX->AppExit();
				}

				paddata_old = paddata;
			}
		}

		// Draw bitmap->screenbuffer
		BM.DrawBitmap(&PCL);
		GFX->Flip();
	}

	BM.ClearBitmap(&PCL);

	// Wait for server thread to complete
	u64 retval;
	sysThreadJoin(id, &retval);

	// Parse thread return value if application is not exiting
	if(GFX->ExitSignalStatus() == NO_SIGNAL && retval != 0)
	{
		// Error - see ftp.cpp
		MSG.ErrorDialog((u32)retval);
		exit(EXIT_FAILURE);
	}

	return 0;
}
