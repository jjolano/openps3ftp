/*
 * Core path resolution. No realpath(), no symlink handling — pure
 * lexical canonicalization of absolute paths under a root.
 */
#include "opftp.h"
#include <string.h>

/*
 * Tokenize a canonical absolute path into components of `stack`
 * (array of pointers into `buf`). Returns component count.
 */
static unsigned split_components(const char* path, char* buf, size_t bufsz,
                                 const char** stack, unsigned stackcap)
{
    unsigned n = 0;
    size_t len = strlen(path);
    if (len + 1 > bufsz)
        return 0;
    memcpy(buf, path, len + 1);

    char* p = buf;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char* start = p;
        while (*p && *p != '/') p++;
        char saved = *p;
        *p = '\0';
        if (n < stackcap)
            stack[n++] = start;
        if (!saved) break;
        p++;
    }
    return n;
}

int opftp_path_resolve(const char* root, const char* cwd, const char* arg,
                       char* out, size_t outsz, bool* trailing_slash)
{
    if (!root || !cwd || !arg || !out)
        return -1;

    char basebuf[OPFTP_MAX_PATH];
    const char* base = NULL;

    if (arg[0] == '/') {
        base = "/";               /* absolute: resolve from root */
    } else if (arg[0] == '\0') {
        base = cwd;               /* empty arg == cwd */
    } else {
        base = cwd;
    }
    if (strlen(base) >= sizeof(basebuf))
        return -1;
    strcpy(basebuf, base);

    /* trailing slash flag: arg ends with '/' (or is "/") */
    bool tsl = false;
    size_t alen = strlen(arg);
    if (alen > 0 && arg[alen - 1] == '/')
        tsl = true;

    const char* stack[512];
    char compbuf[OPFTP_MAX_PATH];
    unsigned n = split_components(basebuf, compbuf, sizeof(compbuf), stack, 512);

    /* base may already contain the root prefix (base == cwd); drop
     * root's components so the rebuild doesn't double it. */
    if (base[0] == '/') {
        const char* rcomp[64];
        char rbuf[OPFTP_MAX_PATH];
        unsigned rn = split_components(root, rbuf, sizeof(rbuf), rcomp, 64);
        if (rn <= n) {
            memmove(stack, stack + rn, (n - rn) * sizeof(*stack));
            n -= rn;
        }
    }

    /* resolve arg components against the stack */
    char argbuf[OPFTP_MAX_PATH];
    const char* astack[512];
    unsigned an = split_components(arg, argbuf, sizeof(argbuf), astack, 512);

    for (unsigned i = 0; i < an; i++) {
        const char* c = astack[i];
        if (strcmp(c, ".") == 0) {
            continue;
        } else if (strcmp(c, "..") == 0) {
            if (n > 0)
                n--;
            /* else: clamp at root */
        } else {
            if (n < 512)
                stack[n++] = c;
        }
    }

    /* rebuild: root prefix + stack */
    char result[OPFTP_MAX_PATH * 2];
    size_t rlen = 0;
    result[0] = '\0';

    /* root prefix */
    size_t rootlen = strlen(root);
    if (rootlen > 1) {
        if (rootlen + 1 > sizeof(result)) return -1;
        memcpy(result, root, rootlen + 1);
        rlen = rootlen;
    } else {
        result[0] = '/';
        result[1] = '\0';
        rlen = 1;
    }

    for (unsigned i = 0; i < n; i++) {
        if (rlen == 1 && result[0] == '/') {
            /* first component after root */
        } else if (rlen > 0 && result[rlen - 1] != '/') {
            if (rlen + 1 >= sizeof(result)) return -1;
            result[rlen++] = '/';
            result[rlen] = '\0';
        }
        size_t cl = strlen(stack[i]);
        if (rlen + cl + 1 > sizeof(result)) return -1;
        memcpy(result + rlen, stack[i], cl);
        rlen += cl;
        result[rlen] = '\0';
    }

    if (rlen == 0) {
        result[0] = '/';
        result[1] = '\0';
        rlen = 1;
    }

    if (rlen + 1 > outsz)
        return -1;
    memcpy(out, result, rlen + 1);
    if (trailing_slash)
        *trailing_slash = tsl;
    return 0;
}

int opftp_path_parent(const char* path, char* out, size_t outsz)
{
    if (!path || !out)
        return -1;
    size_t len = strlen(path);
    if (len <= 1) {              /* "/" or "" */
        if (outsz < 1) return -1;
        out[0] = '\0';
        return 0;
    }
    /* strip trailing slash(es) */
    while (len > 1 && path[len - 1] == '/')
        len--;
    /* find last '/' */
    const char* slash = NULL;
    for (size_t i = 0; i < len; i++)
        if (path[i] == '/')
            slash = path + i;
    if (!slash) {
        if (outsz < 1) return -1;
        out[0] = '\0';
        return 0;
    }
    size_t plen = (size_t)(slash - path);
    if (plen == 0)
        plen = 1;                /* "/a" -> "/" */
    if (plen + 1 > outsz) return -1;
    memcpy(out, path, plen);
    out[plen] = '\0';
    return 0;
}
