#include <iostream>

#include <net/net.h>
#include <net/netctl.h>
#include <io/pad.h>
#include <sys/thread.h>

#include "Sans_ttf.h"
#include "ftp.h"
#include "opf.h"

msgType MSG_OK = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK | MSG_DIALOG_DISABLE_CANCEL_ON);

NoRSX* GFX;

int main()
{
	// start required modules
	netInitialize();
	netCtlInit();

	padInfo padinfo;
	padData paddata;
	ioPadInit(7);

	// start NoRSX
	//GFX = new NoRSX(RESOLUTION_1920x1080);
	GFX = new NoRSX();

	Background BG(GFX);
	Bitmap BM(GFX);
	MsgDialog MSG(GFX);

	NoRSX_Bitmap PCL;
	BM.GenerateBitmap(&PCL);

	// load fonts
	Font F1(Sans_ttf, Sans_ttf_size, GFX);

	// set background
	BG.MonoBitmap(COLOR_BLACK, &PCL);

	// obtain connection information
	union net_ctl_info info;
	s32 noip = netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info);

	// don't need netctl anymore
	netCtlTerm();

	// check if we are connected (have IP address)
	if(noip == 0)
	{
		// start server thread
		sys_ppu_thread_t id;
		sysThreadCreate(&id, fsthread, NULL, 1500, 0x400, 0, const_cast<char *>("oftp"));

		// print text to screen
		F1.PrintfToBitmap(150, 200, &PCL, COLOR_WHITE, "OpenPS3FTP version %s", OFTP_VERSION);
		F1.PrintfToBitmap(150, 250, &PCL, COLOR_WHITE, "IP Address: %s - Port: 21", info.ip_address);
		F1.PrintfToBitmap(150, 300, &PCL, COLOR_WHITE, "Developed by John Olano (@jjolano)");
		F1.PrintfToBitmap(150, 350, &PCL, COLOR_WHITE, "Press X to exit");

		GFX->AppStart();

		while(GFX->GetAppStatus())
		{
			ioPadGetInfo(&padinfo);

			if(padinfo.status[0])
			{
				ioPadGetData(0, &paddata);

				if(paddata.BTN_CROSS)
				{
					GFX->AppExit();
				}
			}

			BM.DrawBitmap(&PCL);
			GFX->Flip();
		}
	}
	else
	{
		// not connected to network
		MSG.Dialog(MSG_OK, "Your console is not connected to a network. Please connect to a network and launch OpenPS3FTP again.\n\nOpenPS3FTP will now exit.");
	}

	// stop modules
	netDeinitialize();
	GFX->NoRSX_Exit();
	ioPadEnd();
	return 0;
}
