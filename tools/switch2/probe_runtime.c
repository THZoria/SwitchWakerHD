// Shared early logging for the C startup tests and Vulkan probe. The C tests
// skip graphics/applet services; the Vulkan host logs before starting them.
#include "probe_runtime.h"
#include <switch.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/reent.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#if defined(WWHD_GAME_HOST)
#define LOG_FS_PATH "/switch/wwhd-switch2/wwhd.log"
#define VARIANT "libounce / WWHD full runtime prototype"
#elif defined(WWHD_PROBE_RUNTIME_HOST)
#define LOG_FS_PATH "/wwhd-switch2-runtime.log"
#define VARIANT "libounce / WWHD RPX loader and guest memory"
#elif defined(WWHD_PROBE_VULKAN_HOST)
#define LOG_FS_PATH "/wwhd-switch2-first-frame.log"
#define VARIANT "libounce / Vulkan host"
#elif defined(WWHD_PROBE_CORE)
#define LOG_FS_PATH "/wwhd-switch2-boot-core.log"
#define VARIANT "libounce_core / newlib malloc"
#elif defined(WWHD_PROBE_POSIX)
#define LOG_FS_PATH "/wwhd-switch2-boot-posix.log"
#define VARIANT "libounce / POSIX open test"
#else
#define LOG_FS_PATH "/wwhd-switch2-boot.log"
#define VARIANT "libounce / mimalloc"
#endif

static bool sm_ready, fs_ready, mounted, sockets_ready;
static Result startup_result;
static int net_fd = -1;
static FsFile log_file;
static bool log_open;
static s64 log_offset;
static Mutex log_mutex;

static void network_write(const char* text, size_t length) {
    if (net_fd < 0) return;
    size_t sent = 0;
    while (sent < length) {
        const ssize_t result = send(net_fd, text + sent, length - sent, MSG_DONTWAIT);
        if (result <= 0) break;
        sent += (size_t)result;
    }
}

void wwhd_probe_log(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (n <= 0) return;
    const size_t length = (size_t)n < sizeof(text) ? (size_t)n : sizeof(text) - 1;
    mutexLock(&log_mutex);
    // Do not wait on an unreachable host or depend on stdio redirection.
    network_write(text, length);
    if (log_open) {
        const Result result = fsFileWrite(&log_file, log_offset, text, length, FsWriteOption_Flush);
        if (R_FAILED(result)) {
            char failure[128];
            const int count = snprintf(failure, sizeof(failure), "[boot] native SD write=0x%08X\n", result);
            if (count > 0) network_write(failure, (size_t)count < sizeof(failure) ? (size_t)count : sizeof(failure) - 1);
        } else {
            log_offset += length;
        }
    }
    mutexUnlock(&log_mutex);
}
#define report wwhd_probe_log

bool wwhd_probe_ready(void) { return R_SUCCEEDED(startup_result); }

// Default __appInit opens the default window before userAppInit. Replace it
// here so even that window/GPU path is excluded from this startup test.
void __appInit(void) {
    startup_result = smInitialize();
    if (R_FAILED(startup_result)) return;
    sm_ready = true;
    // argvSetup (before __appInit) parses nxlink's sender address. Only open
    // sockets for nxlink launches; no listener or network service in SD launches.
    Result socket_result = 0;
    int connect_error = 0;
    if (__nxlink_host.s_addr != 0) {
        socket_result = socketInitializeDefault();
        sockets_ready = R_SUCCEEDED(socket_result);
        if (sockets_ready) {
            net_fd = nxlinkConnectToHost(false, false);
            if (net_fd < 0) connect_error = errno;
        }
    }
    report("[boot net-v4] __appInit reached; %s; smInitialize OK\n", VARIANT);
    report("[boot] newlib ABI: sizeof(_reent)=%lu, deviceData offset=%lu\n",
           (unsigned long)sizeof(struct _reent), (unsigned long)offsetof(struct _reent, deviceData));
    report("[boot] socketInitialize=0x%08X, nxlink fd=%d errno=%d\n",
           socket_result, net_fd, connect_error);
    report("[boot] calling fsInitialize\n");
    startup_result = fsInitialize();
    report("[boot] fsInitialize=0x%08X\n", startup_result);
    if (R_FAILED(startup_result)) return;
    fs_ready = true;
    report("[boot] calling fsdevMountSdmc\n");
    startup_result = fsdevMountSdmc();
    report("[boot] fsdevMountSdmc=0x%08X\n", startup_result);
    if (R_FAILED(startup_result)) return;
    mounted = true;
    report("[boot] calling fsdevGetDeviceFileSystem(sdmc)\n");
    FsFileSystem* sd = fsdevGetDeviceFileSystem("sdmc");
    report("[boot] fsdevGetDeviceFileSystem(sdmc)=%s\n", sd ? "OK" : "NULL");
    if (sd) {
#ifdef WWHD_GAME_HOST
        fsFsCreateDirectory(sd, "/switch");
        fsFsCreateDirectory(sd, "/switch/wwhd-switch2");
#endif
        report("[boot] calling native fsFsCreateFile(%s)\n", LOG_FS_PATH);
        const Result created = fsFsCreateFile(sd, LOG_FS_PATH, 0, 0);
        // Already-existing log is fine; opening below gives the definitive result.
        report("[boot] fsFsCreateFile=0x%08X (existing file may return 0x402)\n", created);
        report("[boot] calling native fsFsOpenFile\n");
        const Result opened = fsFsOpenFile(sd, LOG_FS_PATH, FsOpenMode_Write | FsOpenMode_Append, &log_file);
        report("[boot] fsFsOpenFile=0x%08X\n", opened);
        if (R_SUCCEEDED(opened)) {
            const Result resized = fsFileSetSize(&log_file, 0);
            report("[boot] fsFileSetSize=0x%08X\n", resized);
            if (R_SUCCEEDED(resized)) log_open = true;
            else fsFileClose(&log_file);
        }
    }
    report("[boot] native log active=%d; no POSIX file I/O used\n", log_open);
#if defined(WWHD_PROBE_VULKAN_HOST) || defined(WWHD_GAME_HOST)
    // Follow libounce's default __appInit, but only after logs are available.
    extern bool __libnx_hosversion_is_set(void);
    extern void __libnx_init_time(void);
    extern void __nx_win_init(void) __attribute__((weak));
    if (!__libnx_hosversion_is_set()) {
        const Result setsys = setsysInitialize();
        report("[probe boot] setsysInitialize=0x%08X\n", setsys);
        if (R_SUCCEEDED(setsys)) {
            SetSysFirmwareVersion version;
            const Result rc = setsysGetFirmwareVersion(&version);
            if (R_SUCCEEDED(rc)) hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
            setsysExit();
        }
    }
    report("[probe boot] calling appletInitialize\n");
    startup_result = appletInitialize();
    report("[probe boot] appletInitialize=0x%08X\n", startup_result);
    if (R_FAILED(startup_result)) return;
    report("[probe boot] calling hidInitialize\n");
    startup_result = hidInitialize();
    report("[probe boot] hidInitialize=0x%08X\n", startup_result);
    if (R_FAILED(startup_result)) return;
    report("[probe boot] calling timeInitialize\n");
    startup_result = timeInitialize();
    report("[probe boot] timeInitialize=0x%08X\n", startup_result);
    if (R_FAILED(startup_result)) return;
    __libnx_init_time();
    report("[probe boot] calling default-window initialization\n");
    if (__nx_win_init) __nx_win_init();
    report("[probe boot] default-window initialization returned\n");
#endif
}

__attribute__((constructor)) static void constructor_marker(void) {
    report("[boot] constructor reached\n");
}

static void close_logs(void) {
    mutexLock(&log_mutex);
    if (log_open) { fsFileClose(&log_file); log_open = false; }
    if (net_fd >= 0) {
        shutdown(net_fd, SHUT_WR);
        close(net_fd);
        net_fd = -1;
    }
    if (sockets_ready) socketExit();
    mutexUnlock(&log_mutex);
}

#if defined(WWHD_PROBE_VULKAN_HOST) || defined(WWHD_GAME_HOST)
// Keep libounce's default __appExit: it closes GPU/window before FS/services.
// Its runtime also closes the driver on partial initialization failures.
void userAppExit(void) {
    report("[probe boot] userAppExit; handing driver/service cleanup to libounce\n");
    close_logs();
}
#else
void __appExit(void) {
    report("[boot] __appExit: releasing FS/SM; returning to hbmenu\n");
    close_logs();
    if (mounted) fsdevUnmountAll();
    if (fs_ready) fsExit();
    if (sm_ready) smExit();
}
#endif
