/*
 * OpenPS3FTP — host terminal UI (TUI).
 *
 * C interface to the ANSI terminal implementation in tui.c.  Host
 * builds only (the app/ sources are otherwise PS3-only; the CMake
 * host target guards this).
 */
#ifndef OPFTP_TUI_H
#define OPFTP_TUI_H

#include <openps3ftp/openps3ftp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run the TUI on the calling thread until the user quits (q or
 * Ctrl-C).  The server keeps running on its own reactor thread;
 * opftp_server_snapshot() is polled every frame.  Returns 0 on a
 * clean quit; the caller should then opftp_server_stop()/destroy().
 *
 * `version` is the short brand version string shown in the header
 * badge (e.g. "v4.1").
 */
int opftp_tui_run(opftp_server_t* s, const char* version);

#ifdef __cplusplus
}
#endif

#endif /* OPFTP_TUI_H */
