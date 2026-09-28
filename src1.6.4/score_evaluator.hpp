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

// Only disjoint intervals prove a strict score ordering. Touching/overlapping
// intervals retain the established planner's priority, including exact ties.
inline bool StrictlyScoreDominates(const ScoreSnapshot& winner,
                                  const ScoreSnapshot& loser) {
    return loser.possible_base_score < winner.deterministic_base_score;
}

class ScoreEvaluator {
public:
    ScoreEvaluator();

    static int costForAction(ActionCategory category);
    void reset();
    void recordAction(ActionCategory category);
    int accumulatedActionCost() const;
    ScoreSnapshot snapshot(const RDFW& world, const TerminalChecker& checker) const;
    ScoreSnapshot snapshot(const TerminalSummary& terminal) const;

private:
    int action_cost_;
};

} // namespace _home
