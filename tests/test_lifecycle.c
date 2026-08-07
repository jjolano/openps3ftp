/*
 * C-side lifecycle tests: start/stop/restart, start/run_loop
 * exclusivity, graceful drain. Raw-socket checks against a running
 * server. Must pass under ASan and TSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "openps3ftp/openps3ftp.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static int connect_control(uint16_t port, char* greet, size_t greet_sz)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (connect(fd, (struct sockaddr*) &a, sizeof(a)) != 0)
        return -1;
    size_t off = 0;
    while (off + 1 < greet_sz) {
        ssize_t n = recv(fd, greet + off, 1, 0);
        if (n <= 0) break;
        off += (size_t) n;
        if (off >= 2 && greet[off - 2] == '\r' && greet[off - 1] == '\n')
            break;
    }
    greet[off] = '\0';
    return fd;
}

static void expect(int fd, const char* prefix)
{
    char buf[256];
    size_t off = 0;
    while (off + 1 < sizeof(buf)) {
        ssize_t n = recv(fd, buf + off, 1, 0);
        if (n <= 0) break;
        off += (size_t) n;
        if (off >= 2 && buf[off - 2] == '\r' && buf[off - 1] == '\n')
            break;
    }
    buf[off] = '\0';
    CHECK(strncmp(buf, prefix, strlen(prefix)) == 0);
}

struct runner_ctx {
    opftp_server_t* s;
    int stop_rc;
};

static void* stop_runner(void* p)
{
    struct runner_ctx* c = p;
    usleep(100000);
    c->stop_rc = opftp_server_stop(c->s);
    return NULL;
}

static void test_lifecycle(void)
{
    char tmpl[] = "/tmp/opftp_lc_XXXXXX";
    char* root = mkdtemp(tmpl);
    CHECK(root != NULL);
    if (!root) return;

    opftp_server_t* s = opftp_server_create(NULL);
    CHECK(s != NULL);
    opftp_server_set_port(s, 0);
    opftp_server_set_root(s, root);
    opftp_server_set_workers(s, 1);
    opftp_server_set_stop_timeout(s, 3);

    /* exclusivity: start, then run_loop must fail */
    CHECK(opftp_server_start(s) == 0);
    CHECK(opftp_server_run_loop(s) == -EALREADY);
    CHECK(opftp_server_start(s) == -EALREADY);

    uint16_t port = opftp_server_bound_port(s);
    CHECK(port != 0);

    /* basic session over raw socket */
    char greet[64];
    int fd = connect_control(port, greet, sizeof(greet));
    CHECK(fd >= 0);
    CHECK(strstr(greet, "220") == greet);
    if (fd >= 0) {
        const char* cmd = "USER u\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "331");
        cmd = "PASS p\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "230");
        cmd = "NOOP\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "200");
        close(fd);
    }

    /* stop + restart on the same object */
    CHECK(opftp_server_stop(s) == 0);
    CHECK(opftp_server_start(s) == 0);
    port = opftp_server_bound_port(s);
    CHECK(port != 0);
    fd = connect_control(port, greet, sizeof(greet));
    CHECK(fd >= 0);
    if (fd >= 0) {
        const char* cmd = "QUIT\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "221");
        close(fd);
    }

    CHECK(opftp_server_stop(s) == 0);
    opftp_server_destroy(s);

    /* run_loop mode: a helper thread stops it while run_loop blocks.
     * run_loop is exclusive with start, so use a fresh server and
     * never call start on it. */
    opftp_server_t* s2 = opftp_server_create(NULL);
    opftp_server_set_port(s2, 0);
    opftp_server_set_root(s2, root);

    pthread_t thr;
    struct runner_ctx ctx = { s2, -1 };
    CHECK(pthread_create(&thr, NULL, stop_runner, &ctx) == 0);
    CHECK(opftp_server_run_loop(s2) == 0);
    pthread_join(thr, NULL);
    CHECK(ctx.stop_rc == 0);
    opftp_server_destroy(s2);

    rmdir(root);
}

/* Snapshot API: state + client registry + live transfer progress. */
static void test_snapshot(void)
{
    char tmpl[] = "/tmp/opftp_snap_XXXXXX";
    char* root = mkdtemp(tmpl);
    CHECK(root != NULL);
    if (!root) return;
    /* large enough to keep the worker blocked on the socket buffer
     * until the test drains it (so the snapshot sees it in-flight) */
    char payload[4 * 1024 * 1024];
    memset(payload, 'x', sizeof(payload));
    char fpath[1024];
    snprintf(fpath, sizeof(fpath), "%s/data.bin", root);
    FILE* f = fopen(fpath, "wb");
    CHECK(f != NULL);
    if (f) { fwrite(payload, 1, sizeof(payload), f); fclose(f); }

    opftp_server_t* s = opftp_server_create(NULL);
    CHECK(s != NULL);
    opftp_server_set_port(s, 0);
    opftp_server_set_root(s, root);
    opftp_server_set_workers(s, 2);
    CHECK(opftp_server_start(s) == 0);

    uint16_t port = opftp_server_bound_port(s);

    /* the reactor publishes the first snapshot asynchronously; poll
     * until started so the cache reflects a running server */
    opftp_snapshot_t snap;
    int ready = 0;
    for (int i = 0; i < 100; i++) {
        usleep(20000);
        opftp_server_snapshot(s, &snap);
        if (snap.started && snap.port == port) { ready = 1; break; }
    }
    CHECK(ready);
    CHECK(snap.port == port);
    CHECK(strcmp(snap.root, root) == 0);
    CHECK(snap.workers == 2);
    CHECK(snap.num_clients == 0);

    /* connect + login: the snapshot should show the client */
    char greet[64];
    int fd = connect_control(port, greet, sizeof(greet));
    CHECK(fd >= 0);
    if (fd >= 0) {
        const char* cmd = "USER u\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "331");
        cmd = "PASS p\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "230");

        /* poll until the reactor has refreshed the snapshot */
        int seen = 0;
        for (int i = 0; i < 100; i++) {
            usleep(20000);
            opftp_server_snapshot(s, &snap);
            if (snap.num_clients == 1 && snap.clients[0].logged_in) {
                seen = 1;
                break;
            }
        }
        CHECK(seen);
        CHECK(strcmp(snap.clients[0].user, "u") == 0);
        CHECK(strcmp(snap.clients[0].cwd, "/") == 0);
        CHECK(!snap.clients[0].xfer_active);

        /* PASV + RETR: snapshot must show the active transfer and
         * progress bytes. The client does NOT drain the data socket
         * until the snapshot shows the transfer, so the worker stays
         * blocked mid-transfer. */
        cmd = "TYPE I\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "200");
        cmd = "PASV\r\n";
        send(fd, cmd, strlen(cmd), 0);
        char pasv[128];
        size_t off = 0;
        while (off + 1 < sizeof(pasv)) {
            ssize_t n = recv(fd, pasv + off, 1, 0);
            if (n <= 0) break;
            off += (size_t) n;
            if (off >= 2 && pasv[off - 2] == '\r' && pasv[off - 1] == '\n')
                break;
        }
        pasv[off] = '\0';
        int p1 = 0, p2 = 0;
        CHECK(sscanf(pasv, "227 Entering Passive Mode (%*d,%*d,%*d,%*d,%d,%d)", &p1, &p2) == 2);
        int dport = p1 * 256 + p2;

        int dfd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in da = {0};
        da.sin_family = AF_INET;
        da.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        da.sin_port = htons((unsigned short) dport);
        CHECK(connect(dfd, (struct sockaddr*) &da, sizeof(da)) == 0);

        cmd = "RETR data.bin\r\n";
        send(fd, cmd, strlen(cmd), 0);
        expect(fd, "150");

        /* the transfer runs on a worker; poll the snapshot for progress */
        int progress = 0;
        for (int i = 0; i < 200; i++) {
            usleep(10000);
            opftp_server_snapshot(s, &snap);
            if (snap.num_clients == 1 && snap.clients[0].xfer_active) {
                if (snap.clients[0].xfer_bytes > 0 &&
                    snap.clients[0].xfer_total == sizeof(payload)) {
                    progress = 1;
                    break;
                }
            }
        }
        CHECK(progress);
        CHECK(strcmp(snap.clients[0].xfer_op, "RETR") == 0);
        CHECK(strstr(snap.clients[0].xfer_path, "data.bin") != NULL);

        /* drain the transfer */
        char buf[8192];
        size_t got = 0;
        while (got < sizeof(payload)) {
            ssize_t n = recv(dfd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            got += (size_t) n;
        }
        close(dfd);
        CHECK(got == sizeof(payload));
        expect(fd, "226");

        /* after QUIT the client disappears from the snapshot */
        cmd = "QUIT\r\n";
        send(fd, cmd, strlen(cmd), 0);
        close(fd);
        int gone = 0;
        for (int i = 0; i < 100; i++) {
            usleep(20000);
            opftp_server_snapshot(s, &snap);
            if (snap.num_clients == 0) {
                gone = 1;
                break;
            }
        }
        CHECK(gone);
    }

    CHECK(opftp_server_stop(s) == 0);
    opftp_server_destroy(s);
    unlink(fpath);
    rmdir(root);
}

int main(void)
{
    test_lifecycle();
    test_snapshot();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("lifecycle tests passed\n");
    return 0;
}
