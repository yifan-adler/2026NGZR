#include "rdfw.hpp"
#include <cassert>
#include <iostream>
using namespace _home;

int main(int argc, char** argv) {
    assert(argc == 2);
    auto w = std::make_shared<RDFW>();
    char name[] = "legal_preservation", path[] = "-path";
    char* args[] = {name, path, argv[1]};
    w->Init(3, args);
    w->stage = 1;
    assert(!w->deduplicate_input);
    // Facts can be reordered; late size/type must not erase closed state.
    assert(w->ParseEnv("(:domain (at 0 0) (closed 3) (type 3 container) "
        "(size 3 big) (sort 3 closet) (at 3 3) "
        "(color 2 red) (size 2 small) (sort 2 cup) (at 2 2) "
        "(sort 1 human) (size 1 big) (at 1 1))"));
    assert(std::dynamic_pointer_cast<Container>(w->objects[3])->isOpen == 0);
    const std::string close = "(:task (close X) (:cond (sort X closet)))";
    const std::string cons = "(:cons_not (:task (pickup X) (:cond (sort X cup))))";
    assert(w->ParseInstruction("(:ins " + close + close + cons + cons + ")"));
    assert(w->RunQuestionPreflight());
    assert(w->tasks.size() == 2 && w->not_taskConstrains.size() == 2);
    assert(w->preflightReport().tasks.unique == 1 && w->preflightReport().tasks.duplicates == 1);
    w->Cons_plan();
    assert(w->pickup_cons[2] == 2); // preserve legacy constraint weight
    TerminalChecker tc;
    ScoreEvaluator se;
    assert(se.snapshot(*w,tc).total_goals == 2);
    assert(tc.evaluateAll(*w).goals.size() == 2);
    assert(w->RunQuestionPreflight() && w->tasks.size() == 2); // idempotent
    w->ParseNaturalLanguage("give the red cup to the human.");
    assert(w->tasks.size() == 3 && w->tasks.back().behave == "give");
    assert(w->tasks.back().isUseY && w->tasks.back().Y[0]->sort == "human");
    assert(w->RunQuestionPreflight() && w->tasks.size() == 3);
    w->tasks.pop_back();
    w->deduplicate_input = true;
    assert(w->RunQuestionPreflight() && w->tasks.size() == 1 && w->not_taskConstrains.size() == 1);

    w->Fini();
    w->deduplicate_input = false;
    // A nonstandard human id / collocated big objects / unrelated sparse
    // untyped object cannot veto otherwise usable tasks.
    assert(w->ParseEnv("(at 0 0) (sort 2 cup) (size 2 small) (at 2 1) "
        "(sort 3 closet) (size 3 big) (type 3 container) (at 3 1) "
        "(sort 4 human) (size 4 big) (at 4 1) (sort 7 desk)"));
    assert(w->ParseInstruction("( :ins ( \n:task (give human X) ( :cond (sort X cup))) "
        "( :cons_not ( :task (open X) (:cond (sort X closet)))))"));
    assert(w->tasks.size() == 1 && w->not_taskConstrains.size() == 1);
    assert(w->RunQuestionPreflight());
    assert(w->tasks[0].behave == "give");
    w->objects[4]->sort = "table";
    w->tasks.clear();
    assert(w->ParseInstruction(close));
    assert(w->RunQuestionPreflight() && w->tasks.size() == 1); // no human needed

    w->Fini();
    assert(w->ParseEnv("(at 0 1) (sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cup) (size 2 small) (at 2 2) "
        "(sort 3 closet) (size 3 big) (type 3 container) (at 3 3)"));
    // Recover a complete simple payload, but never turn a malformed
    // constraint's pickup into an executable task.
    assert(w->ParseInstruction("(:ins (:task (pickup X) (:cond (sort X cup))" + close + ")"));
    assert(w->tasks.size() == 2 && w->tasks[0].behave == "pickup");
    w->tasks.clear();
    assert(w->ParseInstruction("(:ins (:cons_not (:task (pickup X) (:cond (sort X cup)))" + close + ")"));
    assert(w->tasks.size() == 1 && w->tasks[0].behave == "close");
    w->Fini();
    std::cout << "legal preservation tests passed\n";
}
