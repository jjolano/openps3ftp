/*
 * RFC 2640 minimal UTF-8 validation. Byte-preserving; rejects
 * malformed sequences, overlong encodings, surrogates, and code
 * points above U+10FFFF.
 */
#include "opftp.h"
#include <stddef.h>

bool opftp_utf8_valid(const char* s)
{
    if (!s) return false;
    const unsigned char* p = (const unsigned char*) s;

    while (*p) {
        unsigned char c = *p;
        if (c < 0x80) {
            p++;
            continue;
        }
        unsigned int cp;
        unsigned int need;
        if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F; need = 1;
            if (cp == 0) return false;               /* overlong */
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F; need = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07; need = 3;
        } else {
            return false;                            /* stray continuation / invalid lead */
        }
        if (need > 0) {
            p++;
            for (unsigned int i = 0; i < need; i++) {
                if ((*p & 0xC0) != 0x80)
                    return false;
                cp = (cp << 6) | (*p & 0x3F);
                p++;
            }
        }
        /* reject overlong, surrogates, > U+10FFFF */
        if (need == 1 && cp < 0x80) return false;
        if (need == 2 && cp < 0x800) return false;
        if (need == 3 && cp < 0x10000) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
    }
    return true;
}
