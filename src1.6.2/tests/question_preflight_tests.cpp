#include "rdfw.hpp"

#include <cassert>
#include <iostream>
#include <memory>

using namespace _home;

namespace {
const char* words;
const std::string env =
    "(hold 0) (plate 0) (at 0 1) "
    "(sort 1 human) (size 1 big) (at 1 1) "
    "(sort 2 cup) (size 2 small) (color 2 red) (at 2 2) "
    "(sort 3 closet) (size 3 big) (type 3 container) (closed 3) (at 3 3) "
    "(sort 4 table) (size 4 big) (at 4 4) "
    "(sort 5 cup) (size 5 small) (color 5 blue) (at 5 2)";

std::shared_ptr<RDFW> World() {
    auto world = std::make_shared<RDFW>();
    char program[] = "question_preflight_tests";
    char option[] = "-path";
    char* args[] = {program, option, const_cast<char*>(words)};
    world->Init(3, args);
    world->deduplicate_input = true; // explicitly test B's unique-goal contract
    world->stage = 1;
    assert(world->ParseEnv(env));
    return world;
}

std::string Task(const std::string& action, const std::string& cond) {
    return "(:task (" + action + ") (:cond " + cond + "))";
}
std::string Info(const std::string& action, const std::string& cond) {
    return "(:info (" + action + ") (:cond " + cond + "))";
}
std::string Not(const std::string& child) { return "(:cons_not " + child + ")"; }
std::string Must(const std::string& child) { return "(:cons_notnot " + child + ")"; }

void DuplicateTasksAndScore() {
    auto w = World();
    const auto close = Task("close X", "(sort X closet)");
    assert(w->ParseInstruction(close + Task("close X", "(type X container) (id X 3)")));
    TerminalChecker checker;
    ScoreEvaluator score;
    assert(score.snapshot(*w, checker).deterministic_base_score == 80);
    assert(w->RunQuestionPreflight());
    assert(w->tasks.size() == 1 && w->preflightReport().tasks.duplicates == 1);
    assert(checker.evaluateAll(*w).goals.size() == 1);
    assert(score.snapshot(*w, checker).deterministic_base_score == 40);
    assert(w->RunQuestionPreflight()); // idempotent, no extra changes
    assert(w->preflightReport().tasks.duplicates == 0 && w->tasks.size() == 1);

    w = World();
    w->ParseInstruction(Task("puton X Y", "(sort X cup) (color X red) (sort Y table)") +
                        Task("puton X Y", "(id Y 4) (color X red) (sort X cup)"));
    assert(w->RunQuestionPreflight() && w->tasks.size() == 1);
    // This is the same canonical list used by ExecuteMainTaskLoop's multi-puton count.
    assert(w->tasks[0].X[0]->id == 2 && w->tasks[0].Y[0]->id == 4);
}

void DuplicateConstraintRisk() {
    auto w = World();
    const auto pickup = Task("pickup X", "(id X 2)");
    w->ParseInstruction(pickup + Not(pickup) + Not(Task("pickup X", "(color X red) (sort X cup)")));
    assert(w->RunQuestionPreflight());
    assert(w->not_taskConstrains.size() == 1);
    assert(w->preflightReport().constraints.duplicates == 1);
    w->Cons_plan();
    assert(w->pickup_cons[2] == 1);
    assert(w->CalculateTaskRisk(w->tasks[0]) == 1); // not A's forbidden threshold 2
    TerminalChecker checker;
    assert(checker.evaluateAll(*w).constraints.size() == 1);

    w = World();
    auto closed = Info("closed X", "(id X 3)");
    w->ParseInstruction(Task("close X", "(id X 3)") + Must(closed) + Must(closed));
    assert(w->RunQuestionPreflight());
    ScoreEvaluator score;
    assert(score.snapshot(*w, checker).deterministic_base_score == 60);
    assert(score.snapshot(*w, checker).total_constraints == 1);
}

void GotoAndConflicts() {
    auto w = World();
    auto go = Task("goto X", "(id X 2)");
    w->ParseInstruction(go + Task("goto X", "(color X red) (sort X cup)"));
    assert(w->CheckAndDeferMultiGoto());
    assert(w->RunQuestionPreflight());
    assert(!w->CheckAndDeferMultiGoto());
    // Different IDs at the SAME location must not be merged.
    w->ParseInstruction(Task("goto X", "(id X 5)"));
    assert(w->RunQuestionPreflight() && w->tasks.size() == 2);
    assert(w->CheckAndDeferMultiGoto());

    w = World();
    const auto open = Task("open X", "(id X 3)");
    const auto close = Task("close X", "(id X 3)");
    const auto opened = Info("opened X", "(id X 3)");
    w->ParseInstruction(open + open + close + Not(open) + Not(open) +
                        Not(opened) + Must(opened) + Must(Info("closed X", "(id X 3)")) +
                        Task("puton X Y", "(id X 2) (id Y 4)") +
                        Task("puton X Y", "(id X 2) (id Y 1)"));
    assert(w->RunQuestionPreflight());
    assert(w->tasks.size() == 4 && w->not_taskConstrains.size() == 1);
    assert(w->not_infoConstrains.size() == 1 && w->notnot_infoConstrains.size() == 2);
    // Contradictions are not a preflight error or a license to drop objectives.
    assert(w->preflightReport().safe());
}

void CanonicalRelationsAndSets() {
    auto w = World();
    w->ParseInstruction(Must(Info("near X Y", "(id X 2) (id Y 5)")) +
                        Must(Info("nextto X Y", "(id X 5) (id Y 2)")) +
                        Not(Info("inside X Y", "(id X 2) (id Y 3)")) +
                        Not(Info("in X Y", "(id Y 3) (id X 2)")));
    assert(w->RunQuestionPreflight());
    assert(w->notnot_infoConstrains.size() == 1 && w->not_infoConstrains.size() == 1);
    assert(w->preflightReport().constraints.duplicates == 2);
    auto group = w->notnot_infoConstrains[0];
    group.X.push_back(w->objects[4]);
    w->notnot_infoConstrains.push_back(group);
    std::swap(group.X[0], group.X[1]);
    w->notnot_infoConstrains.push_back(group);
    assert(w->RunQuestionPreflight());
    assert(w->notnot_infoConstrains.size() == 2); // group != singleton; order immaterial

    w = World();
    w->ParseNaturalLanguage("close the closet. close the closet.");
    assert(w->tasks.size() == 2);
    assert(w->RunQuestionPreflight() && w->tasks.size() == 1); // shared NL gate
}

void MalformedConstraintRecovery() {
    const auto payload = Task("pickup X", "(id X 2)");
    const auto suffix = Task("close X", "(id X 3)") +
                        Not(Task("open X", "(id X 3)"));
    for (const auto& bad : std::vector<std::string>{
            "(:cons_not " + payload, // missing outer close
            "(:cons_notnot " + Info("closed X", "(id X 3)"),
            "(:cons_not (:task (pickup X) (:cond (id X 2)", // missing inner closes
            Must(payload), // invalid child kind, never promote pickup
            "(:cons_not (:cons_not " + payload + "))", // invalid nested wrapper
            "(:cons_not (:unknown " + payload + "))",
            "(:cons_not " + payload + Task("goto X", "(id X 4)") + ")", // two children
            "(:cons_not (:info (inside X) (:cond (id X 2))))"}) {
        auto w = World();
        assert(w->ParseInstruction("(:ins " + bad + suffix + ")"));
        assert(w->tasks.size() == 1 && w->tasks[0].behave == "close");
        assert(w->not_taskConstrains.size() == 1 && w->not_taskConstrains[0].behave == "open");
        assert(w->discardedInstructionCount() >= 1);
        assert(w->RunQuestionPreflight());
    }
    auto w = World();
    w->ParseInstruction(Not(payload) + Must(Info("closed X", "(id X 3)")));
    assert(w->tasks.empty() && w->not_taskConstrains.size() == 1 &&
           w->notnot_infoConstrains.size() == 1);
    w = World();
    w->ParseInstruction("(:cons_not " + payload); // malformed final item
    assert(w->tasks.empty() && w->not_taskConstrains.empty());
}

void WorldSafetyAndStage2Unknowns() {
    auto w = World();
    w->stage = 2;
    w->objects[2]->location = UNKNOWN;
    w->objects[3]->location = UNKNOWN;
    std::dynamic_pointer_cast<Container>(w->objects[3])->isOpen = UNKNOWN;
    assert(w->RunQuestionPreflight());
    w->objects[3]->location = 1; // Stage 2 err may collide with human
    assert(w->RunQuestionPreflight());
    w->stage = 1;
    assert(w->RunQuestionPreflight() && !w->preflightReport().world_warnings.empty());

    w = World();
    w->objects[4]->sort = "human";
    assert(w->RunQuestionPreflight() && !w->preflightReport().world_warnings.empty());
    w->Fini();
    assert(w->preflightReport().world_errors.empty());
    assert(w->ParseEnv(env));
    assert(w->RunQuestionPreflight()); // no cross-question leakage
    w->objects[2]->id = 9;
    assert(!w->RunQuestionPreflight());

    w = World();
    assert(w->ParseEnv("(sort 7 desk) (at 7 7)"));
    assert(w->RunQuestionPreflight()); // unrelated untyped object is local
    w = World();
    assert(w->ParseEnv("(sort 7 desk) (size 7 big) (at 7 7)"));
    w->ParseInstruction(Task("goto X", "(id X 6)")); // sparse placeholder
    assert(w->RunQuestionPreflight());
    assert(w->tasks.empty() && w->preflightReport().tasks.rejected == 1);
}

void RealPlanGate() {
    auto w = World();
    w->Fini();
    auto puton = Task("puton X Y", "(id X 2) (id Y 4)");
    w->SetTestInput(env, " \n(:ins " + puton + puton + ")");
    w->Plan();
    assert(w->preflightReport().tasks.raw == 2 && w->tasks.size() == 1);
    assert(!w->tasks[0].isMultiPuton);
    assert(w->GetScoreSnapshot().total_goals == 1);

    w->Fini();
    w->SetTestInput(env, puton + Task("puton X Y", "(id X 5) (id Y 4)"));
    w->Plan();
    assert(w->tasks.size() == 2 && w->tasks[0].isMultiPuton && w->tasks[1].isMultiPuton);

    w->Fini();
    const auto pickup = Task("pickup X", "(id X 2)");
    w->SetTestInput(env, pickup + Not(pickup) + Not(pickup));
    w->Plan();
    assert(w->preflightReport().constraints.duplicates == 1);
    assert(w->tasks[0].risk == 1); // execution may subsequently release the risk counter
    assert(w->GetScoreSnapshot().total_constraints == 1);

    w->Fini();
    w->SetTestInput("(sort 1 human) (size 1 big)", Task("goto X", "(id X 1)"));
    w->Plan();
    assert(!w->preflightReport().safe());
    assert(w->TestPlatformCalls() == 0); // no correction/Cons_plan/action on unsafe world

    w->Fini();
    w->SetTestInput(env, "close the closet. close the closet.");
    w->Plan();
    assert(w->preflightReport().tasks.duplicates == 1 && w->tasks.size() == 1);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    words = argv[1];
    DuplicateTasksAndScore();
    DuplicateConstraintRisk();
    GotoAndConflicts();
    CanonicalRelationsAndSets();
    MalformedConstraintRecovery();
    WorldSafetyAndStage2Unknowns();
    RealPlanGate();
    std::cout << "Question Preflight tests passed\n";
}
