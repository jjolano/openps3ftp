/*
 * OpenPS3FTP — shared UI model (platform-neutral).
 *
 * The part of the UI that is identical on every renderer: view state,
 * selection/scroll state, and the snapshot diffing that produces the
 * event strip, transfer history, and per-client rate/idle tracking.
 *
 * Two renderers consume this model:
 *   - osd.cpp  (PS3): NoRSX 1280x720 on-screen display
 *   - tui.c    (host): terminal TUI (ANSI)
 *
 * Pure C11, no platform includes.  Renderers own their global
 * `opftp_ui_t` and pass it in; each renderer also keeps its own
 * platform state (pad, NoRSX objects, frame timing, termios) outside
 * this struct.
 */
#ifndef OPFTP_UI_H
#define OPFTP_UI_H

#include <openps3ftp/openps3ftp.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* View ids (order = tab order). */
enum { OPFTP_UI_V_STATUS = 0, OPFTP_UI_V_TRANSFERS, OPFTP_UI_V_CLIENTS,
       OPFTP_UI_V_SETTINGS, OPFTP_UI_V_COUNT };

/* History entry results. */
enum { OPFTP_UI_H_OK = 0, OPFTP_UI_H_ABORTED, OPFTP_UI_H_ERROR };

/* Detail overlay kinds (index is per-view). */
enum { OPFTP_UI_D_NONE = -1, OPFTP_UI_D_HIST = 0, OPFTP_UI_D_XFER,
       OPFTP_UI_D_CLIENT };

enum {
    OPFTP_UI_MAX_HIST   = 48,
    OPFTP_UI_MAX_EVENTS = 4,
    OPFTP_UI_MAX_TRACK  = 24,
};

/* One transfer-history entry (newest first, ring buffer). */
typedef struct opftp_ui_hist {
    char      op[8];
    char      path[OPFTP_SNAPSHOT_PATH];
    uint64_t  bytes, total;
    uint8_t   hh, mm;
    uint8_t   result;             /* OPFTP_UI_H_* */
} opftp_ui_hist_t;

/* One event-strip row (newest first, ring). */
typedef struct opftp_ui_ev {
    char      text[72];
    uint8_t   hh, mm;
    bool      warn;
} opftp_ui_ev_t;

/* Per-client OSD-side state (deltas / idle / rate). */
typedef struct opftp_ui_trk {
    char      peer[64];
    bool      present;
    bool      seen;               /* client present in this frame's snapshot */
    bool      logged_in;
    bool      xfer_active;
    char      xfer_op[8];
    char      xfer_path[OPFTP_SNAPSHOT_PATH];
    uint64_t  xfer_bytes;
    uint64_t  xfer_total;
    uint64_t  idle_reset_us;      /* last connect/login/xfer change */
    uint64_t  xfer_t0_us;
    uint64_t  last_bytes_us;
    double    rate;               /* smoothed bytes/s */
} opftp_ui_trk_t;

/* Detail overlay selection. */
typedef struct opftp_ui_detail {
    int       kind;               /* OPFTP_UI_D_* */
    int       idx;
} opftp_ui_detail_t;

/* Text input: one active field at a time. Renderers collect characters
 * via their native input (PS3 system OSK / terminal keys); the model
 * owns the buffer and the field identity so both renderers stay in
 * sync ("same thing on both screens"). */
enum {
    OPFTP_UI_EDIT_NONE = -1,
    OPFTP_UI_EDIT_ROOT = 0,       /* SETTINGS: Root path */
    OPFTP_UI_EDIT_COUNT,
};
enum { OPFTP_UI_SETTINGS_ROWS = 5 };  /* selectable SETTINGS rows */

#define OPFTP_UI_EDIT_MAX 256

typedef struct opftp_ui_edit {
    int       field;              /* OPFTP_UI_EDIT_* or NONE */
    char      buf[OPFTP_UI_EDIT_MAX];
    int       len;                /* bytes (UTF-8) in buf */
} opftp_ui_edit_t;

/*
 * Full UI state.  Renderers read every field directly (draw code) and
 * mutate the interaction fields (view/sel/scroll/help/detail/quit);
 * the model functions own snap diffing, events, history and tracking.
 *
 * Renderer-owned: version, local_ip, snap, snap_prev, snap_ok.
 * Model-owned:   t0_us, events/hist/trk + counters, session stats.
 */
typedef struct opftp_ui {
    const char* version;

    /* view state */
    int       view;
    int       sel_status, sel_hist, sel_cli;   /* -1 = none */
    int       sel_settings;                    /* -1 = none */
    int       scroll_hist, scroll_cli;
    bool      help;
    opftp_ui_detail_t detail;

    /* text input */
    opftp_ui_edit_t edit;

    /* time */
    uint64_t  t0_us;              /* UI start (server already up) */

    /* snapshot-derived data */
    opftp_snapshot_t snap;
    opftp_snapshot_t snap_prev;   /* last snapshot; memcmp for dirty */
    bool      snap_ok;

    /* local ip (renderer fetches once; "—" if unavailable) */
    char      local_ip[16];

    /* event strip (newest first, ring) */
    opftp_ui_ev_t events[OPFTP_UI_MAX_EVENTS];
    int       ev_head, ev_count;

    /* transfer history (newest first, ring) */
    opftp_ui_hist_t hist[OPFTP_UI_MAX_HIST];
    int       hist_head, hist_count;

    /* per-client tracking for deltas / idle / rate */
    opftp_ui_trk_t trk[OPFTP_UI_MAX_TRACK];

    /* session stats for the uptime strip */
    uint64_t  session_xfers, session_bytes;

    bool      quit;
} opftp_ui_t;

/* Zero the model and set start time / initial selection state. */
void opftp_ui_init(opftp_ui_t* ui, uint64_t now_us);

/*
 * Diff ui->snap against the previous poll: push connect/disconnect
 * events, start/complete transfers (history entries), and update
 * per-client rate/idle tracking.  `now_us` drives all time math.
 * Call every frame, after copying the server snapshot into ui->snap.
 */
void opftp_ui_poll(opftp_ui_t* ui, uint64_t now_us);

/* Navigation (shared by pad and keyboard input). */
void opftp_ui_switch_view(opftp_ui_t* ui, int dir);
void opftp_ui_sel_move(opftp_ui_t* ui, int dir);
void opftp_ui_clear_sel(opftp_ui_t* ui);
void opftp_ui_open_detail(opftp_ui_t* ui);

/* Map a view-list selection index to the snapshot client index. */
int  opftp_ui_sel_to_client(const opftp_ui_t* ui, int idx);

/* Keep the scroll position clamped around a selection. */
int  opftp_ui_clamp_scroll(int sel, int scroll, int visible, int total);

/* View name for the tabs / footer indicator. */
const char* opftp_ui_view_name(int view);

/* ---- text input (one active field) ---- */

/* Open an edit for a field, prefilled with `initial` (may be NULL). */
void opftp_ui_edit_begin(opftp_ui_t* ui, int field, const char* initial);
/* Close the edit and discard the collected text. */
void opftp_ui_edit_cancel(opftp_ui_t* ui);
/* Close the edit; returns the collected buffer (valid until the next
 * opftp_ui_edit_begin). */
const char* opftp_ui_edit_commit(opftp_ui_t* ui);
/* Append one UTF-8 character sequence. 0 ok; -ENOSPC when full. */
int  opftp_ui_edit_type(opftp_ui_t* ui, const char* utf8);
/* Delete the last UTF-8 character. */
void opftp_ui_edit_backspace(opftp_ui_t* ui);

/* Push a user-visible event-strip row (renderer-side errors, e.g. a
 * rejected setting change). */
void opftp_ui_event(opftp_ui_t* ui, const char* text, bool warn,
                    uint64_t now_us);

/* Formatting helpers shared by renderers ("1.5 MB", "3.1 GB/s", …). */
void opftp_ui_fmt_size(char* out, size_t n, uint64_t bytes);
void opftp_ui_fmt_rate(char* out, size_t n, double bps);
void opftp_ui_fmt_dur(char* out, size_t n, uint64_t sec);   /* hh:mm:ss */
void opftp_ui_fmt_idle(char* out, size_t n, uint64_t sec);  /* mm:ss */

#ifdef __cplusplus
}
#endif

#endif /* OPFTP_UI_H */
