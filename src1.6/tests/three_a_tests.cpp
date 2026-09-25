#include "rdfw.hpp"

#include <cassert>
#include <iostream>
#include <memory>

using namespace _home;

int main(int argc, char** argv) {
    assert(argc == 2);

    DeadlineManager budget(std::chrono::milliseconds(1000));
    budget.start();
    assert(budget.elapsed().count() >= 0);
    assert(budget.remaining().count() <= 1000);
    assert(budget.canFinish(std::chrono::milliseconds(500),
                            std::chrono::milliseconds(300)));
    assert(!budget.canFinish(std::chrono::milliseconds(800),
                             std::chrono::milliseconds(300)));

    std::shared_ptr<RDFW> world = std::make_shared<RDFW>();
    char program[] = "three_a_tests";
    char path_option[] = "-path";
    char* init_args[] = {program, path_option, argv[1]};
    world->Init(3, init_args);
    world->stage = 1;
    assert(world->ParseEnv(
        " (hold 0) (plate 0) (at 0 4) "
        "(sort 1 human) (size 1 big) (at 1 4) "
        "(sort 2 closet) (size 2 big) (type 2 container) "
        "(at 2 4) (closed 2)"));

    Instruction close_goal;
    close_goal.behave = "close";
    close_goal.X.push_back(world->objects[2]);
    close_goal.isEnable = false; // terminal truth must not depend on execution flags
    world->tasks.push_back(close_goal);

    TerminalChecker checker;
    assert(checker.evaluateTask(*world, world->tasks[0]) ==
           TerminalStatus::SATISFIED);

    Instruction must_not_open;
    must_not_open.behave = "opened";
    must_not_open.X.push_back(world->objects[2]);
    world->not_infoConstrains.push_back(must_not_open);

    Instruction prohibited_open = must_not_open;
    prohibited_open.behave = "open";
    world->not_taskConstrains.push_back(prohibited_open);

    TerminalSummary terminal = checker.evaluateAll(*world);
    assert(terminal.satisfied_goals == 1);
    assert(terminal.satisfied_constraints == 1);
    assert(terminal.unknown_constraints == 1); // action history is not World State

    ScoreEvaluator score;
    score.recordAction(ActionCategory::MOVE);
    score.recordAction(ActionCategory::PHYSICAL);
    score.recordAction(ActionCategory::HUMAN_INTERACTION);
    score.recordAction(ActionCategory::OBSERVATION);
    ScoreSnapshot snapshot = score.snapshot(*world, checker);
    assert(snapshot.action_cost == 9);
    assert(snapshot.deterministic_base_score == 71);

    world->stage = 2;
    world->containerStateVerified[2] = false;
    assert(checker.evaluateTask(*world, world->tasks[0]) ==
           TerminalStatus::UNKNOWN);
    world->containerStateVerified[2] = true;
    assert(checker.evaluateTask(*world, world->tasks[0]) ==
           TerminalStatus::SATISFIED);

    // Stage 3B: the existing planner is dry-run into a complete CandidatePlan.
    // The preview must restore the live World State exactly.
    world->stage = 1;
    world->tasks.clear();
    Instruction open_goal;
    open_goal.behave = "open";
    open_goal.X.push_back(world->objects[2]);
    world->tasks.push_back(open_goal);
    std::dynamic_pointer_cast<Container>(world->objects[2])->isOpen = false;
    const int location_before_preview = world->location;
    CandidatePlan candidate = world->PreviewCandidatePlan(0);
    assert(candidate.dry_run_succeeded);
    assert(candidate.actions.size() == 1);
    assert(candidate.actions[0].name == "Open");
    assert(candidate.action_cost == 2);
    assert(candidate.estimated_duration == std::chrono::milliseconds(100));
    assert(candidate.gained_goals.size() == 1 && candidate.gained_goals[0] == 0);
    assert(candidate.broken_constraints.size() == 2 &&
           candidate.broken_constraints[0] == 0 &&
           candidate.broken_constraints[1] == 1);
    // Two lost constraints are priced in the projected score; their count
    // alone does not make an otherwise executable task ineligible.
    assert(candidate.eligible);
    assert(candidate.marginal_score == 38);
    assert(candidate.utility == candidate.marginal_score);
    assert(candidate.remainingUtility(candidate.score_before) == 38);
    candidate.executed_actions = candidate.actions.size();
    assert(candidate.remainingDuration() == std::chrono::milliseconds(0));
    assert(candidate.remainingActionCost() == 0);
    assert(candidate.remainingUtility(candidate.score_after) == 0);
    assert(world->location == location_before_preview);
    assert(!std::dynamic_pointer_cast<Container>(world->objects[2])->isOpen);
    assert(checker.evaluateTask(*world, world->tasks[0]) ==
           TerminalStatus::UNSATISFIED);

    world->tasks.clear();
    Instruction goto_goal;
    goto_goal.behave = "goto";
    goto_goal.X.push_back(world->objects[2]);
    world->tasks.push_back(goto_goal);
    world->location = 3;
    world->objects[2]->is_keep = 2;
    assert(world->CalculateTaskRisk(world->tasks[0]) < 2);
    CandidatePlan goto_plan = world->PreviewCandidatePlan(0);
    assert(goto_plan.eligible && goto_plan.dry_run_succeeded);
    assert(goto_plan.actions.size() == 1 && goto_plan.actions[0].name == "Move");
    assert(world->location == 3 && world->objects[2]->location == 4);

    std::cout << "3A/3B infrastructure tests passed\n";
    return 0;
}
