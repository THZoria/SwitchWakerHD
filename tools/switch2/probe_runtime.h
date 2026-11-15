#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Logging initialized before the graphics/window path. Native SD file + nxlink
// when launched through nxlink; no dependency on stdout or POSIX SD descriptors.
void wwhd_probe_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
bool wwhd_probe_ready(void);
#ifdef __cplusplus
}
#endif
