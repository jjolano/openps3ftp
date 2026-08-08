/* OpenPS3FTP vsh plugin — embedded HTTP server (web console). */
#ifndef OPFTP_HTTPD_H
#define OPFTP_HTTPD_H

#include <openps3ftp/openps3ftp.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct opftp_httpd opftp_httpd_t;

/* Start the web console: serves the shared UI model (app/ui.c) as
 * JSON plus the embedded web assets.  `version` is shown in the UI;
 * `port` 0 = default 8080.  Host builds serve assets from `asset_dir`
 * (ps3 builds serve bin2s-embedded copies).  Returns NULL on failure. */
opftp_httpd_t* opftp_httpd_create(opftp_server_t* server, const char* version,
                                  uint16_t port, const char* asset_dir);

void opftp_httpd_destroy(opftp_httpd_t* h);

#ifdef __cplusplus
}
#endif

#endif /* OPFTP_HTTPD_H */
