/*
 * OpenPS3FTP — host harness for the vsh-plugin web console.
 *
 * Runs the FTP server + shared UI model + the plugin's httpd.c on
 * Linux (POSIX sockets), serving the web assets from disk.  Lets us verify the web UI against the real server/model
 * with a desktop browser before touching a PS3.
 *
 * Build:  cmake -S plugin/host -B plugin/host/build && cmake --build plugin/host/build
 * Run:    plugin/host/build/openps3ftp-host  [port]  [webui-dir]
 *         (defaults: 2121 FTP / 8080 web / ../webui)
 */
#include "opftp.h"
#include "httpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* In-process self-test: start server + httpd, talk to /api/state and
 * /api/cmd over a real socket, print the JSON, exit 0 on success.
 * (The shell tool cannot hold background processes, so verification
 * must run inside one foreground command.) */
static int self_test(uint16_t web_port)
{
    opftp_server_t* s = opftp_server_create(NULL);
    if (!s) { fprintf(stderr, "create failed\n"); return 1; }
    fprintf(stderr, "[selftest] server created\n");
    opftp_server_set_port(s, 2121);
    opftp_server_set_root(s, "/");
    opftp_server_set_workers(s, 2);
    opftp_server_set_allow_stop(s, true);
    if (opftp_server_start(s) != 0) { fprintf(stderr, "start failed\n"); return 1; }
    fprintf(stderr, "[selftest] server started\n");

    opftp_httpd_t* h = opftp_httpd_create(s, "host-harness", web_port, "../app/webui");
    if (!h) { fprintf(stderr, "httpd failed\n"); return 1; }
    fprintf(stderr, "[selftest] httpd up\n");

    /* give the model poll loop a tick */
    usleep(1200000);
    fprintf(stderr, "[selftest] slept, connecting\n");

    int ok = 1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(web_port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    char req[512], buf[8192];
    int n;
    size_t got;

    /* read one response fully (loop: head and body may arrive in
     * separate segments; server closes the connection when done) */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }
    snprintf(req, sizeof(req),
             "GET /api/state HTTP/1.0\r\nHost: localhost\r\n\r\n");
    send(fd, req, (int)strlen(req), 0);
    got = 0;
    while ((n = (int)recv(fd, buf + got, sizeof(buf) - 1 - got, 0)) > 0)
        got += (size_t)n;
    buf[got] = 0;
    close(fd);
    printf("=== GET /api/state ===\n%.*s\n", (int)got, buf);
    if (!strstr(buf, "\"running\":true") || !strstr(buf, "\"events\":["))
        ok = 0;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }
    snprintf(req, sizeof(req),
             "POST /api/cmd HTTP/1.0\r\nHost: localhost\r\n"
             "Content-Length: 26\r\n\r\n{\"action\":\"root\",\"path\":\"/tmp\"}");
    send(fd, req, (int)strlen(req), 0);
    got = 0;
    while ((n = (int)recv(fd, buf + got, sizeof(buf) - 1 - got, 0)) > 0)
        got += (size_t)n;
    buf[got] = 0;
    close(fd);
    printf("=== POST /api/cmd root=/tmp ===\n%.*s\n", (int)got, buf);
    if (!strstr(buf, "\"ok\":true"))
        ok = 0;

    opftp_httpd_destroy(h);
    opftp_server_stop(s);
    opftp_server_destroy(s);
    printf(ok ? "SELFTEST PASS\n" : "SELFTEST FAIL\n");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc > 1 && !strcmp(argv[1], "--selftest"))
        return self_test(argc > 2 ? (uint16_t)atoi(argv[2]) : 8080);

    uint16_t web_port = 8080;
    const char* webui_dir = "../app/webui";
    if (argc > 1) web_port = (uint16_t)atoi(argv[1]);
    if (argc > 2) webui_dir = argv[2];

    opftp_server_t* s = opftp_server_create(NULL);
    if (!s) { fprintf(stderr, "create failed\n"); return 1; }
    opftp_server_set_port(s, 2121);
    opftp_server_set_root(s, "/");
    opftp_server_set_workers(s, 2);
    opftp_server_set_allow_stop(s, true);
    if (opftp_server_start(s) != 0) {
        fprintf(stderr, "start failed\n");
        return 1;
    }
    printf("FTP  : %s:%u\n", "0.0.0.0", (unsigned)opftp_server_bound_port(s));

    opftp_httpd_t* h = opftp_httpd_create(s, "host-harness", web_port, webui_dir);
    if (!h) { fprintf(stderr, "httpd failed\n"); return 1; }
    printf("HTTP : %s:%u (webui dir: %s)\n", "0.0.0.0", (unsigned)web_port, webui_dir);
    printf("Ctrl-C to quit\n");

    for (;;) sleep(3600);
    return 0;
}
