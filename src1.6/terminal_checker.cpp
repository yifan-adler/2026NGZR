#include "terminal_checker.hpp"
#include "rdfw.hpp"

#include <memory>
#include <string>

namespace _home {
namespace {

TerminalStatus invertStatus(TerminalStatus status) {
    if (status == TerminalStatus::SATISFIED) return TerminalStatus::UNSATISFIED;
    if (status == TerminalStatus::UNSATISFIED) return TerminalStatus::SATISFIED;
    return TerminalStatus::UNKNOWN;
}

TerminalStatus combineStatus(TerminalStatus aggregate, TerminalStatus next) {
    if (aggregate == TerminalStatus::UNSATISFIED || next == TerminalStatus::UNSATISFIED)
        return TerminalStatus::UNSATISFIED;
    if (aggregate == TerminalStatus::UNKNOWN || next == TerminalStatus::UNKNOWN)
        return TerminalStatus::UNKNOWN;
    return TerminalStatus::SATISFIED;
}

bool isLocationKnown(const RDFW& world, const std::shared_ptr<Object>& object) {
    if (!object || object->location == _home::UNKNOWN) return false;
    if (object->id == 0) return world.location != _home::UNKNOWN;
    return world.stage == 1 || world.IsLocationVerified(object->id);
}

bool isInsideKnown(const RDFW& world, const std::shared_ptr<SmallObject>& object) {
    return object && object->inside != _home::UNKNOWN &&
           (world.stage == 1 || world.IsInsideVerified(object->id));
}

bool isContainerStateKnown(const RDFW& world,
                           const std::shared_ptr<Container>& container) {
    return container && (container->isOpen == 0 || container->isOpen == 1) &&
           (world.stage == 1 || world.IsContainerStateVerified(container->id));
}

TerminalStatus boolStatus(bool value) {
    return value ? TerminalStatus::SATISFIED : TerminalStatus::UNSATISFIED;
}

TerminalStatus evaluatePair(const RDFW& world, const std::string& behave,
                            const std::shared_ptr<Object>& x,
                            const std::shared_ptr<Object>& y) {
    if (!x) return TerminalStatus::UNKNOWN;

    if (behave == "goto" || behave == "move") {
        if (world.location != _home::UNKNOWN &&
            world.IsAbsentFromSensedLocation(x->id, world.location))
            return TerminalStatus::UNSATISFIED;
        if (!isLocationKnown(world, x) || world.location == _home::UNKNOWN)
            return TerminalStatus::UNKNOWN;
        return boolStatus(world.location == x->location);
    }

    if (behave == "open" || behave == "opened" ||
        behave == "close" || behave == "closed") {
        const std::shared_ptr<Object> target = y ? y : x;
        const std::shared_ptr<Container> container =
            std::dynamic_pointer_cast<Container>(target);
        if (!isContainerStateKnown(world, container)) return TerminalStatus::UNKNOWN;
        const bool expect_open = behave == "open" || behave == "opened";
        return boolStatus(static_cast<bool>(container->isOpen) == expect_open);
    }

    if (behave == "pickup") {
        const bool stored = world.hold_id == x->id || world.plate_id == x->id;
        if (world.stage == 2 && !world.IsInsideVerified(x->id))
            return TerminalStatus::UNKNOWN;
        return boolStatus(stored);
    }

    if (behave == "putdown") {
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(x);
        if (!isInsideKnown(world, small)) return TerminalStatus::UNKNOWN;
        if (world.hold_id == x->id || world.plate_id == x->id)
            return TerminalStatus::UNSATISFIED;
        if (small->inside != _home::NONE) return TerminalStatus::UNSATISFIED;
        if (!isLocationKnown(world, x)) return TerminalStatus::UNKNOWN;
        return TerminalStatus::SATISFIED;
    }

    if (behave == "putin" || behave == "inside" || behave == "in") {
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(x);
        if (!small || !y || !isInsideKnown(world, small)) return TerminalStatus::UNKNOWN;
        return boolStatus(small->inside == y->id);
    }

    if (behave == "takeout") {
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(x);
        if (!small || !y || !isInsideKnown(world, small)) return TerminalStatus::UNKNOWN;
        return boolStatus(small->inside != y->id);
    }

    if (behave == "puton" || behave == "on" || behave == "near" ||
        behave == "nextto" || behave == "give") {
        std::shared_ptr<Object> target = y;
        if (behave == "give") target = world.human;
        if (target && isLocationKnown(world, x) &&
            world.IsAbsentFromSensedLocation(target->id, x->location))
            return TerminalStatus::UNSATISFIED;
        if (target && isLocationKnown(world, target) &&
            world.IsAbsentFromSensedLocation(x->id, target->location))
            return TerminalStatus::UNSATISFIED;
        if (!target || !isLocationKnown(world, x) || !isLocationKnown(world, target))
            return TerminalStatus::UNKNOWN;

        if (behave == "puton" || behave == "on" || behave == "give") {
            const std::shared_ptr<SmallObject> small =
                std::dynamic_pointer_cast<SmallObject>(x);
            if (!isInsideKnown(world, small)) return TerminalStatus::UNKNOWN;
            if (world.hold_id == x->id || world.plate_id == x->id)
                return TerminalStatus::UNSATISFIED;
            if (small->inside != _home::NONE) return TerminalStatus::UNSATISFIED;
        }
        return boolStatus(x->location == target->location);
    }

    if (behave == "plate") {
        if (world.stage == 2 && !world.IsInsideVerified(x->id))
            return TerminalStatus::UNKNOWN;
        return boolStatus(world.plate_id == x->id);
    }

    if (behave == "hold") {
        if (world.stage == 2 && !world.IsInsideVerified(x->id))
            return TerminalStatus::UNKNOWN;
        return boolStatus(world.hold_id == x->id);
    }

    return TerminalStatus::UNKNOWN;
}

void countStatus(TerminalStatus status, std::size_t& satisfied,
                 std::size_t& unsatisfied, std::size_t& unknown) {
    if (status == TerminalStatus::SATISFIED) ++satisfied;
    else if (status == TerminalStatus::UNSATISFIED) ++unsatisfied;
    else ++unknown;
}

} // namespace

const char* TerminalStatusName(TerminalStatus status) {
    switch (status) {
    case TerminalStatus::SATISFIED: return "SATISFIED";
    case TerminalStatus::UNSATISFIED: return "UNSATISFIED";
    case TerminalStatus::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

TerminalSummary::TerminalSummary()
    : satisfied_goals(0), unsatisfied_goals(0), unknown_goals(0),
      satisfied_constraints(0), unsatisfied_constraints(0),
      unknown_constraints(0), credited_constraints(0) {
}

bool TerminalSummary::allGoalsSatisfied() const {
    return !goals.empty() && satisfied_goals == goals.size();
}

TerminalStatus TerminalChecker::evaluatePredicate(
    const RDFW& world, const Instruction& instruction) const {
    if (!instruction.IsUsable() || instruction.X.empty())
        return TerminalStatus::UNKNOWN;

    TerminalStatus aggregate = TerminalStatus::SATISFIED;
    for (std::size_t xi = 0; xi < instruction.X.size(); ++xi) {
        if (instruction.Y.empty()) {
            aggregate = combineStatus(
                aggregate,
                evaluatePair(world, instruction.behave, instruction.X[xi], nullptr));
        } else {
            for (std::size_t yi = 0; yi < instruction.Y.size(); ++yi) {
                aggregate = combineStatus(
                    aggregate,
                    evaluatePair(world, instruction.behave,
                                 instruction.X[xi], instruction.Y[yi]));
            }
        }
    }
    return aggregate;
}

TerminalStatus TerminalChecker::evaluateTask(
    const RDFW& world, const Instruction& task) const {
    return evaluatePredicate(world, task);
}

TerminalStatus TerminalChecker::evaluateConstraint(
    const RDFW& world, const Instruction& constraint, ConstraintKind kind) const {
    // A prohibited action is a trajectory property.  A current World State cannot
    // prove that it was never executed, so deterministic terminal evaluation is UNKNOWN.
    if (kind == ConstraintKind::PROHIBITED_ACTION) return TerminalStatus::UNKNOWN;
    const TerminalStatus predicate = evaluatePredicate(world, constraint);
    return kind == ConstraintKind::MUST_NOT_HOLD ? invertStatus(predicate) : predicate;
}

TerminalSummary TerminalChecker::evaluateAll(const RDFW& world) const {
    TerminalSummary result;
    result.goals.reserve(world.tasks.size());
    for (std::size_t i = 0; i < world.tasks.size(); ++i) {
        const TerminalStatus status = evaluateTask(world, world.tasks[i]);
        result.goals.push_back(status);
        countStatus(status, result.satisfied_goals,
                    result.unsatisfied_goals, result.unknown_goals);
    }

    result.constraints.reserve(world.not_infoConstrains.size() +
                               world.notnot_infoConstrains.size() +
                               world.not_taskConstrains.size());
    const bool has_ledger = world.constraint_eligible.size() ==
        world.not_infoConstrains.size() + world.notnot_infoConstrains.size() +
        world.not_taskConstrains.size();
    for (std::size_t i = 0; i < world.not_infoConstrains.size(); ++i) {
        const TerminalStatus status = evaluateConstraint(
            world, world.not_infoConstrains[i], ConstraintKind::MUST_NOT_HOLD);
        result.constraints.push_back(status);
        const bool eligible = !has_ledger || world.constraint_eligible[result.constraints.size()-1];
        result.constraint_eligible.push_back(eligible);
        result.constraint_credited.push_back(eligible && status == TerminalStatus::SATISFIED);
        if (eligible && status == TerminalStatus::SATISFIED) ++result.credited_constraints;
        countStatus(status, result.satisfied_constraints,
                    result.unsatisfied_constraints, result.unknown_constraints);
    }
    for (std::size_t i = 0; i < world.notnot_infoConstrains.size(); ++i) {
        const TerminalStatus status = evaluateConstraint(
            world, world.notnot_infoConstrains[i], ConstraintKind::MUST_HOLD);
        result.constraints.push_back(status);
        const bool eligible = !has_ledger || world.constraint_eligible[result.constraints.size()-1];
        result.constraint_eligible.push_back(eligible);
        result.constraint_credited.push_back(eligible && status == TerminalStatus::SATISFIED);
        if (eligible && status == TerminalStatus::SATISFIED) ++result.credited_constraints;
        countStatus(status, result.satisfied_constraints,
                    result.unsatisfied_constraints, result.unknown_constraints);
    }
    for (std::size_t i = 0; i < world.not_taskConstrains.size(); ++i) {
        const TerminalStatus status = evaluateConstraint(
            world, world.not_taskConstrains[i], ConstraintKind::PROHIBITED_ACTION);
        result.constraints.push_back(status);
        const bool eligible = !has_ledger || world.constraint_eligible[result.constraints.size()-1];
        result.constraint_eligible.push_back(eligible);
        result.constraint_credited.push_back(eligible);
        // A prohibited action remains eligible until that action occurs.
        if (eligible) ++result.credited_constraints;
        countStatus(status, result.satisfied_constraints,
                    result.unsatisfied_constraints, result.unknown_constraints);
    }
    return result;
}

} // namespace _home
