#include "rdfw.hpp"
#include <cassert>
#include <iostream>
#ifdef OFFICIAL_SCORE_TEST
#include "evaluate.h"
#endif

namespace _home {
struct ScoreSemanticsTestAccess {
    static bool Act(RDFW& w, char action) {
        switch (action) {
        case 'm': return w.Move(2);
        case 'o': return w.Open(2);
        case 'c': return w.Close(2);
        case 'p': return w.PutIn(3, 2);
        case 'a': w.AskLoc(3); return true;
        case 's': w.SenseCurrentLocationOnly(true); return true;
        }
        return false;
    }
    static void InitLedger(RDFW& w) { w.InitializeConstraintLedger(); }
    static void Update(RDFW& w) { w.UpdateConstraintLedger("Move", {2}); }
};
}
using namespace _home;

static const std::string domain =
    "(hold 0) (plate 0) (at 0 1) "
    "(sort 1 human) (size 1 big) (at 1 1) "
    "(sort 2 cupboard) (size 2 big) (type 2 container) (closed 2) (at 2 1) "
    "(sort 3 cup) (size 3 small) (color 3 red) (inside 3 2) (at 3 1) "
    "(sort 4 cup) (size 4 small) (color 4 blue) (at 4 2) "
    "(sort 5 table) (size 5 big) (at 5 2)";
static const std::string close_goal = "(:task (close X) (:cond (sort X cupboard))) ";

static void Run(const char* words, const char* name, const std::string& env,
                const std::string& instruction, const std::string& actions,
                int lower, int upper, bool fail = false) {
    auto w = std::make_shared<RDFW>();
    char program[] = "score_semantics_tests", path[] = "-path";
    char* args[] = {program, path, const_cast<char*>(words)};
    w->Init(3, args);
    w->stage = 1;
    assert(w->ParseEnv(env));
    assert(w->ParseInstruction(instruction));
    ScoreSemanticsTestAccess::InitLedger(*w);
    if (fail) w->SetActionResults({false});
#ifdef OFFICIAL_SCORE_TEST
    Evaluate sdk;
    sdk.newteam(name);
    const std::string et = "(:domain " + env + ")";
    const std::string it = "(:ins " + instruction + ")";
    assert(sdk.init_et(et.c_str(), et.size()));
    assert(sdk.init_it(it.c_str(), it.size()));
#endif
    for (char action : actions) {
        const bool ok = ScoreSemanticsTestAccess::Act(*w, action);
        assert(ok == !fail);
#ifdef OFFICIAL_SCORE_TEST
        bool actual = true;
        switch (action) {
        case 'm': actual = sdk.EvaluateMove(2); break;
        case 'o': actual = sdk.EvaluateOpen(2); break;
        case 'c': actual = sdk.EvaluateClose(2); break;
        case 'p': actual = sdk.EvaluatePutIn(3, 2); break;
        case 'a': sdk.EvaluateAskLoc(3); break;
        case 's': { std::vector<unsigned int> ids; sdk.EvaluateSense(ids); break; }
        }
        assert(actual == ok);
#endif
    }
    const auto score = w->GetScoreSnapshot();
    std::cerr << name << " internal=" << score.deterministic_base_score << ":"
              << score.possible_base_score << " expected=" << lower << ":" << upper << '\n';
    assert(score.deterministic_base_score == lower);
    assert(score.possible_base_score == upper);
#ifdef OFFICIAL_SCORE_TEST
    // Exactly 5 seconds removes only the time bonus, preserving SDK scoring.
    const int official = sdk.EndEvaluation(5.0);
    assert(official >= lower && official <= upper);
    for (const char* file : {"vstate.lp", "vtask.lp", "vcons.lp", "vanswer.txt"}) {
        std::ifstream input(file);
        std::ofstream output(std::string(name) + "." + file);
        output << input.rdbuf();
    }
    std::cout << "SDK " << name << " official=" << official << " bounds="
              << lower << ":" << upper << '\n';
#endif
}

int main(int argc, char** argv) {
    assert(argc == 2);
    Run(argv[1], "putdown_inside", domain,
        "(:task (putdown X) (:cond (sort X cup) (color X red)))", "", 40, 40);
    const std::string puton =
        "(:task (puton X Y) (:cond (sort X cup) (color X red) (sort Y cupboard))) ";
    Run(argv[1], "puton_inside_explicit_at", domain, puton, "", 40, 40);
    std::string no_at = domain;
    no_at.replace(no_at.find("(at 3 1)"), 8, "");
    Run(argv[1], "inside_does_not_imply_at", no_at, puton, "", 0, 0);
    const std::string must_open =
        "(:cons_notnot (:info (opened X) (:cond (sort X cupboard)))) ";
    Run(argv[1], "initial_not_checked", domain, close_goal + must_open, "", 60, 60);
    Run(argv[1], "observations_not_persisted", domain, close_goal + must_open, "as", 57, 57);
    Run(argv[1], "first_transition_restores", domain,
        "(:task (open X) (:cond (sort X cupboard))) " + must_open, "o", 58, 58);
    Run(argv[1], "permanent_loss", domain, close_goal + must_open, "oc", 36, 36);
    Run(argv[1], "task_constraint_checks_state", domain, close_goal +
        "(:cons_not (:task (putdown X) (:cond (sort X cup) (color X red))))", "m", 36, 36);
    Run(argv[1], "negate_each_binding", domain, close_goal +
        "(:cons_not (:info (near X Y) (:cond (sort X cup) (sort Y cupboard))))", "m", 36, 36);
    Run(argv[1], "zero_goal_gate", domain,
        "(:task (open X) (:cond (sort X cupboard))) " + must_open, "", 0, 0);
    Run(argv[1], "ambiguous_choice", domain,
        "(:task (putin X Y) (:cond (sort X cup) (sort Y cupboard)))", "", 0, 40);
    Run(argv[1], "all_choices_true", domain,
        "(:task (putdown X) (:cond (sort X cup)))", "", 40, 40);
    std::string opened = domain;
    opened.replace(opened.find("(closed 2)"), 10, "(opened 2)");
    Run(argv[1], "failed_action_charged", opened,
        "(:task (open X) (:cond (sort X cupboard))) "
        "(:cons_not (:info (opened X) (:cond (sort X cupboard))))", "o", 58, 58, true);
    std::string carried = domain;
    carried.replace(carried.find("(hold 0)"), 8, "(hold 3)");
    carried.replace(carried.find("(inside 3 2)"), 12, "");
    Run(argv[1], "on_while_carried", carried, close_goal +
        "(:cons_notnot (:info (on X Y) (:cond (sort X cup) (color X red) (sort Y table))))", "m", 56, 56);
    Run(argv[1], "pickup_constraint_without_pickup", carried, close_goal +
        "(:cons_not (:task (pickup X) (:cond (sort X cup) (color X red))))", "m", 36, 36);
    std::string ready_putin = carried;
    ready_putin.replace(ready_putin.find("(closed 2)"), 10, "(opened 2)");
    Run(argv[1], "putin_removes_at", ready_putin, puton +
        "(:task (putin X Y) (:cond (sort X cup) (color X red) (sort Y cupboard)))", "p", 38, 38);

    auto w = std::make_shared<RDFW>();
    char program[] = "uncertain_history", path[] = "-path";
    char* args[] = {program, path, argv[1]};
    w->Init(3, args);
    w->stage = 1;
    assert(w->ParseEnv(domain));
    assert(w->ParseInstruction(close_goal + must_open));
    ScoreSemanticsTestAccess::InitLedger(*w);
    w->stage = 2;
    w->containerStateVerified[2] = false;
    ScoreSemanticsTestAccess::Update(*w);
    assert(w->constraint_uncertain[0]);
    w->containerStateVerified[2] = true;
    std::dynamic_pointer_cast<Container>(w->objects[2])->isOpen = 1;
    ScoreSemanticsTestAccess::Update(*w);
    assert(w->constraint_uncertain[0]); // recovery cannot prove past truth
    assert(w->GetTerminalSummary().credited_constraints == 0);
    const auto saved = w->constraint_uncertain;
    w->PreviewTaskGroupPlan({0});
    assert(w->constraint_uncertain == saved);
    w->constraint_eligible[0] = false;
    w->constraint_uncertain[0] = false;
    w->containerStateSource[2] = EvidenceSource::CONSTRAINT_DERIVED;
    std::dynamic_pointer_cast<Container>(w->objects[2])->isOpen = 0;
    assert(w->GetTerminalSummary().goals[0] == TerminalStatus::UNKNOWN);
    std::cout << "score semantics tests passed\n";
    return 0;
}
