#pragma once

#include "candidate_plan.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <vector>

namespace _home {

// Search only ranks isolated projections. A result is never an execution permit:
// the caller must compare a complete legacy continuation and verify evidence.
struct TaskGroupSearchOptions {
    std::size_t beam_width = 6;
    std::size_t branch_width = 8;
    std::size_t max_depth = 4;
    std::size_t max_restoration_tasks = 2;
    std::chrono::milliseconds budget{25};
    std::chrono::milliseconds max_plan_duration{5000};
};

struct TaskGroupSearchResult {
    CandidatePlan best_safe;
    CandidatePlan best_broken;
    bool has_safe = false;
    bool has_broken = false;
    std::size_t evaluated = 0;
    bool budget_exhausted = false;
    bool projection_failed = false;
    bool beam_limited = false;
    bool branch_limited = false;
    std::chrono::steady_clock::duration elapsed{};
};

typedef std::function<CandidatePlan(const std::vector<std::size_t>&)> GroupProjector;

TaskGroupSearchResult SearchTaskGroups(
    const std::vector<CandidatePlan>& roots,
    const GroupProjector& project,
    const TaskGroupSearchOptions& options);

} // namespace _home
