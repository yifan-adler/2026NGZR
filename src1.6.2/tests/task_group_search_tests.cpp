#include "task_group_search.hpp"

#include <cassert>
#include <map>
#include <stdexcept>
#include <vector>

using namespace _home;

static CandidatePlan Plan(const std::vector<std::size_t>& ids, int score,
                          bool broken = false) {
    CandidatePlan plan;
    plan.task_indices = ids;
    plan.task_index = ids.front();
    plan.dry_run_succeeded = true;
    plan.score_after.deterministic_base_score = score;
    if (broken) plan.broken_constraints.push_back(0);
    return plan;
}

int main() {
    const CandidatePlan a = Plan({0}, -20, true);
    const CandidatePlan b = Plan({1}, 40);
    const CandidatePlan c = Plan({2}, 30);
    std::map<std::vector<std::size_t>, CandidatePlan> projected;
    projected[{0, 1}] = Plan({0, 1}, 80, true);
    projected[{0, 2}] = Plan({0, 2}, 90, true);
    projected[{1, 0}] = Plan({1, 0}, 100, true);
    projected[{1, 2}] = Plan({1, 2}, 120);
    projected[{2, 0}] = Plan({2, 0}, 85, true);
    projected[{2, 1}] = Plan({2, 1}, 115);
    TaskGroupSearchOptions options;
    options.max_depth = 2;
    const auto projector = [&](const std::vector<std::size_t>& ids) {
        const auto found = projected.find(ids);
        return found == projected.end() ? CandidatePlan() : found->second;
    };
    const TaskGroupSearchResult result = SearchTaskGroups({a, b, c}, projector, options);
    assert(result.has_safe && result.has_broken);
    assert(result.best_safe.task_indices == std::vector<std::size_t>({1, 2}));
    assert(result.best_broken.task_indices == std::vector<std::size_t>({1, 0}));

    TaskGroupSearchOptions stopped;
    stopped.budget = std::chrono::milliseconds(0);
    const TaskGroupSearchResult empty = SearchTaskGroups({a, b, c}, projector, stopped);
    assert(empty.budget_exhausted && empty.evaluated == 0);

    TaskGroupSearchOptions narrow;
    narrow.beam_width = 1;
    narrow.max_depth = 1;
    const TaskGroupSearchResult limited = SearchTaskGroups({a, b, c}, projector, narrow);
    assert(limited.beam_limited);

    CandidatePlan prefix = Plan({0, 1}, 10, true);
    prefix.lost_goals.push_back(0);
    TaskGroupSearchOptions restoration;
    restoration.max_depth = 3;
    bool repeated = false;
    const auto restore_projector = [&](const std::vector<std::size_t>& ids) {
        if (ids == std::vector<std::size_t>({0, 1})) return prefix;
        if (ids == std::vector<std::size_t>({0, 1, 0})) {
            repeated = true;
            return Plan(ids, 70, true);
        }
        return CandidatePlan();
    };
    SearchTaskGroups({a, b}, restore_projector, restoration);
    assert(repeated);

    const TaskGroupSearchResult failed = SearchTaskGroups({a, b},
        [](const std::vector<std::size_t>&) -> CandidatePlan {
            throw std::runtime_error("projection failed");
        }, options);
    assert(failed.projection_failed);

    CandidatePlan dependent = Plan({3}, 0);
    dependent.dry_run_succeeded = false;
    bool dependency_tried = false;
    const auto dependency_projector = [&](const std::vector<std::size_t>& ids) {
        if (ids == std::vector<std::size_t>({1, 3})) {
            dependency_tried = true;
            return Plan(ids, 90);
        }
        return CandidatePlan();
    };
    SearchTaskGroups({b, dependent}, dependency_projector, options);
    assert(dependency_tried);

    CandidatePlan unlocking = Plan({1}, 40);
    unlocking.available_tasks_known = true;
    unlocking.available_tasks = {3};
    const TaskGroupSearchResult unlocked = SearchTaskGroups({unlocking},
        dependency_projector, options);
    assert(unlocked.has_safe);
    assert(unlocked.best_safe.task_indices ==
           std::vector<std::size_t>({1, 3}));
}
