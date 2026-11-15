// C-only diagnostic. No picture is expected; returns after half a second.
#include "probe_runtime.h"
#include <switch.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    wwhd_probe_log("[boot] main reached; startup ready=%d\n", wwhd_probe_ready());
    if (!wwhd_probe_ready()) {
        wwhd_probe_log("[boot] initialization FAILED; returning without Vulkan\n");
        return 1;
    }
#ifdef WWHD_PROBE_POSIX
    wwhd_probe_log("[boot] POSIX test: calling errno accessor\n");
    errno = 0;
    wwhd_probe_log("[boot] POSIX test: errno accessor OK; calling open\n");
    const int fd = open("sdmc:/wwhd-switch2-posix-test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    const int open_error = errno;
    wwhd_probe_log("[boot] POSIX open fd=%d errno=%d\n", fd, open_error);
    if (fd >= 0) {
        wwhd_probe_log("[boot] POSIX test: calling write\n");
        const char text[] = "WWHD Switch 2 POSIX test\n";
        const ssize_t written = write(fd, text, sizeof(text) - 1);
        const int write_error = errno;
        wwhd_probe_log("[boot] POSIX write=%ld errno=%d\n", (long)written, write_error);
        wwhd_probe_log("[boot] POSIX test: calling close\n");
        const int closed = close(fd);
        const int close_error = errno;
        wwhd_probe_log("[boot] POSIX close=%d errno=%d\n", closed, close_error);
    }
#endif
    svcSleepThread(500000000);
    wwhd_probe_log("[boot] main returning normally\n");
    return 0;
}
