#pragma once

#include <chrono>

namespace _home {

class DeadlineManager {
public:
    typedef std::chrono::steady_clock Clock;
    typedef std::chrono::milliseconds Duration;

    explicit DeadlineManager(Duration time_limit = Duration(4700));

    void start();
    void reset(Duration time_limit);
    Duration elapsed() const;
    Duration remaining() const;
    bool deadlineReached() const;
    bool canFinish(Duration estimated_time, Duration safety_margin) const;
    Duration timeLimit() const;

private:
    Clock::time_point start_time_;
    Duration time_limit_;
    bool started_;
};

} // namespace _home
