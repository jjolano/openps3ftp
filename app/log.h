/*
 * OpenPS3FTP — app log file.
 * printf goes nowhere visible on a real console, so server + OSD events
 * are appended to a small log file. Plain C, no deps.
 */
#ifndef OPFTP_APP_LOG_H
#define OPFTP_APP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void opftp_app_log_open(void);                       /* open (append) */
void opftp_app_log(int level, const char* fmt, ...); /* timestamped line */
void opftp_app_log_close(void);

#ifdef __cplusplus
}
#endif

#endif /* OPFTP_APP_LOG_H */