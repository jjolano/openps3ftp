/*
 * OpenPS3FTP web console — third renderer of the shared UI model
 * (app/ui.c).  Polls GET /api/state once a second, renders four
 * views, posts /api/cmd for the settings view.
 *
 * ES5 only (XMLHttpRequest, no fetch/arrows/template literals) so it
 * runs in the PS3 stock web browser and in any modern browser.
 */
var App = (function () {
  "use strict";

  var POLL_MS = 1000;
  var RETRY_MS = 2000;
  var VIEWS = ["STATUS", "TRANSFERS", "CLIENTS", "SETTINGS"];
  var RESULT = ["OK", "ABORTED", "ERROR"];

  var state = null;      /* last /api/state payload */
  var raw = "";          /* last raw JSON, for change detection */
  var view = 0;
  var sel = -1;
  var detail = null;     /* { kind: 'hist'|'client', idx: n } */
  var offline = false;

  /* ---- format helpers (mirror opftp_ui_fmt_size/rate/dur/idle) ---- */

  function fmtSize(b) {
    if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GB";
    if (b >= 1048576)    return (b / 1048576).toFixed(1) + " MB";
    if (b >= 1024)       return (b / 1024).toFixed(1) + " KB";
    return b + " B";
  }
  function fmtRate(bps) {
    if (bps >= 1073741824) return (bps / 1073741824).toFixed(1) + " GB/s";
    if (bps >= 1048576)    return (bps / 1048576).toFixed(1) + " MB/s";
    if (bps >= 1024)       return (bps / 1024).toFixed(1) + " KB/s";
    return Math.round(bps) + " B/s";
  }
  function pad2(n) { return (n < 10 ? "0" : "") + n; }
  function fmtDur(s) {
    return pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor(s / 60) % 60) + ":" + pad2(s % 60);
  }
  function fmtIdle(s) { return pad2(Math.floor(s / 60)) + ":" + pad2(s % 60); }
  function hhmm(h, m) { return pad2(h) + ":" + pad2(m); }

  /* ---- dom helpers ---- */

  function el(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;")
                    .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }
  function rows(list) {
    if (!list || !list.length) {
      return '<tr class="empty"><td>none</td></tr>';
    }
    var out = "", i;
    for (i = 0; i < list.length; i++) {
      out += '<tr class="click' + (sel === i ? ' sel' : '') + '" '
           + 'onclick="App.pick(' + i + ')"><td>' + list[i] + "</td></tr>";
    }
    return out;
  }

  /* ---- visual helpers (markup only) ---- */

  /* rate -> traffic-light color class */
  function rateClass(bps) {
    if (bps >= 1048576) return "ok";
    if (bps >= 102400)  return "warn";
    return "dim";
  }
  function badge(text, cls) {
    return '<span class="badge ' + cls + '">' + text + "</span>";
  }
  function resultBadge(result) {
    return badge(RESULT[result],
                 result === 0 ? "ok" : (result === 1 ? "warn" : "err"));
  }
  /* progress bar as a 2-cell table; pct 0..100, -1 = indeterminate */
  function progBar(pct, warn) {
    var w = (pct < 0) ? 0 : (pct > 100 ? 100 : Math.round(pct));
    return '<table class="prog" cellspacing="0" cellpadding="0"><tr>'
         + '<td class="fill' + (warn ? " warn" : "") + '" width="' + w + '%"></td>'
         + "<td></td></tr></table>";
  }
  function pct(bytes, total) {
    if (!total) return -1;
    return Math.round((bytes / total) * 100);
  }
  function statCard(label, value, sub) {
    return '<span class="stat"><span class="lb">' + label + '</span><br>'
         + '<span class="vl">' + value + "</span>"
         + (sub ? '<br><span class="sb">' + sub + "</span>" : "")
         + "</span>";
  }
  /* zebra class for data rows, keeps click/sel classes intact */
  function rowCls(i, selIdx) {
    return "click" + (i % 2 ? " zeb" : "") + (selIdx === i ? " sel" : "");
  }

  /* ---- rendering ---- */

  function renderHeader() {
    var conn = el("conn");
    if (offline) {
      conn.innerHTML = "&#9679; OFFLINE";
      conn.className = "conn off";
    } else if (state) {
      conn.innerHTML = state.running ? "&#9679; ONLINE" : "&#9679; STOPPED";
      conn.className = "conn" + (state.running ? "" : " off");
    }
    if (state && state.version) el("ver").innerHTML = state.version;
    el("vname").innerHTML = VIEWS[view];
    el("vnum").innerHTML = (view + 1) + "/4";
    var dots = "", i;
    for (i = 0; i < 4; i++) {
      dots += '<span class="' + (i === view ? "on" : "") + '">'
            + (i === view ? "&#9679;" : "&#9675;") + "</span>";
    }
    var vd = el("vdots");
    if (vd) vd.innerHTML = dots;
    var tabs = document.getElementsByClassName("tab");
    for (i = 0; i < tabs.length; i++) {
      tabs[i].className = "tab" + (i === view ? " act" : "");
    }
  }

  function renderStrip() {
    var html = "", i;
    if (state && state.events) {
      for (i = 0; i < state.events.length; i++) {
        var e = state.events[i];
        html += '<span class="ev' + (e.warn ? " warn" : "") + '">'
              + '<span class="t">' + hhmm(e.hh, e.mm) + "</span> "
              + esc(e.text) + "</span>";
      }
    }
    el("cstrip").innerHTML = html || '<span class="dim">no events</span>';
  }

  function renderStatus() {
    var s = state, html = "", i;
    if (!s) {
      return '<div class="panel"><h2>SERVER</h2><table class="kv">'
           + '<tr><td class="k">state</td><td class="v dim">'
           + (offline ? "console unreachable — is the plugin loaded?"
                      : "waiting for first poll")
           + "</td></tr></table></div>";
    }

    /* big stats row */
    var ncli = 0, nact = 0;
    for (i = 0; i < s.clients.length; i++) {
      ncli++;
      if (s.clients[i].xfer_active) nact++;
    }
    html += '<div class="panel"><h2>SERVER</h2><div class="stats">'
      + statCard("STATE",
          s.running ? '<span class="ok">RUNNING</span>' : '<span class="err">STOPPED</span>',
          "port " + s.port)
      + statCard("UPTIME", fmtDur(s.uptime_s), "v" + esc(s.version))
      + statCard("SESSION", fmtSize(s.session.bytes),
          s.session.xfers + " transfer" + (s.session.xfers === 1 ? "" : "s"))
      + statCard("CLIENTS", ncli,
          nact ? nact + " transferring" : "none active")
      + "</div>"
      + '<table class="kv">'
      + '<tr><td class="k">root</td><td class="v">' + esc(s.root) + "</td></tr>"
      + '<tr><td class="k">workers</td><td class="v">' + s.workers + "</td></tr>"
      + "</table></div>";

    /* active transfers: clients currently transferring */
    var act = [], n = 0;
    for (i = 0; i < s.clients.length; i++) {
      if (s.clients[i].xfer_active) act[act.length] = s.clients[i];
    }
    html += '<div class="panel"><h2>ACTIVE TRANSFERS</h2><table class="grid">'
      + "<tr><th>PEER</th><th>OP</th><th>PATH</th>"
      + "<th>PROGRESS</th><th>BYTES</th><th>RATE</th></tr>";
    if (!act.length) {
      html += '<tr class="empty"><td colspan="6">no active transfers</td></tr>';
    } else {
      for (i = 0; i < act.length; i++) {
        var c = act[i];
        html += '<tr class="' + rowCls(n, sel) + '" onclick="App.pick(' + n + ')">'
          + "<td>" + esc(c.peer) + "</td>"
          + "<td>" + badge(esc(c.op), "info") + "</td>"
          + "<td>" + esc(c.path) + "</td>"
          + "<td>" + progBar(pct(c.bytes, c.total), false) + "</td>"
          + '<td class="num">' + fmtSize(c.bytes) + " / " + fmtSize(c.total) + "</td>"
          + '<td class="num ' + rateClass(c.rate) + '">' + fmtRate(c.rate) + "</td></tr>";
        n++;
      }
    }
    return html + "</table></div>";
  }

  function renderTransfers() {
    var s = state, html = '<div class="panel"><h2>TRANSFER HISTORY</h2>'
      + '<table class="grid"><tr><th>TIME</th><th>OP</th><th>PATH</th>'
      + "<th>BYTES</th><th>RESULT</th></tr>";
    if (!s || !s.history.length) {
      html += '<tr class="empty"><td colspan="5">'
            + (offline ? "console unreachable" : "no transfers yet")
            + "</td></tr>";
    } else {
      var i;
      for (i = 0; i < s.history.length; i++) {
        var h = s.history[i];
        html += '<tr class="' + rowCls(i, sel) + '" onclick="App.pick(' + i + ')">'
          + '<td class="num">' + hhmm(h.hh, h.mm) + "</td>"
          + "<td>" + badge(esc(h.op), "info") + "</td>"
          + "<td>" + esc(h.path) + "</td>"
          + '<td class="num">' + fmtSize(h.bytes) + " / " + fmtSize(h.total) + "</td>"
          + "<td>" + resultBadge(h.result) + "</td></tr>";
      }
    }
    return html + "</table></div>";
  }

  function renderClients() {
    var s = state, html = '<div class="panel"><h2>CLIENTS</h2>'
      + '<table class="grid"><tr><th>PEER</th><th>USER</th><th>CWD</th>'
      + "<th>TRANSFER</th><th>RATE</th><th>IDLE</th></tr>";
    if (!s || !s.clients.length) {
      html += '<tr class="empty"><td colspan="6">'
            + (offline ? "console unreachable" : "no clients connected")
            + "</td></tr>";
    } else {
      var i;
      for (i = 0; i < s.clients.length; i++) {
        var c = s.clients[i];
        var tr = c.xfer_active
          ? esc(c.op) + " " + esc(c.path) + "<br>"
            + progBar(pct(c.bytes, c.total), false)
            + '<span class="dim num">' + fmtSize(c.bytes) + " / "
            + fmtSize(c.total) + "</span>"
          : '<span class="dim">idle</span>';
        html += '<tr class="' + rowCls(i, sel) + '" onclick="App.pick(' + i + ')">'
          + "<td>" + esc(c.peer) + "</td>"
          + "<td>" + esc(c.user) + "</td>"
          + "<td>" + esc(c.cwd) + "</td>"
          + "<td>" + tr + "</td>"
          + '<td class="num">'
          +   (c.xfer_active
          ?     '<span class="' + rateClass(c.rate) + '">' + fmtRate(c.rate) + "</span>"
          :     '<span class="dim">&mdash;</span>')
          +   "</td>"
          + '<td class="num">' + fmtIdle(c.idle_s) + "</td></tr>";
      }
    }
    return html + "</table></div>";
  }

  function renderSettings() {
    var s = state, running = s ? s.running : false;
    return '<div class="panel"><h2>SETTINGS</h2><table class="kv">'
      + '<tr><td class="k">root path</td><td class="v">'
      +   '<input type="text" id="rootin" value="' + esc(s ? s.root : "") + '"'
      +   (offline ? ' disabled="disabled"' : "") + '> '
      +   '<button onclick="App.setRoot()"' + (offline ? ' disabled="disabled"' : "")
      +   '>APPLY</button></td></tr>'
      + '<tr><td class="k">server</td><td class="v">'
      +   (running
      ?   '<button class="danger" onclick="App.stop()">STOP SERVER</button>'
      :   '<span class="err">stopped</span>' + (offline ? " — console unreachable" : ""))
      +   "</td></tr></table></div>"
      + '<div class="panel"><h2>ABOUT</h2><table class="kv">'
      + '<tr><td class="k">web console</td><td class="v">OpenPS3FTP '
      +   esc(s ? s.version : "") + " &mdash; third renderer of the shared UI model"
      +   " (OSD, TUI, web). Root path changes and stop apply immediately.</td></tr>"
      + "</table></div>";
  }

  function renderDetail() {
    if (!detail || !state) return "";
    var html = '<div id="ov"><h2>' + (detail.kind === "hist" ? "TRANSFER" : "CLIENT")
      + ' <span class="close" onclick="App.back()">[X]</span></h2>'
      + '<table class="kv">';
    if (detail.kind === "hist") {
      var h = state.history[detail.idx];
      if (!h) return "";
      html += "<tr><td class=\"k\">op</td><td class=\"v\">" + badge(esc(h.op), "info") + "</td></tr>"
        + "<tr><td class=\"k\">path</td><td class=\"v\">" + esc(h.path) + "</td></tr>"
        + "<tr><td class=\"k\">progress</td><td class=\"v\">"
        + progBar(pct(h.bytes, h.total), h.result !== 0)
        + '<span class="num">' + fmtSize(h.bytes) + " / " + fmtSize(h.total) + "</span>"
        + "</td></tr>"
        + '<tr><td class="k">result</td><td class="v">' + resultBadge(h.result) + "</td></tr>"
        + "<tr><td class=\"k\">time</td><td class=\"v\">" + hhmm(h.hh, h.mm) + "</td></tr>";
    } else {
      var c = state.clients[detail.idx];
      if (!c) return "";
      html += "<tr><td class=\"k\">peer</td><td class=\"v\">" + esc(c.peer) + "</td></tr>"
        + "<tr><td class=\"k\">user</td><td class=\"v\">" + esc(c.user) + "</td></tr>"
        + "<tr><td class=\"k\">cwd</td><td class=\"v\">" + esc(c.cwd) + "</td></tr>"
        + "<tr><td class=\"k\">login</td><td class=\"v\">"
        +   (c.logged_in ? badge("LOGGED IN", "ok") : badge("NOT LOGGED IN", "warn"))
        +   "</td></tr>"
        + "<tr><td class=\"k\">transfer</td><td class=\"v\">"
        +   (c.xfer_active
        ?     badge(esc(c.op), "info") + " " + esc(c.path) + "<br>"
        +     progBar(pct(c.bytes, c.total), false)
        +     '<span class="num">' + fmtSize(c.bytes) + " / " + fmtSize(c.total)
        +     ' at <span class="' + rateClass(c.rate) + '">' + fmtRate(c.rate)
        +     "</span></span>"
        :     '<span class="dim">idle ' + fmtIdle(c.idle_s) + "</span>")
        +   "</td></tr>";
    }
    return html + "</table></div>";
  }

  function render() {
    var html = "";
    if (offline) {
      html += '<div id="banner">&#9888; Console offline &mdash; showing last known state; retrying&hellip;</div>';
    } else if (state && !state.running) {
      html += '<div id="banner">Server stopped &mdash; restart the plugin or reboot to resume.</div>';
    }
    if (view === 0) html += renderStatus();
    else if (view === 1) html += renderTransfers();
    else if (view === 2) html += renderClients();
    else html += renderSettings();
    el("view").innerHTML = html;
    el("ovroot").innerHTML = renderDetail();
    renderHeader();
    renderStrip();
  }

  /* ---- actions ---- */

  function go(v) {
    if (v < 0 || v > 3) return;
    view = v;
    sel = -1;
    detail = null;
    render();
  }
  function selMove(d) {
    var n = 0, i;
    if (!state) return;
    if (view === 1) n = state.history.length;
    else if (view === 2) n = state.clients.length;
    else {
      for (i = 0; i < state.clients.length; i++) {
        if (state.clients[i].xfer_active) n++;
      }
    }
    if (!n) { sel = -1; return; }
    sel = (sel < 0 ? (d > 0 ? 0 : n - 1) : sel + d);
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    render();
  }
  function pick(i) {
    /* history + clients views: direct index.  status view: active
       transfers, so map to the underlying client. */
    if (view === 1) { detail = { kind: "hist", idx: i }; }
    else if (view === 2) { detail = { kind: "client", idx: i }; }
    else if (view === 0) {
      var k = 0, c = -1;
      for (var j = 0; j < state.clients.length; j++) {
        if (state.clients[j].xfer_active) {
          if (k === i) { c = j; break; }
          k++;
        }
      }
      if (c >= 0) detail = { kind: "client", idx: c };
    }
    if (detail) render();
  }
  function back() { detail = null; render(); }

  function post(body, done) {
    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/cmd", true);
    xhr.onreadystatechange = function () {
      if (xhr.readyState === 4 && done) done(xhr.status === 200 ? xhr.responseText : null);
    };
    xhr.send(body);
  }

  function setRoot() {
    var inp = el("rootin");
    if (!inp) return;
    post('{"action":"root","path":"' + inp.value.replace(/"/g, '\\"') + '"}',
         function (res) { if (res) render(); });
  }
  function stop() {
    if (!window.confirm("Stop the FTP server?")) return;
    post('{"action":"stop"}', function (res) { if (res) render(); });
  }

  /* ---- polling ---- */

  function poll() {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/api/state", true);
    xhr.onreadystatechange = function () {
      if (xhr.readyState !== 4) return;
      if (xhr.status === 200 && xhr.responseText) {
        if (xhr.responseText === raw) return;   /* nothing changed */
        raw = xhr.responseText;
        state = JSON.parse(xhr.responseText);
        offline = false;
      } else {
        offline = true;
      }
      render();
    };
    xhr.send();
    window.setTimeout(poll, offline ? RETRY_MS : POLL_MS);
  }

  function tick() {
    var d = new Date();
    el("clock").innerHTML = pad2(d.getHours()) + ":" + pad2(d.getMinutes());
    window.setTimeout(tick, 15000);
  }

  /* ---- keyboard (parity with TUI nav) ---- */

  document.onkeydown = function (e) {
    var k = e.keyCode;
    if (k === 37 || k === 39) { go(view + (k === 39 ? 1 : -1)); e.preventDefault(); }
    else if (k === 38 || k === 40) { selMove(k === 40 ? 1 : -1); e.preventDefault(); }
    else if (k === 13) { if (sel >= 0) pick(sel); }
    else if (k === 27) { back(); }
  };

  /* ---- init ---- */

  function init() {
    render();
    poll();
    tick();
  }
  if (document.readyState === "complete") init();
  else window.onload = init;

  return { go: go, pick: pick, back: back, setRoot: setRoot, stop: stop };
})();
