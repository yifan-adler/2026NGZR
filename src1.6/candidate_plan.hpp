#pragma once

#include "score_evaluator.hpp"
#include "terminal_checker.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace _home {

struct CandidateAction {
    std::string name;
    std::vector<unsigned int> arguments;
    ActionCategory category;
    int cost;
    std::chrono::milliseconds estimated_duration;

    CandidateAction();
    CandidateAction(const std::string& action_name,
                    const std::vector<unsigned int>& action_arguments,
                    ActionCategory action_category);
};

// Deterministic deltas from ScoreEvaluator snapshots. Utility is the change
// in deterministic base score; the platform's time bonus is not predicted.
struct CandidatePrediction {
    int goal_gain = 0;
    int goal_loss = 0;
    int constraint_gain = 0;
    int constraint_loss = 0;
    std::size_t completed_goals = 0;
    std::size_t maintained_constraints = 0;
    std::size_t action_count = 0;
    int action_cost = 0;
    int final_reward = 0;
    int utility = 0;
};

struct CandidateEvidence {
    std::size_t object_id = 0;
    std::string fact;
    std::string source;
    bool verified = false;
};

struct CandidateActual {
    CandidatePrediction values;
    bool succeeded = false;
    bool failed_action = false;
    std::vector<std::string> failure_reasons;
};

struct PredictionError {
    int goal_gain = 0;
    int constraint_gain = 0;
    int constraint_loss = 0;
    int action_cost = 0;
    int utility = 0;
};

// A complete action sequence emitted by the existing planner for a task group.
// The constraint trade-off gate uses these score predictions before execution.
struct CandidatePlan {
    std::size_t candidate_id;
    std::size_t generated_ms;
    std::size_t world_revision;
    std::vector<std::size_t> task_indices;
    std::vector<CandidateEvidence> evidence;
    CandidatePrediction prediction;
    std::size_t task_index;
    std::string task_label;
    bool eligible;
    bool dry_run_succeeded;
    std::vector<CandidateAction> actions;
    TerminalSummary terminal_before;
    TerminalSummary terminal_after;
    ScoreSnapshot score_before;
    ScoreSnapshot score_after;
    std::vector<std::size_t> gained_goals;
    std::vector<std::size_t> lost_goals;
    std::vector<std::size_t> broken_constraints;
    std::vector<std::size_t> preserved_constraints;
    int action_cost;
    int marginal_score;
    int utility;
    std::chrono::milliseconds estimated_duration;
    std::size_t executed_actions;

    CandidatePlan();

    int remainingActionCost() const;
    std::chrono::milliseconds remainingDuration() const;
    int remainingUtility(const ScoreSnapshot& current) const;
};

struct CandidateExecutionRecord {
    CandidatePlan candidate;
    CandidateActual actual;
    PredictionError error;
};

CandidatePrediction MakeCandidatePrediction(
    const TerminalSummary& before, const TerminalSummary& after,
    const ScoreSnapshot& score_before, const ScoreSnapshot& score_after,
    std::size_t action_count);
PredictionError ComparePrediction(const CandidatePrediction& prediction,
                                  const CandidatePrediction& actual);

class CandidatePlanEvaluator {
public:
    static CandidatePlan evaluate(
        std::size_t task_index,
        const std::string& task_label,
        bool eligible,
        bool dry_run_succeeded,
        const std::vector<CandidateAction>& actions,
        const TerminalSummary& terminal_before,
        const TerminalSummary& terminal_after,
        const ScoreSnapshot& score_before,
        const ScoreSnapshot& score_after);
};

} // namespace _home
