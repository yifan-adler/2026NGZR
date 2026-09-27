#pragma once

#include "terminal_checker.hpp"

#include <cstddef>

namespace _home {

class RDFW;

enum class ActionCategory {
    MOVE,
    HUMAN_INTERACTION,
    PHYSICAL,
    OBSERVATION
};

struct ScoreSnapshot {
    std::size_t completed_goals;
    std::size_t total_goals;
    std::size_t satisfied_constraints;
    std::size_t total_constraints;
    std::size_t unknown_goals;
    std::size_t unknown_constraints;
    int action_cost;
    int deterministic_base_score;
    int possible_base_score;

    ScoreSnapshot();
};

class ScoreEvaluator {
public:
    ScoreEvaluator();

    static int costForAction(ActionCategory category);
    void reset();
    void recordAction(ActionCategory category);
    int accumulatedActionCost() const;
    ScoreSnapshot snapshot(const RDFW& world, const TerminalChecker& checker) const;

private:
    int action_cost_;
};

} // namespace _home
