#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace _home {
// Fixed counters, no allocation or per-action output. Times are inclusive;
// nested phases must not be summed. Disabled paths never read the clock.
struct StageTiming {
    enum Phase { TERMINAL, BINDING, LEDGER, PROJECTION, CANDIDATES, TRADEOFF, GUARD, PLATFORM, TAIL, COUNT };
    bool enabled = false;
    unsigned long long calls[COUNT] = {};
    long long nanos[COUNT] = {};
    long long max_nanos[COUNT] = {};
    static StageTiming& get() { static thread_local StageTiming value; return value; }
    void reset() {
        enabled = std::getenv("RDFW_STAGE_TIMING") &&
            std::getenv("RDFW_STAGE_TIMING")[0] == '1';
        for (int i=0; i<COUNT; ++i) { calls[i]=0; nanos[i]=0; max_nanos[i]=0; }
    }
    void report(long long elapsed, long long remaining) const {
        if (!enabled) return;
        const char* names[] = {"terminal", "binding", "ledger", "projection", "candidates", "tradeoff", "guarded", "platform", "tail"};
        for (int i=0; i<COUNT; ++i)
            std::printf("[StageTiming] phase=%s calls=%llu us=%lld max_us=%lld\n", names[i], calls[i], nanos[i]/1000, max_nanos[i]/1000);
        std::printf("[StageTiming] phase=plan elapsed_ms=%lld remaining_ms=%lld inclusive=true\n", elapsed, remaining);
    }
};
class StageTimer {
    StageTiming::Phase phase_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
public:
    explicit StageTimer(StageTiming::Phase phase) : phase_(phase), enabled_(StageTiming::get().enabled) {
        if (enabled_) start_ = std::chrono::steady_clock::now();
    }
    ~StageTimer() {
        if (!enabled_) return;
        auto& value = StageTiming::get();
        ++value.calls[phase_];
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start_).count();
        value.nanos[phase_] += elapsed;
        if (elapsed > value.max_nanos[phase_]) value.max_nanos[phase_] = elapsed;
    }
};
template<class F> auto TimedPlatformCall(F call) -> decltype(call()) {
    StageTimer timer(StageTiming::PLATFORM);
    return call();
}
}
