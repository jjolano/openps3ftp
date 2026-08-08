/*
 * OpenPS3FTP — app log file.
 *
 * printf goes nowhere visible on a real PS3, so server + OSD events are
 * appended to a log file. Plain C, no deps; one global FILE, opened
 * once on the main thread before the server starts (no locking needed).
 */
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "log.h"

#ifdef OPFTP_PS3
#define OPFTP_LOG_PATH "/dev_hdd0/tmp/opftp.log"
#else
#define OPFTP_LOG_PATH "/tmp/opftp.log"
#endif

static FILE* g_log;

void opftp_app_log_open(void)
{
    if (!g_log)
        g_log = fopen(OPFTP_LOG_PATH, "a");
}

void opftp_app_log(int level, const char* fmt, ...)
{
    if (!g_log)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    fprintf(g_log, "%02d:%02d:%02d [%d] %s\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec, level, buf);
    fflush(g_log);
}

void opftp_app_log_close(void)
{
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}