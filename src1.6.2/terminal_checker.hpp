#pragma once

#include <cstddef>
#include <vector>

namespace _home {

class RDFW;
class Instruction;

enum class TerminalStatus {
    SATISFIED,
    UNSATISFIED,
    UNKNOWN
};

enum class ConstraintKind {
    MUST_HOLD,
    MUST_NOT_HOLD,
    FORBIDDEN_TASK_STATE
};

const char* TerminalStatusName(TerminalStatus status);

struct TerminalSummary {
    std::vector<TerminalStatus> goals;
    std::vector<TerminalStatus> constraints;
    // Current predicate truth and trajectory eligibility are independent.
    std::vector<bool> constraint_eligible;
    std::vector<bool> constraint_credited;
    std::size_t satisfied_goals;
    std::size_t unsatisfied_goals;
    std::size_t unknown_goals;
    std::size_t satisfied_constraints;
    std::size_t unsatisfied_constraints;
    std::size_t unknown_constraints;
    std::size_t credited_constraints;

    TerminalSummary();
    bool allGoalsSatisfied() const;
};

// Pure, read-only interpretation of the current world state.  It deliberately
// does not inspect isEnable, solved_task_num, or plan existence. Constraint
// credits read the separate irreversible trajectory ledger.
class TerminalChecker {
public:
    TerminalStatus evaluateTask(const RDFW& world, const Instruction& task) const;
    TerminalStatus evaluateConstraint(const RDFW& world,
                                      const Instruction& constraint,
                                      ConstraintKind kind) const;
    TerminalSummary evaluateAll(const RDFW& world) const;

};

} // namespace _home
