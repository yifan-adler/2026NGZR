#include "rdfw.hpp"

#include <cassert>
#include <memory>

using namespace _home;

static Instruction Unary(const char* verb, const std::shared_ptr<Object>& object) {
    Instruction instruction;
    instruction.behave = verb;
    instruction.X.push_back(object);
    return instruction;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    RDFW world;
    char program[] = "task_group_projection_tests";
    char path_option[] = "-path";
    char* args[] = {program, path_option, argv[1]};
    world.Init(3, args);
    world.stage = 1;
    assert(world.ParseEnv(
        "(hold 0) (plate 0) (at 0 1) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 table) (size 2 big) (at 2 2) "
        "(sort 3 sofa) (size 3 big) (at 3 3)"));
    world.tasks.push_back(Unary("goto", world.objects[2]));
    world.tasks.push_back(Unary("goto", world.objects[3]));

    const int initial_location = world.location;
    const std::size_t initial_tasks = world.tasks.size();
    const CandidatePlan plan = world.PreviewTaskGroupPlan({0, 1});
    assert(plan.dry_run_succeeded);
    assert(plan.task_indices == std::vector<std::size_t>({0, 1}));
    assert(plan.actions.size() == 2);
    assert(plan.actions[0].name == "Move" && plan.actions[1].name == "Move");
    assert(plan.broken_constraints.empty());
    assert(world.location == initial_location);
    assert(world.tasks.size() == initial_tasks && world.tasks[0].isEnable);
    assert(world.constraint_eligible.empty());
    assert(world.GetScoreSnapshot().action_cost == 0);

    const CandidatePlan deferred = world.PreviewLegacyContinuation(0, true);
    assert(deferred.dry_run_succeeded);
    assert(world.location == initial_location);
    assert(world.GetScoreSnapshot().action_cost == 0);
    assert(world.TestPlatformCalls() == 0);

    // A complete legacy suffix includes the normal task loop and final
    // phases, while its actions and bookkeeping remain isolated.
    world.tasks.pop_back();
    const CandidatePlan legacy = world.PreviewLegacyContinuation(0, false);
    assert(legacy.dry_run_succeeded);
    assert(legacy.actions.size() == 1);
    assert(legacy.actions[0].name == "Move");
    assert(legacy.score_after.deterministic_base_score == 36);
    assert(world.location == initial_location);
    assert(world.tasks.size() == 1 && world.tasks[0].isEnable);
    assert(world.GetScoreSnapshot().action_cost == 0);
    assert(world.TestPlatformCalls() == 0);

    auto constrained = std::make_shared<RDFW>();
    constrained->Init(3, args);
    constrained->stage = 1;
    assert(constrained->ParseEnv(
        "(hold 0) (plate 0) (at 0 1) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cupboard) (size 2 big) (type 2 container) "
        "(at 2 2) (closed 2)"));
    assert(constrained->ParseInstruction(
        "(:cons_notnot (:info (closed X) (:cond (sort X cupboard)))) "
        "(:task (open X) (:cond (sort X cupboard)))"));
    constrained->Cons_plan();
    const CandidatePlan breaking = constrained->PreviewTaskGroupPlan({0});
    assert(breaking.dry_run_succeeded);
    assert(breaking.broken_constraints == std::vector<std::size_t>({0}));
    bool loss_attached_to_action = false;
    for (const CandidateAction& action : breaking.actions)
        loss_attached_to_action |= action.newly_broken_constraints ==
            std::vector<std::size_t>({0});
    assert(loss_attached_to_action);
    assert(constrained->constraint_eligible.empty());
    assert(constrained->TestPlatformCalls() == 0);
}
