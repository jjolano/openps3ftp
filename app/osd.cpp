/*
 * OpenPS3FTP — PS3 on-screen display (OSD).
 *
 * NoRSX C++ implementation.  Faithful translation of the 1280x720 OSD
 * design mockup (see /tmp/opencode/osd_mockup.html): flat colors only,
 * no rounded corners (the only "radius" in the design is the Circle pad
 * glyph, drawn as a ring primitive), 8px spacing grid, 2px borders,
 * 32px overscan margin.
 *
 * Runs on the main thread while the FTP server runs on its reactor
 * thread; opftp_server_snapshot() is polled every frame (~30fps).
 * Everything is preallocated — no per-frame allocations.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>

#include <cell/pad.h>
#include <cell/sysutil.h>
#include <cell/sysutil_oskdialog.h>
#include <sys/memory.h>
#include <sys/systime.h>
#include <sys/timer.h>

#include <NoRSX.h>

#include "osd.h"
#include "ui.h"

/*
 * Local netctl declarations only: <net/netctl.h> pulls <net/net.h> ->
 * <net/socket.h>, whose struct sockaddr clashes with the ps3dk
 * <sys/socket.h> pulled in by openps3ftp.h (known toolchain wart).
 * The stub only marshals the pointer; the SPRX writes the IP string
 * into ip_address at offset 0 (NET_CTL_IPV4_ADDR_STR_LEN = 16).
 */
#define NET_CTL_INFO_IP_ADDRESS (16)
extern "C" int netCtlGetInfo(int code, void* info);
typedef union {
    char ip_address[16];
} osd_net_ctl_info_t;

/* ------------------------------------------------------------------ *
 * Palette (mockup style guide — flat fills only)
 * ------------------------------------------------------------------ */
enum {
    C_BG      = 0x0B0F16, /* screen background                      */
    C_PANEL   = 0x121826, /* card background                        */
    C_INSET   = 0x0D1220, /* progress track / scrollbar track       */
    C_LINE    = 0x2A3446, /* borders, rules                         */
    C_TEXT    = 0xE7EBF2, /* primary text                           */
    C_MUTED   = 0x8A94A8, /* secondary text                         */
    C_ACC     = 0x33D17A, /* accent / active / OK / LISTENING       */
    C_BLUE    = 0x4FA3E3, /* RETR (server -> client)                */
    C_WARN    = 0xE5A13D, /* warnings, ABORTED                      */
    C_ERR     = 0xE05252, /* errors, STOPPED                        */
    C_ERRBG   = 0x211117, /* flat error strip tint                  */
    C_WARNBG  = 0x1F1710, /* flat warning strip tint                */
    C_FOCUS   = 0x141C2B, /* gamepad focus panel tint               */
    C_EMPTY   = 0xAAB3C5, /* empty-state primary text               */
    C_EMSG    = 0xE8B9B7, /* error row message text                 */
    C_EMSW    = 0xD8C39A, /* warning row message text               */
};

/* Layout: 1280x720 screen, 32px overscan margin, 8px spacing grid. */
enum {
    SCREEN_W = 1280,
    SCREEN_H = 720,
    MX       = 32,   /* horizontal overscan margin                 */
    MY       = 28,   /* vertical overscan margin                   */
    W        = SCREEN_W - 2 * MX,   /* 1216 content width          */

    HDR_TEXT_TOP = 36,              /* brand/clock text top        */
    HDR_BOTTOM   = 84,              /* header border-bottom        */
    FTR_TOP      = 652,             /* footer border-top           */

    CARD_H       = 144,             /* status card height          */
    XFER_CARD_H  = 90,              /* active transfer card height */
    XFER_CARD_GAP = 10,
    SLAB_H       = 30,
    CHIP_H       = 50,
    ER_ROW_H     = 44,
    ER_GAP       = 8,
    HIST_ROW_H   = 56,
    CLI_ROW_H    = 64,
    THEAD_H      = 34,
    SET_ROW_H    = 58,
};

/* Estimated glyph advance for width math (VR font, no metrics API).
 * 0.66 * size per char is a middle ground for mixed-case Latin. */
#define CHAR_W(size) ((int)((size) * 0.66))

/* ------------------------------------------------------------------ *
 * App state
 * ------------------------------------------------------------------ */
static opftp_ui_t g;
static opftp_server_t* g_srv;       /* server handle for applying edits */

/* Renderer-only state (graphics handles, pad edge history, hold/quit
 * timing, frame pacing) — the shared model lives in ui.c / ui.h. */
static NoRSX* gfx;
static Font* font;
static Object* obj;
static uint16_t prev_d1, prev_d2;
static uint64_t start_hold_us;      /* 0 = START not held */
static uint64_t last_frame_us;
static uint64_t last_render_us;     /* 0 = no frame rendered yet */

/* ------------------------------------------------------------------ *
 * On-screen keyboard (system OSK via sysutil) for the model's text
 * fields.  The dialog runs on the TV; its events arrive through the
 * sysutil callback queue, which the OSD loop polls every frame.
 * ------------------------------------------------------------------ */
static sys_memory_container_t osk_container;
static uint16_t osk_init_text[OPFTP_UI_EDIT_MAX + 1];   /* UTF-16 */
static uint16_t osk_message[OPFTP_UI_EDIT_MAX + 1];     /* UTF-16 */
static bool osk_finished;                               /* FINISHED seen */
static bool osk_canceled;                               /* CANCELED seen */

static uint64_t now_us(void);   /* defined below with the model helpers */

static void osk_cb(uint64_t status, uint64_t param, void* userdata)
{
    (void) param; (void) userdata;
    switch (status) {
    case CELL_SYSUTIL_OSKDIALOG_FINISHED:       osk_finished = true; break;
    case CELL_SYSUTIL_OSKDIALOG_INPUT_CANCELED: osk_canceled = true; break;
    default: break;
    }
}

static int utf8_to_u16(const char* in, uint16_t* out, int max)
{
    int n = 0;
    const unsigned char* p = (const unsigned char*) in;
    while (*p && n < max) {
        uint32_t cp;
        int l;
        if (*p < 0x80)             { cp = *p; l = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; l = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; l = 3; }
        else                       { cp = *p & 0x07; l = 4; }
        for (int i = 1; i < l; i++) cp = (cp << 6) | (p[i] & 0x3F);
        out[n++] = (uint16_t)(cp > 0xFFFF ? 0xFFFD : cp);   /* BMP only */
        p += l;
    }
    out[n] = 0;
    return n;
}

static int u16_to_utf8(uint16_t c, char* out)
{
    if (c < 0x80)          { out[0] = (char) c; return 1; }
    if (c < 0x800)         { out[0] = 0xC0 | (c >> 6); out[1] = 0x80 | (c & 0x3F); return 2; }
    out[0] = 0xE0 | (c >> 12); out[1] = 0x80 | ((c >> 6) & 0x3F);
    out[2] = 0x80 | (c & 0x3F); return 3;
}

static void osk_open(const char* initial)
{
    utf8_to_u16(initial, osk_init_text, OPFTP_UI_EDIT_MAX);
    osk_finished = osk_canceled = false;

    CellOskDialogParam param;
    memset(&param, 0, sizeof(param));
    param.allowOskPanelFlg = CELL_OSKDIALOG_PANELMODE_DEFAULT
                           | CELL_OSKDIALOG_PANELMODE_ENGLISH
                           | CELL_OSKDIALOG_PANELMODE_NUMERAL;
    param.firstViewPanel = CELL_OSKDIALOG_PANELMODE_DEFAULT;
    param.controlPoint.x = 0.0f;
    param.controlPoint.y = 0.0f;

    CellOskDialogInputFieldInfo info;
    memset(&info, 0, sizeof(info));
    info.init_text = osk_init_text;
    info.message = osk_message;
    info.limit_length = OPFTP_UI_EDIT_MAX - 1;

    cellOskDialogSetInitialKeyLayout(CELL_OSKDIALOG_INITIAL_PANEL_LAYOUT_FULLKEY);
    cellOskDialogLoadAsync(osk_container, &param, &info);
}

/* Polled every frame while an edit is active: harvest the OSK result
 * into the model and apply the field change. */
static void osk_process(void)
{
    if (g.edit.field == OPFTP_UI_EDIT_NONE)
        return;
    if (!osk_finished && !osk_canceled)
        return;

    CellOskDialogCallbackReturnParam ret;
    memset(&ret, 0, sizeof(ret));
    cellOskDialogUnloadAsync(&ret);

    if (ret.result == CELL_OSKDIALOG_INPUT_FIELD_RESULT_OK &&
        ret.numCharsResultString > 0) {
        /* the OSK replaces the field text wholesale */
        opftp_ui_edit_begin(&g, g.edit.field, NULL);
        for (int i = 0; i < ret.numCharsResultString; i++) {
            char utf8[4];
            int n = u16_to_utf8(ret.pResultString[i], utf8);
            if (n > 0) { utf8[n] = 0; opftp_ui_edit_type(&g, utf8); }
        }
        const char* v = opftp_ui_edit_commit(&g);
        int rc = opftp_server_set_root_runtime(g_srv, v);
        if (rc != 0)
            opftp_ui_event(&g,
                "ROOT NOT CHANGED \xE2\x80\x94 path must be absolute",
                true, now_us());
    } else {
        opftp_ui_edit_cancel(&g);
    }
    osk_finished = osk_canceled = false;
}

/* ------------------------------------------------------------------ *
 * Small drawing helpers (flat rects/lines only)
 * ------------------------------------------------------------------ */
static void fill(int x, int y, int w, int h, u32 c)
{
    if (w <= 0 || h <= 0) return;
    obj->Rectangle(x, y, w, h, c);
}

static void hline(int x, int y, int w, u32 c) { fill(x, y, w, 2, c); }
static void vline(int x, int y, int h, u32 c) { fill(x, y, 2, h, c); }

/* 2px bordered box: fill outer with `c`, inner with `inner`. */
static void box(int x, int y, int w, int h, u32 c, u32 inner)
{
    fill(x, y, w, h, c);
    fill(x + 2, y + 2, w - 4, h - 4, inner);
}

/* Thick (3px) line for the X pad glyph. */
static void thick_line(int x0, int y0, int x1, int y1, u32 c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        fill(x, y, 3, 3, c);
    }
}

/* Filled triangle (pad glyph / direction arrows). */
static void tri_fill(int x0, int y0, int x1, int y1, int x2, int y2, u32 c)
{
    /* sort by y */
    if (y0 > y1) { int t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 > y2) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y0 > y1) { int t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y0 == y2) return;
    for (int y = y0; y <= y2; y++) {
        /* x on edge 0-2 (long edge) */
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1)
            xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else if (y1 == y2)
            xb = x1;
        else
            xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        fill(xa, y, xb - xa + 1, 1, c);
    }
}

/* 24x24 pad glyphs (footer / help overlay). */
static void glyph_tri(int x, int y, u32 c)
{
    /* outer triangle (22 wide x 20 tall), then bg-colored hole */
    tri_fill(x + 12, y + 1, x + 1, y + 21, x + 23, y + 21, c);
    tri_fill(x + 12, y + 7, x + 6, y + 18, x + 18, y + 18, C_BG);
}

static void glyph_x(int x, int y, u32 c)
{
    thick_line(x + 2, y + 2, x + 21, y + 21, c);
    thick_line(x + 2, y + 21, x + 21, y + 2, c);
}

static void glyph_circle(int x, int y, u32 c)
{
    /* ring: outer filled circle + bg-colored inner circle */
    obj->Circle(x + 12, y + 12, 12, c);
    obj->Circle(x + 12, y + 12, 9, C_BG);
}

static void glyph_start(int x, int y, const char* label)
{
    int w = CHAR_W(16) * (int)strlen(label) + 24;
    box(x, y, w, 26, C_TEXT, C_BG);
    font->Printf(x + (w - CHAR_W(16) * (int)strlen(label)) / 2, y + 3,
                 C_TEXT, 16, "%s", label);
}

/* Direction arrow. up: RETR/LIST (blue), down: STOR/APPE/COPY (acc). */
static void arrow(int x, int y, bool up, int size, u32 c)
{
    if (size == 22) {                       /* transfer cards */
        if (up) {
            tri_fill(x + 11, y, x, y + 12, x + 22, y + 12, c);
            fill(x + 8, y + 12, 6, 15, c);
        } else {
            fill(x + 8, y, 6, 15, c);
            tri_fill(x + 11, y + 27, x, y + 15, x + 22, y + 15, c);
        }
    } else {                                /* 16px history rows */
        if (up) {
            tri_fill(x + 8, y, x, y + 9, x + 16, y + 9, c);
            fill(x + 6, y + 9, 4, 10, c);
        } else {
            fill(x + 6, y, 4, 10, c);
            tri_fill(x + 8, y + 19, x, y + 10, x + 16, y + 10, c);
        }
    }
}

/* ------------------------------------------------------------------ *
 * Text helpers (font draws directly into the current buffer)
 * ------------------------------------------------------------------ */
static int tw(int size, const char* s)
{
    return CHAR_W(size) * (int)strlen(s);
}

static void text(int x, int y, u32 c, int size, const char* fmt, ...)
{
    char buf[384];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    font->Printf(x, y, c, size, "%s", buf);
}

/* Right-aligned text; returns left edge. */
static void text_r(int xr, int y, u32 c, int size, const char* fmt, ...)
{
    char buf[384];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    font->Printf(xr - tw(size, buf), y, c, size, "%s", buf);
}

/* Center text on cx (the horizontal center). */
static void text_c(int cx, int y, u32 c, int size, const char* fmt, ...)
{
    char buf[384];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    font->Printf(cx - tw(size, buf) / 2, y, c, size, "%s", buf);
}

/* Truncate with "…" (CSS ellipsis -> manual end-cut) into `out`. */
static void truncate(char* out, size_t outsz, const char* in, int w, int size)
{
    int maxchars = w / CHAR_W(size);
    if (maxchars < 1) maxchars = 1;
    if (maxchars > (int)outsz - 4) maxchars = (int)outsz - 4;
    size_t len = strlen(in);
    if ((int)len <= maxchars) {
        strncpy(out, in, outsz - 1);
        out[outsz - 1] = 0;
        return;
    }
    memcpy(out, in, maxchars);
    out[maxchars] = '\xE2';                 /* "…" (U+2026) */
    out[maxchars + 1] = '\x80';
    out[maxchars + 2] = '\xA6';
    out[maxchars + 3] = 0;
}

static uint64_t now_us(void)
{
    uint64_t sec = 0, nsec = 0;
    sysGetCurrentTime(&sec, &nsec);
    return sec * 1000000ull + nsec / 1000ull;
}

/* ------------------------------------------------------------------ *
 * Section label: accent bar + caps text + optional boxed count.
 * `right` text is right-aligned on the content row.
 * ------------------------------------------------------------------ */
static void slab(int y, const char* label, const char* cnt, bool cnt_muted,
                 const char* right)
{
    fill(MX, y + 4, 6, 22, C_ACC);
    text(MX + 18, y + 4, C_MUTED, 22, "%s", label);
    int x = MX + 18 + tw(22, label) + 14;
    if (cnt) {
        int w = tw(20, cnt) + 24;
        box(x, y + 2, w, 26, C_LINE, C_BG);
        text_c(x + w / 2, y + 5, cnt_muted ? C_MUTED : C_TEXT, 20, "%s", cnt);
        x += w + 14;
    }
    if (right)
        text_r(MX + W, y + 4, C_MUTED, 22, "%s", right);
}

/* ------------------------------------------------------------------ *
 * Header: brand + version, view tabs, clock
 * ------------------------------------------------------------------ */
static void draw_header(const char* version)
{
    /* brand mark + word */
    fill(MX, 46, 16, 16, C_ACC);
    text(MX + 30, HDR_TEXT_TOP, C_TEXT, 36, "OpenPS3FTP");
    int bx = MX + 30 + tw(36, "OpenPS3FTP") + 14;
    int bw = tw(20, version) + 24;
    box(bx, 41, bw, 28, C_LINE, C_BG);
    text_c(bx + bw / 2, 45, C_MUTED, 20, "%s", version);

    /* clock (UTC — no TZ API in ps3dk; see report) */
    uint64_t sec = now_us() / 1000000ull;
    char clock[8];
    snprintf(clock, sizeof(clock), "%02" PRIu64 ":%02" PRIu64,
             (sec / 3600) % 24, (sec / 60) % 60);
    text_r(MX + W, HDR_TEXT_TOP, C_TEXT, 34, "%s", clock);

    /* view tabs, right-aligned before the clock */
    int x = MX + W - tw(34, clock) - 28;
    for (int i = OPFTP_UI_V_COUNT - 1; i >= 0; i--) {
        const char* n = opftp_ui_view_name(i);
        int w = tw(23, n) + 28;
        x -= w;
        if (g.view == i) {
            box(x, 43, w, 36, C_ACC, C_BG);
            text_c(x + w / 2, 49, C_ACC, 23, "%s", n);
        } else {
            text(x, 49, C_MUTED, 23, "%s", n);
        }
        x -= 10;
    }

    hline(MX, HDR_BOTTOM, W, C_LINE);
}

/* ------------------------------------------------------------------ *
 * Footer: pad hints + view indicator
 * ------------------------------------------------------------------ */
static void draw_footer(void)
{
    hline(MX, FTR_TOP, W, C_LINE);
    int y = FTR_TOP + 12;
    int x = MX;

    glyph_tri(x, y, C_TEXT);      text(x + 30, y + 2, C_MUTED, 24, "HELP");      x += 30 + tw(24, "HELP") + 36;
    glyph_x(x, y, C_TEXT);        text(x + 30, y + 2, C_MUTED, 24, "SELECT");    x += 30 + tw(24, "SELECT") + 36;
    glyph_circle(x, y, C_TEXT);   text(x + 30, y + 2, C_MUTED, 24, "BACK");      x += 30 + tw(24, "BACK") + 36;
    glyph_start(x, y, "START"); text(x + 74 + 14, y + 2, C_MUTED, 24, "QUIT");

    /* view indicator right-aligned: "STATUS — VIEW 1/4" */
    char right[40];
    snprintf(right, sizeof(right), " — VIEW %d/%d", g.view + 1, OPFTP_UI_V_COUNT);
    int rw = tw(22, opftp_ui_view_name(g.view)) + tw(22, right);
    text(MX + W - rw, y + 2, C_TEXT, 22, "%s", opftp_ui_view_name(g.view));
    text(MX + W - rw + tw(22, opftp_ui_view_name(g.view)), y + 2, C_MUTED, 22, "%s", right);
}

/* ------------------------------------------------------------------ *
 * Event strip (error/warning rows) — 0, 1 or 2 rows above the card.
 * Returns the height consumed (0 when no events).
 * ------------------------------------------------------------------ */
static int draw_event_strip(int y)
{
    int n = g.ev_count < 2 ? g.ev_count : 2;
    if (n <= 0) return 0;
    int yy = y;
    for (int i = 0; i < n; i++) {
        const opftp_ui_ev_t* e = &g.events[(g.ev_head - i + OPFTP_UI_MAX_EVENTS) % OPFTP_UI_MAX_EVENTS];
        u32 col = e->warn ? C_WARN : C_ERR;
        u32 tint = e->warn ? C_WARNBG : C_ERRBG;
        box(MX, yy, W, ER_ROW_H, col, tint);

        /* "!" icon box */
        int icx = MX + 14;
        box(icx, yy + (ER_ROW_H - 26) / 2, 26, 26, col, tint);
        text_c(icx + 13, yy + (ER_ROW_H - 26) / 2 + 3, col, 20, "!");

        /* message, truncated */
        char msg[96];
        truncate(msg, sizeof(msg), e->text, W - 28 - 26 - 14 - 110, 23);
        text(MX + 14 + 26 + 14, yy + (ER_ROW_H - 23) / 2,
             e->warn ? C_EMSW : C_EMSG, 23, "%s", msg);

        /* "N RECENT" badge on the newest row when 2+ events */
        if (i == 0 && g.ev_count >= 2) {
            char b[24];
            snprintf(b, sizeof(b), "%d RECENT", g.ev_count);
            int bw = tw(20, b) + 24;
            box(MX + W - 14 - bw, yy + (ER_ROW_H - 26) / 2, bw, 26, col, tint);
            text_c(MX + W - 14 - bw / 2, yy + (ER_ROW_H - 26) / 2 + 3, col, 20, "%s", b);
        }
        yy += ER_ROW_H + ER_GAP;
    }
    return yy - y - ER_GAP + 12;
}

/* ------------------------------------------------------------------ *
 * Status card: LED + state word, stat columns, uptime strip
 * ------------------------------------------------------------------ */
static void draw_status_card(int y)
{
    const opftp_snapshot_t* s = &g.snap;
    bool listening = g.snap_ok && s->started;
    u32 state_col = listening ? C_ACC : C_ERR;

    box(MX, y, W, CARD_H, C_LINE, C_PANEL);

    /* LED + state word */
    fill(MX + 24, y + 36, 20, 20, state_col);
    text(MX + 24 + 20 + 18, y + 18, state_col, 56, "%s",
         listening ? "LISTENING" : "STOPPED");

    /* stat columns (right-aligned block, 2px rules between) */
    const char* vals[4] = { 0 };
    char portb[8], addr[16], rootb[40], workers[8];
    snprintf(portb, sizeof(portb), "%u", (unsigned)s->port);
    snprintf(addr, sizeof(addr), "%s", g.local_ip[0] ? g.local_ip : "\xE2\x80\x94");
    snprintf(rootb, sizeof(rootb), "%s", s->root);
    snprintf(workers, sizeof(workers), "%d", s->workers);
    vals[0] = portb; vals[1] = addr; vals[2] = rootb; vals[3] = workers;
    const char* labels[4] = { "PORT", "ADDRESS", "ROOT", "WORKERS" };
    const int colw[4] = { 110, 350, 220, 120 };

    int xr = MX + W - 24;                 /* right edge of stats block */
    for (int i = 3; i >= 0; i--) {
        char v[40];
        truncate(v, sizeof(v), vals[i], colw[i], 38);
        text_r(xr, y + 18, C_TEXT, 38, "%s", v);
        text_r(xr, y + 18 + 38 + 6, C_MUTED, 20, "%s", labels[i]);
        xr -= colw[i];
        if (i > 0) vline(xr, y + 18, 76, C_LINE);   /* 2px rule */
    }

    /* uptime strip */
    hline(MX + 24, y + 90, W - 48, C_LINE);
    uint64_t up = (now_us() - g.t0_us) / 1000000ull;
    char dur[16], gb[16];
    opftp_ui_fmt_dur(dur, sizeof(dur), up);
    opftp_ui_fmt_size(gb, sizeof(gb), g.session_bytes);
    text(MX + 24, y + 104, C_MUTED, 22,
         "UPTIME %s \xC2\xB7 %" PRIu64 " TRANSFERS \xC2\xB7 %s THIS SESSION",
         dur, g.session_xfers, gb);
}

/* ------------------------------------------------------------------ *
 * Progress bar: inset track, fill, 2px cut ticks at 25/50/75%.
 * ------------------------------------------------------------------ */
static void progress_bar(int x, int y, int w, uint64_t bytes, uint64_t total, bool up)
{
    box(x, y, w, 22, C_LINE, C_INSET);
    int iw = w - 4;
    int pct = (total && bytes < total) ? (int)(bytes * 100 / total) : 100;
    if (pct > 100) pct = 100;
    if (pct > 0)
        fill(x + 2, y + 2, iw * pct / 100, 18, up ? C_BLUE : C_ACC);
    for (int t = 1; t <= 3; t++)            /* cut ticks in bg color */
        fill(x + 2 + iw * t / 4 - 1, y + 2, 2, 18, C_BG);
}

/* ------------------------------------------------------------------ *
 * Active transfer card (STATUS view). Returns next y.
 * ------------------------------------------------------------------ */
static void draw_xfer_card(int y, int idx, bool focused)
{
    const opftp_snapshot_client_t* c = &g.snap.clients[idx];
    bool up = !strcmp(c->xfer_op, "RETR") || !strcmp(c->xfer_op, "LIST");
    u32 arrow_col = up ? C_BLUE : C_ACC;

    box(MX, y, W, XFER_CARD_H, focused ? C_ACC : C_LINE,
        focused ? C_FOCUS : C_PANEL);

    int cx = MX + 18;                       /* content x */

    arrow(cx, y + 16, up, 22, arrow_col);   cx += 22 + 14;
    text(cx, y + 18, C_MUTED, 20, "%s", c->xfer_op);   cx += tw(20, c->xfer_op) + 14;

    /* name (truncate to ~62 chars at 26px), pct right */
    char pctb[8];
    if (c->xfer_total)
        snprintf(pctb, sizeof(pctb), "%d%%", (int)(c->xfer_bytes * 100 / c->xfer_total));
    else
        snprintf(pctb, sizeof(pctb), "--%%");
    char name[128];
    truncate(name, sizeof(name), c->xfer_path,
             W - 36 - 22 - 14 - tw(20, c->xfer_op) - 14 - tw(32, pctb) - 14, 26);
    text(cx, y + 16, C_TEXT, 26, "%s", name);
    text_r(MX + W - 18, y + 17, C_TEXT, 32, "%s", pctb);

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
    int mw = tw(23, meta);
    int barw = W - 36 - mw - 18;
    progress_bar(cx, y + 52, barw, c->xfer_bytes, c->xfer_total, up);
    text_r(MX + W - 18, y + 54, C_MUTED, 23, "%s", meta);
}

/* ------------------------------------------------------------------ *
 * STATUS view
 * ------------------------------------------------------------------ */
static void draw_status(void)
{
    int y = HDR_BOTTOM + 12;

    y += draw_event_strip(y);

    draw_status_card(y);
    y += CARD_H + 12;

    /* active transfers slab + count */
    int nxfer = 0;
    for (int i = 0; i < g.snap.num_clients; i++)
        if (g.snap.clients[i].xfer_active) nxfer++;
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d", nxfer);
    slab(y, "ACTIVE TRANSFERS", nxfer ? cnt : 0, false, 0);
    y += SLAB_H + 12;

    /* transfer cards (fixed 90px rows; up to what fits — mockup shows 2) */
    int area_bottom = FTR_TOP - 12 - CHIP_H - 12 - SLAB_H - 12;
    int area_h = area_bottom - y;
    int nfit = (area_h + XFER_CARD_GAP) / (XFER_CARD_H + XFER_CARD_GAP);
    int shown = 0;
    for (int i = 0; i < g.snap.num_clients && shown < nfit; i++) {
        if (!g.snap.clients[i].xfer_active) continue;
        draw_xfer_card(y + shown * (XFER_CARD_H + XFER_CARD_GAP), i,
                       g.sel_status == shown);
        shown++;
    }
    if (nxfer == 0) {
        /* empty state fills the remaining area */
        int eh = area_h;
        if (eh >= 90) {
            box(MX, y, W, eh, C_LINE, C_BG);
            text_c(MX + W / 2, y + eh / 2 - 26, C_EMPTY, 28, "No active transfers");
            text_c(MX + W / 2, y + eh / 2 + 10, C_MUTED, 22,
                   "Transfers appear here while clients send or receive files");
        }
        y += eh;
    } else if (shown > 0) {
        y += shown * (XFER_CARD_H + XFER_CARD_GAP) - XFER_CARD_GAP;
    }

    /* connected clients slab + chips */
    snprintf(cnt, sizeof(cnt), "%d", g.snap.num_clients);
    slab(y, "CONNECTED CLIENTS", g.snap.num_clients ? cnt : 0, false, 0);
    y += SLAB_H + 12;

    /* chip per client; cap at 4 + "+N" (full list on CLIENTS view) */
    char chipt[96];
    int x = MX;
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
        truncate(peerb, sizeof(peerb), c->peer, 170, 25);
        int cw = tw(25, peerb) + 12 + tw(21, chipt) + 32 + 4;
        if (x + cw + 12 > MX + W) break;    /* no room: leave to "+N" */
        box(x, y, cw, CHIP_H, C_LINE, C_PANEL);
        fill(x + 16, y + 20, 10, 10, C_ACC);
        text(x + 16 + 10 + 12, y + 13, C_TEXT, 25, "%s", peerb);
        text(x + 16 + 10 + 12 + tw(25, peerb) + 12, y + 15, C_MUTED, 21, "%s", chipt);
        x += cw + 12;
        drawn++;
    }
    if (g.snap.num_clients > drawn) {
        char more[16];
        snprintf(more, sizeof(more), "+%d", g.snap.num_clients - drawn);
        int cw = tw(21, more) + 32 + 4;
        box(x, y, cw, CHIP_H, C_LINE, C_PANEL);
        text_c(x + cw / 2, y + 13, C_MUTED, 21, "%s", more);
    }
}

/* ------------------------------------------------------------------ *
 * Table primitives (transfers history / clients)
 * ------------------------------------------------------------------ */
struct Col { int w; const char* label; bool right; };

static int table_begin(int* y, const struct Col* cols, int ncol)
{
    *y = HDR_BOTTOM + 12 + SLAB_H + 12;
    text(MX + 10, *y, C_MUTED, 20, "%s", cols[0].label);
    for (int i = 1; i < ncol; i++) {
        int x = MX + 10;
        for (int j = 0; j < i; j++) x += cols[j].w + 14;
        if (cols[i].right)
            text_r(x + cols[i].w, *y, C_MUTED, 20, "%s", cols[i].label);
        else
            text(x, *y, C_MUTED, 20, "%s", cols[i].label);
    }
    hline(MX, *y + THEAD_H, W, C_LINE);
    *y += THEAD_H;
    return FTR_TOP - 12 - *y;               /* rows area height */
}

/* Scrollbar: plain rect thumb, 32% of track, offset proportional. */
static void draw_scrollbar(int y, int h, int scroll, int visible, int total)
{
    int x = MX + W - 10;
    box(x, y, 10, h, C_LINE, C_INSET);
    if (total <= visible) return;
    int th = h * 32 / 100;
    int range = total - visible;
    int ty = y + (h - th) * scroll / range;
    fill(x + 2, ty, 6, th, C_MUTED);
}

/* ------------------------------------------------------------------ *
 * TRANSFERS view — history table (newest first), focus + scrollbar
 * ------------------------------------------------------------------ */
static void draw_transfers(void)
{
    static const struct Col cols[] = {
        { 88, "TIME", false }, { 30, "", false }, { 56, "DIR", false },
        { 0, "FILE", false },  { 140, "SIZE", true }, { 118, "RESULT", true },
    };
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d", g.hist_count);
    char showing[24] = { 0 };
    if (g.hist_count > 0)
        snprintf(showing, sizeof(showing), "SHOWING %d\xE2\x80\x93%d",
                 g.scroll_hist + 1,
                 g.scroll_hist + 8 < g.hist_count ? g.scroll_hist + 8 : g.hist_count);
    slab(HDR_BOTTOM + 12, "TRANSFER HISTORY", g.hist_count ? cnt : 0, false,
         g.hist_count ? showing : 0);

    int y, rows_h = table_begin(&y, cols, 6);
    int visible = rows_h / HIST_ROW_H;
    if (visible < 1) visible = 1;
    int total = g.hist_count;
    g.scroll_hist = opftp_ui_clamp_scroll(g.sel_hist, g.scroll_hist, visible, total);

    /* row backgrounds + borders (two passes keep the 2px rules clean) */
    for (int r = 0; r < visible; r++) {
        int ry = y + r * HIST_ROW_H;
        if (g.scroll_hist + r >= total) break;
        if (g.sel_hist == g.scroll_hist + r)
            fill(MX, ry, W, HIST_ROW_H, C_FOCUS);
        hline(MX, ry + HIST_ROW_H - 2, W, C_LINE);
    }
    for (int r = 0; r < visible; r++) {
        int ry = y + r * HIST_ROW_H;
        int idx = (g.hist_head - (g.scroll_hist + r) + OPFTP_UI_MAX_HIST) % OPFTP_UI_MAX_HIST;
        if (g.scroll_hist + r >= total) break;
        const opftp_ui_hist_t* h = &g.hist[idx];
        bool up = !strcmp(h->op, "RETR") || !strcmp(h->op, "LIST");
        u32 ac = up ? C_BLUE : C_ACC;

        if (g.sel_hist == g.scroll_hist + r)     /* focus outline + tint */
            box(MX, ry, W, HIST_ROW_H, C_ACC, C_FOCUS);

        int x = MX + 10;
        char t[8];
        snprintf(t, sizeof(t), "%02d:%02d", h->hh, h->mm);
        text(x, ry + 16, C_MUTED, 24, "%s", t);   x += 88 + 14;
        arrow(x + 7, ry + 18, up, 16, ac);        x += 30 + 14;
        text(x, ry + 16, C_MUTED, 20, "%s", h->op); x += 56 + 14;

        char f[128];
        truncate(f, sizeof(f), h->path, W - 20 - 88 - 30 - 56 - 140 - 118 - 14 * 5, 24);
        text(x, ry + 16, C_TEXT, 24, "%s", f);    x += W - 20 - 88 - 30 - 56 - 140 - 118 - 14 * 5;

        char sz[16];
        opftp_ui_fmt_size(sz, sizeof(sz), h->bytes);
        text_r(x + 140, ry + 16, C_TEXT, 24, "%s", sz);
        x += 140 + 14;

        const char* rs = h->result == OPFTP_UI_H_OK ? "OK" : h->result == OPFTP_UI_H_ABORTED ? "ABORTED" : "ERROR";
        u32 rc = h->result == OPFTP_UI_H_OK ? C_ACC : h->result == OPFTP_UI_H_ABORTED ? C_WARN : C_ERR;
        int bw = tw(20, rs) + 24;
        box(x + (118 - bw) / 2, ry + 15, bw, 26, rc, C_BG);
        text_c(x + 59, ry + 18, rc, 20, "%s", rs);
    }

    draw_scrollbar(y, rows_h, g.scroll_hist, visible, total);
}

/* ------------------------------------------------------------------ *
 * CLIENTS view — connected client table
 * ------------------------------------------------------------------ */
static void draw_clients(void)
{
    static const struct Col cols[] = {
        { 230, "IP ADDRESS", false }, { 150, "USER", false },
        { 0, "CURRENT DIRECTORY", false }, { 120, "IDLE", true },
    };
    char cnt[8], slots[24];
    snprintf(cnt, sizeof(cnt), "%d", g.snap.num_clients);
    snprintf(slots, sizeof(slots), "%d OF %d SLOTS IN USE",
             g.snap.num_clients, OPFTP_SNAPSHOT_MAX_CLIENTS);
    slab(HDR_BOTTOM + 12, "CLIENTS", g.snap.num_clients ? cnt : 0, false, slots);

    int y, rows_h = table_begin(&y, cols, 4);
    int visible = rows_h / CLI_ROW_H;
    if (visible < 1) visible = 1;
    int total = g.snap.num_clients;
    g.scroll_cli = opftp_ui_clamp_scroll(g.sel_cli, g.scroll_cli, visible, total);

    for (int r = 0; r < visible; r++) {
        int ry = y + r * CLI_ROW_H;
        if (g.scroll_cli + r >= total) break;
        if (g.sel_cli == g.scroll_cli + r)
            fill(MX, ry, W, CLI_ROW_H, C_FOCUS);
        hline(MX, ry + CLI_ROW_H - 2, W, C_LINE);
    }
    for (int r = 0; r < visible; r++) {
        int ry = y + r * CLI_ROW_H;
        int ci = g.scroll_cli + r;
        if (ci >= total) break;
        const opftp_snapshot_client_t* c = &g.snap.clients[ci];

        if (g.sel_cli == ci)                     /* focus outline + tint */
            box(MX, ry, W, CLI_ROW_H, C_ACC, C_FOCUS);

        int x = MX + 10;
        fill(x, ry + 27, 10, 10, c->xfer_active ? C_ACC : C_MUTED);
        x += 10 + 14;
        char ip[24];
        truncate(ip, sizeof(ip), c->peer, 230 - 10 - 24, 26);
        text(x, ry + 19, C_TEXT, 26, "%s", ip);   x += 230 - 10 - 24 + 14;

        char usr[20];
        truncate(usr, sizeof(usr), c->user[0] ? c->user : "?", 150, 26);
        text(x, ry + 19, C_TEXT, 26, "%s", usr);  x += 150 + 14;

        char cwd[96];
        truncate(cwd, sizeof(cwd), c->cwd, W - 20 - 230 - 150 - 120 - 14 * 3, 26);
        text(x, ry + 19, C_TEXT, 26, "%s", cwd);  x += W - 20 - 230 - 150 - 120 - 14 * 3;

        char idle[8];
        uint64_t idl = 0;
        for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++) {
            if (!g.trk[i].present || strcmp(g.trk[i].peer, c->peer)) continue;
            /* busy: time since the transfer started; idle: since the
             * last command (connect/login/xfer start/end) */
            idl = ((now_us() - (c->xfer_active ? g.trk[i].xfer_t0_us
                                               : g.trk[i].idle_reset_us)) / 1000000ull);
            break;
        }
        opftp_ui_fmt_idle(idle, sizeof(idle), idl);
        text_r(x + 120, ry + 19, C_TEXT, 26, "%s", idle);
    }

    draw_scrollbar(y, rows_h, g.scroll_cli, visible, total);

    /* note under the table */
    text(MX, y + rows_h + 8, C_MUTED, 22,
         "Square marker: client has a transfer in progress. "
         "Idle time is mm:ss since the last command.");
}

/* ------------------------------------------------------------------ *
 * SETTINGS view — read-only key/value list
 * ------------------------------------------------------------------ */
static void draw_settings(void)
{
    int y0 = HDR_BOTTOM + 12;
    y0 += draw_event_strip(y0);                 /* apply errors visible here */
    slab(y0, "SETTINGS", 0, false, 0);
    int y = y0 + SLAB_H + 12;

    const char* keys[5] = { "Port", "Root path", "Worker threads", "TLS", "Require TLS" };
    char vals[5][24];
    snprintf(vals[0], sizeof(vals[0]), "%u", (unsigned)g.snap.port);
    snprintf(vals[1], sizeof(vals[1]), "%s", g.snap.root);
    snprintf(vals[2], sizeof(vals[2]), "%d", g.snap.workers);
    snprintf(vals[3], sizeof(vals[3]), "%s", "OFF");   /* no TLS configured */
    snprintf(vals[4], sizeof(vals[4]), "%s", "OFF");
    bool sw[5] = { false, false, false, false, false };

    box(MX, y, W, SET_ROW_H * 5 + 2, C_LINE, C_PANEL);
    for (int i = 0; i < 5; i++) {
        int ry = y + i * SET_ROW_H;
        bool focused = g.sel_settings == i;
        if (focused) {                          /* focus outline + tint */
            box(MX, ry, W, SET_ROW_H, C_ACC, C_FOCUS);
            text(MX + 24, ry + 16, C_ACC, 28, "%s", keys[i]);
        } else {
            text(MX + 24, ry + 16, C_TEXT, 28, "%s", keys[i]);
        }
        if (i < 3) {
            text_r(MX + W - 24, ry + 16, C_TEXT, 28, "%s", vals[i]);
            if (i == 1) {                       /* Root path: editable */
                char hint[16];
                snprintf(hint, sizeof(hint), "X \xC2\xB7 EDIT");
                u32 hc = focused ? C_ACC : C_MUTED;
                text_r(MX + W - 24 - tw(28, vals[i]) - 40, ry + 18, hc, 20,
                       "%s", hint);
            }
        } else {
            u32 sc = sw[i] ? C_ACC : C_MUTED;
            u32 sb = sw[i] ? C_ACC : C_LINE;
            int bw = tw(22, vals[i]) + 32;
            box(MX + W - 24 - bw, ry + 12, bw, 34, sb, C_PANEL);
            text_c(MX + W - 24 - bw / 2, ry + 18, sc, 22, "%s", vals[i]);
        }
        if (i < 4) hline(MX + 24, ry + SET_ROW_H, W - 48, C_LINE);
    }

    text(MX, y + SET_ROW_H * 5 + 2 + 14, C_MUTED, 22,
         "Press X on the Root path row to change it (system keyboard). "
         "Other values are read-only.");
}

/* ------------------------------------------------------------------ *
 * Overlays (solid panels, drawn last)
 * ------------------------------------------------------------------ */
static void overlay_panel(int* x, int* y, int* w, int* h)
{
    *w = 960; *h = 300;
    *x = (SCREEN_W - *w) / 2;
    *y = (SCREEN_H - *h) / 2;
    box(*x, *y, *w, *h, C_LINE, C_PANEL);
}

static void draw_detail_hist(void)
{
    if (g.detail.idx < 0 || g.detail.idx >= g.hist_count) return;
    const opftp_ui_hist_t* h = &g.hist[(g.hist_head - g.detail.idx + OPFTP_UI_MAX_HIST) % OPFTP_UI_MAX_HIST];
    bool up = !strcmp(h->op, "RETR") || !strcmp(h->op, "LIST");
    int x, y, w, hh;
    overlay_panel(&x, &y, &w, &hh);
    x += 24;

    text(x, y + 20, C_MUTED, 22, "TRANSFER DETAIL");
    arrow(x, y + 62, up, 22, up ? C_BLUE : C_ACC);
    text(x + 36, y + 60, C_TEXT, 28, "%s", h->op);

    char sz[16], tot[16];
    opftp_ui_fmt_size(sz, sizeof(sz), h->bytes);
    opftp_ui_fmt_size(tot, sizeof(tot), h->total);
    text(x + 36 + tw(28, h->op) + 24, y + 62, C_MUTED, 24,
         "%02d:%02d \xC2\xB7 %s", h->hh, h->mm, sz);
    if (h->total) {
        text_r(x + w - 48, y + 62, C_MUTED, 24, "of %s", tot);
    }

    /* full path, wrapped at ~56 chars */
    char line[128];
    size_t len = strlen(h->path), off = 0, li = 0;
    while (off < len && li < 3) {
        size_t take = len - off > 56 ? 56 : len - off;
        memcpy(line, h->path + off, take);
        line[take] = 0;
        text(x, y + 110 + (int)li * 32, C_TEXT, 24, "%s", line);
        off += take; li++;
    }
    if (off < len) text(x, y + 110 + (int)li * 32, C_MUTED, 24, "\xE2\x80\xA6");

    const char* rs = h->result == OPFTP_UI_H_OK ? "OK" : h->result == OPFTP_UI_H_ABORTED ? "ABORTED" : "ERROR";
    u32 rc = h->result == OPFTP_UI_H_OK ? C_ACC : h->result == OPFTP_UI_H_ABORTED ? C_WARN : C_ERR;
    text(x, y + hh - 44, rc, 24, "RESULT: %s", rs);
    text_r(x + w - 72, y + hh - 44, C_MUTED, 20, "\xC3\x97 Close");
}

static void draw_detail_xfer(void)
{
    if (g.detail.idx < 0 || g.detail.idx >= g.snap.num_clients) return;
    const opftp_snapshot_client_t* c = &g.snap.clients[g.detail.idx];
    if (!c->xfer_active) return;
    bool up = !strcmp(c->xfer_op, "RETR") || !strcmp(c->xfer_op, "LIST");
    int x, y, w, hh;
    overlay_panel(&x, &y, &w, &hh);
    x += 24;

    text(x, y + 20, C_MUTED, 22, "ACTIVE TRANSFER");
    arrow(x, y + 62, up, 22, up ? C_BLUE : C_ACC);
    text(x + 36, y + 60, C_TEXT, 28, "%s", c->xfer_op);

    char sz[24], tot[24], rate[16];
    opftp_ui_fmt_size(sz, sizeof(sz), c->xfer_bytes);
    opftp_ui_fmt_size(tot, sizeof(tot), c->xfer_total);
    opftp_ui_trk_t* t = 0;
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++)
        if (g.trk[i].present && !strcmp(g.trk[i].peer, c->peer)) { t = &g.trk[i]; break; }
    opftp_ui_fmt_rate(rate, sizeof(rate), t && t->rate > 0 ? t->rate : 0);
    if (c->xfer_total)
        text_r(x + w - 48, y + 62, C_TEXT, 24, "%s of %s \xC2\xB7 %s", sz, tot, rate);
    else
        text_r(x + w - 48, y + 62, C_TEXT, 24, "%s \xC2\xB7 %s", sz, rate);

    size_t len = strlen(c->xfer_path), off = 0, li = 0;
    while (off < len && li < 3) {
        size_t take = len - off > 56 ? 56 : len - off;
        char line[128];
        memcpy(line, c->xfer_path + off, take);
        line[take] = 0;
        text(x, y + 110 + (int)li * 32, C_TEXT, 24, "%s", line);
        off += take; li++;
    }
    if (off < len) text(x, y + 110 + (int)li * 32, C_MUTED, 24, "\xE2\x80\xA6");

    text(x, y + hh - 44, C_MUTED, 24, "CLIENT %s", c->peer);
    text_r(x + w - 72, y + hh - 44, C_MUTED, 20, "\xC3\x97 Close");
}

static void draw_detail_client(void)
{
    if (g.detail.idx < 0 || g.detail.idx >= g.snap.num_clients) return;
    const opftp_snapshot_client_t* c = &g.snap.clients[g.detail.idx];
    int x, y, w, hh;
    overlay_panel(&x, &y, &w, &hh);
    x += 24;

    text(x, y + 20, C_MUTED, 22, "CLIENT DETAIL");
    text(x, y + 60, C_TEXT, 28, "%s", c->peer);
    text(x, y + 104, C_MUTED, 24, "USER");
    text(x + 80, y + 104, C_TEXT, 24, "%s", c->user[0] ? c->user : "?");
    text(x, y + 140, C_MUTED, 24, "STATUS");
    text(x + 110, y + 140, c->xfer_active ? C_ACC : C_MUTED, 24, "%s",
         c->xfer_active ? c->xfer_op : "idle");

    size_t len = strlen(c->cwd), off = 0, li = 0;
    text(x, y + 176, C_MUTED, 22, "CWD");
    while (off < len && li < 2) {
        size_t take = len - off > 56 ? 56 : len - off;
        char line[128];
        memcpy(line, c->cwd + off, take);
        line[take] = 0;
        text(x, y + 200 + (int)li * 32, C_TEXT, 24, "%s", line);
        off += take; li++;
    }
    if (off < len) text(x, y + 200 + (int)li * 32, C_MUTED, 24, "\xE2\x80\xA6");
    text_r(x + w - 72, y + hh - 44, C_MUTED, 20, "\xC3\x97 Close");
}

static void draw_help(void)
{
    int x, y, w, hh;
    overlay_panel(&x, &y, &w, &hh);
    x += 24;

    text(x, y + 20, C_MUTED, 22, "HELP");

    static const char* rows[6] = {
        "Move selection", "Switch view", "Open details",
        "Close / clear selection", "Close this panel", "Hold to quit",
    };
    int gy = y + 64;
    glyph_tri(x, gy, C_TEXT);       text(x + 34, gy + 2, C_TEXT, 24, "%s", rows[0]);  gy += 40;
    /* d-pad arrows as a small cross */
    fill(x + 8, gy + 4, 8, 16, C_TEXT); fill(x + 4, gy + 8, 16, 8, C_TEXT);
    text(x + 34, gy + 2, C_TEXT, 24, "%s", rows[1]);  gy += 40;
    glyph_x(x, gy, C_TEXT);         text(x + 34, gy + 2, C_TEXT, 24, "%s", rows[2]);  gy += 40;
    glyph_circle(x, gy, C_TEXT);    text(x + 34, gy + 2, C_TEXT, 24, "%s", rows[3]);  gy += 40;
    glyph_tri(x, gy, C_TEXT);       text(x + 34, gy + 2, C_TEXT, 24, "%s", rows[4]);  gy += 40;
    glyph_start(x, gy, "START"); text(x + 74 + 14, gy + 2, C_TEXT, 24, "%s", rows[5]);

    text(x, y + hh - 44, C_MUTED, 20, "\xE2\x96\xB3 Help  \xC3\x97 Select  \xE2\x97\x8B Back  START Quit");
}

static void draw_detail_or_help(void)
{
    if (g.detail.kind >= 0) {
        if (g.detail.kind == 0) draw_detail_hist();
        else if (g.detail.kind == 1) draw_detail_xfer();
        else draw_detail_client();
    } else if (g.help) {
        draw_help();
    }
}

/* ------------------------------------------------------------------ *
 * Pad input — edge-triggered navigation (act on press, not hold)
 * ------------------------------------------------------------------ */
static uint16_t pad_d1, pad_d2;

static void poll_pad(void)
{
    CellPadData pd;
    memset(&pd, 0, sizeof(pd));
    if (cellPadGetData(0, &pd) != CELL_PAD_OK) {
        pad_d1 = pad_d2 = 0;
        return;
    }
    pad_d1 = pd.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
    pad_d2 = pd.button[CELL_PAD_BTN_OFFSET_DIGITAL2];
}

/* Returns true when any pad edge was seen this frame (a button event
 * was consumed, whether or not it changed state). */
static bool handle_input(void)
{
    uint16_t p1 = pad_d1 & ~prev_d1;        /* edge-trigger only */
    uint16_t p2 = pad_d2 & ~prev_d2;
    uint64_t now = now_us();

    /* START: hold 2s to quit */
    if (pad_d1 & CELL_PAD_CTRL_START) {
        if (!start_hold_us) start_hold_us = now;
        else if (now - start_hold_us >= 2000000ull) g.quit = true;
    } else {
        start_hold_us = 0;
    }

    if (p2 & CELL_PAD_CTRL_TRIANGLE) {      /* help overlay toggle */
        if (g.detail.kind >= 0) g.detail.kind = -1;
        else g.help = !g.help;
        goto done;
    }
    if (g.help) {
        if (p2 & (CELL_PAD_CTRL_CIRCLE | CELL_PAD_CTRL_CROSS)) g.help = false;
        goto done;
    }
    if (g.detail.kind >= 0) {               /* overlay open: close only */
        if (p2 & (CELL_PAD_CTRL_CIRCLE | CELL_PAD_CTRL_CROSS)) g.detail.kind = -1;
        goto done;
    }
    if (g.edit.field != OPFTP_UI_EDIT_NONE) {
        /* the OSK owns input while a text field is active */
        goto done;
    }

    if (p1 & (CELL_PAD_CTRL_LEFT | CELL_PAD_CTRL_RIGHT))
        opftp_ui_switch_view(&g, (p1 & CELL_PAD_CTRL_RIGHT) ? 1 : -1);

    if (p1 & CELL_PAD_CTRL_UP)   opftp_ui_sel_move(&g, -1);
    if (p1 & CELL_PAD_CTRL_DOWN) opftp_ui_sel_move(&g, 1);

    if (p2 & CELL_PAD_CTRL_CROSS) {
        if (g.view == OPFTP_UI_V_SETTINGS && g.sel_settings == 1) {
            /* Root path row: open the system OSK for the field */
            opftp_ui_edit_begin(&g, OPFTP_UI_EDIT_ROOT, g.snap.root);
            osk_open(g.edit.buf);
            goto done;
        }
        opftp_ui_open_detail(&g);
    }
    if (p2 & CELL_PAD_CTRL_CIRCLE)          /* back: clear selection */
        opftp_ui_clear_sel(&g);

done:
    prev_d1 = pad_d1;
    prev_d2 = pad_d2;
    return (p1 | p2) != 0;
}

/* ------------------------------------------------------------------ *
 * Render dispatch
 * ------------------------------------------------------------------ */
static void render(void)
{
    Background bg(gfx);
    bg.Mono(C_BG);

    draw_header(g.version);
    switch (g.view) {
    case OPFTP_UI_V_STATUS:    draw_status();    break;
    case OPFTP_UI_V_TRANSFERS: draw_transfers(); break;
    case OPFTP_UI_V_CLIENTS:   draw_clients();   break;
    case OPFTP_UI_V_SETTINGS:  draw_settings();  break;
    }
    draw_footer();
    draw_detail_or_help();
}

/* ------------------------------------------------------------------ *
 * C entry point — OSD lifetime on the calling (main) thread
 * ------------------------------------------------------------------ */
extern "C" int opftp_osd_run(opftp_server_t* s, const char* version)
{
    opftp_ui_init(&g, now_us());
    g.version = version;

    /* local IP for the ADDRESS stat (one shot; "—" if unavailable) */
    {
        osd_net_ctl_info_t info;
        memset(&info, 0, sizeof(info));
        if (netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) == 0)
            strncpy(g.local_ip, info.ip_address, sizeof(g.local_ip) - 1);
        else
            strncpy(g.local_ip, "\xE2\x80\x94", sizeof(g.local_ip) - 1);
    }

    start_hold_us = 0;
    last_frame_us = g.t0_us;
    last_render_us = 0;
    g_srv = s;

    if (cellPadInit(1) != CELL_PAD_OK)
        printf("OpenPS3FTP: OSD pad init failed\n");

    /* sysutil: the OSK needs a memory container + the callback queue
     * (polled every frame below) */
    if (sys_memory_container_create(&osk_container, 4 * 1024 * 1024) != 0)
        printf("OpenPS3FTP: OSK container create failed\n");
    cellSysutilRegisterCallback(0, osk_cb, NULL);

    NoRSX gfx_dev(RESOLUTION_1280x720);
    Font font_dev(LATIN2, &gfx_dev); /* PS3 system font (VR LATIN2) */
    Object obj_dev(&gfx_dev);

    gfx = &gfx_dev;
    font = &font_dev;
    obj = &obj_dev;

    gfx_dev.AppStart();
    while (gfx_dev.GetAppStatus() && !g.quit) {
        uint64_t f0 = now_us();

        /* poll + diff run every iteration, regardless of rendering:
         * START-hold quit and transfer detection stay responsive */
        poll_pad();
        bool any_edge = handle_input();
        cellSysutilCheckCallback();     /* delivers OSK events */
        osk_process();
        opftp_server_snapshot(s, &g.snap);
        g.snap_ok = true;
        bool snap_changed = memcmp(&g.snap, &g.snap_prev, sizeof(g.snap)) != 0;
        if (snap_changed)
            g.snap_prev = g.snap;
        opftp_ui_poll(&g, now_us());

        if (gfx_dev.ExitSignalStatus()) g.quit = true;

        /* Idle-skip: the framebuffer persists between flips, so when
         * nothing changed we skip render()+Flip() entirely and the last
         * frame stays on screen. Dirty when: a pad event was consumed,
         * the snapshot differs from the last one (memcmp; the server
         * memsets its cache, so identical state == identical bytes), or
         * >=1s since the last rendered frame so the clock/uptime tick. */
        uint64_t since_render = now_us() - last_render_us;
        bool dirty = any_edge || snap_changed || since_render >= 1000000ull;

        if (dirty) {
            render();
            gfx_dev.Flip();
            last_render_us = now_us();

            /* ~30fps pacing when rendering */
            uint64_t elapsed = now_us() - f0;
            if (elapsed < 33000ull)
                sys_timer_usleep(33000ull - elapsed);
        } else {
            /* idle: longer sleep (pad + snapshot still polled each
             * iteration above) */
            sys_timer_usleep(100000ull);
        }
        last_frame_us = now_us();
    }

    cellPadEnd();
    cellSysutilUnregisterCallback(0);

    printf("OpenPS3FTP: OSD closed (quit=%d)\n", g.quit ? 1 : 0);
    return 0;
}
