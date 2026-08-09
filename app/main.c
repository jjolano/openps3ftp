/*
 * OpenPS3FTP — new headless PS3 app.
 * Runs the opftp server on port 2121 rooted at "/".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openps3ftp/openps3ftp.h>
#include <sys/systime.h>    /* sysSleep; no sys/timer.h in stock PSL1GHT */

#include "log.h"

/* Server log callback -> app log file (see log.c). */
static void log_cb(int level, const char* msg)
{
    opftp_app_log(level, "%s", msg);
}

#ifdef OPFTP_PS3
/* lv2 sys_net bridge — see plugin/netbridge.c for the implementation.
 * Overrides libc/libnet socket functions with raw lv2 syscalls (700-716)
 * so the app works on RPCS3 (which stubs libnet module imports). */
#include "../plugin/netbridge.h"
#endif

#ifndef OPFTP_HEADLESS
#include "osd.h"
#endif

int main(void)
{
    opftp_app_log_open();
    static const opftp_callbacks_t cb = { .log = log_cb, .log_level = 3 };
    opftp_server_t* s = opftp_server_create(&cb);
    if (!s) {
        printf("OpenPS3FTP: create failed\n");
        opftp_app_log(0, "create failed");
        return 1;
    }
    opftp_server_set_port(s, 2121);
    opftp_server_set_root(s, "/");
    opftp_server_set_workers(s, 2);
#ifdef OPFTP_HEADLESS
    /* RPCS3's lv2 sys_net only supports AF_INET (it aborts on
     * AF_INET6); the headless variant is the RPCS3 test build. */
    opftp_server_set_v4only(s, true);
#endif
#ifndef OPFTP_PS3
    /* Host build has no OSD to quit from; allow remote shutdown. */
    opftp_server_set_allow_stop(s, true);
#endif

    int rc = opftp_server_start(s);
    if (rc != 0) {
        printf("OpenPS3FTP: start failed (%d)\n", rc);
        return 1;
    }
    printf("OpenPS3FTP: listening on port 2121\n");
    opftp_app_log(0, "listening on port 2121");

#ifdef OPFTP_PS3
#ifndef OPFTP_HEADLESS
    /* Run the on-screen display on the main thread while the server
     * runs on its reactor thread. Returns when the user quits (hold
     * START for 2s) or the home button exit signal arrives. */
    opftp_osd_run(s, "v4.1");
#else
    /* Headless variant (RPCS3 harness): no OSD, no RSX. The reactor
     * runs on its own thread; keep the main thread alive until the
     * console is shut down. The STOP command can stop the server. */
    for (;;)
        sysSleep(1000);
#endif
#else
    /* Host build: run until the console is shut down; the STOP command
     * can be used to stop the server from an FTP client. */
    for (;;)
        sysSleep(1000);
#endif

    opftp_server_stop(s);
    opftp_server_destroy(s);
    opftp_app_log_close();
    return 0;
}
