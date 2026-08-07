/*
 * OpenPS3FTP — host app: same server setup as the PS3 app
 * (app/main.c), but with the terminal TUI instead of the NoRSX OSD.
 * Optional args: [root path] [port].
 */
#include <stdio.h>
#include <stdlib.h>

#include <openps3ftp/openps3ftp.h>

#include "tui.h"

int main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : "/";
    int port = argc > 2 ? atoi(argv[2]) : 2121;

    opftp_server_t* s = opftp_server_create(NULL);
    if (!s) {
        printf("OpenPS3FTP: create failed\n");
        return 1;
    }
    opftp_server_set_port(s, (uint16_t)port);
    opftp_server_set_root(s, root);
    opftp_server_set_workers(s, 2);
    opftp_server_set_allow_stop(s, true);   /* host build: remote STOP */

    int rc = opftp_server_start(s);
    if (rc != 0) {
        printf("OpenPS3FTP: start failed (%d)\n", rc);
        return 1;
    }
    printf("OpenPS3FTP: listening on port %u\n",
           (unsigned)opftp_server_bound_port(s));

    /* Run the TUI on the main thread while the server runs on its
     * reactor thread. Returns when the user quits (q or Ctrl-C). */
    opftp_tui_run(s, "v4.1");

    opftp_server_stop(s);
    opftp_server_destroy(s);
    return 0;
}
