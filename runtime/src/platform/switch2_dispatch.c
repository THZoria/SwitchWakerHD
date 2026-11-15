/* Keep the mandatory guest tail edge in Clang C, without changing the GCC
 * C++ runtime ABI or relying on optional sibling-call optimisation. */
#include "ppc.h"

#if !defined(__SWITCH2__) || !defined(__clang__)
#error "Compile the Switch 2 dispatch trampoline with Clang"
#endif

/* A null target means the resolver already completed the guarded HLE call.
 * Exceptions from that call unwind through this ordinary C frame. */
extern PpcFunc ppc_dispatch_resolve_switch2(Cpu* c);

void ppc_dispatch(Cpu* c) {
    PpcFunc target = ppc_dispatch_resolve_switch2(c);
    if (!target) return;
    MUSTTAIL return target(c);
}
