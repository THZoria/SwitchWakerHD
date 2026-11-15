/* Original loader metadata, replacing the build-PC GNU ld --nx-module-name
 * extension when the private runtime is linked by embedded LLD. */
#include <stdint.h>
__attribute__((section(".nx-module-name"), used))
static const struct {
    uint32_t reserved, length;
    char name[13];
} module_name = {0, 12, "wwhd_switch2"};
