#include "task_group_search.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace _home {
namespace {

struct Node {
    CandidatePlan plan;
    std::size_t restorations;
    Node(const CandidatePlan& value, std::size_t count)
        : plan(value), restorations(count) {}
};

bool Better(const CandidatePlan& a, const CandidatePlan& b) {
    if (a.score_after.deterministic_base_score != b.score_after.deterministic_base_score)
        return a.score_after.deterministic_base_score > b.score_after.deterministic_base_score;
    if (a.action_cost != b.action_cost) return a.action_cost < b.action_cost;
    if (a.broken_constraints.size() != b.broken_constraints.size())
        return a.broken_constraints.size() < b.broken_constraints.size();
    return a.task_indices < b.task_indices;
}

void Consider(const CandidatePlan& plan, TaskGroupSearchResult& result) {
    if (!plan.dry_run_succeeded) return;
    if (plan.broken_constraints.empty()) {
        if (!result.has_safe || Better(plan, result.best_safe)) result.best_safe = plan;
        result.has_safe = true;
    } else {
        if (!result.has_broken || Better(plan, result.best_broken)) result.best_broken = plan;
        result.has_broken = true;
    }
}

// Keep both classes in the beam, including negative prefixes that can share a
// one-time constraint loss with later tasks. Beam limits are resource cuts, not
// proofs that a discarded branch cannot improve the score.
std::vector<Node> SelectBeam(std::vector<Node> nodes, std::size_t width) {
    std::stable_sort(nodes.begin(), nodes.end(),
        [](const Node& a, const Node& b) { return Better(a.plan, b.plan); });
    std::vector<Node> selected;
    if (width == 0) return selected;
    std::set<std::vector<std::size_t> > chosen;
    for (std::size_t turn = 0; turn < width; ++turn) {
        const bool prefer_safe = turn % 2 == 0;
        const Node* choice = nullptr;
        for (int fallback = 0; fallback < 2 && !choice; ++fallback) {
            const bool safe = fallback == 0 ? prefer_safe : !prefer_safe;
            for (int diversity = 0; diversity < 2 && !choice; ++diversity) {
                for (const Node& node : nodes) {
                    if (node.plan.broken_constraints.empty() != safe ||
                        chosen.count(node.plan.task_indices)) continue;
                    bool distinct = true;
                    for (const Node& old : selected)
                        if (old.plan.broken_constraints == node.plan.broken_constraints &&
                            old.plan.projected_location == node.plan.projected_location)
                            distinct = false;
                    if (diversity == 0 && !distinct) continue;
                    choice = &node;
                    break;
                }
            }
        }
        if (!choice) break;
        selected.push_back(*choice);
        chosen.insert(choice->plan.task_indices);
    }
    return selected;
}

int RelationScore(const CandidatePlan& parent, const CandidatePlan& next) {
    int score = 0;
    for (std::size_t id : parent.lost_goals)
        if (id == next.task_index) score += 1000;
    for (std::size_t id : parent.broken_constraints)
        if (std::find(next.broken_constraints.begin(),
                      next.broken_constraints.end(), id) != next.broken_constraints.end())
            score += 500;
    for (const CandidateAction& left : parent.actions) {
        for (const CandidateAction& right : next.actions) {
            for (unsigned int id : left.arguments)
                if (std::find(right.arguments.begin(), right.arguments.end(), id) !=
                    right.arguments.end()) score += 10;
        }
    }
    score += std::max(-40, std::min(40, next.utility));
    return score;
}

} // namespace

TaskGroupSearchResult SearchTaskGroups(
    const std::vector<CandidatePlan>& roots,
    const GroupProjector& project,
    const TaskGroupSearchOptions& options) {
    TaskGroupSearchResult result;
    const auto started = std::chrono::steady_clock::now();
    const auto expired = [&]() {
        return std::chrono::steady_clock::now() - started >= options.budget;
    };
    if (options.budget.count() <= 0 || !options.beam_width || !options.branch_width ||
        !options.max_depth) {
        result.budget_exhausted = true;
        return result;
    }

    std::vector<Node> frontier;
    std::vector<std::size_t> root_ids;
    std::map<std::size_t, const CandidatePlan*> root_by_id;
    for (const CandidatePlan& root : roots) {
        if (expired()) { result.budget_exhausted = true; break; }
        if (root.task_indices.size() != 1) continue;
        ++result.evaluated;
        root_ids.push_back(root.task_indices.front());
        root_by_id[root.task_indices.front()] = &root;
        // A task that fails at the root can become feasible after a predecessor.
        if (!root.dry_run_succeeded ||
            root.remainingDuration() > options.max_plan_duration) continue;
        Consider(root, result);
        frontier.push_back(Node{root, 0});
    }
    std::sort(root_ids.begin(), root_ids.end());
    root_ids.erase(std::unique(root_ids.begin(), root_ids.end()), root_ids.end());
    const std::size_t root_count = frontier.size();
    frontier = SelectBeam(frontier, options.beam_width);
    result.beam_limited = frontier.size() < root_count;
    std::set<std::vector<std::size_t> > seen;
    for (const Node& node : frontier) seen.insert(node.plan.task_indices);

    for (std::size_t depth = 2;
         depth <= options.max_depth + options.max_restoration_tasks && !frontier.empty();
         ++depth) {
        std::vector<Node> next;
        // SelectBeam already interleaves safe and broken fronts.
        for (const Node& parent : frontier) {
            if (expired()) { result.budget_exhausted = true; break; }
            std::size_t expanded = 0;
            std::vector<std::size_t> choices = parent.plan.available_tasks_known ?
                parent.plan.available_tasks : root_ids;
            for (std::size_t lost : parent.plan.lost_goals)
                if (std::find(choices.begin(), choices.end(), lost) == choices.end())
                    choices.push_back(lost);
            std::stable_sort(choices.begin(), choices.end(),
                [&](std::size_t a, std::size_t b) {
                    const auto ia = root_by_id.find(a);
                    const auto ib = root_by_id.find(b);
                    const int sa = ia == root_by_id.end() ? 1000 :
                        RelationScore(parent.plan, *ia->second);
                    const int sb = ib == root_by_id.end() ? 1000 :
                        RelationScore(parent.plan, *ib->second);
                    return sa == sb ? a < b : sa > sb;
                });
            for (std::size_t id : choices) {
                if (expired()) { result.budget_exhausted = true; break; }
                if (expanded == options.branch_width) {
                    result.branch_limited = true;
                    break;
                }
                const bool repeated = std::find(parent.plan.task_indices.begin(),
                    parent.plan.task_indices.end(), id) != parent.plan.task_indices.end();
                const bool restoration = std::find(parent.plan.lost_goals.begin(),
                    parent.plan.lost_goals.end(), id) != parent.plan.lost_goals.end();
                if (repeated && !restoration) continue;
                if (restoration && parent.restorations >= options.max_restoration_tasks)
                    continue;
                if (!restoration && parent.plan.task_indices.size() - parent.restorations >=
                    options.max_depth)
                    continue;
                std::vector<std::size_t> group = parent.plan.task_indices;
                group.push_back(id);
                if (!seen.insert(group).second) continue;
                ++expanded;
                CandidatePlan plan;
                try {
                    plan = project(group);
                } catch (...) {
                    result.projection_failed = true;
                    result.elapsed = std::chrono::steady_clock::now() - started;
                    return result;
                }
                ++result.evaluated;
                if (!plan.dry_run_succeeded ||
                    plan.remainingDuration() > options.max_plan_duration) continue;
                Consider(plan, result);
                next.push_back(Node{plan, parent.restorations + (restoration ? 1u : 0u)});
            }
        }
        const std::size_t next_count = next.size();
        frontier = SelectBeam(next, options.beam_width);
        result.beam_limited |= frontier.size() < next_count;
    }
    result.elapsed = std::chrono::steady_clock::now() - started;
    return result;
}

} // namespace _home
