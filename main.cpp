#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>

#include <NoRSX.h>

#include "const.h"
#include "server.h"

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

int main(void)
{
    // Initialize required sysmodules.
    load_sysmodules();

    // Initialize framebuffer.
    NoRSX* gfx;
    gfx = new NoRSX();

    MsgDialog md(gfx);
    Font font(LATIN2, gfx);
    Background bg(gfx);

    // netctl variables
    s32 netctl_state;
    net_ctl_info netctl_info;

    // server ppu thread id
    sys_ppu_thread_t server_tid;

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
    {
        lv2syscall8(837,
            (u64)"CELL_FS_IOS:BUILTIN_FLSH1",
            (u64)"CELL_FS_FAT",
            (u64)MOUNT_POINT,
            0, 0 /* rw */, 0, 0, 0);
    }

    // Create the server thread.
    sysThreadCreate(&server_tid, server_start, (void*)gfx, 1000, 0x10000, THREAD_JOINABLE, (char*)"ftpd");

    // Start application loop.
    gfx->AppStart();
    while(gfx->GetAppStatus())
    {
        // Draw frame.
        bg.Mono(COLOR_BLACK);

        font.Printf(50, 75, COLOR_WHITE, "OpenPS3FTP version " APP_VERSION);
        font.Printf(50, 175, COLOR_WHITE, "IP: %s", netctl_info.ip_address);

        gfx->Flip();
    }

    // Join server thread and wait for exit...
    sysThreadJoin(server_tid, NULL);
    
    // Unmount dev_blind.
    {
        lv2syscall1(838, (u64)MOUNT_POINT);
    }

unload:
    // Unload sysmodules.
    unload_sysmodules();

    // Free up graphics.
    gfx->NoRSX_Exit();
    return 0;
}
