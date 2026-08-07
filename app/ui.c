/*
 * OpenPS3FTP — shared UI model (platform-neutral).
 *
 * Pure C11, no platform includes: view/selection state and the
 * snapshot diffing that produces the event strip, transfer history,
 * and per-client rate/idle tracking.  See ui.h for the contract.
 * Transcribed from the PS3 OSD (app/osd.cpp); behavior-preserving.
 *
 * Renderers own their `opftp_ui_t` (one global each) and drive the
 * model from opftp_osd_run equivalents: init once, copy the server
 * snapshot into ui->snap every frame, then opftp_ui_poll(ui, now).
 */

#include "ui.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>

/* ------------------------------------------------------------------ *
 * Formatting helpers shared by renderers ("1.5 MB", "3.1 GB/s", …)
 * ------------------------------------------------------------------ */
void opftp_ui_fmt_size(char* out, size_t n, uint64_t bytes)
{
    if (bytes >= (uint64_t)1 << 30)
        snprintf(out, n, "%.1f GB", (double)bytes / (1 << 30));
    else if (bytes >= (uint64_t)1 << 20)
        snprintf(out, n, "%.1f MB", (double)bytes / (1 << 20));
    else if (bytes >= (uint64_t)1 << 10)
        snprintf(out, n, "%.1f KB", (double)bytes / (1 << 10));
    else
        snprintf(out, n, "%" PRIu64 " B", bytes);
}

void opftp_ui_fmt_rate(char* out, size_t n, double bps)
{
    if (bps >= (double)(1 << 30))
        snprintf(out, n, "%.1f GB/s", bps / (1 << 30));
    else if (bps >= (double)(1 << 20))
        snprintf(out, n, "%.1f MB/s", bps / (1 << 20));
    else if (bps >= (double)(1 << 10))
        snprintf(out, n, "%.1f KB/s", bps / (1 << 10));
    else
        snprintf(out, n, "%.0f B/s", bps);
}

/* hh:mm:ss duration (uptime). */
void opftp_ui_fmt_dur(char* out, size_t n, uint64_t sec)
{
    snprintf(out, n, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
             sec / 3600, (sec / 60) % 60, sec % 60);
}

/* mm:ss idle. */
void opftp_ui_fmt_idle(char* out, size_t n, uint64_t sec)
{
    snprintf(out, n, "%02" PRIu64 ":%02" PRIu64, sec / 60, sec % 60);
}

/* ------------------------------------------------------------------ *
 * Event / history rings
 * ------------------------------------------------------------------ */
static void ev_push(opftp_ui_t* ui, const char* text, bool warn, uint64_t now_us)
{
    uint64_t sec = now_us / 1000000ull;
    ui->ev_head = (ui->ev_head + 1) % OPFTP_UI_MAX_EVENTS;
    opftp_ui_ev_t* e = &ui->events[ui->ev_head];
    e->hh = (uint8_t)((sec / 3600) % 24);
    e->mm = (uint8_t)((sec / 60) % 60);
    e->warn = warn;
    strncpy(e->text, text, sizeof(e->text) - 1);
    e->text[sizeof(e->text) - 1] = 0;
    if (ui->ev_count < OPFTP_UI_MAX_EVENTS) ui->ev_count++;
}

static void hist_push(opftp_ui_t* ui, const char* op, const char* path,
                      uint64_t bytes, uint64_t total, int result, uint64_t now_us)
{
    uint64_t sec = now_us / 1000000ull;
    ui->hist_head = (ui->hist_head + 1) % OPFTP_UI_MAX_HIST;
    opftp_ui_hist_t* h = &ui->hist[ui->hist_head];
    h->hh = (uint8_t)((sec / 3600) % 24);
    h->mm = (uint8_t)((sec / 60) % 60);
    h->bytes = bytes; h->total = total; h->result = (uint8_t)result;
    strncpy(h->op, op, sizeof(h->op) - 1);
    h->op[sizeof(h->op) - 1] = 0;
    strncpy(h->path, path, sizeof(h->path) - 1);
    h->path[sizeof(h->path) - 1] = 0;
    if (ui->hist_count < OPFTP_UI_MAX_HIST) ui->hist_count++;
    ui->session_xfers++;
    ui->session_bytes += bytes;
}

static opftp_ui_trk_t* trk_find(opftp_ui_t* ui, const char* peer)
{
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++)
        if (ui->trk[i].present && !strcmp(ui->trk[i].peer, peer))
            return &ui->trk[i];
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++)     /* free slot */
        if (!ui->trk[i].present) {
            memset(&ui->trk[i], 0, sizeof(ui->trk[i]));
            strncpy(ui->trk[i].peer, peer, sizeof(ui->trk[i].peer) - 1);
            return &ui->trk[i];
        }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Snapshot diffing: events, history, idle/rate tracking
 * ------------------------------------------------------------------ */
void opftp_ui_poll(opftp_ui_t* ui, uint64_t now_us)
{
    const opftp_snapshot_t* s = &ui->snap;

    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++) ui->trk[i].seen = false;

    for (int i = 0; i < s->num_clients; i++) {
        const opftp_snapshot_client_t* c = &s->clients[i];
        opftp_ui_trk_t* t = trk_find(ui, c->peer);
        if (!t) continue;
        bool is_new = !t->present;

        if (is_new) {                       /* new client */
            char ev[72];
            snprintf(ev, sizeof(ev), "CLIENT CONNECTED \xE2\x80\x94 %s", c->peer);
            ev_push(ui, ev, false, now_us);
            t->idle_reset_us = now_us;
            t->logged_in = c->logged_in;
        }
        t->present = true;
        t->seen = true;

        if (c->logged_in != t->logged_in) {
            t->logged_in = c->logged_in;
            t->idle_reset_us = now_us;
        }

        if (c->xfer_active) {
            if (!t->xfer_active) {          /* transfer started */
                t->xfer_active = true;
                strncpy(t->xfer_op, c->xfer_op, sizeof(t->xfer_op) - 1);
                t->xfer_op[sizeof(t->xfer_op) - 1] = 0;
                strncpy(t->xfer_path, c->xfer_path, sizeof(t->xfer_path) - 1);
                t->xfer_path[sizeof(t->xfer_path) - 1] = 0;
                t->xfer_bytes = c->xfer_bytes;
                t->xfer_total = c->xfer_total;
                t->xfer_t0_us = now_us;
                t->last_bytes_us = now_us;
                t->rate = 0;
                t->idle_reset_us = now_us;
            } else {
                /* rate (smoothed) from byte deltas */
                uint64_t dt = now_us - t->last_bytes_us;
                if (dt > 0 && c->xfer_bytes >= t->xfer_bytes) {
                    double inst = (double)(c->xfer_bytes - t->xfer_bytes) * 1000000.0 / (double)dt;
                    t->rate = t->rate > 0 ? t->rate * 0.5 + inst * 0.5 : inst;
                }
                t->xfer_bytes = c->xfer_bytes;
                t->xfer_total = c->xfer_total;
                t->last_bytes_us = now_us;
            }
        } else if (t->xfer_active) {        /* transfer completed */
            t->xfer_active = false;
            /* snapshot no longer carries the transfer, so use tracked
             * values; incomplete vs. total -> ABORTED (no way to tell
             * a user cancel from a failed transfer via the snapshot). */
            int result = (t->xfer_bytes && t->xfer_total &&
                          t->xfer_bytes < t->xfer_total) ? OPFTP_UI_H_ABORTED : OPFTP_UI_H_OK;
            hist_push(ui, t->xfer_op, t->xfer_path, t->xfer_bytes, t->xfer_total, result, now_us);
            t->idle_reset_us = now_us;
        }
    }

    /* clients that vanished while tracked: disconnect event; an active
     * transfer that never completed -> ERROR. */
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++) {
        opftp_ui_trk_t* t = &ui->trk[i];
        if (!t->present || t->seen) continue;
        char ev[72];
        snprintf(ev, sizeof(ev), "CLIENT DISCONNECTED \xE2\x80\x94 %s", t->peer);
        ev_push(ui, ev, true, now_us);
        if (t->xfer_active)
            hist_push(ui, t->xfer_op, t->xfer_path, t->xfer_bytes, t->xfer_total, OPFTP_UI_H_ERROR, now_us);
        memset(t, 0, sizeof(*t));           /* forget the client */
    }
}

/* ------------------------------------------------------------------ *
 * Selection / navigation
 * ------------------------------------------------------------------ */
static int sel_count(const opftp_ui_t* ui)
{
    switch (ui->view) {
    case OPFTP_UI_V_STATUS: {
        int n = 0;
        for (int i = 0; i < ui->snap.num_clients; i++)
            if (ui->snap.clients[i].xfer_active) n++;
        return n;
    }
    case OPFTP_UI_V_TRANSFERS: return ui->hist_count;
    case OPFTP_UI_V_CLIENTS:   return ui->snap.num_clients;
    case OPFTP_UI_V_SETTINGS:  return OPFTP_UI_SETTINGS_ROWS;
    default:                   return 0;
    }
}

static int* sel_ptr(opftp_ui_t* ui)
{
    switch (ui->view) {
    case OPFTP_UI_V_STATUS:    return &ui->sel_status;
    case OPFTP_UI_V_TRANSFERS: return &ui->sel_hist;
    case OPFTP_UI_V_CLIENTS:   return &ui->sel_cli;
    case OPFTP_UI_V_SETTINGS:  return &ui->sel_settings;
    default:                   return 0;
    }
}

void opftp_ui_sel_move(opftp_ui_t* ui, int dir)
{
    int* s = sel_ptr(ui);
    if (!s) return;
    int n = sel_count(ui);
    if (n <= 0) { *s = -1; return; }
    *s += dir;
    if (*s < 0) *s = 0;
    if (*s >= n) *s = n - 1;
}

void opftp_ui_clear_sel(opftp_ui_t* ui)
{
    int* s = sel_ptr(ui);
    if (s) *s = -1;
}

/* Map a list index to the snapshot client index it refers to. */
int opftp_ui_sel_to_client(const opftp_ui_t* ui, int idx)
{
    if (ui->view == OPFTP_UI_V_CLIENTS) return idx;
    int k = 0;
    for (int i = 0; i < ui->snap.num_clients; i++) {
        if (ui->snap.clients[i].xfer_active) {
            if (k == idx) return i;
            k++;
        }
    }
    return -1;
}

void opftp_ui_open_detail(opftp_ui_t* ui)
{
    int* s = sel_ptr(ui);
    if (!s || *s < 0) return;
    if (ui->view == OPFTP_UI_V_TRANSFERS) {
        ui->detail.kind = OPFTP_UI_D_HIST;          /* history entry */
        ui->detail.idx = *s;
    } else if (ui->view == OPFTP_UI_V_STATUS) {
        int ci = opftp_ui_sel_to_client(ui, *s);
        if (ci >= 0) { ui->detail.kind = OPFTP_UI_D_XFER; ui->detail.idx = ci; }
    } else if (ui->view == OPFTP_UI_V_CLIENTS) {
        ui->detail.kind = OPFTP_UI_D_CLIENT;        /* client */
        ui->detail.idx = *s;
    }
}

void opftp_ui_switch_view(opftp_ui_t* ui, int dir)
{
    ui->view = (ui->view + dir + OPFTP_UI_V_COUNT) % OPFTP_UI_V_COUNT;
    ui->sel_status = ui->sel_hist = ui->sel_cli = ui->sel_settings = -1;
    ui->scroll_hist = ui->scroll_cli = 0;
    ui->detail.kind = OPFTP_UI_D_NONE;
    ui->help = false;
}

/* Keep the scroll position clamped around a selection. */
int opftp_ui_clamp_scroll(int sel, int scroll, int visible, int total)
{
    if (sel < 0) sel = 0;
    if (sel >= total) sel = total - 1;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + visible) scroll = sel - visible + 1;
    if (scroll > total - visible) scroll = total - visible;
    if (scroll < 0) scroll = 0;
    return scroll;
}

/* ------------------------------------------------------------------ *
 * Lifecycle / view names
 * ------------------------------------------------------------------ */
void opftp_ui_init(opftp_ui_t* ui, uint64_t now_us)
{
    memset(ui, 0, sizeof(*ui));
    ui->t0_us = now_us;
    ui->sel_status = ui->sel_hist = ui->sel_cli = ui->sel_settings = -1;
    ui->detail.kind = OPFTP_UI_D_NONE;
    ui->edit.field = OPFTP_UI_EDIT_NONE;
}

/* ------------------------------------------------------------------ *
 * Text input (one active field; buffer owned by the model)
 * ------------------------------------------------------------------ */
void opftp_ui_edit_begin(opftp_ui_t* ui, int field, const char* initial)
{
    if (field < 0 || field >= OPFTP_UI_EDIT_COUNT) return;
    ui->edit.field = field;
    ui->edit.len = 0;
    ui->edit.buf[0] = 0;
    if (initial) {
        strncpy(ui->edit.buf, initial, sizeof(ui->edit.buf) - 1);
        ui->edit.buf[sizeof(ui->edit.buf) - 1] = 0;
        ui->edit.len = (int)strlen(ui->edit.buf);
    }
}

void opftp_ui_edit_cancel(opftp_ui_t* ui)
{
    ui->edit.field = OPFTP_UI_EDIT_NONE;
    ui->edit.len = 0;
    ui->edit.buf[0] = 0;
}

const char* opftp_ui_edit_commit(opftp_ui_t* ui)
{
    ui->edit.field = OPFTP_UI_EDIT_NONE;
    return ui->edit.buf;        /* valid until the next edit_begin */
}

int opftp_ui_edit_type(opftp_ui_t* ui, const char* utf8)
{
    if (ui->edit.field == OPFTP_UI_EDIT_NONE || !utf8)
        return -EINVAL;
    size_t n = strlen(utf8);
    if ((size_t)ui->edit.len + n >= sizeof(ui->edit.buf))
        return -ENOSPC;
    memcpy(ui->edit.buf + ui->edit.len, utf8, n + 1);
    ui->edit.len += (int)n;
    return 0;
}

void opftp_ui_edit_backspace(opftp_ui_t* ui)
{
    if (ui->edit.field == OPFTP_UI_EDIT_NONE || ui->edit.len <= 0)
        return;
    int i = ui->edit.len - 1;
    while (i > 0 && ((unsigned char)ui->edit.buf[i] & 0xC0) == 0x80)
        i--;                    /* drop the whole last UTF-8 char */
    ui->edit.buf[i] = 0;
    ui->edit.len = i;
}

void opftp_ui_event(opftp_ui_t* ui, const char* text, bool warn, uint64_t now_us)
{
    ev_push(ui, text, warn, now_us);
}

const char* opftp_ui_view_name(int view)
{
    static const char* const names[OPFTP_UI_V_COUNT] = {
        "STATUS", "TRANSFERS", "CLIENTS", "SETTINGS"
    };
    if (view < 0 || view >= OPFTP_UI_V_COUNT) return "";
    return names[view];
}
