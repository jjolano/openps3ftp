/*
 * OpenPS3FTP — vsh plugin (SPRX) entry point.
 *
 * Loaded into the vsh (XMB) process on CFW (Evilnat/Cobra) or PS3HEN
 * via boot_plugins.txt (see samples/vsh/hello-plugin in the PS3DK
 * repo for the deployment convention).  module_start runs the FTP
 * server inside the vsh process; the server's reactor thread does the
 * work after we return SYS_PRX_RESIDENT.
 *
 * No screen of its own: status comes from the shared UI model
 * (app/ui.c) via whatever renderer is attached (web UI, XMB settings
 * injection, ...).
 *
 * Build:
 *   cmake -S plugin -B plugin/build \
 *     -DCMAKE_TOOLCHAIN_FILE=<PS3DK>/cmake/ps3-ppu-toolchain.cmake
 *   cmake --build plugin/build
 * Produces: plugin/openps3ftp-plugin.sprx
 */
#include <sys/prx.h>
#include <cell/vsh.h>

#include <openps3ftp/openps3ftp.h>

#include "httpd.h"

#define OPFTP_PLUGIN_VERSION "v4.1-vsh"

static opftp_server_t* g_server;
static opftp_httpd_t* g_httpd;

int openps3ftp_module_start(uint64_t arg)
{
    vshtask_notify_t notify;
    (void)arg;

    notify = (vshtask_notify_t)vsh_get_nid_func("vshtask", VSHTASK_NOTIFY_FNID, 0);
    if (notify)
        notify(0, "OpenPS3FTP starting...");

    g_server = opftp_server_create(NULL);
    if (!g_server)
        return SYS_PRX_NO_RESIDENT;
    opftp_server_set_port(g_server, 2121);
    opftp_server_set_root(g_server, "/");
    opftp_server_set_workers(g_server, 2);
    opftp_server_set_v4only(g_server, true);
    if (opftp_server_start(g_server) != 0) {
        opftp_server_destroy(g_server);
        g_server = NULL;
        return SYS_PRX_NO_RESIDENT;
    }

    /* Web console: serves the shared UI model (app/ui.c) over HTTP.
     * Open the stock XMB browser at http://<ip>:8080/ */
    g_httpd = opftp_httpd_create(g_server, OPFTP_PLUGIN_VERSION, 8080, NULL);

    if (notify)
        notify(0, "OpenPS3FTP: listening on 2121, web UI on 8080");
    return SYS_PRX_RESIDENT;   /* stay loaded in the XMB */
}

int openps3ftp_module_stop(void)
{
    if (g_httpd) {
        opftp_httpd_destroy(g_httpd);
        g_httpd = NULL;
    }
    if (g_server) {
        opftp_server_stop(g_server);
        opftp_server_destroy(g_server);
        g_server = NULL;
    }
    return SYS_PRX_STOP_OK;
}

SYS_MODULE_INFO(openps3ftp, 0, 1, 0);
SYS_MODULE_START(openps3ftp_module_start);
SYS_MODULE_STOP(openps3ftp_module_stop);
SYS_MODULE_EXIT(openps3ftp_module_stop);
