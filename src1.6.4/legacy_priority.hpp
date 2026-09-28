#pragma once
#include "terminal_checker.hpp"

namespace _home {
// Frozen 1.6.1 predicate heuristic, used only to preserve decision priority.
// It is neither an official score estimate nor a lower/upper bound.
class LegacyPriorityChecker {
public:
    TerminalStatus evaluatePredicate(const RDFW&, const Instruction&) const;
    TerminalStatus evaluateTask(const RDFW&, const Instruction&) const;
    TerminalStatus evaluateConstraint(const RDFW&, const Instruction&, ConstraintKind) const;
    TerminalSummary evaluateAll(const RDFW&) const;
};
}
