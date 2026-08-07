/*
 * Unit tests for the shared UI model (app/ui.h / app/ui.c) — the
 * snapshot-diffing logic (events, transfer history, rate/idle
 * tracking) shared by the PS3 OSD and the host TUI.
 *
 * Deterministic: fake `now_us` values, synthetic snapshots, no sleeps.
 * Assert-based; exit code 0 = pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "openps3ftp/openps3ftp.h"
#include "ui.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        failures++; \
    } \
} while (0)

/* Build a synthetic snapshot with the given clients and feed it to the
 * model (replaces ui->snap wholesale; trk state persists). */
typedef struct {
    const char* peer;
    bool logged_in;
    bool xfer_active;
    const char* xfer_op;
    const char* xfer_path;
    uint64_t xfer_bytes;
    uint64_t xfer_total;
} test_client_t;

static void feed(opftp_ui_t* ui, uint64_t now,
                 const test_client_t* clients, int nclients)
{
    memset(&ui->snap, 0, sizeof(ui->snap));
    ui->snap.started = true;
    ui->snap.port = 2121;
    ui->snap.num_clients = nclients;
    for (int i = 0; i < nclients; i++) {
        opftp_snapshot_client_t* c = &ui->snap.clients[i];
        strncpy(c->peer, clients[i].peer, sizeof(c->peer) - 1);
        strncpy(c->user, "ftp", sizeof(c->user) - 1);
        strncpy(c->cwd, "/dev_hdd0", sizeof(c->cwd) - 1);
        c->logged_in = clients[i].logged_in;
        c->xfer_active = clients[i].xfer_active;
        if (clients[i].xfer_op)
            strncpy(c->xfer_op, clients[i].xfer_op, sizeof(c->xfer_op) - 1);
        if (clients[i].xfer_path)
            strncpy(c->xfer_path, clients[i].xfer_path,
                    sizeof(c->xfer_path) - 1);
        c->xfer_bytes = clients[i].xfer_bytes;
        c->xfer_total = clients[i].xfer_total;
    }
    opftp_ui_poll(ui, now);
}

static opftp_ui_trk_t* trk_of(opftp_ui_t* ui, const char* peer)
{
    for (int i = 0; i < OPFTP_UI_MAX_TRACK; i++)
        if (ui->trk[i].present && !strcmp(ui->trk[i].peer, peer))
            return &ui->trk[i];
    return 0;
}

/* ---------------- init / view names / fmt ---------------- */

static void test_init(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 1234567ull);
    CHECK(ui.view == OPFTP_UI_V_STATUS);
    CHECK(ui.sel_status == -1 && ui.sel_hist == -1 && ui.sel_cli == -1);
    CHECK(ui.scroll_hist == 0 && ui.scroll_cli == 0);
    CHECK(ui.detail.kind == OPFTP_UI_D_NONE);
    CHECK(ui.t0_us == 1234567ull);
    CHECK(ui.ev_count == 0 && ui.hist_count == 0);
    CHECK(!ui.quit && !ui.help);
    CHECK_STR(opftp_ui_view_name(OPFTP_UI_V_STATUS), "STATUS");
    CHECK_STR(opftp_ui_view_name(OPFTP_UI_V_SETTINGS), "SETTINGS");
}

static void test_fmt(void)
{
    char b[32];
    opftp_ui_fmt_size(b, sizeof(b), 0);         CHECK_STR(b, "0 B");
    opftp_ui_fmt_size(b, sizeof(b), 1023);      CHECK_STR(b, "1023 B");
    opftp_ui_fmt_size(b, sizeof(b), 1024);      CHECK_STR(b, "1.0 KB");
    opftp_ui_fmt_size(b, sizeof(b), 1536);      CHECK_STR(b, "1.5 KB");
    opftp_ui_fmt_size(b, sizeof(b), 1ull << 20); CHECK_STR(b, "1.0 MB");
    opftp_ui_fmt_size(b, sizeof(b), 1ull << 30); CHECK_STR(b, "1.0 GB");
    opftp_ui_fmt_rate(b, sizeof(b), 0);         CHECK_STR(b, "0 B/s");
    opftp_ui_fmt_rate(b, sizeof(b), 1024);      CHECK_STR(b, "1.0 KB/s");
    opftp_ui_fmt_rate(b, sizeof(b), 1ull << 20); CHECK_STR(b, "1.0 MB/s");
    opftp_ui_fmt_dur(b, sizeof(b), 3661);       CHECK_STR(b, "01:01:01");
    opftp_ui_fmt_idle(b, sizeof(b), 65);        CHECK_STR(b, "01:05");
}

/* ---------------- events / tracking / history ---------------- */

static void test_connect_and_login(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 1000000ull);
    static const test_client_t c1[] = {
        { "10.0.0.5", false, false, 0, 0, 0, 0 },
    };

    feed(&ui, 1000000ull, c1, 1);
    CHECK(ui.ev_count == 1);
    CHECK(strstr(ui.events[ui.ev_head].text, "CLIENT CONNECTED") != 0);
    CHECK(strstr(ui.events[ui.ev_head].text, "10.0.0.5") != 0);
    CHECK(!ui.events[ui.ev_head].warn);
    opftp_ui_trk_t* t = trk_of(&ui, "10.0.0.5");
    CHECK(t && t->present && !t->logged_in && t->idle_reset_us == 1000000ull);

    /* login transition: no event, idle clock reset */
    static const test_client_t c2[] = {
        { "10.0.0.5", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 11000000ull, c2, 1);
    CHECK(ui.ev_count == 1);                    /* still just the connect */
    CHECK(trk_of(&ui, "10.0.0.5")->logged_in);
    CHECK(trk_of(&ui, "10.0.0.5")->idle_reset_us == 11000000ull);
}

static void test_transfer_lifecycle(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 2000000ull);
    static const test_client_t c0[] = {
        { "10.0.0.6", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 2000000ull, c0, 1);

    /* start: no history yet, tracking armed */
    static const test_client_t c1[] = {
        { "10.0.0.6", true, true, "RETR", "/game/data.pkg", 0, 1000 },
    };
    feed(&ui, 3000000ull, c1, 1);
    CHECK(ui.hist_count == 0);
    opftp_ui_trk_t* t = trk_of(&ui, "10.0.0.6");
    CHECK(t && t->xfer_active && t->rate == 0.0 && t->xfer_t0_us == 3000000ull);

    /* progress 1s later: rate == byte delta / dt */
    static const test_client_t c2[] = {
        { "10.0.0.6", true, true, "RETR", "/game/data.pkg", 500, 1000 },
    };
    feed(&ui, 4000000ull, c2, 1);
    t = trk_of(&ui, "10.0.0.6");
    CHECK(t && t->rate > 499.0 && t->rate < 501.0 && t->xfer_bytes == 500);

    /* progress to total, then completion: the completion snapshot no
     * longer carries the transfer, so the model reports the last
     * tracked progress (bytes == total -> OK). */
    static const test_client_t c2b[] = {
        { "10.0.0.6", true, true, "RETR", "/game/data.pkg", 1000, 1000 },
    };
    feed(&ui, 4500000ull, c2b, 1);
    static const test_client_t c3[] = {
        { "10.0.0.6", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 5000000ull, c3, 1);
    CHECK(ui.hist_count == 1);
    opftp_ui_hist_t* h = &ui.hist[ui.hist_head];
    CHECK_STR(h->op, "RETR");
    CHECK_STR(h->path, "/game/data.pkg");
    CHECK(h->bytes == 1000 && h->total == 1000);
    CHECK(h->result == OPFTP_UI_H_OK);
    CHECK(ui.session_xfers == 1 && ui.session_bytes == 1000);
    CHECK(!trk_of(&ui, "10.0.0.6")->xfer_active);

    /* partial completion -> ABORTED */
    static const test_client_t c4[] = {
        { "10.0.0.6", true, true, "STOR", "/partial.bin", 0, 1000 },
    };
    feed(&ui, 6000000ull, c4, 1);
    static const test_client_t c5[] = {
        { "10.0.0.6", true, true, "STOR", "/partial.bin", 300, 1000 },
    };
    feed(&ui, 7000000ull, c5, 1);
    static const test_client_t c6[] = {
        { "10.0.0.6", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 8000000ull, c6, 1);
    CHECK(ui.hist_count == 2);
    h = &ui.hist[ui.hist_head];
    CHECK_STR(h->op, "STOR");
    CHECK(h->bytes == 300 && h->result == OPFTP_UI_H_ABORTED);
}

static void test_disconnect(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 9000000ull);
    static const test_client_t c0[] = {
        { "10.0.0.7", true, true, "RETR", "/big.bin", 100, 1000 },
    };
    feed(&ui, 9000000ull, c0, 1);
    CHECK(ui.ev_count == 1);                    /* connected */

    /* client vanishes mid-transfer */
    feed(&ui, 10000000ull, 0, 0);
    CHECK(ui.ev_count == 2);
    CHECK(ui.events[ui.ev_head].warn);
    CHECK(strstr(ui.events[ui.ev_head].text, "CLIENT DISCONNECTED") != 0);
    CHECK(ui.hist_count == 1);
    opftp_ui_hist_t* h = &ui.hist[ui.hist_head];
    CHECK_STR(h->op, "RETR");
    CHECK(h->result == OPFTP_UI_H_ERROR);
    CHECK(trk_of(&ui, "10.0.0.7") == 0);        /* track slot freed */

    /* idle client vanishes: disconnect event, no history entry */
    static const test_client_t c1[] = {
        { "10.0.0.8", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 11000000ull, c1, 1);
    feed(&ui, 12000000ull, 0, 0);
    CHECK(ui.hist_count == 1);                  /* still just the ERROR one */
    CHECK(ui.ev_count == 4);                    /* connect, disconnect ×2 */
}

static void test_event_ring(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 13000000ull);
    for (int i = 0; i < 5; i++) {
        static const test_client_t c1[] = {
            { "10.0.0.9", false, false, 0, 0, 0, 0 },
        };
        feed(&ui, 13000000ull + (uint64_t)i * 1000, c1, 1);
        feed(&ui, 13000000ull + (uint64_t)i * 1000 + 500, 0, 0);
    }
    CHECK(ui.ev_count == OPFTP_UI_MAX_EVENTS);  /* capped at 4 */
    CHECK(strstr(ui.events[ui.ev_head].text, "10.0.0.9") != 0); /* newest */
}

static void test_history_ring(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 14000000ull);
    static const test_client_t c0[] = {
        { "10.0.0.10", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 14000000ull, c0, 1);
    for (int i = 0; i < 50; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/x/f%02d.bin", i);
        test_client_t tx = { "10.0.0.10", true, true, "RETR",
                             path, 0, 100 };
        test_client_t tf = { "10.0.0.10", true, true, "RETR",
                             path, 100, 100 };
        test_client_t tc = { "10.0.0.10", true, false, 0, 0, 0, 0 };
        feed(&ui, 14000000ull + (uint64_t)i * 300000 + 100000, &tx, 1);
        feed(&ui, 14000000ull + (uint64_t)i * 300000 + 200000, &tf, 1);
        feed(&ui, 14000000ull + (uint64_t)i * 300000 + 300000, &tc, 1);
    }
    CHECK(ui.hist_count == OPFTP_UI_MAX_HIST);  /* capped at 48 */
    CHECK(ui.session_xfers == 50);
    CHECK(ui.session_bytes == 5000);
    CHECK_STR(ui.hist[ui.hist_head].path, "/x/f49.bin");   /* newest */
}

/* ---------------- navigation ---------------- */

static void test_navigation(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 15000000ull);
    static const test_client_t c[] = {
        { "10.0.0.11", true, true,  "RETR", "/a.bin", 0, 10 },
        { "10.0.0.12", true, false, 0, 0, 0, 0 },
    };
    feed(&ui, 15000000ull, c, 2);

    /* STATUS selection covers only xfer-active clients */
    opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_status == 0);                  /* clamped at 0 (1 active) */
    for (int i = 0; i < 5; i++) opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_status == 0);                  /* still clamped */
    CHECK(opftp_ui_sel_to_client(&ui, 0) == 0); /* -> snapshot client 0 */
    opftp_ui_clear_sel(&ui);
    CHECK(ui.sel_status == -1);

    /* view switching wraps and resets selection/detail */
    opftp_ui_open_detail(&ui);                  /* no selection: no-op */
    CHECK(ui.detail.kind == OPFTP_UI_D_NONE);
    opftp_ui_switch_view(&ui, 1);
    CHECK(ui.view == OPFTP_UI_V_TRANSFERS);
    opftp_ui_switch_view(&ui, 1);
    CHECK(ui.view == OPFTP_UI_V_CLIENTS);
    opftp_ui_switch_view(&ui, 1);
    CHECK(ui.view == OPFTP_UI_V_SETTINGS);
    opftp_ui_switch_view(&ui, 1);               /* wraps */
    CHECK(ui.view == OPFTP_UI_V_STATUS);
    opftp_ui_switch_view(&ui, -1);              /* and backwards */
    CHECK(ui.view == OPFTP_UI_V_SETTINGS);
    opftp_ui_switch_view(&ui, -1);              /* SETTINGS -> CLIENTS */
    CHECK(ui.view == OPFTP_UI_V_CLIENTS);

    /* CLIENTS view: 2 rows, selection mapping is identity */
    opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_cli == 0);
    opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_cli == 1);
    opftp_ui_sel_move(&ui, 1);                  /* clamped at 1 */
    CHECK(ui.sel_cli == 1);
    CHECK(opftp_ui_sel_to_client(&ui, 1) == 1);
    opftp_ui_open_detail(&ui);
    CHECK(ui.detail.kind == OPFTP_UI_D_CLIENT && ui.detail.idx == 1);

    /* switching views closes an open detail overlay */
    opftp_ui_switch_view(&ui, 1);               /* CLIENTS -> SETTINGS */
    CHECK(ui.view == OPFTP_UI_V_SETTINGS);
    CHECK(ui.detail.kind == OPFTP_UI_D_NONE);

    /* TRANSFERS view: empty history keeps the selection at -1 */
    opftp_ui_switch_view(&ui, -2);              /* SETTINGS -> TRANSFERS */
    CHECK(ui.view == OPFTP_UI_V_TRANSFERS);
    CHECK(ui.sel_hist == -1);                   /* reset on switch */
    opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_hist == -1);                   /* nothing to select */
    CHECK(ui.hist_count == 0);                  /* nothing completed yet */
    opftp_ui_open_detail(&ui);                  /* empty list: no-op */
    CHECK(ui.detail.kind == OPFTP_UI_D_NONE);
}

static void test_clamp_scroll(void)
{
    CHECK(opftp_ui_clamp_scroll(5, 0, 8, 10) == 0);
    CHECK(opftp_ui_clamp_scroll(9, 0, 8, 10) == 2);
    CHECK(opftp_ui_clamp_scroll(2, 5, 8, 10) == 2);   /* sel < scroll */
    CHECK(opftp_ui_clamp_scroll(0, 0, 8, 0) == 0);    /* empty list */
    CHECK(opftp_ui_clamp_scroll(-1, 0, 8, 10) == 0);
}

static void test_edit(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 16000000ull);
    CHECK(ui.edit.field == OPFTP_UI_EDIT_NONE);
    CHECK(ui.sel_settings == -1);

    /* begin with initial text, type, backspace, commit */
    opftp_ui_edit_begin(&ui, OPFTP_UI_EDIT_ROOT, "/dev_hdd0");
    CHECK(ui.edit.field == OPFTP_UI_EDIT_ROOT);
    CHECK_STR(ui.edit.buf, "/dev_hdd0");
    CHECK(opftp_ui_edit_type(&ui, "/game") == 0);
    CHECK_STR(ui.edit.buf, "/dev_hdd0/game");
    opftp_ui_edit_backspace(&ui);
    CHECK_STR(ui.edit.buf, "/dev_hdd0/gam");
    const char* out = opftp_ui_edit_commit(&ui);
    CHECK_STR(out, "/dev_hdd0/gam");
    CHECK(ui.edit.field == OPFTP_UI_EDIT_NONE);

    /* typing without an active edit is rejected */
    CHECK(opftp_ui_edit_type(&ui, "x") == -EINVAL);

    /* multi-byte backspace removes a whole UTF-8 char */
    opftp_ui_edit_begin(&ui, OPFTP_UI_EDIT_ROOT, 0);
    /* "/a—·b" — split the literal so the hex escapes don't eat the
     * following hex-digit chars ('b' is a hex digit) */
    CHECK(opftp_ui_edit_type(&ui, "/a\xE2\x80\x94\xC2\xB7" "b") == 0);
    CHECK(ui.edit.len == 8);
    opftp_ui_edit_backspace(&ui);
    CHECK_STR(ui.edit.buf, "/a\xE2\x80\x94\xC2\xB7");
    opftp_ui_edit_backspace(&ui);
    CHECK_STR(ui.edit.buf, "/a\xE2\x80\x94");
    opftp_ui_edit_backspace(&ui);
    CHECK_STR(ui.edit.buf, "/a");
    opftp_ui_edit_cancel(&ui);
    CHECK(ui.edit.field == OPFTP_UI_EDIT_NONE);
    CHECK(ui.edit.len == 0);

    /* max length is enforced */
    opftp_ui_edit_begin(&ui, OPFTP_UI_EDIT_ROOT, 0);
    char big[OPFTP_UI_EDIT_MAX];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    CHECK(opftp_ui_edit_type(&ui, big) == 0);
    CHECK(ui.edit.len == OPFTP_UI_EDIT_MAX - 1);
    CHECK(opftp_ui_edit_type(&ui, "y") == -ENOSPC);
    opftp_ui_edit_cancel(&ui);

    /* settings selection: 5 rows, clamped */
    opftp_ui_switch_view(&ui, -1);              /* STATUS -> SETTINGS */
    CHECK(ui.view == OPFTP_UI_V_SETTINGS);
    CHECK(ui.sel_settings == -1);
    opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_settings == 0);
    opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_settings == 1);                /* the ROOT row */
    for (int i = 0; i < 5; i++) opftp_ui_sel_move(&ui, 1);
    CHECK(ui.sel_settings == OPFTP_UI_SETTINGS_ROWS - 1);
    opftp_ui_clear_sel(&ui);
    CHECK(ui.sel_settings == -1);
}

static void test_event(void)
{
    opftp_ui_t ui;
    opftp_ui_init(&ui, 17000000ull);
    opftp_ui_event(&ui, "ROOT NOT CHANGED", true, 17000000ull);
    CHECK(ui.ev_count == 1);
    CHECK(ui.events[ui.ev_head].warn);
    CHECK_STR(ui.events[ui.ev_head].text, "ROOT NOT CHANGED");
}

int main(void)
{
    test_init();
    test_fmt();
    test_connect_and_login();
    test_transfer_lifecycle();
    test_disconnect();
    test_event_ring();
    test_history_ring();
    test_navigation();
    test_clamp_scroll();
    test_edit();
    test_event();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
