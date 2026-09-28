#include "deadline_manager.hpp"

namespace _home {

DeadlineManager::DeadlineManager(Duration time_limit)
    : start_time_(), time_limit_(time_limit), started_(false) {
}

void DeadlineManager::start() {
    start_time_ = Clock::now();
    started_ = true;
}

void DeadlineManager::reset(Duration time_limit) {
    time_limit_ = time_limit;
    start();
}

DeadlineManager::Duration DeadlineManager::elapsed() const {
    if (!started_) return Duration(0);
    return std::chrono::duration_cast<Duration>(Clock::now() - start_time_);
}

DeadlineManager::Duration DeadlineManager::remaining() const {
    const Duration used = elapsed();
    return used >= time_limit_ ? Duration(0) : time_limit_ - used;
}

bool DeadlineManager::deadlineReached() const {
    return started_ && elapsed() >= time_limit_;
}

bool DeadlineManager::canFinish(Duration estimated_time, Duration safety_margin) const {
    if (estimated_time.count() < 0 || safety_margin.count() < 0) return false;
    if (deadlineReached()) return false;
    return estimated_time + safety_margin <= remaining();
}

DeadlineManager::Duration DeadlineManager::timeLimit() const {
    return time_limit_;
}

} // namespace _home
