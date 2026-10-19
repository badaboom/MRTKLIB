/*------------------------------------------------------------------------------
 * utest_stream_serial.c : serial stream write byte count (#343)
 *
 * A pseudo-terminal pair stands in for the serial port: the slave side is
 * opened as an STR_SERIAL stream, the master side counts what arrives.
 * strwrite() must return the bytes written (writeserial() used to return 0
 * for every write), and strsum() / strstat() must see them as output.
 *
 * Explicit CHECK macro (not assert) so it is robust under -DNDEBUG.
 *-----------------------------------------------------------------------------*/
#define _XOPEN_SOURCE 600 /* posix_openpt, grantpt, unlockpt, ptsname */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mrtklib/rtklib.h"

#define NWRITE 10
#define NBYTE 100

static int fails = 0;

#define CHECK(cond, msg)                 \
    do {                                 \
        if (!(cond)) {                   \
            printf("FAIL: %s\n", (msg)); \
            fails++;                     \
        }                                \
    } while (0)

int main(void) {
    stream_t* s = (stream_t*)calloc(1, sizeof(stream_t));
    uint8_t buf[NBYTE], rbuf[4096];
    char path[256], msg[256], smsg[MAXSTRMSG] = "";
    const char* name;
    struct pollfd pfd;
    int master, i, ret, sum = 0, got = 0, inb, inr, outb, outr, stat;

    if (!s || (master = posix_openpt(O_RDWR | O_NOCTTY)) < 0 || grantpt(master) || unlockpt(master) ||
        !(name = ptsname(master)) || strncmp(name, "/dev/", 5)) {
        printf("FAIL: pty setup\n");
        return 1;
    }
    snprintf(path, sizeof(path), "%s:115200:8:n:1:off", name + 5); /* stropen adds /dev/ */

    strinit(s);
    if (!stropen(s, STR_SERIAL, STR_MODE_RW, path)) {
        printf("FAIL: stropen %s: %s\n", path, s->msg);
        return 1;
    }
    memset(buf, 'A', sizeof(buf));
    for (i = 0; i < NWRITE; i++) {
        ret = strwrite(s, buf, NBYTE);
        sum += ret;
    }
    strsum(s, &inb, &inr, &outb, &outr);
    stat = strstat(s, smsg);

    pfd.fd = master;
    pfd.events = POLLIN;
    while (got < NWRITE * NBYTE && poll(&pfd, 1, 500) > 0) {
        ret = (int)read(master, rbuf, sizeof(rbuf));
        if (ret <= 0) break;
        got += ret;
    }
    strclose(s);
    close(master);
    free(s);

    printf("strwrite sum=%d strsum outb=%d strstat=%d received=%d\n", sum, outb, stat, got);
    snprintf(msg, sizeof(msg), "pty received %d bytes, expected %d", got, NWRITE * NBYTE);
    CHECK(got == NWRITE * NBYTE, msg);
    snprintf(msg, sizeof(msg), "strwrite returned %d bytes in total, expected %d", sum, got);
    CHECK(sum == got, msg);
    snprintf(msg, sizeof(msg), "strsum outb=%d, expected %d", outb, got);
    CHECK(outb == got, msg);
    snprintf(msg, sizeof(msg), "strstat=%d after writes, expected 3 (active)", stat);
    CHECK(stat == 3, msg);

    printf("%s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
