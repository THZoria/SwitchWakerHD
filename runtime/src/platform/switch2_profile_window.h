#pragma once
#include <cstdint>

namespace switch2::profile {
class Window {
    enum Phase { Idle, Settle, Measure } phase_ = Idle;
    uint64_t started_ = 0;
public:
    static constexpr uint64_t settle_ns = 3'000'000'000ull;
    static constexpr uint64_t measure_ns = 15'000'000'000ull;
    enum Event { None, Begin, Finish, Cancel };
    bool active() const { return phase_ != Idle; }
    bool start(uint64_t now) {
        if (active()) return false;
        phase_ = Settle;
        started_ = now;
        return true;
    }
    Event poll(uint64_t now, bool menu_open) {
        if (!active()) return None;
        if (menu_open || now < started_) { phase_ = Idle; return Cancel; }
        if (phase_ == Settle && now - started_ >= settle_ns) {
            phase_ = Measure; started_ = now; return Begin;
        }
        if (phase_ == Measure && now - started_ >= measure_ns) {
            phase_ = Idle; return Finish;
        }
        return None;
    }
};
}
