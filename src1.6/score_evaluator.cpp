#include "score_evaluator.hpp"
#include "rdfw.hpp"

namespace _home {

ScoreSnapshot::ScoreSnapshot()
    : completed_goals(0), total_goals(0), satisfied_constraints(0),
      total_constraints(0), unknown_goals(0), unknown_constraints(0),
      action_cost(0), deterministic_base_score(0), possible_base_score(0) {
}

ScoreEvaluator::ScoreEvaluator() : action_cost_(0) {
}

void ScoreEvaluator::reset() {
    action_cost_ = 0;
}

int ScoreEvaluator::costForAction(ActionCategory category) {
    switch (category) {
    case ActionCategory::MOVE:
        return 4;
    case ActionCategory::HUMAN_INTERACTION:
    case ActionCategory::PHYSICAL:
        return 2;
    case ActionCategory::OBSERVATION:
        return 1;
    }
    return 0;
}

void ScoreEvaluator::recordAction(ActionCategory category) {
    action_cost_ += costForAction(category);
}

int ScoreEvaluator::accumulatedActionCost() const {
    return action_cost_;
}

ScoreSnapshot ScoreEvaluator::snapshot(const RDFW& world,
                                       const TerminalChecker& checker) const {
    const TerminalSummary terminal = checker.evaluateAll(world);
    ScoreSnapshot result;
    result.completed_goals = terminal.satisfied_goals;
    result.total_goals = terminal.goals.size();
    result.satisfied_constraints = terminal.credited_constraints;
    result.total_constraints = terminal.constraints.size();
    result.unknown_goals = terminal.unknown_goals;
    result.unknown_constraints = terminal.unknown_constraints;
    result.action_cost = action_cost_;

    const int goal_points = static_cast<int>(result.completed_goals) * 40;
    const int constraint_points = result.completed_goals == 0
        ? 0 : static_cast<int>(result.satisfied_constraints) * 20;
    result.deterministic_base_score = goal_points + constraint_points - action_cost_;
    const std::size_t possible_goals = terminal.satisfied_goals + terminal.unknown_goals;
    std::size_t possible_constraints = terminal.credited_constraints;
    for (std::size_t i = 0; i < terminal.constraints.size(); ++i)
        if (terminal.constraint_eligible[i] &&
            terminal.constraints[i] == TerminalStatus::UNKNOWN &&
            !terminal.constraint_credited[i]) ++possible_constraints;
    result.possible_base_score = static_cast<int>(possible_goals) * 40 +
        (possible_goals ? static_cast<int>(possible_constraints) * 20 : 0) - action_cost_;
    return result;
}

} // namespace _home
