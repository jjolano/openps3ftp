/*
 * OpenPS3FTP — host terminal UI (TUI).
 * Faithful text port of the PS3 OSD (app/osd.cpp); shares the UI
 * model via ui.h and the dark-console theme with app/webui/style.css.
 * ANSI truecolor, no external deps.
 */
#define _POSIX_C_SOURCE 200809L   /* clock_gettime, gmtime_r, sigaction */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openps3ftp/openps3ftp.h>
#include "ui.h"
#include "tui.h"

/* ------------------------------------------------------------------ *
 * Palette (same values as osd.cpp — the shared dark-console theme
 * codified in app/webui/style.css: near-black layered surfaces,
 * green accent, blue-grey labels, amber warn, red err)
 * ------------------------------------------------------------------ */
enum {
    C_BG      = 0x0B0E11,   /* screen background                    */
    C_PANEL   = 0x10151A,   /* card/panel background                */
    C_INSET   = 0x0D1216,   /* inset: tracks, strip band            */
    C_LINE    = 0x1E262E,   /* borders, rules                       */
    C_RAISE   = 0x2A3640,   /* raised surface border (overlays)     */
    C_TEXT    = 0xC8D0D8,   /* primary text                         */
    C_BRIGHT  = 0xE8EEF4,   /* brand, headline values               */
    C_MUTED   = 0x7D8894,   /* secondary text                       */
    C_LABEL   = 0x5A7684,   /* labels, table heads, hints           */
    C_ACC     = 0x3FAE6A,   /* green accent / ok / LISTENING        */
    C_BLUE    = 0x5E8CA8,   /* RETR direction (steel blue-grey kin) */
    C_WARN    = 0xE8A23A,   /* amber warn                           */
    C_ERR     = 0xD0504A,   /* red err                              */
    C_ERRBG   = 0x2A1512,   /* error strip tint                     */
    C_WARNBG  = 0x3A2A10,   /* warning strip tint (webui banner)    */
    C_ERRLINE = 0x5A2A28,   /* error strip edge                     */
    C_WARNLINE= 0x6A4A1A,   /* warning strip edge                   */
    C_FOCUS   = 0x16222B,   /* selected row tint                    */
    C_EMPTY   = 0x5A6570,   /* empty-state text                     */
    C_EMSG    = 0xF0B0AA,   /* error strip message                  */
    C_EMSW    = 0xE8C06A,   /* warning strip message                */
};

/* ------------------------------------------------------------------ *
 * Cell canvas: the TUI analog of the OSD framebuffer. Draw anywhere,
 * then emit the whole frame in one write.
 * ------------------------------------------------------------------ */
typedef struct { uint32_t fg, bg; char s[8]; } cell_t;

static int scr_rows = 24, scr_cols = 80;
static cell_t* cv;

static int uw(const char* s)          /* display width in UTF-8 chars */
{
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; n++) {
        p += (*p < 0x80) ? 1 : (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
    }
    return n;
}

static void put(int row, int col, uint32_t fg, uint32_t bg, const char* s)
{
    if (row < 0 || row >= scr_rows) return;
    for (const unsigned char* p = (const unsigned char*)s; *p && col < scr_cols;) {
        int l = (*p < 0x80) ? 1 : (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
        cell_t* c = &cv[row * scr_cols + col];
        memcpy(c->s, p, l < 8 ? l : 7);
        c->s[l < 8 ? l : 7] = 0;
        c->fg = fg; c->bg = bg;
        p += l; col++;
    }
}

/* Text on a surface: bg != 0 keeps panels solid (unset bg cells emit
 * no SGR and would speckle with the terminal's default background). */
static void txt(int row, int col, uint32_t fg, uint32_t bg, const char* fmt, ...)
{
    char b[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof(b), fmt, va);
    va_end(va);
    put(row, col, fg, bg, b);
}

static void txt_r(int row, int xr, uint32_t fg, uint32_t bg, const char* fmt, ...)
{
    char b[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof(b), fmt, va);
    va_end(va);
    put(row, xr - uw(b), fg, bg, b);
}

static void txt_c(int row, int cx, uint32_t fg, uint32_t bg, const char* fmt, ...)
{
    char b[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof(b), fmt, va);
    va_end(va);
    put(row, cx - uw(b) / 2, fg, bg, b);
}

static void hline(int y, int x0, int x1, const char* ch, uint32_t fg, uint32_t bg)
{
    for (int c = x0; c <= x1; c++) put(y, c, fg, bg, ch);
}

static void vline(int y0, int y1, int x, const char* ch, uint32_t fg, uint32_t bg)
{
    for (int r = y0; r <= y1; r++) put(r, x, fg, bg, ch);
}

/* Bordered panel: bg fill + 1-cell box. */
static void box(int y, int x, int h, int w, uint32_t fg, uint32_t bg)
{
    for (int r = y; r < y + h; r++)
        for (int c = x; c < x + w; c++) put(r, c, 0, bg, " ");
    hline(y, x + 1, x + w - 2, "\xE2\x94\x80", fg, bg);   /* ─ */
    hline(y + h - 1, x + 1, x + w - 2, "\xE2\x94\x80", fg, bg);
    vline(y + 1, y + h - 2, x, "\xE2\x94\x82", fg, bg);   /* │ */
    vline(y + 1, y + h - 2, x + w - 1, "\xE2\x94\x82", fg, bg);
    put(y, x, fg, bg, "\xE2\x94\x8C");                    /* ┌ */
    put(y, x + w - 1, fg, bg, "\xE2\x94\x90");            /* ┐ */
    put(y + h - 1, x, fg, bg, "\xE2\x94\x94");            /* └ */
    put(y + h - 1, x + w - 1, fg, bg, "\xE2\x94\x98");    /* ┘ */
}

/* Truncate with "…" to maxchars display cells. */
static void t_trunc(char* out, size_t n, const char* in, int maxchars)
{
    if (maxchars < 1) maxchars = 1;
    int w = 0;
    const unsigned char* p = (const unsigned char*)in;
    char* o = out;
    while (*p && w < maxchars) {
        int l = (*p < 0x80) ? 1 : (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
        if (w + 1 > maxchars) break;
        if (o - out > (long)n - 8) break;
        memcpy(o, p, l); o += l; p += l; w++;
    }
    if (*p && o - out < (long)n - 4) {
        memcpy(o, "\xE2\x80\xA6", 3); o += 3;   /* … */
    }
    *o = 0;
}

/* ------------------------------------------------------------------ *
 * Frame emission: merge same-SGR runs, one write per frame.
 * ------------------------------------------------------------------ */
static char* fbuf;
static size_t fbuf_cap;

static void frame_emit(void)
{
    size_t need = (size_t)scr_rows * ((size_t)scr_cols * 26 + 16) + 32;
    if (!fbuf || fbuf_cap < need) {
        free(fbuf);
        fbuf = malloc(need);
        fbuf_cap = need;
    }
    size_t o = 0;
    fbuf[o++] = '\x1b'; fbuf[o++] = '['; fbuf[o++] = 'H';
    for (int r = 0; r < scr_rows; r++) {
        uint32_t fg = 0, bg = 0;
        bool open = false;
        int cur = 0;                    /* terminal column cursor */
        for (int c = 0; c < scr_cols; c++) {
            const cell_t* cell = &cv[r * scr_cols + c];
            if (!cell->s[0]) continue;
            if (c > cur) {
                /* gap between content cells: plain default-color
                 * spaces (a terminal shows nothing for unset cells) */
                if (open) {
                    fbuf[o++] = '\x1b'; fbuf[o++] = '[';
                    fbuf[o++] = '0'; fbuf[o++] = 'm';
                    open = false;
                    fg = bg = 0;
                }
                do { fbuf[o++] = ' '; } while (++cur < c);
            }
            if (cell->fg != fg || cell->bg != bg) {
                fg = cell->fg; bg = cell->bg;
                if (open) { fbuf[o++] = '\x1b'; fbuf[o++] = '[';
                            fbuf[o++] = '0'; fbuf[o++] = 'm'; open = false; }
                fbuf[o++] = '\x1b'; fbuf[o++] = '[';
                if (fg) {
                    o += (size_t)sprintf(fbuf + o, "38;2;%d;%d;%dm",
                                         (fg >> 16) & 255, (fg >> 8) & 255, fg & 255);
                } else {
                    o += (size_t)sprintf(fbuf + o, "39m");
                }
                if (bg) {
                    o += (size_t)sprintf(fbuf + o, "\x1b[48;2;%d;%d;%dm",
                                         (bg >> 16) & 255, (bg >> 8) & 255, bg & 255);
                }
                open = true;
            }
            o += (size_t)sprintf(fbuf + o, "%s", cell->s);
            cur = c + uw(cell->s);
        }
        if (open) { fbuf[o++] = '\x1b'; fbuf[o++] = '['; fbuf[o++] = '0'; fbuf[o++] = 'm'; }
        fbuf[o++] = '\x1b'; fbuf[o++] = '['; fbuf[o++] = 'K';
        if (r < scr_rows - 1) fbuf[o++] = '\n';
    }
    fbuf[o++] = '\x1b'; fbuf[o++] = '['; fbuf[o++] = 'J';
    write(1, fbuf, o);
}

/* ------------------------------------------------------------------ *
 * App state
 * ------------------------------------------------------------------ */
static opftp_ui_t g;
static opftp_server_t* g_srv;
static uint64_t last_render_us;
static struct termios orig_term;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static void nap(uint64_t us)
{
    struct timespec ts = { (time_t)(us / 1000000ull), (long)(us % 1000000ull) * 1000 };
    nanosleep(&ts, 0);
}

static void get_size(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 10 && ws.ws_col > 30) {
        scr_rows = ws.ws_row; scr_cols = ws.ws_col;
    } else {
        scr_rows = 24; scr_cols = 80;
    }
}

static void on_sig(int s)
{
    tcsetattr(0, TCSANOW, &orig_term);
    const char r[] = "\x1b[0m\x1b[?25h";
    write(1, r, sizeof(r) - 1);
    _exit(128 + s);
}

/* ------------------------------------------------------------------ *
 * Shared bits: header / footer / slab / progress bar
 * ------------------------------------------------------------------ */
static void draw_header(void)
{
    put(0, 0, C_ACC, 0, "\xE2\x96\xA0");                  /* ■ brand mark */
    txt(0, 2, C_BRIGHT, 0, "OpenPS3FTP");
    txt(0, 2 + uw("OpenPS3FTP") + 2, C_LABEL, 0, "%s", g.version);

    /* clock (UTC, like the OSD) */
    char clock[8];
    time_t t = time(0);
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(clock, sizeof(clock), "%02d:%02d", tm.tm_hour, tm.tm_min);

    /* view tabs, right-aligned before the clock; active tab is a
     * solid accent chip (webui .tab.act) */
    int tabs_w = 0;
    for (int i = 0; i < OPFTP_UI_V_COUNT; i++)
        tabs_w += uw(opftp_ui_view_name(i)) + 4;
    int x = scr_cols - uw(clock) - 2 - tabs_w;
    for (int i = 0; i < OPFTP_UI_V_COUNT; i++) {
        const char* n = opftp_ui_view_name(i);
        if (g.view == i) {
            put(0, x, C_BG, C_ACC, " ");
            put(0, x + 1, C_BG, C_ACC, n);
            put(0, x + 1 + uw(n), C_BG, C_ACC, " ");
        } else {
            put(0, x, 0, 0, " ");
            put(0, x + 1, C_MUTED, 0, n);
            put(0, x + 1 + uw(n), 0, 0, " ");
        }
        x += uw(n) + 4;
    }
    txt_r(0, scr_cols - 1, C_LABEL, 0, "%s", clock);
    hline(1, 0, scr_cols - 1, "\xE2\x94\x80", C_LINE, 0);
}

static void draw_footer(void)
{
    int row = scr_rows - 1;
    hline(row - 1, 0, scr_cols - 1, "\xE2\x94\x80", C_LINE, 0);
    txt(row, 0, C_LABEL, 0, "? help   Enter detail   Esc back   q quit");
    char right[40];
    snprintf(right, sizeof(right), " \xE2\x80\x94 VIEW %d/%d", g.view + 1,
             OPFTP_UI_V_COUNT);
    int vw = uw(opftp_ui_view_name(g.view));
    int rx = scr_cols - 1 - (vw + uw(right));   /* right block, 1-col margin */
    txt(row, rx, C_MUTED, 0, "%s", opftp_ui_view_name(g.view));
    txt(row, rx + vw, C_LABEL, 0, "%s", right);
}

/* Section label: accent bar + label + optional boxed count + right text. */
static void slab(int y, const char* label, const char* cnt, const char* right)
{
    put(y, 0, C_ACC, 0, "\xE2\x96\x8D");                  /* ▍ */
    txt(y, 2, C_LABEL, 0, "%s", label);
    int x = 2 + uw(label) + 2;
    if (cnt) {
        txt(y, x, C_LINE, 0, "[");
        txt(y, x + 1, C_MUTED, 0, "%s", cnt);
        txt(y, x + 1 + uw(cnt), C_LINE, 0, "]");
    }
    if (right) txt_r(y, scr_cols - 1, C_LABEL, 0, "%s", right);
}

/* Block progress bar: colored fill, inset track. */
static void progress_bar(int y, int x, int w, uint64_t bytes, uint64_t total,
                         bool up)
{
    uint32_t fillc = up ? C_BLUE : C_ACC;
    int pct = (total && bytes < total) ? (int)(bytes * 100 / total) : 100;
    if (pct > 100) pct = 100;
    int filled = w * pct / 100;
    for (int c = 0; c < w; c++) {
        if (c < filled)
            put(y, x + c, fillc, C_INSET, "\xE2\x96\x88");     /* █ */
        else
            put(y, x + c, C_MUTED, C_INSET, "\xE2\x96\x91");   /* ░ */
    }
}

/* ------------------------------------------------------------------ *
 * STATUS view
 * ------------------------------------------------------------------ */
static int draw_event_strip(int y)
{
    int n = g.ev_count < 2 ? g.ev_count : 2;
    for (int i = 0; i < n; i++) {
        const opftp_ui_ev_t* e =
            &g.events[(g.ev_head - i + OPFTP_UI_MAX_EVENTS) % OPFTP_UI_MAX_EVENTS];
        uint32_t col = e->warn ? C_WARN : C_ERR;
        uint32_t bg = e->warn ? C_WARNBG : C_ERRBG;
        uint32_t edge = e->warn ? C_WARNLINE : C_ERRLINE;
        hline(y + i, 0, scr_cols - 1, " ", 0, bg);
        put(y + i, 0, col, bg, "\xE2\x96\x8E");            /* ▎edge tick */
        put(y + i, 1, edge, bg, " ");
        put(y + i, 3, col, bg, "!");
        txt(y + i, 5, e->warn ? C_EMSW : C_EMSG, bg, "%s", e->text);
        if (i == 0 && g.ev_count >= 2) {
            char b[24];
            snprintf(b, sizeof(b), "%d RECENT", g.ev_count);
            txt_r(y, scr_cols - 2, col, bg, "%s", b);
        }
    }
    return n;
}

static void draw_status_card(int y)
{
    bool listening = g.snap_ok && g.snap.started;
    uint32_t state_col = listening ? C_ACC : C_ERR;
    box(y, 0, 6, scr_cols, C_LINE, C_PANEL);
    put(y + 1, 1, state_col, C_PANEL, "\xE2\x97\x8F");    /* ● */
    txt(y + 1, 4, state_col, C_PANEL, "%s", listening ? "LISTENING" : "STOPPED");

    /* stat columns (right-aligned block, labels under values) */
    char portb[8], addr[24], rootb[40], workers[8];
    snprintf(portb, sizeof(portb), "%u", (unsigned)g.snap.port);
    snprintf(addr, sizeof(addr), "%s", g.local_ip[0] ? g.local_ip : "\xE2\x80\x94");
    snprintf(rootb, sizeof(rootb), "%s", g.snap.root);
    snprintf(workers, sizeof(workers), "%d", g.snap.workers);
    const char* vals[4] = { portb, addr, rootb, workers };
    const char* labels[4] = { "PORT", "ADDRESS", "ROOT", "WORKERS" };
    const int widths[4] = { 6, 16, 12, 7 };
    int xr = scr_cols - 2;
    int seg[4];
    for (int i = 3; i >= 0; i--) { seg[i] = xr - widths[i]; xr = seg[i] - 2; }
    for (int i = 0; i < 4; i++) {
        char v[20];
        t_trunc(v, sizeof(v), vals[i], widths[i]);
        put(y + 1, seg[i], C_BRIGHT, C_PANEL, v);
        put(y + 2, seg[i], C_LABEL, C_PANEL, labels[i]);
    }

    /* uptime strip (own row above the bottom border) */
    hline(y + 4, 1, scr_cols - 2, "\xE2\x94\x80", C_LINE, C_PANEL);
    uint64_t up = (now_us() - g.t0_us) / 1000000ull;
    char dur[16], gb[16];
    opftp_ui_fmt_dur(dur, sizeof(dur), up);
    opftp_ui_fmt_size(gb, sizeof(gb), g.session_bytes);
    txt(y + 5, 1, C_LABEL, C_PANEL, "UPTIME %s \xC2\xB7 %" PRIu64
        " TRANSFERS \xC2\xB7 %s THIS SESSION", dur, g.session_xfers, gb);
}

static void draw_xfer_card(int y, int idx, bool focused)
{
    const opftp_snapshot_client_t* c = &g.snap.clients[idx];
    bool up = !strcmp(c->xfer_op, "RETR") || !strcmp(c->xfer_op, "LIST");
    uint32_t ac = up ? C_BLUE : C_ACC;
    uint32_t cb = focused ? C_FOCUS : C_PANEL;
    box(y, 0, 3, scr_cols, focused ? C_ACC : C_LINE, cb);

    /* op + name + pct */
    put(y + 1, 1, ac, cb, up ? "\xE2\x86\x91" : "\xE2\x86\x93");
    txt(y + 1, 4, C_MUTED, cb, "%s", c->xfer_op);
    char pctb[8];
    if (c->xfer_total)
        snprintf(pctb, sizeof(pctb), "%d%%", (int)(c->xfer_bytes * 100 / c->xfer_total));
    else
        snprintf(pctb, sizeof(pctb), "--%%");
    int x = 4 + uw(c->xfer_op) + 2;
    char name[128];
    t_trunc(name, sizeof(name), c->xfer_path, scr_cols - 4 - uw(c->xfer_op) - 2
            - uw(pctb) - 4);
    txt(y + 1, x, C_TEXT, cb, "%s", name);
    txt_r(y + 1, scr_cols - 2, C_BRIGHT, cb, "%s", pctb);

    /* progress bar + meta */
    opftp_ui_trk_t* t = 0;
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++)
        if (g.trk[i].present && !strcmp(g.trk[i].peer, c->peer)) { t = &g.trk[i]; break; }
    char sz[24], tot[24], rate[16], meta[96];
    opftp_ui_fmt_size(sz, sizeof(sz), c->xfer_bytes);
    opftp_ui_fmt_size(tot, sizeof(tot), c->xfer_total);
    if (t && t->rate > 0) opftp_ui_fmt_rate(rate, sizeof(rate), t->rate);
    else snprintf(rate, sizeof(rate), "0 B/s");
    if (c->xfer_total)
        snprintf(meta, sizeof(meta), "%s / %s \xC2\xB7 %s", sz, tot, rate);
    else
        snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", sz, rate);
    int barw = scr_cols - 4 - uw(meta) - 3;
    if (barw < 8) barw = 8;
    progress_bar(y + 2, 1, barw, c->xfer_bytes, c->xfer_total, up);
    txt_r(y + 2, scr_cols - 2, C_LABEL, cb, "%s", meta);
}

static void draw_status(void)
{
    int y = 2 + draw_event_strip(2);

    draw_status_card(y);
    y += 6;

    /* active transfers slab + cards */
    int nxfer = 0;
    for (int i = 0; i < g.snap.num_clients; i++)
        if (g.snap.clients[i].xfer_active) nxfer++;
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d", nxfer);
    slab(y, "ACTIVE TRANSFERS", nxfer ? cnt : 0, 0);
    y += 1;

    int footer_top = scr_rows - 2;
    int avail = footer_top - y;
    int nfit = avail / 3;
    int shown = 0;
    for (int i = 0; i < g.snap.num_clients && shown < nfit; i++) {
        if (!g.snap.clients[i].xfer_active) continue;
        draw_xfer_card(y + shown * 3, i, g.sel_status == shown);
        shown++;
    }
    if (nxfer == 0 && avail >= 5) {
        box(y, 0, 3, scr_cols, C_LINE, C_INSET);
        txt_c(y + 1, scr_cols / 2, C_EMPTY, C_INSET, "No active transfers");
        y += 3;
    } else {
        y += shown * 3;
    }

    /* connected clients slab + chips */
    snprintf(cnt, sizeof(cnt), "%d", g.snap.num_clients);
    slab(y, "CONNECTED CLIENTS", g.snap.num_clients ? cnt : 0, 0);
    y += 1;
    if (y >= footer_top) return;

    char chipt[96];
    int x = 0;
    int drawn = 0;
    for (int i = 0; i < g.snap.num_clients && drawn < 4; i++) {
        const opftp_snapshot_client_t* c = &g.snap.clients[i];
        const char* user = c->user[0] ? c->user : "?";
        if (c->xfer_active) {
            snprintf(chipt, sizeof(chipt), "%s \xC2\xB7 %s", user, c->xfer_op);
        } else {
            uint64_t idl = 0;
            for (int t = 0; t < OPFTP_UI_MAX_TRACK; t++)
                if (g.trk[t].present && !strcmp(g.trk[t].peer, c->peer))
                    idl = (now_us() - g.trk[t].idle_reset_us) / 1000000ull;
            char idle[8];
            opftp_ui_fmt_idle(idle, sizeof(idle), idl);
            snprintf(chipt, sizeof(chipt), "%s \xC2\xB7 idle %s", user, idle);
        }
        char peerb[24];
        t_trunc(peerb, sizeof(peerb), c->peer, 17);
        int cw = 2 + uw(peerb) + 1 + uw(chipt);
        if (x + cw > scr_cols - 8) break;
        put(y, x, C_ACC, 0, "\xE2\x97\x8F");
        txt(y, x + 2, C_TEXT, 0, "%s", peerb);
        txt(y, x + 2 + uw(peerb) + 1, C_LABEL, 0, "%s", chipt);
        x += cw + 2;
        drawn++;
    }
    if (g.snap.num_clients > drawn) {
        char more[16];
        snprintf(more, sizeof(more), "+%d", g.snap.num_clients - drawn);
        txt(y, x, C_LABEL, 0, "%s", more);
    }
}

/* ------------------------------------------------------------------ *
 * Table primitives (transfers history / clients)
 * ------------------------------------------------------------------ */
static void draw_scrollbar(int y, int h, int scroll, int visible, int total)
{
    int x = scr_cols - 1;
    for (int r = 0; r < h; r++) put(y + r, x, 0, C_INSET, " ");
    if (total <= visible) return;
    int th = h * 32 / 100;
    if (th < 1) th = 1;
    int range = total - visible;
    int ty = y + (h - th) * scroll / range;
    for (int r = ty; r < ty + th && r < y + h; r++)
        put(r, x, C_MUTED, C_INSET, "\xE2\x96\x88");
}

/* ------------------------------------------------------------------ *
 * TRANSFERS view
 * ------------------------------------------------------------------ */
static void draw_transfers(void)
{
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d", g.hist_count);
    char showing[24] = { 0 };
    if (g.hist_count > 0)
        snprintf(showing, sizeof(showing), "SHOWING %d\xE2\x80\x93%d",
                 g.scroll_hist + 1,
                 g.scroll_hist + 8 < g.hist_count ? g.scroll_hist + 8 : g.hist_count);
    slab(2, "TRANSFER HISTORY", g.hist_count ? cnt : 0,
         g.hist_count ? showing : 0);

    /* head */
    txt(3, 1, C_LABEL, 0, "TIME");
    txt(3, 8, C_LABEL, 0, "DIR");
    txt(3, 18, C_LABEL, 0, "FILE");
    txt_r(3, scr_cols - 12, C_LABEL, 0, "SIZE");
    txt_r(3, scr_cols - 2, C_LABEL, 0, "RESULT");
    hline(4, 0, scr_cols - 2, "\xE2\x94\x80", C_LINE, 0);

    int y = 5;
    int rows_h = scr_rows - 2 - y;
    if (rows_h < 1) rows_h = 1;
    int total = g.hist_count;
    g.scroll_hist = opftp_ui_clamp_scroll(g.sel_hist, g.scroll_hist, rows_h, total);

    for (int r = 0; r < rows_h; r++) {
        int idx = g.scroll_hist + r;
        if (idx >= total) break;
        int ry = y + r;
        const opftp_ui_hist_t* h =
            &g.hist[(g.hist_head - idx + OPFTP_UI_MAX_HIST) % OPFTP_UI_MAX_HIST];
        bool up = !strcmp(h->op, "RETR") || !strcmp(h->op, "LIST");
        uint32_t ac = up ? C_BLUE : C_ACC;
        uint32_t bg = (g.sel_hist == idx) ? C_FOCUS : 0;

        char t[8];
        snprintf(t, sizeof(t), "%02d:%02d", h->hh, h->mm);
        txt(ry, 1, C_MUTED, bg, "%s", t);
        put(ry, 8, ac, bg, up ? "\xE2\x86\x91" : "\xE2\x86\x93");
        txt(ry, 11, C_MUTED, bg, "%s", h->op);
        char f[128];
        t_trunc(f, sizeof(f), h->path, scr_cols - 18 - 9 - 10 - 6);
        txt(ry, 18, C_TEXT, bg, "%s", f);
        char sz[16];
        opftp_ui_fmt_size(sz, sizeof(sz), h->bytes);
        txt_r(ry, scr_cols - 12, C_TEXT, bg, "%s", sz);
        const char* rs = h->result == OPFTP_UI_H_OK ? "OK"
                       : h->result == OPFTP_UI_H_ABORTED ? "ABORTED" : "ERROR";
        uint32_t rc = h->result == OPFTP_UI_H_OK ? C_ACC
                    : h->result == OPFTP_UI_H_ABORTED ? C_WARN : C_ERR;
        txt_r(ry, scr_cols - 2, rc, bg, "%s", rs);
    }
    draw_scrollbar(y, rows_h, g.scroll_hist, rows_h, total);
}

/* ------------------------------------------------------------------ *
 * CLIENTS view
 * ------------------------------------------------------------------ */
static void draw_clients(void)
{
    char cnt[8], slots[24];
    snprintf(cnt, sizeof(cnt), "%d", g.snap.num_clients);
    snprintf(slots, sizeof(slots), "%d OF %d SLOTS IN USE",
             g.snap.num_clients, OPFTP_SNAPSHOT_MAX_CLIENTS);
    slab(2, "CLIENTS", g.snap.num_clients ? cnt : 0, slots);

    txt(3, 2, C_LABEL, 0, "IP ADDRESS");
    txt(3, 20, C_LABEL, 0, "USER");
    txt(3, 34, C_LABEL, 0, "CURRENT DIRECTORY");
    txt_r(3, scr_cols - 2, C_LABEL, 0, "IDLE");
    hline(4, 0, scr_cols - 2, "\xE2\x94\x80", C_LINE, 0);

    int y = 5;
    int rows_h = scr_rows - 2 - y - 1;
    if (rows_h < 1) rows_h = 1;
    int total = g.snap.num_clients;
    g.scroll_cli = opftp_ui_clamp_scroll(g.sel_cli, g.scroll_cli, rows_h, total);

    for (int r = 0; r < rows_h; r++) {
        int ci = g.scroll_cli + r;
        if (ci >= total) break;
        int ry = y + r;
        const opftp_snapshot_client_t* c = &g.snap.clients[ci];
        uint32_t bg = (g.sel_cli == ci) ? C_FOCUS : 0;

        put(ry, 0, c->xfer_active ? C_ACC : C_LINE, bg, "\xE2\x96\xA0");
        char ip[24];
        t_trunc(ip, sizeof(ip), c->peer, 16);
        txt(ry, 2, C_TEXT, bg, "%s", ip);
        char usr[16];
        t_trunc(usr, sizeof(usr), c->user[0] ? c->user : "?", 11);
        txt(ry, 20, C_TEXT, bg, "%s", usr);
        char cwd[128];
        t_trunc(cwd, sizeof(cwd), c->cwd, scr_cols - 34 - 8 - 6);
        txt(ry, 34, C_TEXT, bg, "%s", cwd);

        char idle[8];
        uint64_t idl = 0;
        for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++) {
            if (!g.trk[i].present || strcmp(g.trk[i].peer, c->peer)) continue;
            idl = (now_us() - (c->xfer_active ? g.trk[i].xfer_t0_us
                                              : g.trk[i].idle_reset_us)) / 1000000ull;
            break;
        }
        opftp_ui_fmt_idle(idle, sizeof(idle), idl);
        txt_r(ry, scr_cols - 2, C_MUTED, bg, "%s", idle);
    }
    draw_scrollbar(y, rows_h, g.scroll_cli, rows_h, total);

    txt(scr_rows - 2, 0, C_LABEL, 0,
        "Square marker: client has a transfer in progress. "
        "Idle time is mm:ss since the last command.");
}

/* ------------------------------------------------------------------ *
 * SETTINGS view
 * ------------------------------------------------------------------ */
static void draw_settings(void)
{
    int ev_rows = draw_event_strip(2);          /* apply errors visible here */
    slab(2 + ev_rows, "SETTINGS", 0, "READ-ONLY");
    int y = 4 + ev_rows;

    const char* keys[5] = { "Port", "Root path", "Worker threads", "TLS",
                            "Require TLS" };
    char vals[5][24];
    char rootv[48];
    snprintf(vals[0], sizeof(vals[0]), "%u", (unsigned)g.snap.port);
    t_trunc(rootv, sizeof(rootv), g.snap.root, scr_cols - 22);
    snprintf(vals[1], sizeof(vals[1]), "%s", rootv);
    snprintf(vals[2], sizeof(vals[2]), "%d", g.snap.workers);
    snprintf(vals[3], sizeof(vals[3]), "OFF");
    snprintf(vals[4], sizeof(vals[4]), "OFF");

    box(y, 0, 7, scr_cols, C_LINE, C_PANEL);
    for (int i = 0; i < 5; i++) {
        int ry = y + 1 + i;
        uint32_t bg = (g.sel_settings == i) ? C_FOCUS : C_PANEL;
        hline(ry, 1, scr_cols - 2, " ", 0, bg);   /* focus fill */
        if (g.sel_settings == i)
            put(ry, 1, C_ACC, bg, "\xE2\x96\xB6");   /* ▶ marker */
        txt(ry, 4, C_TEXT, bg, "%s", keys[i]);
        if (i < 3) {
            txt_r(ry, scr_cols - 2, C_TEXT, bg, "%s", vals[i]);
            if (i == 1) {           /* Root path: editable */
                txt_r(ry, scr_cols - 2 - uw(vals[i]) - 2, C_ACC, bg, "[EDIT]");
                if (g.sel_settings == i)
                    txt_r(ry, scr_cols - 2 - uw(vals[i]) - 2
                          - uw(" [Enter]"), C_MUTED, bg, "Enter");
            }
        } else {
            txt_r(ry, scr_cols - 2, C_LINE, bg, "[");
            txt_r(ry, scr_cols - 3, C_LABEL, bg, "%s", vals[i]);
        }
        if (i < 4) hline(ry + 1, 1, scr_cols - 2, "\xE2\x94\x80", C_LINE, C_PANEL);
    }

    if (g.edit.field == OPFTP_UI_EDIT_NONE)
        txt(y + 7, 0, C_LABEL, 0,
            "Press Enter on the Root path row to change it. "
            "Other values are read-only.");
}

/* ------------------------------------------------------------------ *
 * Overlays (drawn last)
 * ------------------------------------------------------------------ */
static void overlay_panel(int* y, int* x, int* hh, int* w)
{
    *w = scr_cols - 8 < 52 ? scr_cols - 8 : 52;
    *x = (scr_cols - *w) / 2;
    *y = (scr_rows - *hh) / 2;
    box(*y, *x, *hh, *w, C_RAISE, C_PANEL);   /* raised border, webui #ov */
}

static void draw_detail_hist(void)
{
    if (g.detail.idx < 0 || g.detail.idx >= g.hist_count) return;
    const opftp_ui_hist_t* h =
        &g.hist[(g.hist_head - g.detail.idx + OPFTP_UI_MAX_HIST) % OPFTP_UI_MAX_HIST];
    bool up = !strcmp(h->op, "RETR") || !strcmp(h->op, "LIST");
    int x, y, w, hh = 8;
    overlay_panel(&y, &x, &hh, &w);

    txt(y + 1, x + 2, C_LABEL, C_PANEL, "TRANSFER DETAIL");
    put(y + 2, x + 2, up ? C_BLUE : C_ACC, C_PANEL, up ? "\xE2\x86\x91" : "\xE2\x86\x93");
    char sz[16], tot[16];
    opftp_ui_fmt_size(sz, sizeof(sz), h->bytes);
    opftp_ui_fmt_size(tot, sizeof(tot), h->total);
    txt(y + 2, x + 5, C_TEXT, C_PANEL, "%s", h->op);
    txt(y + 2, x + 5 + uw(h->op) + 2, C_MUTED, C_PANEL, "%02d:%02d \xC2\xB7 %s", h->hh, h->mm, sz);
    if (h->total)
        txt_r(y + 2, x + w - 2, C_MUTED, C_PANEL, "of %s", tot);

    size_t len = strlen(h->path), off = 0, li = 0;
    while (off < len && li < 3) {
        int take = (int)(len - off > (size_t)(w - 8) ? (size_t)(w - 8) : len - off);
        char line[128];
        memcpy(line, h->path + off, take);
        line[take] = 0;
        txt(y + 3 + (int)li, x + 2, C_TEXT, C_PANEL, "%s", line);
        off += (size_t)take; li++;
    }
    if (off < len) txt(y + 3 + (int)li, x + 2, C_MUTED, C_PANEL, "\xE2\x80\xA6");

    const char* rs = h->result == OPFTP_UI_H_OK ? "OK"
                   : h->result == OPFTP_UI_H_ABORTED ? "ABORTED" : "ERROR";
    uint32_t rc = h->result == OPFTP_UI_H_OK ? C_ACC
                : h->result == OPFTP_UI_H_ABORTED ? C_WARN : C_ERR;
    txt(y + hh - 2, x + 2, rc, C_PANEL, "RESULT: %s", rs);
    txt_r(y + hh - 2, x + w - 2, C_LABEL, C_PANEL, "\xC3\x97 Close");
}

static void draw_detail_xfer(void)
{
    if (g.detail.idx < 0 || g.detail.idx >= g.snap.num_clients) return;
    const opftp_snapshot_client_t* c = &g.snap.clients[g.detail.idx];
    if (!c->xfer_active) return;
    bool up = !strcmp(c->xfer_op, "RETR") || !strcmp(c->xfer_op, "LIST");
    int x, y, w, hh = 8;
    overlay_panel(&y, &x, &hh, &w);

    txt(y + 1, x + 2, C_LABEL, C_PANEL, "ACTIVE TRANSFER");
    put(y + 2, x + 2, up ? C_BLUE : C_ACC, C_PANEL, up ? "\xE2\x86\x91" : "\xE2\x86\x93");
    txt(y + 2, x + 5, C_TEXT, C_PANEL, "%s", c->xfer_op);

    char sz[24], tot[24], rate[16];
    opftp_ui_fmt_size(sz, sizeof(sz), c->xfer_bytes);
    opftp_ui_fmt_size(tot, sizeof(tot), c->xfer_total);
    opftp_ui_trk_t* t = 0;
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++)
        if (g.trk[i].present && !strcmp(g.trk[i].peer, c->peer)) { t = &g.trk[i]; break; }
    opftp_ui_fmt_rate(rate, sizeof(rate), t && t->rate > 0 ? t->rate : 0);
    if (c->xfer_total)
        txt_r(y + 2, x + w - 2, C_TEXT, C_PANEL, "%s of %s \xC2\xB7 %s", sz, tot, rate);
    else
        txt_r(y + 2, x + w - 2, C_TEXT, C_PANEL, "%s \xC2\xB7 %s", sz, rate);

    size_t len = strlen(c->xfer_path), off = 0, li = 0;
    while (off < len && li < 3) {
        int take = (int)(len - off > (size_t)(w - 8) ? (size_t)(w - 8) : len - off);
        char line[128];
        memcpy(line, c->xfer_path + off, take);
        line[take] = 0;
        txt(y + 3 + (int)li, x + 2, C_TEXT, C_PANEL, "%s", line);
        off += (size_t)take; li++;
    }
    if (off < len) txt(y + 3 + (int)li, x + 2, C_MUTED, C_PANEL, "\xE2\x80\xA6");

    txt(y + hh - 2, x + 2, C_MUTED, C_PANEL, "CLIENT %s", c->peer);
    txt_r(y + hh - 2, x + w - 2, C_LABEL, C_PANEL, "\xC3\x97 Close");
}

static void draw_detail_client(void)
{
    if (g.detail.idx < 0 || g.detail.idx >= g.snap.num_clients) return;
    const opftp_snapshot_client_t* c = &g.snap.clients[g.detail.idx];
    int x, y, w, hh = 9;
    overlay_panel(&y, &x, &hh, &w);

    txt(y + 1, x + 2, C_LABEL, C_PANEL, "CLIENT DETAIL");
    txt(y + 2, x + 2, C_TEXT, C_PANEL, "%s", c->peer);
    txt(y + 3, x + 2, C_LABEL, C_PANEL, "USER");
    txt(y + 3, x + 8, C_TEXT, C_PANEL, "%s", c->user[0] ? c->user : "?");
    txt(y + 4, x + 2, C_LABEL, C_PANEL, "STATUS");
    txt(y + 4, x + 12, c->xfer_active ? C_ACC : C_MUTED, C_PANEL, "%s",
        c->xfer_active ? c->xfer_op : "idle");

    size_t len = strlen(c->cwd), off = 0, li = 0;
    txt(y + 5, x + 2, C_LABEL, C_PANEL, "CWD");
    while (off < len && li < 2) {
        int take = (int)(len - off > (size_t)(w - 8) ? (size_t)(w - 8) : len - off);
        char line[128];
        memcpy(line, c->cwd + off, take);
        line[take] = 0;
        txt(y + 6 + (int)li, x + 2, C_TEXT, C_PANEL, "%s", line);
        off += (size_t)take; li++;
    }
    if (off < len) txt(y + 6 + (int)li, x + 2, C_MUTED, C_PANEL, "\xE2\x80\xA6");
    txt_r(y + hh - 2, x + w - 2, C_LABEL, C_PANEL, "\xC3\x97 Close");
}

static void draw_help(void)
{
    int x, y, w, hh = 9;
    overlay_panel(&y, &x, &hh, &w);

    txt(y + 1, x + 2, C_LABEL, C_PANEL, "HELP");
    static const char* rows[6] = {
        "\xE2\x86\x91\xE2\x86\x93 Move selection",
        "\xE2\x86\x90\xE2\x86\x92 Switch view",
        "Enter  Open details",
        "Esc    Close / clear selection",
        "?      Close this panel",
        "q      Quit",
    };
    for (int i = 0; i < 6; i++)
        txt(y + 2 + i, x + 2, C_TEXT, C_PANEL, "%s", rows[i]);
}

static void draw_detail_or_help(void)
{
    if (g.detail.kind >= 0) {
        if (g.detail.kind == OPFTP_UI_D_HIST) draw_detail_hist();
        else if (g.detail.kind == OPFTP_UI_D_XFER) draw_detail_xfer();
        else draw_detail_client();
    } else if (g.help) {
        draw_help();
    }
}

/* Text-entry bar (drawn over the view while an edit is active). */
static void draw_edit_line(void)
{
    int y = scr_rows - 3;
    hline(y, 0, scr_cols - 1, " ", 0, C_INSET);
    txt(y, 1, C_ACC, C_INSET, "EDIT ROOT PATH:");
    int x = 1 + uw("EDIT ROOT PATH:") + 2;
    int maxw = scr_cols - x - 24;
    if (maxw < 8) maxw = 8;
    char buf[128];
    t_trunc(buf, sizeof(buf), g.edit.buf, maxw);
    txt(y, x, C_TEXT, C_INSET, "%s", buf);
    if ((now_us() / 500000ull) & 1)             /* 2Hz cursor blink */
        put(y, x + uw(buf), C_ACC, C_INSET, "\xE2\x96\x8C");
    txt_r(y, scr_cols - 2, C_LABEL, C_INSET, "[Enter] apply  [Esc] cancel");
}

/* ------------------------------------------------------------------ *
 * Input — raw mode key events (edge-triggered per keypress)
 * ------------------------------------------------------------------ */
static void help_toggle(void)
{
    if (g.detail.kind >= 0) g.detail.kind = OPFTP_UI_D_NONE;
    else g.help = !g.help;
}

static void esc_key(void)
{
    if (g.detail.kind >= 0) g.detail.kind = OPFTP_UI_D_NONE;
    else if (g.help) g.help = false;
    else opftp_ui_clear_sel(&g);
}

/* Apply a committed edit (currently the Root path field). */
static void edit_apply(void)
{
    const char* v = opftp_ui_edit_commit(&g);
    int rc = opftp_server_set_root_runtime(g_srv, v);
    if (rc != 0)
        opftp_ui_event(&g, "ROOT NOT CHANGED \xE2\x80\x94 path must be absolute",
                       true, now_us());
}

static void edit_open(void)
{
    opftp_ui_edit_begin(&g, OPFTP_UI_EDIT_ROOT, g.snap.root);
}

/* Returns true when any key was consumed. */
static bool read_keys(void)
{
    bool edge = false;
    unsigned char buf[32];
    int len = 0;

    /* gather everything currently available */
    for (;;) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(0, &rf);
        struct timeval tv = { 0, 0 };
        if (select(1, &rf, 0, 0, &tv) <= 0) break;
        ssize_t n = read(0, buf + len, (size_t)(sizeof(buf) - len));
        if (n <= 0) break;
        len += (int)n;
        edge = true;
        if (len >= (int)sizeof(buf)) break;
    }
    if (!edge) return false;

    /* parse; escape sequences can still be arriving through a PTY, so
     * wait a short grace for the trailing bytes of a partial one */
    int i = 0;
    while (i < len) {
        if (g.edit.field != OPFTP_UI_EDIT_NONE) {
            /* text entry: all keys go to the field */
            if (buf[i] == 0x1b) {
                opftp_ui_edit_cancel(&g);
                if (i + 2 < len && buf[i + 1] == '[') i += 3;
                else i += 1;
                continue;
            }
            switch (buf[i]) {
            case '\n': case '\r':
                edit_apply();
                break;
            case 0x7f: case 0x08:
                opftp_ui_edit_backspace(&g);
                break;
            default:
                if (buf[i] >= 32 && buf[i] < 127) {
                    char c[2] = { (char)buf[i], 0 };
                    opftp_ui_edit_type(&g, c);
                }
                break;
            }
            i += 1;
            continue;
        }
        if (buf[i] == 0x1b) {
            while (i + 1 < len && i + 2 >= len) {
                fd_set rf;
                FD_ZERO(&rf);
                FD_SET(0, &rf);
                struct timeval tv = { 0, 20000 };
                if (select(1, &rf, 0, 0, &tv) <= 0) break;
                ssize_t n = read(0, buf + len, (size_t)(sizeof(buf) - len));
                if (n <= 0) break;
                len += (int)n;
            }
            if (i + 1 >= len) {         /* lone ESC */
                esc_key();
                i += 1;
                continue;
            }
            if (buf[i + 1] == '[' && i + 2 < len) {
                switch (buf[i + 2]) {
                case 'A': opftp_ui_sel_move(&g, -1); break;
                case 'B': opftp_ui_sel_move(&g, 1); break;
                case 'C': opftp_ui_switch_view(&g, 1); break;
                case 'D': opftp_ui_switch_view(&g, -1); break;
                default: break;
                }
                i += 3;
                continue;
            }
            esc_key();                  /* ESC + non-arrow: lone esc */
            i += 1;
            continue;
        }
        switch (buf[i]) {
        case 'q': case 0x03:
            g.quit = true;
            break;
        case '\n': case '\r':
            if (g.view == OPFTP_UI_V_SETTINGS && g.sel_settings == 1)
                edit_open();            /* Root path row */
            else
                opftp_ui_open_detail(&g);
            break;
        case '?': case 'h':
            help_toggle();
            break;
        default:
            break;
        }
        i += 1;
    }
    return edge;
}

/* ------------------------------------------------------------------ *
 * Render dispatch
 * ------------------------------------------------------------------ */
static void render(void)
{
    memset(cv, 0, (size_t)scr_rows * scr_cols * sizeof(cv[0]));
    draw_header();
    switch (g.view) {
    case OPFTP_UI_V_STATUS:    draw_status();    break;
    case OPFTP_UI_V_TRANSFERS: draw_transfers(); break;
    case OPFTP_UI_V_CLIENTS:   draw_clients();   break;
    case OPFTP_UI_V_SETTINGS:  draw_settings();  break;
    }
    draw_footer();
    draw_detail_or_help();
    if (g.edit.field != OPFTP_UI_EDIT_NONE)
        draw_edit_line();
}

/* ------------------------------------------------------------------ *
 * Entry point — TUI lifetime on the calling thread
 * ------------------------------------------------------------------ */
int opftp_tui_run(opftp_server_t* s, const char* version)
{
    get_size();
    cv = calloc((size_t)scr_rows * scr_cols, sizeof(cv[0]));
    if (!cv) return 1;
    last_render_us = 0;

    tcgetattr(0, &orig_term);
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);

    write(1, "\x1b[?25l", 6);
    write(1, "\x1b[2J", 4);

    g_srv = s;
    opftp_ui_init(&g, now_us());
    g.version = version;

    /* local IP for the ADDRESS stat (one shot; "—" if unavailable) */
    g.local_ip[0] = 0;
    {
        struct ifaddrs* ifa0 = 0;
        if (getifaddrs(&ifa0) == 0) {
            for (struct ifaddrs* a = ifa0; a; a = a->ifa_next) {
                if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
                struct sockaddr_in* sin = (struct sockaddr_in*)a->ifa_addr;
                if (sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;
                inet_ntop(AF_INET, &sin->sin_addr, g.local_ip, sizeof(g.local_ip));
                break;
            }
            freeifaddrs(ifa0);
        }
        if (!g.local_ip[0]) strncpy(g.local_ip, "\xE2\x80\x94", sizeof(g.local_ip) - 1);
    }

    while (!g.quit) {
        uint64_t f0 = now_us();

        bool any_edge = read_keys();
        opftp_server_snapshot(s, &g.snap);
        g.snap_ok = true;
        bool snap_changed = memcmp(&g.snap, &g.snap_prev, sizeof(g.snap)) != 0;
        if (snap_changed)
            g.snap_prev = g.snap;
        opftp_ui_poll(&g, now_us());

        uint64_t since_render = now_us() - last_render_us;
        bool dirty = any_edge || snap_changed || since_render >= 1000000ull;

        if (dirty) {
            render();
            frame_emit();
            last_render_us = now_us();

            uint64_t elapsed = now_us() - f0;
            if (elapsed < 33000ull)
                nap(33000ull - elapsed);
        } else {
            nap(100000ull);
        }
    }

    /* restore terminal */
    write(1, "\x1b[0m\x1b[?25h", 10);
    tcsetattr(0, TCSANOW, &orig_term);
    free(cv);
    free(fbuf);
    fbuf = 0;
    return 0;
}
