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

bool constraintEvidenceUsable(const RDFW& world, unsigned int id) {
    const std::size_t offset = world.not_infoConstrains.size();
    for (std::size_t i = 0; i < world.notnot_infoConstrains.size(); ++i) {
        const std::size_t index = offset + i;
        if (index >= world.constraint_eligible.size()) continue;
        if (world.constraint_eligible[index] &&
            (index >= world.constraint_uncertain.size() || !world.constraint_uncertain[index])) continue;
        const Instruction& c = world.notnot_infoConstrains[i];
        for (const auto& x : c.X) if (x && x->id == id) return false;
        for (const auto& y : c.Y) if (y && y->id == id) return false;
        if (id < world.objects.size() && world.objects[id] &&
            (c.conditionX.IsObjectSatisfy(world.objects[id]) ||
             (c.isUseY && c.conditionY.IsObjectSatisfy(world.objects[id])))) return false;
    }
    return true;
}

bool isLocationKnown(const RDFW& world, const std::shared_ptr<Object>& object) {
    if (!object || object->location == _home::UNKNOWN) return false;
    if (object->id == 0) return world.location != _home::UNKNOWN;
    return world.stage == 1 || (world.IsLocationVerified(object->id) &&
        (world.LocationSource(object->id) != EvidenceSource::CONSTRAINT_DERIVED ||
         constraintEvidenceUsable(world, object->id)));
}

int scoreLocation(const RDFW& world, const std::shared_ptr<Object>& object) {
    if (!object) return _home::UNKNOWN;
    if (object->id == 0) return world.location;
    if (world.stage == 1 && object->id < world.score_locations.size())
        return world.score_locations[object->id];
    return object->location;
}

bool isInsideKnown(const RDFW& world, const std::shared_ptr<SmallObject>& object) {
    return object && object->inside != _home::UNKNOWN &&
           (world.stage == 1 || (world.IsInsideVerified(object->id) &&
            (world.InsideSource(object->id) != EvidenceSource::CONSTRAINT_DERIVED ||
             constraintEvidenceUsable(world, object->id))));
}

bool isContainerStateKnown(const RDFW& world,
                           const std::shared_ptr<Container>& container) {
    return container && (container->isOpen == 0 || container->isOpen == 1) &&
           (world.stage == 1 || (world.IsContainerStateVerified(container->id) &&
            (world.ContainerSource(container->id) != EvidenceSource::CONSTRAINT_DERIVED ||
             constraintEvidenceUsable(world, container->id))));
}

TerminalStatus boolStatus(bool value) {
    return value ? TerminalStatus::SATISFIED : TerminalStatus::UNSATISFIED;
}

std::vector<std::shared_ptr<Object>> scoreBindings(
    const RDFW& world, const Condition& condition,
    const std::vector<std::shared_ptr<Object>>& planner_bindings) {
    if (condition.sort.empty() && condition.color.empty() &&
        condition.declared_type.empty() && !condition.has_explicit_id)
        return planner_bindings;
    std::vector<std::shared_ptr<Object>> result;
    for (const auto& object : world.objects)
        if (object && object->id > 0 && condition.IsObjectSatisfy(object))
            result.push_back(object);
    return result;
}

TerminalStatus evaluatePair(const RDFW& world, const std::string& behave,
                            const std::shared_ptr<Object>& x,
                            const std::shared_ptr<Object>& y) {
    if (!x) return TerminalStatus::UNKNOWN;

    if (behave == "goto" || behave == "move") {
        if (world.stage == 1)
            return boolStatus(scoreLocation(world, x) != _home::UNKNOWN &&
                              world.location == scoreLocation(world, x));
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
        if (!small) return TerminalStatus::UNKNOWN;
        if (world.stage == 2 && !world.IsInsideVerified(x->id))
            return TerminalStatus::UNKNOWN;
        if (world.hold_id == x->id || world.plate_id == x->id)
            return TerminalStatus::UNSATISFIED;
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
        if (world.stage == 1 && target &&
            (scoreLocation(world, x) == _home::UNKNOWN ||
             scoreLocation(world, target) == _home::UNKNOWN))
            return TerminalStatus::UNSATISFIED;
        if (target && isLocationKnown(world, x) &&
            world.IsAbsentFromSensedLocation(target->id, x->location))
            return TerminalStatus::UNSATISFIED;
        if (target && isLocationKnown(world, target) &&
            world.IsAbsentFromSensedLocation(x->id, target->location))
            return TerminalStatus::UNSATISFIED;
        if (!target || !isLocationKnown(world, x) || !isLocationKnown(world, target))
            return TerminalStatus::UNKNOWN;

        if (behave == "puton" || behave == "give") {
            const std::shared_ptr<SmallObject> small =
                std::dynamic_pointer_cast<SmallObject>(x);
            if (!isInsideKnown(world, small)) return TerminalStatus::UNKNOWN;
            if (world.hold_id == x->id || world.plate_id == x->id)
                return TerminalStatus::UNSATISFIED;
        }
        return boolStatus(scoreLocation(world, x) == scoreLocation(world, target));
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

TerminalStatus TerminalChecker::evaluateTask(
    const RDFW& world, const Instruction& task) const {
    if (!task.IsUsable() || task.X.empty()) return TerminalStatus::UNKNOWN;
    const auto xs = scoreBindings(world, task.conditionX, task.X);
    const auto ys = scoreBindings(world, task.conditionY, task.Y);
    // The SDK selects exactly one grounding with 1{task(...):cond}1,
    // then reads the first answer set. Mixed groundings cannot prove a score.
    TerminalStatus result = TerminalStatus::UNKNOWN;
    bool first = true;
    for (const auto& x : xs) {
        const std::size_t count = ys.empty() ? 1 : ys.size();
        for (std::size_t i = 0; i < count; ++i) {
            const TerminalStatus next = evaluatePair(world, task.behave, x,
                ys.empty() ? nullptr : ys[i]);
            if (!first && result != next) return TerminalStatus::UNKNOWN;
            result = next;
            first = false;
        }
    }
    return result;
}

TerminalStatus TerminalChecker::evaluateConstraint(
    const RDFW& world, const Instruction& constraint, ConstraintKind kind) const {
    // cons(task) forbids the task's resulting predicate, not the command.
    // Each grounded constraint must hold; negate BEFORE combining bindings.
    if (!constraint.IsUsable() || constraint.X.empty()) return TerminalStatus::UNKNOWN;
    const auto xs = scoreBindings(world, constraint.conditionX, constraint.X);
    const auto ys = scoreBindings(world, constraint.conditionY, constraint.Y);
    TerminalStatus result = TerminalStatus::SATISFIED;
    for (const auto& x : xs) {
        const std::size_t count = ys.empty() ? 1 : ys.size();
        for (std::size_t i = 0; i < count; ++i) {
            TerminalStatus next = evaluatePair(world, constraint.behave, x,
                ys.empty() ? nullptr : ys[i]);
            if (kind != ConstraintKind::MUST_HOLD) next = invertStatus(next);
            result = combineStatus(result, next);
        }
    }
    return result;
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
        const bool certain = !has_ledger || world.constraint_uncertain.size() != world.constraint_eligible.size() ||
            !world.constraint_uncertain[result.constraints.size()-1];
        result.constraint_credited.push_back(eligible && certain);
        if (eligible && certain) ++result.credited_constraints;
        countStatus(status, result.satisfied_constraints,
                    result.unsatisfied_constraints, result.unknown_constraints);
    }
    for (std::size_t i = 0; i < world.notnot_infoConstrains.size(); ++i) {
        const TerminalStatus status = evaluateConstraint(
            world, world.notnot_infoConstrains[i], ConstraintKind::MUST_HOLD);
        result.constraints.push_back(status);
        const bool eligible = !has_ledger || world.constraint_eligible[result.constraints.size()-1];
        result.constraint_eligible.push_back(eligible);
        const bool certain = !has_ledger || world.constraint_uncertain.size() != world.constraint_eligible.size() ||
            !world.constraint_uncertain[result.constraints.size()-1];
        result.constraint_credited.push_back(eligible && certain);
        if (eligible && certain) ++result.credited_constraints;
        countStatus(status, result.satisfied_constraints,
                    result.unsatisfied_constraints, result.unknown_constraints);
    }
    for (std::size_t i = 0; i < world.not_taskConstrains.size(); ++i) {
        const TerminalStatus status = evaluateConstraint(
            world, world.not_taskConstrains[i], ConstraintKind::FORBIDDEN_TASK_STATE);
        result.constraints.push_back(status);
        const bool eligible = !has_ledger || world.constraint_eligible[result.constraints.size()-1];
        result.constraint_eligible.push_back(eligible);
        const bool certain = !has_ledger || world.constraint_uncertain.size() != world.constraint_eligible.size() ||
            !world.constraint_uncertain[result.constraints.size()-1];
        result.constraint_credited.push_back(eligible && certain);
        if (eligible && certain) ++result.credited_constraints;
        countStatus(status, result.satisfied_constraints,
                    result.unsatisfied_constraints, result.unknown_constraints);
    }
    return result;
}

} // namespace _home
