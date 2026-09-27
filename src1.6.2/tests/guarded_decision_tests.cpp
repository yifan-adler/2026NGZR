#include "rdfw.hpp"

#include <cassert>
#include <cstdlib>
#include <memory>

using namespace _home;

static std::shared_ptr<RDFW> Run(const char* words, const char* mode) {
    if (mode) setenv("RDFW_TASK_GROUP_MODE", mode, 1);
    else unsetenv("RDFW_TASK_GROUP_MODE");
    auto world = std::make_shared<RDFW>();
    char program[] = "guarded_decision_tests";
    char path_option[] = "-path";
    char* arguments[] = {program, path_option, const_cast<char*>(words)};
    world->Init(3, arguments);
    world->stage = 1;
    world->SetTestInput(
        "(hold 0) (plate 0) (at 0 1) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 table) (size 2 big) (at 2 2)",
        "(:task (goto X) (:cond (sort X table)))");
    world->Plan();
    return world;
}

static std::shared_ptr<RDFW> RunRoute(
    const char* words, const char* mode,
    const std::vector<bool>& action_results = {}) {
    if (mode) setenv("RDFW_TASK_GROUP_MODE", mode, 1);
    else unsetenv("RDFW_TASK_GROUP_MODE");
    auto world = std::make_shared<RDFW>();
    char program[] = "guarded_decision_tests";
    char path_option[] = "-path";
    char* arguments[] = {program, path_option, const_cast<char*>(words)};
    world->Init(3, arguments);
    world->stage = 1;
    world->SetTestInput(
        "(hold 0) (plate 0) (at 0 1) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cupboard) (size 2 big) (type 2 container) (closed 2) (at 2 2) "
        "(sort 3 closet) (size 3 big) (type 3 container) (opened 3) (at 3 3) "
        "(sort 4 cup) (size 4 small) (at 4 2)",
        "(:task (open X) (:cond (sort X cupboard))) "
        "(:task (close X) (:cond (sort X closet))) "
        "(:task (pickup X) (:cond (sort X cup)))");
    world->SetActionResults(action_results);
    world->Plan();
    return world;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto legacy = Run(argv[1], nullptr);
    const auto guarded = Run(argv[1], "guarded");
    unsetenv("RDFW_TASK_GROUP_MODE");
    assert(legacy->GetScoreSnapshot().deterministic_base_score == 36);
    assert(guarded->GetScoreSnapshot().deterministic_base_score == 36);
    assert(legacy->TestPlatformCalls() == 1);
    assert(guarded->TestPlatformCalls() == 1);
    assert(guarded->GetTerminalSummary().allGoalsSatisfied());

    const auto old_route = RunRoute(argv[1], nullptr);
    const auto new_route = RunRoute(argv[1], "guarded");
    unsetenv("RDFW_TASK_GROUP_MODE");
    assert(old_route->GetScoreSnapshot().deterministic_base_score == 102);
    assert(new_route->GetScoreSnapshot().deterministic_base_score == 106);
    assert(old_route->TestPlatformCalls() == 6);
    assert(new_route->TestPlatformCalls() == 5);
    assert(new_route->GetTerminalSummary().allGoalsSatisfied());
    bool group_recorded = false;
    for (const auto& record : new_route->DecisionFeedback()) {
        if (record.candidate.task_indices.size() <= 1) continue;
        group_recorded = true;
        assert(record.actual.succeeded && !record.actual.failed_action);
        assert(record.error.utility == 0 && record.error.action_cost == 0);
        assert(record.actual.values.action_count == record.candidate.actions.size());
    }
    assert(group_recorded);

    // The first approved action fails. Recovery begins from the real state;
    // the failed command is charged once and no later group action is issued.
    const auto recovered = RunRoute(argv[1], "guarded", {false});
    unsetenv("RDFW_TASK_GROUP_MODE");
    assert(recovered->GetTerminalSummary().allGoalsSatisfied());
    assert(recovered->TestPlatformCalls() == 7);
    assert(recovered->GetScoreSnapshot().deterministic_base_score == 98);
    assert(!recovered->DecisionFeedback().empty());
    const auto& failure = recovered->DecisionFeedback().front();
    assert(!failure.actual.succeeded && failure.actual.failed_action);
    assert(failure.actual.values.action_count == 1);
    assert(failure.actual.values.action_cost == 4);
}
