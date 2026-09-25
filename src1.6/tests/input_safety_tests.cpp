#include "rdfw.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace _home;

namespace {

std::shared_ptr<RDFW> MakeWorld(const char* words_path) {
    std::shared_ptr<RDFW> world = std::make_shared<RDFW>();
    char program[] = "input_safety_tests";
    char path_option[] = "-path";
    char* init_args[] = {program, path_option, const_cast<char*>(words_path)};
    world->Init(3, init_args);
    world->stage = 1;
    const bool established = world->ParseEnv(
        "(at 0 1) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cup) (size 2 small) (color 2 red) (at 2 2) "
        "(sort 3 closet) (size 3 big) (type 3 container) (closed 3) (at 3 3) "
        "(sort 4 table) (size 4 big) (at 4 4)");
    assert(established);
    return world;
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    std::shared_ptr<RDFW> world = MakeWorld(argv[1]);

    // Negative and huge ids are isolated; neither can become a vector index or
    // trigger an unbounded allocation.
    const std::size_t base_object_count = world->objects.size();
    assert(world->ParseEnv("(sort -1 cup) (at 0 1)"));
    assert(world->objects.size() == base_object_count);
    assert(world->ParseEnv("(sort 999999999 cup) (at 0 1)"));
    assert(world->objects.size() == base_object_count);
    const std::size_t base_location_count = world->rightlocation.size();
    assert(world->ParseEnv("(at 2 999999999) (at 0 1)"));
    assert(world->rightlocation.size() == base_location_count);
    assert(world->ParseEnv("(at 2 12junk) (at 0 1)"));
    assert(world->rightlocation.size() == base_location_count);
    assert(!world->ParseEnv(std::string(MAX_INPUT_BYTES + 1, 'x')));
    assert(world->objects.size() == base_object_count);

    // Missing fields and invalid cross references are local errors.
    assert(world->ParseEnv("(sort 8) (at 0 1)"));
    assert(world->objects.size() == base_object_count);
    assert(world->ParseEnv("(inside 2 99) (at 0 1)"));
    assert(std::dynamic_pointer_cast<SmallObject>(world->objects[2])->inside == UNKNOWN);
    assert(world->ParseEnv("(hold 3) (at 0 1)"));
    assert(world->hold_id == NONE && !world->hold);

    // A normal platform :domain wrapper must expose its leaf facts without
    // treating the wrapper itself as a malformed fact.
    assert(world->ParseEnv(
        "(:domain (hold 0) (sort 5 bowl) (size 5 small) (at 5 5))"));
    assert(world->IsValidObjectId(5));
    assert(world->objects[5]->sort == "bowl" && world->objects[5]->location == 5);

    // The largest supported sparse id is bounded and all object/location
    // matrices grow in the correct dimension.
    assert(world->ParseEnv(
        "(sort 255 bottle) (size 255 small) (at 255 12) (at 0 1)"));
    assert(world->objects.size() == 256);
    assert(world->IsValidObjectId(255));
    assert(world->move_cons.size() > 255 && world->move_cons[255].size() > 12);
    assert(world->putin_cons.size() > 255 && world->putin_cons[255].size() > 255);

    const std::size_t discarded_before = world->discardedInstructionCount();
    const std::size_t tasks_before = world->tasks.size();

    // Missing explicit type is inferred from the actual Container object.
    assert(world->ParseInstruction(
        "(:task (open X) (:cond (sort X closet)))"));
    assert(world->tasks.size() == tasks_before + 1);
    assert(world->tasks.back().validationStatus ==
           Instruction::ValidationStatus::INFERRED);

    // Wrong runtime type, invalid id, missing field, empty binding, invalid
    // binding name, wrong arity, and an empty action node are all rejected.
    assert(world->ParseInstruction(
        "(:task (open X) (:cond (id X 2)))"));
    assert(world->ParseInstruction(
        "(:task (pickup X) (:cond (id X -1)))"));
    assert(world->ParseInstruction(
        "(:task (pickup X) (:cond (sort X)))"));
    assert(world->ParseInstruction(
        "(:task (pickup X) (:cond (sort X does_not_exist)))"));
    assert(world->ParseInstruction(
        "(:task (putin X) (:cond (sort X cup)))"));
    assert(world->ParseInstruction(
        "(:task (putin X Y) (:cond (sort X cup) (sort Z closet)))"));
    assert(world->ParseInstruction(
        "(:task () (:cond (sort X cup)))"));
    assert(world->tasks.size() == tasks_before + 1);

    // A malformed task is isolated at the next instruction marker; the valid
    // task after it is retained.
    const std::size_t before_recovery = world->tasks.size();
    assert(world->ParseInstruction(
        "(:ins\n"
        "  (:task (pickup X) (:cond (sort X cup))\n"
        "  (:task (close X) (:cond (sort X closet)))\n"
        ")"));
    assert(world->tasks.size() == before_recovery + 2);
    assert(world->tasks.back().behave == "close");

    // A resolved binary binding can repair stale parser metadata without
    // weakening runtime type validation.
    Instruction repaired;
    repaired.behave = "putin";
    repaired.X.push_back(world->objects[2]);
    repaired.Y.push_back(world->objects[3]);
    assert(world->ValidateInstruction(repaired, RDFW::InstructionKind::TASK));
    assert(repaired.isUseY);
    assert(repaired.validationStatus == Instruction::ValidationStatus::REPAIRED);

    Instruction bad_reference;
    bad_reference.behave = "pickup";
    bad_reference.X.push_back(std::make_shared<SmallObject>(world->objects[2]));
    assert(!world->ValidateInstruction(bad_reference,
                                       RDFW::InstructionKind::TASK));

    // Empty/malformed NL input must not dereference an empty syntax tree.
    assert(!world->ParseNaturalLanguageSentence(""));
    assert(!world->ParseNaturalLanguageSentence("next to."));

    // ObjectPtrCast now reports a standard exception type.
    bool caught_standard_exception = false;
    try {
        (void)ObjectPtrCast<Container>(world->objects[2]);
    } catch (const std::runtime_error&) {
        caught_standard_exception = true;
    }
    assert(caught_standard_exception);
    assert(world->discardedInstructionCount() >= discarded_before + 7);

    std::cout << "input/index safety tests passed\n";
    return 0;
}
