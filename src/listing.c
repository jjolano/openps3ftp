/*
 * LIST/NLST line formatting and mode strings.
 */
#include "opftp.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

void opftp_mode_str(uint16_t mode, char out[11])
{
    static const char rwx[] = "rwxrwxrwx";
    out[0] = (mode & S_IFMT) == S_IFDIR ? 'd' : '-';
    for (int i = 0; i < 9; i++) {
        uint16_t bit = (uint16_t) (0400 >> i);
        out[1 + i] = (mode & bit) ? rwx[i] : '-';
    }
    out[10] = '\0';
}

static const char* const MONTHS[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int opftp_listing_format(char* out, size_t n, const opftp_dirent_t* de,
                         const char* display_name)
{
    char modestr[11];
    opftp_mode_str(de->mode, modestr);

    struct tm tm;
    time_t t = (time_t) de->mtime;
    if (localtime_r(&t, &tm) == NULL) {
        memset(&tm, 0, sizeof(tm));
        tm.tm_mon = 0; tm.tm_mday = 1;
    }

    uint32_t uid = de->uid, gid = de->gid;
    if (uid == 0 && gid == 0) { uid = 1; gid = 1; }

    return snprintf(out, n, "%s 1 %u %u %12llu %s %02d %02d:%02d %s\r\n",
                    modestr, uid, gid,
                    (unsigned long long) de->size,
                    MONTHS[tm.tm_mon < 12 ? tm.tm_mon : 0],
                    tm.tm_mday, tm.tm_hour, tm.tm_min,
                    display_name ? display_name : de->name);
}
