#include "rdfw.hpp"
#include <cassert>
#include <memory>

namespace _home {
struct ScoreSemanticsTestAccess {
    static bool Trade(RDFW& w, const CandidatePlan& p,
                      const std::vector<CandidatePlan>& alternatives = {}) {
        return w.ShouldStartConstraintTrade(p, alternatives, "interval_test");
    }
    static void Init(RDFW& w) { w.InitializeConstraintLedger(); }
    static bool Fits(RDFW& w, const CandidatePlan& p) { return w.CanStartPlan(p, "interval_test"); }
    static void Budget(RDFW& w, int ms) { w.deadline_manager.reset(std::chrono::milliseconds(ms)); }
};
}
using namespace _home;

int main(int argc, char** argv) {
    assert(argc == 2);
    DeadlineManager platform_budget;
    assert(platform_budget.timeLimit().count() == 5000);
    auto w = std::make_shared<RDFW>();
    char program[] = "interval_gate_tests", path[] = "-path";
    char* args[] = {program, path, argv[1]};
    w->Init(3, args);
    w->stage = 2;
    assert(w->ParseEnv("(hold 0) (plate 0) (at 0 1) (sort 1 cupboard) "
        "(size 1 big) (type 1 container) (at 1 1) (closed 1)"));
    assert(w->ParseInstruction("(:task (close X) (:cond (sort X cupboard))) "
        "(:cons_notnot (:info (closed X) (:cond (sort X cupboard))))"));
    ScoreSemanticsTestAccess::Init(*w);
    w->constraint_uncertain[0] = true;
    const auto current = w->GetScoreSnapshot();
    assert(current.deterministic_base_score == 0);
    assert(current.possible_base_score == 60);
    CandidatePlan candidate;
    candidate.task_index = 0;
    candidate.eligible = candidate.dry_run_succeeded = true;
    candidate.actions.push_back(CandidateAction("Close", {1}, ActionCategory::PHYSICAL));
    candidate.score_after.deterministic_base_score = -2;
    candidate.score_after.possible_base_score = 38;
    // A low evidence-supported score is not proof of a bad physical plan.
    assert(ScoreSemanticsTestAccess::Trade(*w, candidate));
    CandidatePlan legacy_deferred = candidate;
    legacy_deferred.legacy_priority_known = true;
    legacy_deferred.legacy_before = w->GetTerminalSummary();
    legacy_deferred.legacy_after = legacy_deferred.legacy_before;
    legacy_deferred.legacy_after.goals[0] = TerminalStatus::SATISFIED;
    legacy_deferred.legacy_after_score = 40;
    assert(!ScoreSemanticsTestAccess::Trade(*w, legacy_deferred));
    assert(w->tasks[0].isEnable); // priority deferral cannot disable a task
    assert(w->GetScoreSnapshot().deterministic_base_score == 0);
    candidate.broken_constraints = {0};
    assert(ScoreSemanticsTestAccess::Trade(*w, candidate));
    // A feasible, certain alternative strictly dominates the candidate upper.
    CandidatePlan other;
    other.task_index = 1;
    other.eligible = other.dry_run_succeeded = true;
    other.score_after.deterministic_base_score = 39;
    other.score_after.possible_base_score = 99;
    assert(!ScoreSemanticsTestAccess::Trade(*w, candidate, {other}));
    other.score_after.deterministic_base_score = 38;
    assert(ScoreSemanticsTestAccess::Trade(*w, candidate, {other}));
    other.dry_run_succeeded = false;
    other.score_after.deterministic_base_score = 100;
    assert(ScoreSemanticsTestAccess::Trade(*w, candidate, {other}));
    // The same ordering used by guarded replacement/stop ignores overlapping
    // lower bounds, even when their numeric difference appears attractive.
    ScoreSnapshot challenger, legacy;
    legacy.deterministic_base_score = 10; legacy.possible_base_score = 80;
    challenger.deterministic_base_score = 79; challenger.possible_base_score = 100;
    assert(!StrictlyScoreDominates(challenger, legacy));
    challenger.deterministic_base_score = 80;
    assert(!StrictlyScoreDominates(challenger, legacy));
    challenger.deterministic_base_score = 81;
    assert(StrictlyScoreDominates(challenger, legacy));
    // Summary reuse includes history eligibility and uses current action cost.
    TerminalChecker checker;
    ScoreEvaluator evaluator;
    evaluator.recordAction(ActionCategory::PHYSICAL);
    const auto terminal = checker.evaluateAll(*w);
    const auto direct = evaluator.snapshot(*w, checker);
    const auto reused = evaluator.snapshot(terminal);
    assert(direct.deterministic_base_score == reused.deterministic_base_score);
    assert(direct.possible_base_score == reused.possible_base_score);
    const auto constraints = checker.evaluateConstraints(*w);
    assert(constraints.goals.empty());
    assert(constraints.constraints == terminal.constraints);
    assert(constraints.constraint_credited == terminal.constraint_credited);
    CandidatePlan incomplete;
    assert(!ScoreSemanticsTestAccess::Fits(*w, incomplete));
    // A short remaining wall budget must not truncate a one-task projection
    // into a misleading zero-duration plan. It still cannot authorize action.
    w->stage = 1;
    std::dynamic_pointer_cast<Container>(w->objects[1])->isOpen = true;
    ScoreSemanticsTestAccess::Budget(*w, 50);
    auto full_plan = w->PreviewCandidatePlan(0);
    assert(full_plan.dry_run_succeeded);
    assert(full_plan.actions.size() == 1);
    assert(full_plan.actions[0].name == "Close");
    assert(full_plan.remainingDuration().count() == 100);
    assert(!ScoreSemanticsTestAccess::Fits(*w, full_plan));
    assert(w->TestPlatformCalls() == 0);
}
