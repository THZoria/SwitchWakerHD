// Cross-compiler capability check: C game code may use Clang while the host
// runtime/library ABI stays with devkitA64 and its matching rebuilt libounce.
#include "ppc.h"
void wwhd_tailcall_check(Cpu* c) {
    PPC_ENTER_FN(0, wwhd_tailcall_check);
    MUSTTAIL return ppc_dispatch(c);
}
