#include "rdfw.hpp"

#include <cassert>
#include <iostream>
#include <memory>

using namespace _home;

namespace {

std::shared_ptr<RDFW> World(const char* words, int stage, const std::string& env) {
    std::shared_ptr<RDFW> world = std::make_shared<RDFW>();
    char program[] = "recovery_evidence_tests";
    char path_option[] = "-path";
    char* args[] = {program, path_option, const_cast<char*>(words)};
    world->Init(3, args);
    world->stage = stage;
    assert(world->ParseEnv(env));
    return world;
}

Instruction Unary(const std::string& behave, const std::shared_ptr<Object>& x) {
    Instruction task;
    task.behave = behave;
    task.X.push_back(x);
    return task;
}

const std::string kitchen =
    "(hold 0) (plate 0) (at 0 2) "
    "(sort 1 human) (size 1 big) (at 1 1) "
    "(sort 2 cupboard) (size 2 big) (type 2 container) (at 2 3) (opened 2) "
    "(sort 3 book) (size 3 small) (color 3 red) (at 3 4)";

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    // A: a guaranteed initial must-inside repairs inside, but the cupboard's
    // unverified initial location does not become a Sense-quality book location.
    auto inside = World(argv[1], 2, kitchen);
    Instruction must_in = Unary("inside", inside->objects[3]);
    must_in.Y.push_back(inside->objects[2]);
    inside->notnot_infoConstrains.push_back(must_in);
    inside->ApplyMustInConstraintCorrection();
    assert(inside->IsInsideVerified(3));
    assert(inside->InsideSource(3) == EvidenceSource::CONSTRAINT_DERIVED);
    assert(!inside->IsLocationVerified(3));
    assert(inside->LocationSource(3) == EvidenceSource::CONSTRAINT_HEURISTIC);
    Instruction putin = Unary("putin", inside->objects[3]);
    putin.Y.push_back(inside->objects[2]);
    assert(inside->ZeroActionPreCheck(putin));

    // B and F: a reliable must-closed fact is visible to both the zero-action
    // check and the dry-run planner, so no verification action is generated.
    auto closed = World(argv[1], 2, kitchen);
    assert(closed->ContainerSource(2) == EvidenceSource::INITIAL);
    assert(!closed->IsContainerStateVerified(2));
    closed->notnot_infoConstrains.push_back(Unary("closed", closed->objects[2]));
    closed->ApplyOpenCloseCorrection();
    assert(closed->IsContainerStateVerified(2));
    assert(closed->ContainerSource(2) == EvidenceSource::CONSTRAINT_DERIVED);
    Instruction close_goal = Unary("close", closed->objects[2]);
    assert(closed->ZeroActionPreCheck(close_goal));
    closed->tasks.push_back(close_goal);
    CandidatePlan zero = closed->PreviewCandidatePlan(0);
    assert(zero.dry_run_succeeded && zero.actions.empty());
    auto explicit_info = World(argv[1], 2, kitchen);
    explicit_info->ParseInfo(Unary("closed", explicit_info->objects[2]));
    assert(explicit_info->ContainerSource(2) == EvidenceSource::EXPLICIT_INFO);
    assert(explicit_info->IsContainerStateVerified(2));
    Instruction explicit_close = Unary("close", explicit_info->objects[2]);
    assert(explicit_info->ZeroActionPreCheck(explicit_close));

    // C: conflicting initial locations do not give either endpoint a trusted
    // must-near inference, even if the robot stands at one reported location.
    auto conflict = World(argv[1], 2,
        "(hold 0) (plate 0) (at 0 4) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 table) (size 2 big) (at 2 2) "
        "(sort 3 book) (size 3 small) (color 3 red) (at 3 4)");
    Instruction near = Unary("near", conflict->objects[3]);
    near.Y.push_back(conflict->objects[2]);
    conflict->notnot_infoConstrains.push_back(near);
    conflict->ApplyMustNearConstraintCorrection();
    assert(!conflict->IsLocationVerified(3));
    Instruction goto_book = Unary("goto", conflict->objects[3]);
    assert(!conflict->ZeroActionPreCheck(goto_book));
    auto weak = World(argv[1], 2,
        "(hold 0) (plate 0) (at 0 2) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 table) (size 2 big) (at 2 2) "
        "(sort 3 book) (size 3 small) (color 3 red)");
    Instruction weak_near = Unary("near", weak->objects[3]);
    weak_near.Y.push_back(weak->objects[2]);
    weak->notnot_infoConstrains.push_back(weak_near);
    weak->ApplyMustNearConstraintCorrection();
    assert(weak->objects[3]->location == 2);
    assert(weak->LocationSource(3) == EvidenceSource::CONSTRAINT_HEURISTIC);
    assert(!weak->IsLocationVerified(3));
    Instruction weak_goto = Unary("goto", weak->objects[3]);
    assert(!weak->ZeroActionPreCheck(weak_goto));

    // D: an old disabled/high-risk task is reconsidered from current state.
    auto changed = World(argv[1], 1,
        "(hold 0) (plate 0) (at 0 2) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cup) (size 2 small) (color 2 blue) (at 2 3)");
    changed->tasks.push_back(Unary("goto", changed->objects[2]));
    changed->tasks[0].isEnable = false;
    changed->tasks[0].risk = 5;
    changed->objects[2]->location = 4;
    CandidatePlan renewed = changed->PreviewCandidatePlan(0);
    assert(changed->tasks[0].isEnable && changed->tasks[0].risk < 2);
    assert(renewed.eligible && renewed.dry_run_succeeded &&
           renewed.actions.size() == 1 && renewed.actions[0].name == "Move");
    assert(renewed.candidate_id > 0 && renewed.task_indices.size() == 1);
    assert(renewed.prediction.goal_gain == 1);
    assert(renewed.prediction.action_count == 1);
    assert(renewed.prediction.action_cost == 4);
    assert(renewed.prediction.utility == 36);
    assert(!renewed.evidence.empty());
    assert(changed->location == 2 && changed->objects[2]->location == 4);
    changed->tasks[0].isEnable = false;
    changed->tasks[0].risk = 5;
    changed->ExecuteTerminalRecovery();
    assert(changed->GetTerminalSummary().allGoalsSatisfied());
    assert(changed->TestPlatformCalls() == 1);
    assert(changed->DecisionFeedback().size() == 1);
    const CandidateExecutionRecord& feedback = changed->DecisionFeedback().front();
    assert(feedback.candidate.candidate_id != renewed.candidate_id);
    assert(feedback.candidate.prediction.goal_gain == 1);
    assert(feedback.actual.values.goal_gain == 1);
    assert(feedback.actual.values.action_cost == 4);
    assert(feedback.actual.succeeded && !feedback.actual.failed_action);
    assert(feedback.error.goal_gain == 0 && feedback.error.action_cost == 0 &&
           feedback.error.utility == 0);

    // E: an ineligible task cannot make Stop depend on the first stale list.
    // The final scan runs and correctly leaves two protected constraints intact.
    auto stopped = World(argv[1], 1,
        "(hold 0) (plate 0) (at 0 2) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cupboard) (size 2 big) (type 2 container) (at 2 2) (closed 2)");
    stopped->SetTestInput(
        "(hold 0) (plate 0) (at 0 2) (sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cupboard) (size 2 big) (type 2 container) (at 2 2) (closed 2)",
        "(:cons_notnot (:info (closed X) (:cond (sort X cupboard)))) "
        "(:cons_not (:info (opened X) (:cond (sort X cupboard)))) "
        "(:task (open X) (:cond (sort X cupboard)))");
    stopped->Plan();
    assert(stopped->GetStopRescanCount() > 0);
    assert(stopped->TestPlatformCalls() == 0);

    auto already_done = World(argv[1], 2, kitchen);
    already_done->SetTestInput(kitchen,
        "(:cons_notnot (:info (closed X) (:cond (sort X cupboard)))) "
        "(:task (close X) (:cond (sort X cupboard)))");
    already_done->Plan();
    assert(already_done->GetTerminalSummary().satisfied_goals == 1);
    assert(already_done->TestPlatformCalls() == 0);

    std::cout << "recovery/evidence tests passed\n";
    return 0;
}
