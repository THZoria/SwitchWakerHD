// Host-only diagnostic lifecycle tests. No clock sleeps or console needed.
#include "platform/switch2_profile_window.h"
#include <cassert>

int main() {
    using W = switch2::profile::Window;
    W w;
    assert(!w.active());
    assert(w.poll(100, true) == W::None);  // opening Dev never starts capture
    assert(w.start(100));
    assert(w.active());
    assert(!w.start(200));  // cannot replace a running sample's scene
    assert(w.poll(100 + W::settle_ns - 1, false) == W::None);
    const auto begin = 100 + W::settle_ns;
    assert(w.poll(begin, false) == W::Begin);
    assert(w.poll(begin + W::measure_ns - 1, false) == W::None);
    assert(w.poll(begin + W::measure_ns, false) == W::Finish);
    assert(!w.active());
    assert(w.poll(begin + W::measure_ns + 1, false) == W::None);  // once only

    assert(w.start(1000));
    assert(w.poll(1001, true) == W::Cancel);  // menu cancels settling
    assert(!w.active());
    assert(w.start(2000));
    assert(w.poll(2000 + W::settle_ns, false) == W::Begin);
    assert(w.poll(2000 + W::settle_ns + 1, true) == W::Cancel);  // not measured over menu
    assert(!w.active());
    assert(w.start(5000));
    assert(w.poll(4999, false) == W::Cancel);  // invalid backwards clock
    assert(w.start(6000));
    const auto late = 6000 + W::settle_ns + W::measure_ns;
    assert(w.poll(late, false) == W::Begin);  // a stall in settling isn't measured time
    assert(w.poll(late + W::measure_ns - 1, false) == W::None);
    assert(w.poll(late + W::measure_ns, false) == W::Finish);
}
