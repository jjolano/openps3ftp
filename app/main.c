/*
 * OpenPS3FTP — new headless PS3 app.
 * Runs the opftp server on port 2121 rooted at "/".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openps3ftp/openps3ftp.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include "osd.h"

int main(void)
{
    opftp_server_t* s = opftp_server_create(NULL);
    if (!s) {
        printf("OpenPS3FTP: create failed\n");
        return 1;
    }
    opftp_server_set_port(s, 2121);
    opftp_server_set_root(s, "/");
    opftp_server_set_workers(s, 2);

    int rc = opftp_server_start(s);
    if (rc != 0) {
        printf("OpenPS3FTP: start failed (%d)\n", rc);
        return 1;
    }
    printf("OpenPS3FTP: listening on port 2121\n");

#ifdef OPFTP_PS3
    /* Run the on-screen display on the main thread while the server
     * runs on its reactor thread. Returns when the user quits (hold
     * START for 2s) or the home button exit signal arrives. */
    opftp_osd_run(s, "v4.1");
#else
    /* Host build: run until the console is shut down; the STOP command
     * can be used to stop the server from an FTP client. */
    for (;;)
        sys_timer_sleep(1000);
#endif

    opftp_server_stop(s);
    opftp_server_destroy(s);
    return 0;
}
