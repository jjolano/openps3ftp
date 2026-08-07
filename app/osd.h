/*
 * OpenPS3FTP — PS3 on-screen display (OSD).
 *
 * C-compatible interface to the NoRSX (C++) OSD implementation in
 * osd.cpp.  Only built for the PS3 target (app/ is PS3-only; the
 * caller additionally guards with OPFTP_PS3).
 */
#ifndef OPFTP_OSD_H
#define OPFTP_OSD_H

#include <openps3ftp/openps3ftp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run the OSD on the calling thread until the user quits (hold START
 * for 2s, or the home button exit signal).  The server keeps running
 * on its own reactor thread; opftp_server_snapshot() is polled every
 * frame.  Returns 0 on a clean quit; the caller should then
 * opftp_server_stop()/opftp_server_destroy().
 *
 * `version` is the short brand version string shown in the header
 * badge (e.g. "v4.1").
 */
int opftp_osd_run(opftp_server_t* s, const char* version);

#ifdef __cplusplus
}
#endif

#endif /* OPFTP_OSD_H */
