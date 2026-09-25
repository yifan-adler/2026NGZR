/*
 * File: rdfw.cpp
 * Author : ShiQiao Chen(陈世侨)
 * Affiliation: WuHan University of Technology
 */
 #include <numeric>
 #include <unordered_map>
 #include <set>
 #include <map>
# include <cerrno>
# include <cctype>
# include <cstdlib>
# include <algorithm>
# include <cstring>
# include <iostream>
# include <limits>
# include <regex>
# include <sstream>
# include "rdfw.hpp"
# include "parser.hpp"
using namespace _home;
using namespace std;

const std::size_t RDFW::NO_FAILED_REVISION;

void split_string(vector<string> &out, const string &str_source, char mark);
ostream &operator<<(ostream &os, shared_ptr<Object> obj);
ostream &operator<<(ostream &os, shared_ptr<SyntaxNode> sn);
ostream &operator<<(ostream &os, const Instruction &instr);

namespace {

bool ParseBoundedInt(const std::string& text, int minimum, int maximum,
                     int& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || !end || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

std::string TrimCopy(const std::string& input) {
    std::size_t begin = 0;
    while (begin < input.size() &&
           std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    std::size_t end = input.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

std::vector<std::string> SplitWhitespace(const std::string& input) {
    std::istringstream stream(input);
    std::vector<std::string> result;
    std::string token;
    while (stream >> token) result.push_back(token);
    return result;
}

bool ParseInstructionFormTree(const std::string& form,
                              std::shared_ptr<SyntaxNode>& result,
                              std::string& error) {
    result.reset();
    if (form.empty() || form.size() > 64 * 1024) {
        error = "empty or oversized instruction form";
        return false;
    }

    std::shared_ptr<SyntaxNode> root = std::make_shared<SyntaxNode>();
    std::vector<std::shared_ptr<SyntaxNode>> path(1, root);
    std::size_t text_start = 0;
    std::size_t node_count = 1;
    for (std::size_t i = 0; i < form.size(); ++i) {
        if (form[i] == '(') {
            const std::string value = TrimCopy(form.substr(text_start, i - text_start));
            if (!value.empty()) path.back()->value += value;
            if (path.size() >= 64 || node_count >= 1024) {
                error = "instruction nesting/node limit exceeded";
                return false;
            }
            std::shared_ptr<SyntaxNode> child = std::make_shared<SyntaxNode>();
            path.back()->sons.push_back(child);
            path.push_back(child);
            ++node_count;
            text_start = i + 1;
        } else if (form[i] == ')') {
            if (path.size() <= 1) {
                error = "unexpected closing parenthesis";
                return false;
            }
            const std::string value = TrimCopy(form.substr(text_start, i - text_start));
            if (!value.empty()) path.back()->value += value;
            path.pop_back();
            text_start = i + 1;
        }
    }
    if (path.size() != 1 || root->sons.size() != 1 || !root->sons[0]) {
        error = "unbalanced or empty instruction form";
        return false;
    }
    result = root->sons[0];
    return true;
}

bool InstructionMarkerAt(const std::string& input, std::size_t pos,
                         std::string* tag = nullptr) {
    static const char* markers[] = {
        "(:cons_notnot", "(:cons_not", "(:task", "(:info"
    };
    if (pos >= input.size() || input[pos] != '(') return false;
    std::size_t word = pos + 1;
    while (word < input.size() &&
           std::isspace(static_cast<unsigned char>(input[word]))) ++word;
    for (const char* marker : markers) {
        const std::size_t length = std::strlen(marker) - 1;
        const std::size_t after = word + length;
        if (after <= input.size() && input.compare(word, length, marker + 1) == 0 &&
            (after == input.size() ||
             std::isspace(static_cast<unsigned char>(input[after])) ||
             input[after] == '(' || input[after] == ')')) {
            if (tag) *tag = marker;
            return true;
        }
    }
    return false;
}

bool IsSmallObject(const std::shared_ptr<Object>& object) {
    return static_cast<bool>(std::dynamic_pointer_cast<SmallObject>(object));
}

bool IsBigObject(const std::shared_ptr<Object>& object) {
    return static_cast<bool>(std::dynamic_pointer_cast<BigObject>(object));
}

bool IsContainerObject(const std::shared_ptr<Object>& object) {
    return static_cast<bool>(std::dynamic_pointer_cast<Container>(object));
}

std::string IndexListString(const std::vector<std::size_t>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << values[i];
    }
    out << ']';
    return out.str();
}

const char* BoolString(bool value) {
    return value ? "true" : "false";
}

const char* EvidenceName(EvidenceSource source) {
    switch (source) {
    case EvidenceSource::UNKNOWN: return "unknown";
    case EvidenceSource::INITIAL: return "initial";
    case EvidenceSource::EXPLICIT_INFO: return "explicit_info";
    case EvidenceSource::CONSTRAINT_DERIVED: return "constraint_derived";
    case EvidenceSource::CONSTRAINT_HEURISTIC: return "constraint_heuristic";
    case EvidenceSource::SENSE: return "sense";
    case EvidenceSource::ACTION_SUCCESS: return "action_success";
    case EvidenceSource::ACTION_FAILURE: return "action_failure";
    case EvidenceSource::ASK_ANSWER: return "ask_answer";
    }
    return "unknown";
}

std::string JsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 32) {
            const char* hex = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}

std::string PredictionJson(const CandidatePrediction& p) {
    std::ostringstream out;
    out << "{\"goal_gain\":" << p.goal_gain
        << ",\"goal_loss\":" << p.goal_loss
        << ",\"constraint_gain\":" << p.constraint_gain
        << ",\"constraint_loss\":" << p.constraint_loss
        << ",\"completed_goals\":" << p.completed_goals
        << ",\"maintained_constraints\":" << p.maintained_constraints
        << ",\"action_count\":" << p.action_count
        << ",\"action_cost\":" << p.action_cost
        << ",\"final_reward\":" << p.final_reward
        << ",\"utility\":" << p.utility << '}';
    return out.str();
}

std::string EvidenceJson(const std::vector<CandidateEvidence>& evidence) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < evidence.size(); ++i) {
        if (i) out << ',';
        out << "{\"object_id\":" << evidence[i].object_id
            << ",\"fact\":" << JsonString(evidence[i].fact)
            << ",\"source\":" << JsonString(evidence[i].source)
            << ",\"verified\":" << BoolString(evidence[i].verified) << '}';
    }
    out << ']';
    return out.str();
}

} // namespace


/**
 * Load the team name.
 */
RDFW::RDFW() : Plug("RDFW") {

}

RDFW::~RDFW() {
    delete nlp_parser;
    nlp_parser = nullptr;
}

CandidatePlan RDFW::PreviewCandidatePlan(std::size_t candidate_task_index) {
    RefreshTaskStates();
    if (candidate_task_index < tasks.size() && tasks[candidate_task_index].IsUsable())
        CalculateTaskRisk(tasks[candidate_task_index]);
    return BuildCandidatePlan(candidate_task_index);
}

std::vector<CandidateEvidence> RDFW::CaptureCandidateEvidence() const {
    std::vector<CandidateEvidence> evidence;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i]) continue;
        if (i < objectLocationSource.size() &&
            objectLocationSource[i] != EvidenceSource::UNKNOWN) {
            CandidateEvidence fact;
            fact.object_id = i;
            fact.fact = "location";
            fact.source = EvidenceName(objectLocationSource[i]);
            fact.verified = i < objectLocationVerified.size() && objectLocationVerified[i];
            evidence.push_back(fact);
        }
        if (i < objectInsideSource.size() &&
            objectInsideSource[i] != EvidenceSource::UNKNOWN) {
            CandidateEvidence fact;
            fact.object_id = i;
            fact.fact = "inside";
            fact.source = EvidenceName(objectInsideSource[i]);
            fact.verified = i < objectInsideVerified.size() && objectInsideVerified[i];
            evidence.push_back(fact);
        }
        if (i < containerStateSource.size() &&
            containerStateSource[i] != EvidenceSource::UNKNOWN) {
            CandidateEvidence fact;
            fact.object_id = i;
            fact.fact = "container_state";
            fact.source = EvidenceName(containerStateSource[i]);
            fact.verified = i < containerStateVerified.size() && containerStateVerified[i];
            evidence.push_back(fact);
        }
    }
    return evidence;
}

CandidatePlan RDFW::BuildCandidatePlan(std::size_t candidate_task_index) {
    return BuildTaskGroupPlan(std::vector<std::size_t>(1, candidate_task_index));
}

CandidatePlan RDFW::BuildTaskGroupPlan(const std::vector<std::size_t>& group) {
    if (group.empty()) return CandidatePlan();
    const std::size_t candidate_task_index = group.front();
    if (candidate_task_index >= tasks.size()) return CandidatePlan();
    for (std::size_t index : group)
        if (index >= tasks.size()) return CandidatePlan();
    InitializeConstraintLedger();

    struct ObjectState {
        int location;
        int is_keep;
        int unable_site;
        bool is_small;
        int inside;
        int on;
        bool is_container;
        int is_open;
        std::vector<int> contents;
    };

    const TerminalSummary terminal_before = terminal_checker.evaluateAll(*this);
    const ScoreSnapshot score_before = score_evaluator.snapshot(*this, terminal_checker);
    std::vector<ObjectState> object_states(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        ObjectState state = {UNKNOWN, 0, UNKNOWN, false, UNKNOWN, UNKNOWN,
                             false, 0, std::vector<int>()};
        if (objects[i]) {
            state.location = objects[i]->location;
            state.is_keep = objects[i]->is_keep;
            state.unable_site = objects[i]->unable_site;
            const std::shared_ptr<SmallObject> small =
                std::dynamic_pointer_cast<SmallObject>(objects[i]);
            if (small) {
                state.is_small = true;
                state.inside = small->inside;
                state.on = small->on;
            }
            const std::shared_ptr<Container> container =
                std::dynamic_pointer_cast<Container>(objects[i]);
            if (container) {
                state.is_container = true;
                state.is_open = container->isOpen;
                for (std::size_t j = 0; j < container->smallObjectsInside.size(); ++j)
                    if (container->smallObjectsInside[j])
                        state.contents.push_back(container->smallObjectsInside[j]->id);
            }
        }
        object_states[i] = state;
    }

    const std::vector<Instruction> saved_tasks = tasks;
    const int saved_location = location;
    const int saved_hold_id = hold_id;
    const int saved_plate_id = plate_id;
    const int saved_task_index = task_index;
    const int saved_err_times = err_times;
    const int saved_solved_task_num = solved_task_num;
    const bool saved_is_pass = isPass;
    const bool saved_is_keep = isKeepConstrain;
    const bool saved_is_multi_goto = isMultiGotoMode;
    const bool saved_hold_mustnear = hold_mustnear;
    const bool saved_plate_mustnear = plate_mustnear;
    const bool saved_normal_stop = normal_stop_requested;
    const std::vector<int> saved_goto_cons = goto_cons;
    const std::vector<std::vector<int> > saved_putin_cons = putin_cons;
    const std::vector<std::vector<int> > saved_takeout_cons = takeout_cons;
    const std::vector<std::vector<int> > saved_putdown_cons = putdown_cons;
    const std::vector<int> saved_putdown1_cons = putdown1_cons;
    const std::vector<std::vector<int> > saved_move_cons = move_cons;
    const std::vector<int> saved_open_cons = open_cons;
    const std::vector<int> saved_close_cons = close_cons;
    const std::vector<int> saved_pickup_cons = pickup_cons;
    const std::vector<int> saved_givehuman_cons = givehuman_cons;
    const std::vector<int> saved_fromplate_cons = fromplate_cons;
    const std::vector<int> saved_toplate_cons = toplate_cons;
    const std::vector<std::vector<int> > saved_mustnear_cons = mustnear_cons;
    const std::vector<bool> saved_rightlocation = rightlocation;
    const std::vector<bool> saved_lock_by_mustnear = lock_by_mustnear;
    const std::vector<int> saved_mustnear_component = mustNearComponent;
    const std::vector<bool> saved_inferred = objectLocationInferredByMustNear;
    const std::vector<bool> saved_pos_correct = posCorrectFlag;
    const std::vector<bool> saved_pos_sensed = posSensedFlag;
    const std::vector<bool> saved_location_verified = objectLocationVerified;
    const std::vector<bool> saved_inside_verified = objectInsideVerified;
    const std::vector<bool> saved_container_verified = containerStateVerified;
    const std::vector<EvidenceSource> saved_location_source = objectLocationSource;
    const std::vector<EvidenceSource> saved_inside_source = objectInsideSource;
    const std::vector<EvidenceSource> saved_container_source = containerStateSource;
    const std::size_t saved_world_revision = world_revision;
    const std::vector<std::size_t> saved_failed_revision = failed_task_revision;
    const std::vector<LocationSensedInfo> saved_sensed_objects = locationSensedObjects;
    int saved_uf_parent[256];
    int saved_uf_size[256];
    int saved_uf_group_loc[256];
    std::memcpy(saved_uf_parent, uf_parent, sizeof(uf_parent));
    std::memcpy(saved_uf_size, uf_size, sizeof(uf_size));
    std::memcpy(saved_uf_group_loc, uf_groupLoc, sizeof(uf_groupLoc));
    const ScoreEvaluator saved_score_evaluator = score_evaluator;
    const std::vector<bool> saved_constraint_eligible = constraint_eligible;
    const bool saved_shadow_dry_run = shadow_dry_run;
    std::vector<CandidateAction>* const saved_shadow_sink = shadow_action_sink;
    const bool saved_has_active_candidate = has_active_candidate;
    const CandidatePlan saved_active_candidate = active_candidate;
    const bool saved_log_suppression = DebugLogSuppressed();
    const std::ios_base::iostate saved_cout_state = std::cout.rdstate();

    std::vector<CandidateAction> actions;
    shadow_dry_run = true;
    shadow_action_sink = &actions;
    has_active_candidate = false;
    task_index = static_cast<int>(candidate_task_index);
    DebugLogSuppressed() = true;
    std::cout.setstate(std::ios_base::failbit);
    bool succeeded = true;
    for (std::size_t step = 0; step < group.size(); ++step) {
        const std::size_t index = group[step];
        if (!tasks[index].IsUsable()) { succeeded = false; break; }
        task_index = static_cast<int>(index);
        try {
            if (!(ZeroActionPreCheck(tasks[index]) || SolveTask(tasks[index]))) {
                succeeded = false;
                break;
            }
            // Match the real task loop before projecting the next task.
            if (step + 1 < group.size()) {
                tasks[index].isEnable = false;
                AfterSolveTask(tasks[index]);
            }
        } catch (const std::exception& error) {
            LOG_ERROR("[InputSafety] group isolated exception: %s", error.what());
            succeeded = false;
            break;
        }
    }
    const TerminalSummary terminal_after = terminal_checker.evaluateAll(*this);
    const ScoreSnapshot score_after = score_evaluator.snapshot(*this, terminal_checker);
    DebugLogSuppressed() = saved_log_suppression;
    std::cout.clear(saved_cout_state);

    tasks = saved_tasks;
    goto_cons = saved_goto_cons;
    putin_cons = saved_putin_cons;
    takeout_cons = saved_takeout_cons;
    putdown_cons = saved_putdown_cons;
    putdown1_cons = saved_putdown1_cons;
    move_cons = saved_move_cons;
    open_cons = saved_open_cons;
    close_cons = saved_close_cons;
    pickup_cons = saved_pickup_cons;
    givehuman_cons = saved_givehuman_cons;
    fromplate_cons = saved_fromplate_cons;
    toplate_cons = saved_toplate_cons;
    mustnear_cons = saved_mustnear_cons;
    rightlocation = saved_rightlocation;
    lock_by_mustnear = saved_lock_by_mustnear;
    mustNearComponent = saved_mustnear_component;
    objectLocationInferredByMustNear = saved_inferred;
    posCorrectFlag = saved_pos_correct;
    posSensedFlag = saved_pos_sensed;
    objectLocationVerified = saved_location_verified;
    objectInsideVerified = saved_inside_verified;
    containerStateVerified = saved_container_verified;
    objectLocationSource = saved_location_source;
    objectInsideSource = saved_inside_source;
    containerStateSource = saved_container_source;
    world_revision = saved_world_revision;
    failed_task_revision = saved_failed_revision;
    locationSensedObjects = saved_sensed_objects;
    std::memcpy(uf_parent, saved_uf_parent, sizeof(uf_parent));
    std::memcpy(uf_size, saved_uf_size, sizeof(uf_size));
    std::memcpy(uf_groupLoc, saved_uf_group_loc, sizeof(uf_groupLoc));

    for (std::size_t i = 0; i < objects.size() && i < object_states.size(); ++i) {
        if (!objects[i]) continue;
        const ObjectState& state = object_states[i];
        objects[i]->location = state.location;
        objects[i]->is_keep = state.is_keep;
        objects[i]->unable_site = state.unable_site;
        if (state.is_small) {
            const std::shared_ptr<SmallObject> small =
                std::dynamic_pointer_cast<SmallObject>(objects[i]);
            if (small) {
                small->inside = state.inside;
                small->on = state.on;
            }
        }
    }
    for (std::size_t i = 0; i < objects.size() && i < object_states.size(); ++i) {
        if (!objects[i] || !object_states[i].is_container) continue;
        const std::shared_ptr<Container> container =
            std::dynamic_pointer_cast<Container>(objects[i]);
        if (!container) continue;
        container->isOpen = object_states[i].is_open;
        container->smallObjectsInside.clear();
        for (std::size_t j = 0; j < object_states[i].contents.size(); ++j) {
            const int id = object_states[i].contents[j];
            if (id > 0 && static_cast<std::size_t>(id) < objects.size()) {
                const std::shared_ptr<SmallObject> small =
                    std::dynamic_pointer_cast<SmallObject>(objects[id]);
                if (small) container->smallObjectsInside.push_back(small);
            }
        }
    }

    location = saved_location;
    hold_id = saved_hold_id;
    plate_id = saved_plate_id;
    hold = saved_hold_id > 0 && static_cast<std::size_t>(saved_hold_id) < objects.size()
        ? std::dynamic_pointer_cast<SmallObject>(objects[saved_hold_id]) : nullptr;
    plate = saved_plate_id > 0 && static_cast<std::size_t>(saved_plate_id) < objects.size()
        ? std::dynamic_pointer_cast<SmallObject>(objects[saved_plate_id]) : nullptr;
    task_index = saved_task_index;
    err_times = saved_err_times;
    solved_task_num = saved_solved_task_num;
    isPass = saved_is_pass;
    isKeepConstrain = saved_is_keep;
    isMultiGotoMode = saved_is_multi_goto;
    hold_mustnear = saved_hold_mustnear;
    plate_mustnear = saved_plate_mustnear;
    normal_stop_requested = saved_normal_stop;
    score_evaluator = saved_score_evaluator;
    constraint_eligible = saved_constraint_eligible;
    shadow_dry_run = saved_shadow_dry_run;
    shadow_action_sink = saved_shadow_sink;
    has_active_candidate = saved_has_active_candidate;
    active_candidate = saved_active_candidate;

    CandidatePlan plan = CandidatePlanEvaluator::evaluate(
        candidate_task_index, tasks[candidate_task_index].behave,
        tasks[candidate_task_index].isEnable &&
            tasks[candidate_task_index].IsUsable() &&
            tasks[candidate_task_index].risk < 2,
        succeeded, actions, terminal_before, terminal_after,
        score_before, score_after);
    plan.candidate_id = next_candidate_id++;
    plan.generated_ms = static_cast<std::size_t>(deadline_manager.elapsed().count());
    plan.world_revision = world_revision;
    plan.evidence = CaptureCandidateEvidence();
    plan.task_indices = group;
    // Preserve the legacy eligibility annotation; the score gate separately
    // evaluates whether a complete trade is profitable.
    // Large legacy suites keep the prior two-constraint cutoff; bounded
    // questions price those losses in the score gate.
    if (tasks.size() > 24)
        plan.eligible = plan.eligible && plan.broken_constraints.size() < 2;
    return plan;
}

CandidatePlan RDFW::BuildSyntheticPutOnCandidate(unsigned int object_id,
                                                  unsigned int target_id) {
    if (!IsValidObjectId(static_cast<int>(object_id)) ||
        !IsValidObjectId(static_cast<int>(target_id)) ||
        !dynamic_pointer_cast<SmallObject>(objects[object_id])) return CandidatePlan();
    Instruction synthetic;
    synthetic.behave = "puton";
    synthetic.X.push_back(objects[object_id]);
    synthetic.Y.push_back(objects[target_id]);
    synthetic.isUseY = true;
    synthetic.isEnable = true;
    synthetic.risk = 0;
    tasks.push_back(synthetic);
    CandidatePlan result = BuildCandidatePlan(tasks.size() - 1);
    tasks.pop_back();
    // The temporary puton exists only to obtain the legacy action sequence.
    // Remove its terminal from both score snapshots before recording a
    // prediction; it is not an official task and disappears before execution.
    TerminalSummary before = result.terminal_before;
    TerminalSummary after = result.terminal_after;
    if (!before.goals.empty()) before.goals.pop_back();
    if (!after.goals.empty()) after.goals.pop_back();
    auto adjusted_score = [](ScoreSnapshot score, const TerminalSummary& terminal) {
        score.completed_goals = static_cast<std::size_t>(std::count(
            terminal.goals.begin(), terminal.goals.end(), TerminalStatus::SATISFIED));
        score.total_goals = terminal.goals.size();
        score.unknown_goals = static_cast<std::size_t>(std::count(
            terminal.goals.begin(), terminal.goals.end(), TerminalStatus::UNKNOWN));
        score.deterministic_base_score = static_cast<int>(score.completed_goals) * 40 +
            (score.completed_goals == 0 ? 0 :
             static_cast<int>(score.satisfied_constraints) * 20) - score.action_cost;
        return score;
    };
    const ScoreSnapshot score_before = adjusted_score(result.score_before, before);
    const ScoreSnapshot score_after = adjusted_score(result.score_after, after);
    CandidatePlan corrected = CandidatePlanEvaluator::evaluate(
        result.task_index, "multi-goto-puton", result.eligible,
        result.dry_run_succeeded, result.actions, before, after,
        score_before, score_after);
    corrected.candidate_id = result.candidate_id;
    corrected.generated_ms = result.generated_ms;
    corrected.world_revision = result.world_revision;
    corrected.evidence = result.evidence;
    corrected.task_indices = corrected.gained_goals;
    return corrected;
}

CandidatePlan RDFW::PreviewFinalMove(unsigned int destination) {
    InitializeConstraintLedger();
    const TerminalSummary before = terminal_checker.evaluateAll(*this);
    const ScoreSnapshot score_before = score_evaluator.snapshot(*this, terminal_checker);
    const int old_location = location;
    const int old_hold_location = hold ? hold->location : UNKNOWN;
    const int old_plate_location = plate ? plate->location : UNKNOWN;
    const std::vector<bool> old_eligible = constraint_eligible;
    const std::size_t old_revision = world_revision;
    const ScoreEvaluator old_evaluator = score_evaluator;

    location = static_cast<int>(destination);
    if (hold) hold->location = location;
    if (plate) plate->location = location;
    score_evaluator.recordAction(ActionCategory::MOVE);
    UpdateConstraintLedger("Move", std::vector<unsigned int>{destination});
    const TerminalSummary after = terminal_checker.evaluateAll(*this);
    const ScoreSnapshot score_after = score_evaluator.snapshot(*this, terminal_checker);

    location = old_location;
    if (hold) hold->location = old_hold_location;
    if (plate) plate->location = old_plate_location;
    constraint_eligible = old_eligible;
    world_revision = old_revision;
    score_evaluator = old_evaluator;
    CandidatePlan plan = CandidatePlanEvaluator::evaluate(
        std::numeric_limits<std::size_t>::max(), "multi-goto-final-move",
        true, true,
        std::vector<CandidateAction>{CandidateAction(
            "Move", std::vector<unsigned int>{destination}, ActionCategory::MOVE)},
        before, after, score_before, score_after);
    plan.candidate_id = next_candidate_id++;
    plan.generated_ms = static_cast<std::size_t>(deadline_manager.elapsed().count());
    plan.world_revision = world_revision;
    return plan;
}

std::vector<CandidatePlan> RDFW::EvaluateShadowCandidates(
    const char* phase, bool include_goto, std::size_t legacy_choice) {
    RefreshTaskStates();
    const TerminalSummary current = terminal_checker.evaluateAll(*this);
    std::vector<CandidatePlan> candidates;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (!tasks[i].isEnable || !tasks[i].IsUsable() ||
            current.goals[i] == TerminalStatus::SATISFIED) continue;
        if (!include_goto && tasks[i].behave == "goto") continue;
        CalculateTaskRisk(tasks[i]);
        candidates.push_back(BuildCandidatePlan(i));
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const CandidatePlan& lhs, const CandidatePlan& rhs) {
            if (lhs.utility != rhs.utility) return lhs.utility > rhs.utility;
            return lhs.task_index < rhs.task_index;
        });

    for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
        const CandidatePlan& candidate = candidates[rank];
        std::ostringstream considered;
        considered << "{\"schema\":\"decision_feedback.v1\",\"event\":\"candidate_considered\""
            << ",\"candidate_id\":" << candidate.candidate_id
            << ",\"phase\":" << JsonString(phase)
            << ",\"task_index\":" << candidate.task_index
            << ",\"task_label\":" << JsonString(candidate.task_label)
            << ",\"legacy_choice\":" << BoolString(candidate.task_index == legacy_choice)
            << ",\"eligible\":" << BoolString(candidate.eligible)
            << ",\"dry_run_succeeded\":" << BoolString(candidate.dry_run_succeeded)
            << ",\"prediction\":" << PredictionJson(candidate.prediction) << '}';
        LOG("[DecisionFeedback] %s", considered.str().c_str());
        LOG(CYAN_BLUE "[3B][Candidate] phase=%s rank=%zu task_index=%zu "
                      "behave=%s legacy_choice=%s eligible=%s complete=%s "
                      "actions=%zu duration_ms=%lld action_cost=%d "
                      "score_before=%d score_after=%d marginal_score=%d "
                      "utility=%d gained_goals=%s lost_goals=%s "
                      "broken_constraints=%s preserved_constraints=%s\n" RESET,
            phase, rank + 1, candidate.task_index, candidate.task_label.c_str(),
            BoolString(candidate.task_index == legacy_choice),
            BoolString(candidate.eligible), BoolString(candidate.dry_run_succeeded),
            candidate.actions.size(),
            static_cast<long long>(candidate.estimated_duration.count()),
            candidate.action_cost, candidate.score_before.deterministic_base_score,
            candidate.score_after.deterministic_base_score,
            candidate.marginal_score, candidate.utility,
            IndexListString(candidate.gained_goals).c_str(),
            IndexListString(candidate.lost_goals).c_str(),
            IndexListString(candidate.broken_constraints).c_str(),
            IndexListString(candidate.preserved_constraints).c_str());
    }
    return candidates;
}

void RDFW::RefreshTaskStates() {
    if (failed_task_revision.size() < tasks.size())
        failed_task_revision.resize(tasks.size(), NO_FAILED_REVISION);
    const TerminalSummary current = terminal_checker.evaluateAll(*this);
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (!tasks[i].IsUsable() || current.goals[i] == TerminalStatus::SATISFIED)
            continue;
        if (failed_task_revision[i] == world_revision) continue;
        if (!tasks[i].isEnable)
            LOG(CYAN_BLUE "[Recovery] reactivated task=%zu behave=%s revision=%zu\n" RESET,
                i, tasks[i].behave.c_str(), world_revision);
        tasks[i].isEnable = true;
        tasks[i].isfalse = false;
    }
}

const CandidatePlan* RDFW::FindCandidate(
    const std::vector<CandidatePlan>& candidates,
    std::size_t candidate_task_index) const {
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].task_index == candidate_task_index) return &candidates[i];
    return nullptr;
}

bool RDFW::HasExecutableCandidate(
    const std::vector<CandidatePlan>& candidates) const {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].eligible || !candidates[i].dry_run_succeeded) continue;
        if (deadline_manager.canFinish(candidates[i].remainingDuration(),
                                       plan_safety_margin))
            return true;
    }
    return false;
}

void RDFW::LogProgressSnapshot(const char* phase) const
{
    const ScoreSnapshot score = score_evaluator.snapshot(*this, terminal_checker);
    const TerminalSummary terminal = terminal_checker.evaluateAll(*this);
    std::ostringstream completed_goal_ids;
    std::ostringstream satisfied_constraint_ids;
    bool first_goal = true;
    bool first_constraint = true;
    for (std::size_t i = 0; i < terminal.goals.size(); ++i) {
        if (terminal.goals[i] != TerminalStatus::SATISFIED) continue;
        if (!first_goal) completed_goal_ids << ',';
        completed_goal_ids << i << ':' << tasks[i].behave;
        first_goal = false;
    }
    for (std::size_t i = 0; i < terminal.constraints.size(); ++i) {
        if (!terminal.constraint_eligible[i] ||
            (terminal.constraints[i] != TerminalStatus::SATISFIED &&
             i < not_infoConstrains.size() + notnot_infoConstrains.size())) continue;
        if (!first_constraint) satisfied_constraint_ids << ',';
        satisfied_constraint_ids << i;
        first_constraint = false;
    }
    LOG(CYAN_BLUE "[3A][%s] goals=%zu/%zu (unknown=%zu), constraints=%zu/%zu "
                  "(unknown=%zu), base_score=%d, action_cost=%d, "
                  "elapsed=%lldms, remaining=%lldms\n" RESET,
        phase,
        score.completed_goals, score.total_goals, score.unknown_goals,
        score.satisfied_constraints, score.total_constraints,
        score.unknown_constraints, score.deterministic_base_score,
        score.action_cost,
        static_cast<long long>(deadline_manager.elapsed().count()),
        static_cast<long long>(deadline_manager.remaining().count()));
    LOG(CYAN_BLUE "[3A][%s] completed_goals=[%s], satisfied_constraints=[%s]\n" RESET,
        phase, completed_goal_ids.str().c_str(), satisfied_constraint_ids.str().c_str());
}

bool RDFW::StopGate(const char* phase, bool include_goto,
                    const std::vector<CandidatePlan>& candidates)
{
    LogProgressSnapshot(phase);
    const TerminalSummary terminal = terminal_checker.evaluateAll(*this);
    if (terminal.allGoalsSatisfied()) {
        LOG(GREEN "[3A][StopGate] all goals are SATISFIED; normal stop before %s\n" RESET,
            phase);
        normal_stop_requested = true;
        return true;
    }
    if (deadline_manager.deadlineReached()) {
        LOG(YELLOW "[3A][StopGate] deadline reached; normal stop before %s\n" RESET,
            phase);
        normal_stop_requested = true;
        return true;
    }
    if (!HasExecutableCandidate(candidates)) {
        ++stop_rescan_count;
        const std::vector<CandidatePlan> fresh = EvaluateShadowCandidates(
            "stop-rescan", true, std::numeric_limits<std::size_t>::max());
        bool positive = false;
        for (const CandidatePlan& plan : fresh)
            if (plan.eligible && plan.dry_run_succeeded && plan.marginal_score > 0 &&
                deadline_manager.canFinish(plan.remainingDuration(), plan_safety_margin))
                positive = true;
        LOG(YELLOW "[3B][StopGate] no complete candidate plan fits before %s; "
                   "candidate_count=%zu fresh_positive=%s include_goto=%s\n" RESET,
            phase, candidates.size(), BoolString(positive), BoolString(include_goto));
        return true;
    }
    return false;
}

bool RDFW::CanStartPlan(const CandidatePlan& candidate, const char* phase) const
{
    const std::chrono::milliseconds remaining_plan = candidate.remainingDuration();
    if (deadline_manager.canFinish(remaining_plan, plan_safety_margin)) {
        LOG(CYAN_BLUE "[3B][Deadline] phase=%s task_index=%zu behave=%s "
                      "can_finish=true remaining_plan_ms=%lld safety_ms=%lld "
                      "budget_remaining_ms=%lld\n" RESET,
            phase, candidate.task_index, candidate.task_label.c_str(),
            static_cast<long long>(remaining_plan.count()),
            static_cast<long long>(plan_safety_margin.count()),
            static_cast<long long>(deadline_manager.remaining().count()));
        return true;
    }
    LOG(YELLOW "[3B][Deadline] phase=%s task_index=%zu behave=%s "
               "can_finish=false remaining_plan_ms=%lld safety_ms=%lld "
               "budget_remaining_ms=%lld\n" RESET,
        phase, candidate.task_index, candidate.task_label.c_str(),
        static_cast<long long>(remaining_plan.count()),
        static_cast<long long>(plan_safety_margin.count()),
        static_cast<long long>(deadline_manager.remaining().count()));
    return false;
}

bool RDFW::ShouldStartConstraintTrade(
    const CandidatePlan& candidate,
    const std::vector<CandidatePlan>& alternatives,
    const char* phase) {
    const bool bounded_uncertainty = stage == 2 && tasks.size() <= 24;
    if (candidate.broken_constraints.empty() && !bounded_uncertainty) return true;
    if (!candidate.dry_run_succeeded) return false;

    // With many correlated/duplicate goals, one observation can resolve dozens
    // of UNKNOWN entries at once.  Their individual upper bounds are not
    // independent; retain the established planner for this regime.
    TerminalSummary initial_hypothesis;
    if (bounded_uncertainty) {
        const int original_stage = stage;
        stage = 1;
        initial_hypothesis = terminal_checker.evaluateAll(*this);
        stage = original_stage;
    }
    auto conservativeScore = [&](const CandidatePlan& plan) {
        if (!bounded_uncertainty)
            return plan.score_after.deterministic_base_score;
        int score = plan.score_after.deterministic_base_score;
        const std::size_t goals = std::min(plan.terminal_before.goals.size(),
                                           plan.terminal_after.goals.size());
        for (std::size_t i = 0; i < goals; ++i)
            if (plan.terminal_before.goals[i] == TerminalStatus::UNKNOWN &&
                plan.terminal_after.goals[i] != TerminalStatus::UNKNOWN &&
                i < initial_hypothesis.goals.size() &&
                initial_hypothesis.goals[i] == TerminalStatus::SATISFIED)
                score -= 40;
        const std::size_t constraints = std::min(
            plan.terminal_before.constraints.size(),
            plan.terminal_after.constraints.size());
        for (std::size_t i = 0; i < constraints; ++i)
            if (plan.terminal_before.constraint_eligible[i] &&
                plan.terminal_before.constraints[i] == TerminalStatus::UNKNOWN &&
                plan.terminal_after.constraints[i] != TerminalStatus::UNKNOWN &&
                i < initial_hypothesis.constraints.size() &&
                initial_hypothesis.constraints[i] == TerminalStatus::SATISFIED)
                score -= 20;
        return score;
    };

    const ScoreSnapshot current = score_evaluator.snapshot(*this, terminal_checker);
    if (candidate.broken_constraints.empty()) {
        if (!bounded_uncertainty || candidate.actions.empty()) return true;
        // An initially satisfied task may still be UNKNOWN until sensed.  A
        // physical plan must beat the possible no-action reward in this case.
        const TerminalStatus before = candidate.task_index < candidate.terminal_before.goals.size()
            ? candidate.terminal_before.goals[candidate.task_index] : TerminalStatus::UNKNOWN;
        if (before == TerminalStatus::UNKNOWN &&
            candidate.score_after.deterministic_base_score <= current.possible_base_score &&
            conservativeScore(candidate) <= current.deterministic_base_score) {
            LOG("[TradeoffDecision] phase=%s task=%zu decision=skip uncertain_no_action\n",
                phase, candidate.task_index);
            return false;
        }
        return true;
    }

    // A valid incumbent is always available: stop now.  Include every
    // currently executable one-task plan that preserves the constraint.
    int incumbent = current.deterministic_base_score;
    for (const CandidatePlan& other : alternatives) {
        if (other.task_index == candidate.task_index || !other.eligible ||
            !other.dry_run_succeeded || !other.broken_constraints.empty() ||
            !deadline_manager.canFinish(other.remainingDuration(), plan_safety_margin))
            continue;
        incumbent = std::max(incumbent, conservativeScore(other));
    }

    auto profitable = [&](const CandidatePlan& plan) {
        return plan.dry_run_succeeded &&
            conservativeScore(plan) > incumbent &&
            deadline_manager.canFinish(plan.remainingDuration(), plan_safety_margin);
    };
    if (profitable(candidate)) {
        LOG("[TradeoffDecision] phase=%s task=%zu decision=execute "
            "score=%d incumbent=%d group=1\n", phase, candidate.task_index,
            conservativeScore(candidate), incumbent);
        return true;
    }

    // Search only tasks that would forfeit the same still-creditable
    // constraint. A lost goal may be restored at the end, while the
    // constraint ledger correctly keeps an earlier violation lost.
    std::vector<std::size_t> related;
    for (const CandidatePlan& other : alternatives) {
        if (other.task_index == candidate.task_index || !other.eligible ||
            !other.dry_run_succeeded) continue;
        bool shared = false;
        for (std::size_t broken : candidate.broken_constraints)
            shared |= std::find(other.broken_constraints.begin(),
                                other.broken_constraints.end(), broken) !=
                      other.broken_constraints.end();
        if (shared) related.push_back(other.task_index);
    }
    std::sort(related.begin(), related.end());
    const auto search_start = std::chrono::steady_clock::now();
    const std::chrono::milliseconds search_limit(25);
    std::vector<std::size_t> group(1, candidate.task_index);
    int best_score = conservativeScore(candidate);
    for (std::size_t prefix = 0; prefix <= related.size() && group.size() <= 4;
         ++prefix) {
        if (std::chrono::steady_clock::now() - search_start >= search_limit ||
            deadline_manager.remaining() <= plan_safety_margin + search_limit)
            break;
        CandidatePlan projected = prefix == 0 ? candidate : BuildTaskGroupPlan(group);
        if (projected.dry_run_succeeded) {
            best_score = std::max(best_score,
                conservativeScore(projected));
            if (profitable(projected)) {
                LOG("[TradeoffDecision] phase=%s task=%zu decision=execute "
                    "score=%d incumbent=%d group=%zu\n", phase,
                    candidate.task_index, conservativeScore(projected),
                    incumbent, group.size());
                return true;
            }
            std::vector<std::size_t> with_restoration = group;
            for (std::size_t lost : projected.lost_goals) {
                if (with_restoration.size() >= 6) break;
                if (std::find(with_restoration.begin(), with_restoration.end(), lost) ==
                    with_restoration.end()) with_restoration.push_back(lost);
            }
            if (with_restoration.size() > group.size() &&
                std::chrono::steady_clock::now() - search_start < search_limit) {
                CandidatePlan restored = BuildTaskGroupPlan(with_restoration);
                if (restored.dry_run_succeeded) {
                    best_score = std::max(best_score,
                        conservativeScore(restored));
                    if (profitable(restored)) {
                        LOG("[TradeoffDecision] phase=%s task=%zu decision=execute "
                            "score=%d incumbent=%d group=%zu\n", phase,
                            candidate.task_index,
                            conservativeScore(restored),
                            incumbent, with_restoration.size());
                        return true;
                    }
                }
            }
        }
        if (prefix == related.size() || group.size() == 4) break;
        group.push_back(related[prefix]);
    }
    LOG("[TradeoffDecision] phase=%s task=%zu decision=skip best=%d "
        "incumbent=%d search_ms=%lld\n", phase, candidate.task_index,
        best_score, incumbent,
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - search_start).count()));
    return false;
}

void RDFW::BeginCandidateExecution(const CandidatePlan& candidate,
                                   const char* selection_reason) {
    active_candidate = candidate;
    if (active_candidate.candidate_id == 0) {
        active_candidate.candidate_id = next_candidate_id++;
        active_candidate.generated_ms = static_cast<std::size_t>(deadline_manager.elapsed().count());
        active_candidate.world_revision = world_revision;
    }
    if (active_candidate.evidence.empty())
        active_candidate.evidence = CaptureCandidateEvidence();
    active_candidate.executed_actions = 0;
    has_active_candidate = true;
    active_action_failed = false;
    active_actual_actions = 0;
    active_failure_reasons.clear();
    active_terminal_before = terminal_checker.evaluateAll(*this);
    active_score_before = score_evaluator.snapshot(*this, terminal_checker);
    std::ostringstream selected;
    selected << "{\"schema\":\"decision_feedback.v1\",\"event\":\"candidate_selected\""
        << ",\"candidate_id\":" << active_candidate.candidate_id
        << ",\"task_index\":" << active_candidate.task_index
        << ",\"tasks\":[";
    for (std::size_t i = 0; i < active_candidate.task_indices.size(); ++i) {
        if (i) selected << ',';
        selected << active_candidate.task_indices[i];
    }
    selected << "],\"task_label\":" << JsonString(active_candidate.task_label)
        << ",\"generated_ms\":" << active_candidate.generated_ms
        << ",\"world_revision\":" << active_candidate.world_revision
        << ",\"selection_reason\":" << JsonString(selection_reason)
        << ",\"prediction\":" << PredictionJson(active_candidate.prediction)
        << ",\"expected_reward\":" << active_candidate.prediction.final_reward
        << ",\"evidence\":" << EvidenceJson(active_candidate.evidence) << '}';
    LOG("[DecisionFeedback] %s", selected.str().c_str());
    const ScoreSnapshot current = score_evaluator.snapshot(*this, terminal_checker);
    LOG(CYAN_BLUE "[3B][Remaining] event=begin task_index=%zu behave=%s "
                  "executed=0 total=%zu remaining_utility=%d "
                  "remaining_duration_ms=%lld\n" RESET,
        active_candidate.task_index, active_candidate.task_label.c_str(),
        active_candidate.actions.size(), active_candidate.remainingUtility(current),
        static_cast<long long>(active_candidate.remainingDuration().count()));
}

void RDFW::EndCandidateExecution(bool succeeded) {
    if (!has_active_candidate) return;
    const ScoreSnapshot current = score_evaluator.snapshot(*this, terminal_checker);
    const TerminalSummary terminal = terminal_checker.evaluateAll(*this);
    CandidateExecutionRecord record;
    record.candidate = active_candidate;
    record.actual.values = MakeCandidatePrediction(
        active_terminal_before, terminal,
        active_score_before, current, active_actual_actions);
    record.actual.succeeded = succeeded;
    record.actual.failed_action = active_action_failed;
    record.actual.failure_reasons = active_failure_reasons;
    if (!succeeded && record.actual.failure_reasons.empty())
        record.actual.failure_reasons.push_back("task_not_completed");
    record.error = ComparePrediction(active_candidate.prediction, record.actual.values);
    decision_feedback.push_back(record);
    std::ostringstream result;
    result << "{\"schema\":\"decision_feedback.v1\",\"event\":\"candidate_result\""
        << ",\"candidate_id\":" << record.candidate.candidate_id
        << ",\"prediction\":" << PredictionJson(record.candidate.prediction)
        << ",\"actual\":" << PredictionJson(record.actual.values)
        << ",\"succeeded\":" << BoolString(record.actual.succeeded)
        << ",\"failed_action\":" << BoolString(record.actual.failed_action)
        << ",\"failure_reasons\":[";
    for (std::size_t i = 0; i < record.actual.failure_reasons.size(); ++i) {
        if (i) result << ',';
        result << JsonString(record.actual.failure_reasons[i]);
    }
    result << "],\"error\":{\"goal_gain\":" << record.error.goal_gain
        << ",\"constraint_gain\":" << record.error.constraint_gain
        << ",\"constraint_loss\":" << record.error.constraint_loss
        << ",\"action_cost\":" << record.error.action_cost
        << ",\"utility\":" << record.error.utility << "}}";
    LOG("[DecisionFeedback] %s", result.str().c_str());
    LOG(CYAN_BLUE "[3B][Remaining] event=end task_index=%zu behave=%s "
                  "succeeded=%s executed=%zu total=%zu remaining_utility=%d "
                  "remaining_duration_ms=%lld\n" RESET,
        active_candidate.task_index, active_candidate.task_label.c_str(),
        BoolString(succeeded), active_candidate.executed_actions,
        active_candidate.actions.size(), active_candidate.remainingUtility(current),
        static_cast<long long>(active_candidate.remainingDuration().count()));
    has_active_candidate = false;
    if (!shadow_dry_run) RefreshTaskStates();
}

void RDFW::RecordAction(ActionCategory category, const char* name,
                        const std::vector<unsigned int>& arguments) {
    score_evaluator.recordAction(category);
    const CandidateAction action(name, arguments, category);
    if (shadow_dry_run && shadow_action_sink) shadow_action_sink->push_back(action);
    if (!shadow_dry_run && has_active_candidate) {
        ++active_actual_actions;
        if (active_candidate.executed_actions < active_candidate.actions.size())
            ++active_candidate.executed_actions;
        const ScoreSnapshot current = score_evaluator.snapshot(*this, terminal_checker);
        const bool can_finish = deadline_manager.canFinish(
            active_candidate.remainingDuration(), plan_safety_margin);
        LOG(CYAN_BLUE "[3B][Remaining] event=action task_index=%zu action=%s "
                      "executed=%zu total=%zu remaining_utility=%d "
                      "remaining_duration_ms=%lld can_finish=%s\n" RESET,
            active_candidate.task_index, name,
            active_candidate.executed_actions, active_candidate.actions.size(),
            active_candidate.remainingUtility(current),
            static_cast<long long>(active_candidate.remainingDuration().count()),
            BoolString(can_finish));
    }
}

void RDFW::InitializeConstraintLedger() {
    const std::size_t count = not_infoConstrains.size() +
        notnot_infoConstrains.size() + not_taskConstrains.size();
    if (constraint_eligible.size() == count) return;
    constraint_eligible.assign(count, true);
    const TerminalSummary initial = terminal_checker.evaluateAll(*this);
    for (std::size_t i = 0; i < count; ++i)
        if (initial.constraints[i] == TerminalStatus::UNSATISFIED)
            constraint_eligible[i] = false;
}

void RDFW::UpdateConstraintLedger(
    const char* action, const std::vector<unsigned int>& arguments) {
    InitializeConstraintLedger();
    const TerminalSummary current = terminal_checker.evaluateAll(*this);
    for (std::size_t i = 0; i < current.constraints.size(); ++i) {
        if (current.constraints[i] == TerminalStatus::UNSATISFIED)
            constraint_eligible[i] = false;
    }
    // Action prohibitions refer to the executed transition, rather than a
    // predicate of the resulting world state.
    std::string performed(action);
    std::transform(performed.begin(), performed.end(), performed.begin(), ::tolower);
    const std::size_t offset = not_infoConstrains.size() + notnot_infoConstrains.size();
    for (std::size_t i = 0; i < not_taskConstrains.size(); ++i) {
        const Instruction& forbidden = not_taskConstrains[i];
        if (!forbidden.IsUsable() || forbidden.X.empty() || !forbidden.X[0]) continue;
        bool same_action = forbidden.behave == performed;
        if (forbidden.behave == "goto" && performed == "move")
            same_action = !arguments.empty() &&
                forbidden.X[0]->location == static_cast<int>(arguments[0]);
        if (!same_action || arguments.empty()) continue;
        if (performed != "move" && forbidden.X[0]->id != arguments[0]) continue;
        if (!forbidden.Y.empty() && forbidden.Y[0] &&
            (arguments.size() < 2 || forbidden.Y[0]->id != arguments[1])) continue;
        constraint_eligible[offset + i] = false;
    }
    ++world_revision;
}

void RDFW::RecordActionOutcome(const char* name, bool succeeded) {
    if (shadow_dry_run || !has_active_candidate || succeeded) return;
    active_action_failed = true;
    active_failure_reasons.push_back(std::string(name) + "_returned_false");
}

bool RDFW::DryRunActionSucceeds(
    const char* name, const std::vector<unsigned int>& arguments) const {
    const std::string action(name);
    if (action == "Move")
        return !arguments.empty() && arguments[0] <=
               static_cast<unsigned int>(MAX_LOCATION_ID);
    if (arguments.empty() || arguments[0] >= objects.size() ||
        !objects[arguments[0]]) return false;

    const unsigned int a = arguments[0];
    if (action == "PickUp") {
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(objects[a]);
        return small && hold_id == NONE && small->location == location &&
               (small->inside == NONE || small->inside == UNKNOWN);
    }
    if (action == "PutDown") return hold_id == static_cast<int>(a);
    if (action == "ToPlate")
        return hold_id == static_cast<int>(a) && plate_id == NONE;
    if (action == "FromPlate")
        return plate_id == static_cast<int>(a) && hold_id == NONE;
    if (action == "Open" || action == "Close") {
        const std::shared_ptr<Container> container =
            std::dynamic_pointer_cast<Container>(objects[a]);
        return container && container->location == location;
    }
    if ((action == "PutIn" || action == "TakeOut") && arguments.size() >= 2) {
        const unsigned int b = arguments[1];
        if (b >= objects.size() || !objects[b]) return false;
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(objects[a]);
        const std::shared_ptr<Container> container =
            std::dynamic_pointer_cast<Container>(objects[b]);
        if (!small || !container || container->location != location ||
            !container->isOpen) return false;
        if (action == "PutIn") return hold_id == static_cast<int>(a);
        return hold_id == NONE && small->inside == static_cast<int>(b);
    }
    return true;
}

void RDFW::DryRunSenseIds(std::vector<unsigned int>& sensed_ids) const {
    sensed_ids.clear();
    if (location < 0) return;
    for (std::size_t i = 1; i < objects.size(); ++i) {
        if (!objects[i] || objects[i]->location != location) continue;
        if (static_cast<int>(i) == hold_id || static_cast<int>(i) == plate_id) continue;
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(objects[i]);
        if (small && small->inside > 0 &&
            static_cast<std::size_t>(small->inside) < objects.size()) {
            const std::shared_ptr<Container> container =
                std::dynamic_pointer_cast<Container>(objects[small->inside]);
            if (container && !container->isOpen) continue;
        }
        sensed_ids.push_back(static_cast<unsigned int>(i));
    }
}

/**
 * @brief 初始化动态数组
 * @param max_size 数组的最大大小，默认为100
 */
void RDFW::InitializeDynamicArrays(int max_size) {
    cout << "#(RDFW): Initializing dynamic arrays with size " << max_size << endl;

    // 初始化一维数组 - 使用reserve优化内存分配
    goto_cons.clear();
    goto_cons.reserve(max_size);
    goto_cons.resize(max_size, 0);

    putdown1_cons.clear();
    putdown1_cons.reserve(max_size);
    putdown1_cons.resize(max_size, 0);

    open_cons.clear();
    open_cons.reserve(max_size);
    open_cons.resize(max_size, 0);

    close_cons.clear();
    close_cons.reserve(max_size);
    close_cons.resize(max_size, 0);

    pickup_cons.clear();
    pickup_cons.reserve(max_size);
    pickup_cons.resize(max_size, 0);

    givehuman_cons.clear();
    givehuman_cons.reserve(max_size);
    givehuman_cons.resize(max_size, 0);

    fromplate_cons.clear();
    fromplate_cons.reserve(max_size);
    fromplate_cons.resize(max_size, 0);

    toplate_cons.clear();
    toplate_cons.reserve(max_size);
    toplate_cons.resize(max_size, 0);

    rightlocation.clear();
    rightlocation.reserve(max_size);
    rightlocation.resize(max_size, false);

    objectLocationVerified.assign(max_size, false);
    objectLocationInferredByMustNear.assign(max_size, false);
    objectInsideVerified.assign(max_size, false);
    containerStateVerified.assign(max_size, false);
    objectLocationSource.assign(max_size, EvidenceSource::UNKNOWN);
    objectInsideSource.assign(max_size, EvidenceSource::UNKNOWN);
    containerStateSource.assign(max_size, EvidenceSource::UNKNOWN);

    // 初始化二维数组 - 优化内存分配
    putin_cons.clear();
    putin_cons.reserve(max_size);
    for (int i = 0; i < max_size; i++) {
        putin_cons.emplace_back(max_size, 0);
    }

    takeout_cons.clear();
    takeout_cons.reserve(max_size);
    for (int i = 0; i < max_size; i++) {
        takeout_cons.emplace_back(max_size, 0);
    }

    putdown_cons.clear();
    putdown_cons.reserve(max_size);
    for (int i = 0; i < max_size; i++) {
        putdown_cons.emplace_back(max_size, 0);
    }

    move_cons.clear();
    move_cons.reserve(max_size);
    for (int i = 0; i < max_size; i++) {
        move_cons.emplace_back(max_size, 0);
    }

    mustnear_cons.clear();
    mustnear_cons.reserve(max_size);
    for (int i = 0; i < max_size; i++) {
        mustnear_cons.emplace_back(max_size, 0);
    }

    cout << "#(RDFW): Dynamic arrays initialization completed" << endl;
}


/**
 * @brief Initialization Function
 * @param argc:     argument count
 * @param argv:     argument value
 * Tasks:
 * 1. Parse Arguments and set options --> LOG
 * 2.
 */
void RDFW::Init(int argc, char **argv) // 改
{
    string path = "../example/words.txt";   // loading word dictionary
    if (argc > 1)
    {
        // Task 1: Parse Arguments and set options --> LOG
        bool optionFound = false; // 记录是否找到匹配的选项

        for (int i = 1; i < argc; i++)
        {
            LOG("%s", argv[i]);

            const std::string arg(argv[i]);
            const std::string nextArg(i + 1 < argc ? argv[i + 1] : "");

            if (arg == "-deduplicate") {
                int value = 0;
                if (ParseBoundedInt(nextArg, 0, 1, value)) {
                    deduplicate_input = value != 0;
                    ++i;
                } else LOG_ERROR("Invalid -deduplicate value: %s", nextArg.c_str());
                continue;
            }

            // Change Option
            enum Options
            {
                NLP,
                ERR,
                ASK_2,
                PATH,
                STAGE
            };

            int option = -1;
            if (arg == "-nlp"){
                option = NLP;
                optionFound = true;
            } else if (arg == "-err") {
                option = ERR;
                optionFound = true;
            } else if (arg == "-ask_2"){
                option = ASK_2;
                optionFound = true;
            } else if (arg == "-path") {
                option = PATH;
                optionFound = true;
            } else if (arg == "-stage") {
                option = STAGE;
                optionFound = true;
            }

            // Set Option Value (Option value is in the next arg.)
            int parsed_option = 0;
            switch (option)
            {
                case NLP:
                    if (!ParseBoundedInt(nextArg, 0, 1, parsed_option)) {
                        LOG_ERROR("Invalid -nlp value: %s", nextArg.c_str());
                        break;
                    }
                    isNaturalParse = parsed_option;
                    i++;
                    break;
                case ERR:
                    if (!ParseBoundedInt(nextArg, 0, 1, parsed_option)) {
                        LOG_ERROR("Invalid -err value: %s", nextArg.c_str());
                        break;
                    }
                    isErrorCorrection = parsed_option;
                    i++;
                    break;
                case ASK_2:
                    if (!ParseBoundedInt(nextArg, 0, 1, parsed_option)) {
                        LOG_ERROR("Invalid -ask_2 value: %s", nextArg.c_str());
                        break;
                    }
                    isAskTwice = parsed_option;
                    i++;
                    break;
                case PATH:
                    if (nextArg.empty()) {
                        LOG_ERROR("Missing -path value");
                        break;
                    }
                    path = nextArg;
                    i++;
                    break;
                case STAGE:
                {
                    int temp = 0;
                    if (!ParseBoundedInt(nextArg, 1, 2, temp)) {
                        LOG_ERROR("Invalid -stage value: %s", nextArg.c_str());
                        break;
                    }
                    if(temp==1) stage=1;
                    if(temp==2) stage=2;
                    i++;
                }
                break;
            }
        }

        if (!optionFound)
        {
            LOG("notfound"); // 处理未匹配的选项
            // 可以输出错误信息或执行其他操作
        }
    }
    LOG("nlp %d, err %d", isNaturalParse, isErrorCorrection);

    // objects[0] is an identity view of this Robot, not an owning reference.
    // Owning shared_from_this() here creates RDFW -> objects[0] -> RDFW.
    objects.push_back(shared_ptr<Object>(static_cast<Object*>(this),
                                         [](Object*) {}));

    if (isErrorCorrection)
        posCorrectFlag.push_back(false);
    else
        posCorrectFlag.push_back(true);

    // 初始化动态数组
    InitializeDynamicArrays(100);  // 设置为100以支持更大的位置索引

    // 初始化位置感知记录数组，为所有可能的位置预留空间
    posSensedFlag.resize(100, false);  // 为100个位置预留空间，全部标记为未感知

    // 初始化位置感知物体记录数组
    locationSensedObjects.resize(100);  // 为100个位置预留空间

    nlp_parser = new parser();
    nlp_parser->words_map_initialize(path);
}


void RDFW::Plan() // 改
{
    deadline_manager.reset(deadline_manager.timeLimit());
    score_evaluator.reset();
    constraint_eligible.clear();
    world_revision = 0;
    stop_rescan_count = 0;
    failed_task_revision.clear();
    normal_stop_requested = false;
    shadow_dry_run = false;
    shadow_action_sink = nullptr;
    has_active_candidate = false;
    decision_feedback.clear();
    next_candidate_id = 1;
    active_actual_actions = 0;
    active_action_failed = false;
    active_failure_reasons.clear();
    discarded_instruction_count = 0;
    preflight_report = QuestionPreflightReport();

    // ==================== 测试开始前的状态验证 ====================
    cout << "#(RDFW): Starting new test - verifying clean state" << endl;

    // 验证关键状态变量是否已重置
    SetSolvedTaskNum(0);
    task_index = 0;
    err_times = 0;
    isPass = false;
    isKeepConstrain = false;
    isMultiGotoMode = false;
    isAutoConstrain = false;

    // 验证机器人状态
    location = UNKNOWN;
    hold = nullptr;
    hold_id = 0;

    // 验证感知状态
    for (size_t i = 0; i < posSensedFlag.size(); i++) {
        posSensedFlag[i] = false;
    }
    fill(objectLocationVerified.begin(), objectLocationVerified.end(), false);
    fill(objectLocationInferredByMustNear.begin(), objectLocationInferredByMustNear.end(), false);
    fill(objectInsideVerified.begin(), objectInsideVerified.end(), false);
    fill(containerStateVerified.begin(), containerStateVerified.end(), false);
    fill(objectLocationSource.begin(), objectLocationSource.end(), EvidenceSource::UNKNOWN);
    fill(objectInsideSource.begin(), objectInsideSource.end(), EvidenceSource::UNKNOWN);
    fill(containerStateSource.begin(), containerStateSource.end(), EvidenceSource::UNKNOWN);

    // 验证位置感知物体记录
    for (auto& loc_info : locationSensedObjects) {
        loc_info.object_ids.clear();
        loc_info.container_id = 0;
        loc_info.has_container = false;
    }

    // 验证约束查找表
    lock_by_mustnear.clear();
    mustNearComponent.clear();

    // 验证并查集状态
    memset(uf_parent, -1, sizeof(uf_parent));
    memset(uf_size, 0, sizeof(uf_size));
    memset(uf_groupLoc, -1, sizeof(uf_groupLoc));

    // 验证动态数组状态
    fill(goto_cons.begin(), goto_cons.end(), 0);
    fill(putdown1_cons.begin(), putdown1_cons.end(), 0);
    fill(open_cons.begin(), open_cons.end(), 0);
    fill(close_cons.begin(), close_cons.end(), 0);
    fill(pickup_cons.begin(), pickup_cons.end(), 0);
    fill(givehuman_cons.begin(), givehuman_cons.end(), 0);
    fill(fromplate_cons.begin(), fromplate_cons.end(), 0);
    fill(toplate_cons.begin(), toplate_cons.end(), 0);
    fill(rightlocation.begin(), rightlocation.end(), false);

    // 验证二维数组状态
    for (auto& row : putin_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : takeout_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : putdown_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : move_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : mustnear_cons) fill(row.begin(), row.end(), 0);

    // 验证解析器状态
    if (nlp_parser) {
        parser::clear_static_state();
    }

    cout << "#(RDFW): Pre-test state verification completed" << endl;

    // print test name
    printf(GREEN "%s\n" RESET, GetTestName().c_str());


    cout <<  "--------------------------------------------" << endl;
    cout << "Ready to Parse Env Infos." << endl;

    string env_str = GetEnvDes();

    // Try to Parse Env, write to Object, SmallObject
    if (ParseEnv(env_str) == false) {
        cout<<"ParseEnv Failed! Skipping this test."<<endl;
        cout<<"# Test skipped due to environment parsing failure"<<endl;
        return;  // Exit Plan() gracefully, allowing Fini() to be called
    } else {
        cout << endl << "ParseEnv finished" << endl;
    }

    cout <<  "--------------------------------------------" << endl;
    cout << "Ready to Parse Tasks and Cons." << endl;

    const string task_str = TrimCopy(GetTaskDes());

    if(task_str.empty()) {
        LOG_ERROR("Instruction description is empty; continuing with no tasks");
        isNaturalParse = 0;
    } else if(task_str[0]=='(')  {  // check IT or NT
        isNaturalParse=0;
    } else {
        isNaturalParse=1;
    }

    // parse natrual language
    if (isNaturalParse) {
        ParseNaturalLanguage(task_str);
    } else {
        if (ParseInstruction(task_str) == false) {
            cout << "Instruction input contained no usable forms; continuing safely." << endl;
        }
    }

    // One gate for both IT and NL, before any info/correction/risk/goal consumer.
    if (!RunQuestionPreflight()) {
        LOG_ERROR("[Preflight] unsafe WorldState; skipping question before planning");
        return;
    }

    // Parse Info Complements
    for (const auto &v : infos){
        ParseInfo(v);
    }


    if(stage == 2){
        PrintEnv();
    // ==================== 约束纠错与补全 ====================
    // 在解析完成后、位置推断前进行约束纠错
    ApplyOpenCloseCorrection();
    ApplyMustInConstraintCorrection();
    ApplyMustNearConstraintCorrection();

    }
    InitializeConstraintLedger();
    // Find Human Infomation from Objects
    human = nullptr;
    for (const auto &obj : objects) {
        if (obj && obj->sort == "human") {
            human = std::dynamic_pointer_cast<BigObject>(obj);
            if (!human) {
                LOG_ERROR("Human object %d is not a big object; give tasks will be isolated",
                          obj->id);
            }
            break;
        }
    }

    // print environment, instructions,
    PrintEnv();
    PrintInstruction();
    cout<<"Parse Tasks, Cons and Infos finished."<<endl;
    cout <<  "--------------------------------------------" << endl;



    /*=======================约束条件规划===================*/

    cout << "Ready to Cons  plan" << endl;
    Cons_plan();
    FilterConstraintsByTaskConflicts();
    cout<<"cons_plan finished"<<endl;
    cout <<  "--------------------------------------------" << endl;

    /*=======================任务优化===================*/
    tasks = TaskOptimization();
    failed_task_revision.assign(tasks.size(), NO_FAILED_REVISION);
    cout<<"taskoptimization finished"<<endl;
    cout <<  "--------------------------------------------" << endl;

    if (stage == 2) {
        const TerminalSummary verified = terminal_checker.evaluateAll(*this);
        // Interpret the initial description as a hypothesis only.  A goal
        // that looks complete there but is unverified can make stopping worth
        // more than the pessimistic score suggests.
        stage = 1;
        const TerminalSummary initial_hypothesis = terminal_checker.evaluateAll(*this);
        stage = 2;
        bool disputed_initial_reward = false;
        for (std::size_t i = 0; i < verified.goals.size(); ++i)
            if (verified.goals[i] == TerminalStatus::UNKNOWN &&
                initial_hypothesis.goals[i] == TerminalStatus::SATISFIED &&
                tasks[i].behave != "goto")
                disputed_initial_reward = true;
        if (tasks.size() <= 24 && disputed_initial_reward &&
            deadline_manager.remaining() > plan_safety_margin +
                std::chrono::milliseconds(100)) {
            LOG("[TradeoffEvidence] probing current location before decisions\n");
            SenseCurrentLocationOnly(false);
        }
    }

    /*=======================任务执行===================*/
    if (false)
    {
        cout << "2222222" << endl;
        cout << "2222222" << endl;
    }
    else
    {
        const size_t tasksSize = tasks.size();
        const bool performErrorCorrection = isErrorCorrection;//是否开启纠错模式
        cout << tasksSize << endl;

        // 检查并延迟多goto任务
        bool defer_multi_goto = CheckAndDeferMultiGoto();

        // 设置多goto模式标志
        //isMultiGotoMode = defer_multi_goto;

         // 执行主任务循环
         ExecuteMainTaskLoop(defer_multi_goto);

        /*============== mustchooseone and goto ==============*/
        // 只在“剩余启用的 goto < 2”时才调用 MustChooseOne，避免抢跑 goto
        if (!normal_stop_requested && solved_task_num == 0) {
            size_t remain_goto = 0;
            for (auto &t : tasks) if (t.isEnable && t.behave == "goto") ++remain_goto;
            if (remain_goto < 2) {
                MustChooseOne();
            } else {
                LOG(YELLOW "[Defer] skip MustChooseOne because multi-goto remain=%zu\n" RESET, remain_goto);
            }
        }

    }
    /*============== 检查阶段 ==============*/
    //检查一遍
    cout<<"-----check check check-----"<<endl;

    // 执行检查阶段
    if (!normal_stop_requested)
        ExecuteCheckPhase(CheckAndDeferMultiGoto());

    /*============== Multi-GOTO聚合 ==============*/
    // 执行Multi-GOTO聚合
    if (!normal_stop_requested) {
        PrintEnv();
        ExecuteMultiGotoAggregation();
    }

    if (!deadline_manager.deadlineReached()) ExecuteTerminalRecovery();

    /*============== 结果输出 ==============*/
    cout << endl
         << "Sovled Task Num:" << solved_task_num << "    " << "Expect Num:" << tasks.size() << endl;
    LogProgressSnapshot("final");
}

/*============== 主任务循环 ==============*/
void RDFW::ExecuteMainTaskLoop(bool defer_multi_goto)
{
    /*if(stage==2)
    {
        SenseCurrentLocationOnly();
    }*/

    // ============== 统计puton任务中出现多次的大物体 ==============
    map<unsigned int, int> bigObjectTaskCount;  // 大物体ID -> 任务数量
    map<unsigned int, vector<unsigned int>> bigObjectTaskIndices;  // 大物体ID -> 任务索引列表

    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].isEnable && tasks[i].behave == "puton" &&
            !tasks[i].Y.empty() && tasks[i].Y[0] != nullptr) {
            unsigned int bigObjectId = tasks[i].Y[0]->id;
            bigObjectTaskCount[bigObjectId]++;
            bigObjectTaskIndices[bigObjectId].push_back(i);
        }
    }

    // 标记出现多次的大物体
    for (const auto& pair : bigObjectTaskCount) {
        if (pair.second >= 2) {
            unsigned int bigObjectId = pair.first;
            int taskCount = pair.second;
            if (!IsValidObjectId(static_cast<int>(bigObjectId))) {
                LOG_ERROR("[MultiPuton] ignored invalid object id %u", bigObjectId);
                continue;
            }
            LOG(GREEN "[MultiPuton] Big object %d (sort: %s) appears in %d puton tasks" RESET,
                bigObjectId, objects[bigObjectId]->sort.c_str(), taskCount);

            // 标记这些任务为多puton任务
            for (unsigned int taskIdx : bigObjectTaskIndices[bigObjectId]) {
                tasks[taskIdx].isMultiPuton = true;  // 假设Instruction类有这个成员
                LOG(YELLOW "[MultiPuton] Marked task %d as multi-puton for object %d" RESET,
                    taskIdx, bigObjectId);
            }
        }
    }
    // ============== 多puton统计结束 ==============

    const size_t tasksSize = tasks.size();
    for (task_index = 0; task_index < tasksSize; ++task_index)
    {
        // 跳过包含不存在物体的任务
        if (!tasks[task_index].IsUsable()) {
            continue;
        }

        // 延后多 GOTO：本轮不做，留给末尾聚合
        if (defer_multi_goto && tasks[task_index].behave == "goto") {
            continue;
        }

        const std::vector<CandidatePlan> candidates = EvaluateShadowCandidates(
            "main-loop", false, task_index);
        if (StopGate("main-loop", false, candidates)) return;
        const CandidatePlan* selected_candidate = FindCandidate(candidates, task_index);
        if (!selected_candidate) continue;
        if (!CanStartPlan(*selected_candidate, "main-loop")) continue;
        if (!ShouldStartConstraintTrade(*selected_candidate, candidates, "main-loop"))
            continue;

        isPass = false;
         /*============== 风险预判 ================*/
        if(CalculateTaskRisk(tasks[task_index])>=2) {     //先行判断
            cout<<tasks[task_index].behave<<" "<<"的风险系数是："<<tasks[task_index].risk<<endl;
            stringstream ss;
            ss << tasks[task_index];
            LOG(GREEN "too many cons\n %s" RESET, ss.str().c_str());
            continue;
        }

        /*============== 任务执行 ================*/

        // —— 零动作预验证：仅 stage2 且当前判定为“已满足”的任务才触发
        BeginCandidateExecution(*selected_candidate, "legacy_main_loop_task_order");
        bool zero_ok = ZeroActionPreCheck(tasks[task_index]);
        if (zero_ok) {
            stringstream ss; ss << tasks[task_index];
            LOG(GREEN "Task done (zero-action prevalidated)\n %s" RESET, ss.str().c_str());
            SetSolvedTaskNum(solved_task_num + 1);
            tasks[task_index].isEnable = false;
            AfterSolveTask(tasks[task_index]);
            EndCandidateExecution(true);
        } else if (SolveTask(tasks[task_index])) {
            stringstream ss; ss << tasks[task_index];
            LOG(GREEN "Task done\n %s" RESET, ss.str().c_str());
            SetSolvedTaskNum(solved_task_num + 1);
            tasks[task_index].isEnable = false;
            AfterSolveTask(tasks[task_index]);
            EndCandidateExecution(true);
        } else {
            stringstream ss; ss << tasks[task_index];
            LOG(GREEN "Task not done\n %s" RESET, ss.str().c_str());
            tasks[task_index].isEnable=0;
            tasks[task_index].isfalse=1;
            failed_task_revision[task_index] = world_revision;
            EndCandidateExecution(false);
        }
    }
}


/*============== 检查阶段 ==============*/
void RDFW::ExecuteCheckPhase(bool defer_multi_goto)
{
    for(task_index = 0; task_index < tasks.size(); ++task_index){
        // 跳过包含不存在物体的任务
        if (!tasks[task_index].IsUsable()) {
            continue;
        }

        // 延后多 GOTO：检查一遍也不做，留给末尾聚合
        if (defer_multi_goto && tasks[task_index].isEnable && tasks[task_index].behave == "goto") {
            continue;
        }
        if(tasks[task_index].isEnable&&CalculateTaskRisk(tasks[task_index])<2){
            const std::vector<CandidatePlan> candidates = EvaluateShadowCandidates(
                "check-phase", false, task_index);
            if (StopGate("check-phase", false, candidates)) return;
            const CandidatePlan* selected_candidate = FindCandidate(candidates, task_index);
            if (!selected_candidate) continue;
            if (!CanStartPlan(*selected_candidate, "check-phase")) continue;
            if (!ShouldStartConstraintTrade(*selected_candidate, candidates, "check-phase"))
                continue;
            BeginCandidateExecution(*selected_candidate, "legacy_check_phase_task_order");
            bool zero_ok = ZeroActionPreCheck(tasks[task_index]);
            if (zero_ok) {
                stringstream ss; ss << tasks[task_index];
                LOG(GREEN "Task done (zero-action prevalidated)\n %s" RESET, ss.str().c_str());
                SetSolvedTaskNum(solved_task_num + 1);
                tasks[task_index].isEnable = false;
                AfterSolveTask(tasks[task_index]);
                EndCandidateExecution(true);
            } else if (SolveTask(tasks[task_index])) {
                stringstream ss; ss << tasks[task_index];
                LOG(GREEN "Task done\n %s" RESET, ss.str().c_str());
                SetSolvedTaskNum(solved_task_num + 1);
                tasks[task_index].isEnable = false;
                AfterSolveTask(tasks[task_index]);
                EndCandidateExecution(true);
            } else {
                tasks[task_index].isEnable = false;
                tasks[task_index].isfalse = true;
                failed_task_revision[task_index] = world_revision;
                EndCandidateExecution(false);
            }
        }
    }
}

void RDFW::ExecuteTerminalRecovery() {
    const std::size_t max_attempts = tasks.size() * 2 + 2;
    for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
        const TerminalSummary current = terminal_checker.evaluateAll(*this);
        if (current.allGoalsSatisfied() || deadline_manager.deadlineReached()) return;
        const std::vector<CandidatePlan> candidates = EvaluateShadowCandidates(
            "terminal-recovery", true, std::numeric_limits<std::size_t>::max());
        const CandidatePlan* best = nullptr;
        for (const CandidatePlan& plan : candidates) {
            if (!plan.eligible || !plan.dry_run_succeeded ||
                plan.marginal_score <= 0 || plan.actions.empty()) continue;
            if (!deadline_manager.canFinish(plan.remainingDuration(), plan_safety_margin)) continue;
            if (!ShouldStartConstraintTrade(plan, candidates, "terminal-recovery"))
                continue;
            if (!best || plan.marginal_score > best->marginal_score) best = &plan;
        }
        if (!best) {
            LOG(YELLOW "[Recovery] stop: no positive legal candidate after full rescan; "
                       "goals=%zu/%zu revision=%zu\n" RESET,
                current.satisfied_goals, current.goals.size(), world_revision);
            return;
        }
        task_index = static_cast<int>(best->task_index);
        const std::size_t revision_before = world_revision;
        LOG(CYAN_BLUE "[Recovery] execute task=%zu behave=%s marginal=%d "
                      "actions=%zu revision=%zu\n" RESET,
            best->task_index, best->task_label.c_str(), best->marginal_score,
            best->actions.size(), world_revision);
        BeginCandidateExecution(*best, "terminal_recovery_positive_marginal_score");
        const bool reported_success = ZeroActionPreCheck(tasks[task_index]) ||
            SolveTask(tasks[task_index]);
        const bool terminal_success = reported_success &&
            terminal_checker.evaluateTask(*this, tasks[task_index]) ==
                TerminalStatus::SATISFIED;
        if (terminal_success) {
            tasks[task_index].isEnable = false;
            AfterSolveTask(tasks[task_index]);
            SetSolvedTaskNum(solved_task_num + 1);
        } else {
            tasks[task_index].isEnable = false;
            tasks[task_index].isfalse = true;
            failed_task_revision[task_index] = world_revision;
        }
        EndCandidateExecution(terminal_success);
        if (revision_before == world_revision && !terminal_success) return;
    }
    LOG(YELLOW "[Recovery] attempt cap reached\n" RESET);
}





/**====================== 约束条件规划 =========================== */


void RDFW::Cons_plan(){
    int x;
    for(auto cons:not_infoConstrains){
        // 跳过包含不存在物体的约束
        if (!cons.IsUsable() || cons.X.empty() || !cons.X[0]) {
            continue;
        }

        if(cons.behave=="on") {
            if (cons.Y.empty() || !cons.Y[0] || cons.Y[0]->location < 0 ||
                !EnsureLocationCapacity(cons.Y[0]->location)) continue;
            if(cons.X[0]->location!=cons.Y[0]->location) putdown_cons[cons.X[0]->id][cons.Y[0]->location]++;
            else if(cons.X[0]->id==plate_id||cons.X[0]->id==hold_id) putdown_cons[cons.X[0]->id][cons.Y[0]->location]++;
        }
        else if(cons.behave=="inside"||cons.behave=="in") {
             if (cons.Y.empty() || !cons.Y[0]) continue;
             auto small=dynamic_pointer_cast<SmallObject>(cons.X[0]);
            if(small && small->inside!=cons.Y[0]->id) putin_cons[cons.X[0]->id][cons.Y[0]->id]++;
        }
        else if(cons.behave == "near"||cons.behave == "nextto") {
            if (cons.Y.empty() || !cons.Y[0]) continue;
            if(cons.Y[0]->location!=cons.X[0]->location) //如果约束没有触犯
            {
            if(cons.Y[0]->location!=UNKNOWN) move_cons[cons.X[0]->id][cons.Y[0]->location]++;
            if(cons.X[0]->location!=UNKNOWN) move_cons[cons.Y[0]->id][cons.X[0]->location]++;
            }
        }
        else if(cons.behave == "plate") toplate_cons[cons.X[0]->id]++;
        else if(cons.behave == "opened") {
            auto cont=dynamic_pointer_cast<Container>(cons.X[0]);
            if(cont && cont->isOpen!=1) open_cons[cons.X[0]->id]++;
        }
        else if(cons.behave == "closed")
        {
            auto cont=dynamic_pointer_cast<Container>(cons.X[0]);
            if(cont && cont->isOpen==1) close_cons[cons.X[0]->id]++;
        }
    }
    for(auto cons:notnot_infoConstrains){
        // 跳过包含不存在物体的约束
        if (!cons.IsUsable() || cons.X.empty() || !cons.X[0]) {
            continue;
        }

            if(cons.behave=="on" && !cons.Y.empty() && cons.Y[0] &&
               cons.X[0]->location==cons.Y[0]->location) {
                auto small=dynamic_pointer_cast<SmallObject>(cons.X[0]);
               if(small && small->inside!=cons.Y[0]->id) cons.X[0]->is_keep++;
            }
            else if((cons.behave=="near" || cons.behave=="nextto") && !cons.Y.empty()){
                // 关系图已由 BuildMustNearRelations 对称构建；这里仅维护当前
                // 已满足关系的 keep 权重，且覆盖 every 产生的 X × Y。
                for (const auto& x_obj : cons.X) {
                    if (!x_obj) continue;
                    for (const auto& y_obj : cons.Y) {
                        if (!y_obj || x_obj->id == y_obj->id) continue;
                        if (x_obj->location != UNKNOWN && x_obj->location == y_obj->location) {
                            x_obj->is_keep++;
                            y_obj->is_keep++;
                        }
                    }
                }
            }
            else if(cons.behave=="plate"&& plate_id==cons.X[0]->id)fromplate_cons[cons.X[0]->id]++;
            else if(cons.behave=="inside"||cons.behave=="in")
            {
            if (cons.Y.empty() || !cons.Y[0]) continue;
            auto small=dynamic_pointer_cast<SmallObject>(cons.X[0]);
            if(small && small->inside==cons.Y[0]->id)  takeout_cons[cons.X[0]->id][cons.Y[0]->id]++;
            }
           else if(cons.behave=="closed") {
             auto cont=dynamic_pointer_cast<Container>(cons.X[0]);
            if(cont && cont->isOpen!=1) open_cons[cons.X[0]->id]++;
           }
           else if(cons.behave=="opened") {
            auto cont=dynamic_pointer_cast<Container>(cons.X[0]);
            if(cont && cont->isOpen!=1) close_cons[cons.X[0]->id]++;
           }
    }
    for(auto cons:not_taskConstrains){
        // 跳过包含不存在物体的约束
        if (!cons.IsUsable() || cons.X.empty() || !cons.X[0]) {
            continue;
        }

        //这里不用判断一开始是否触犯约束
         if(cons.behave=="takeout" && !cons.Y.empty() && cons.Y[0]) takeout_cons[cons.X[0]->id][cons.Y[0]->id]++;
         else if(cons.behave=="putin" && !cons.Y.empty() && cons.Y[0]) putin_cons[cons.X[0]->id][cons.Y[0]->id]++;
         else if(cons.behave=="puton" && !cons.Y.empty() && cons.Y[0] &&
                 cons.Y[0]->location >= 0 && EnsureLocationCapacity(cons.Y[0]->location))
             putdown_cons[cons.X[0]->id][cons.Y[0]->location]++;
         else if(cons.behave=="goto" && cons.X[0]->location >= 0 &&
                 EnsureLocationCapacity(cons.X[0]->location)) goto_cons[cons.X[0]->location]++;
         else if(cons.behave=="open") open_cons[cons.X[0]->id]++;
         else if(cons.behave=="close") close_cons[cons.X[0]->id]++;
         else if(cons.behave=="pickup") pickup_cons[cons.X[0]->id]++;
         else if(cons.behave=="give") givehuman_cons[cons.X[0]->id]++;
         else if(cons.behave == "putdown") putdown1_cons[cons.X[0]->id]++ ;
    }
    if(IsValidObjectId(hold_id) && location >= 0 && EnsureLocationCapacity(location)) {
        x=hold_id;
     if(objects[x]->is_keep>putdown1_cons[x]+putdown_cons[x][location]+fromplate_cons[x]){
        cout<<"the hold object must putdown here!"<<endl;
         PutDown(x);
     }
     }
    if(IsValidObjectId(plate_id) && location >= 0 && EnsureLocationCapacity(location))
    {
        x=plate_id;
     if(objects[x]->is_keep>putdown1_cons[x]+putdown_cons[x][location]+fromplate_cons[x]){
        cout<<"the plate object must putdown here!"<<endl;
        if(hold_id>0) PutDown(hold_id);
        FromPlate(x);
         PutDown(x);
     }
    }

}


void RDFW::FilterConstraintsByTaskConflicts() {
    // 只计算goto_cons和open_cons与多少任务冲突，超过4个则舍弃这个约束
    vector<pair<int, int>> discarded_goto;
    int sum_conflict_count=0;
    int max_effect=0;
    int max_effect_loc=0;
    // 1. 处理goto_cons
    for (int loc = 0; loc < (int)goto_cons.size(); ++loc) {
        if (goto_cons[loc] > 0) {
            int conflict_count = 0;
            for (const auto& task : tasks) {
                // 判断任务是否会与goto约束冲突
                if (task.isEnable && task.IsUsable()) {
                    const bool has_x = !task.X.empty() && task.X[0];
                    const bool has_y = !task.Y.empty() && task.Y[0];
                    if ((task.behave == "goto" && has_x && task.X[0]->location == loc) ||
                        (task.behave == "putin" && has_y && task.Y[0]->location == loc) ||
                        (task.behave == "putin" && has_x && task.X[0]->location == loc)||
                        (task.behave == "puton" && has_y && task.Y[0]->location == loc)||
                        (task.behave == "puton" && has_x && task.X[0]->location == loc)||
                        (task.behave == "open" && has_x && task.X[0]->location == loc)||
                        (task.behave == "close" && has_x && task.X[0]->location == loc)||
                        (task.behave == "pickup" && has_x && task.X[0]->location == loc)||
                        (task.behave == "give" && has_x && task.X[0]->location == loc)||
                        (task.behave == "give" && has_y && task.Y[0]->location == loc)||
                        (task.behave == "takeout" && has_y && task.Y[0]->location == loc)) {
                        conflict_count++;
                        cout << "[FilterConstraintsByTaskConflicts] Conflict found between goto_cons at location " << loc << " and task " << task.behave << endl;
                    }
                }
            }
            if (conflict_count - goto_cons[loc] > 2) {
                discarded_goto.push_back(make_pair(loc, conflict_count));
                sum_conflict_count+=conflict_count;
                if (conflict_count - goto_cons[loc] > max_effect) {
                    max_effect = conflict_count - goto_cons[loc];
                    max_effect_loc = loc;
                }
                cout << "[FilterConstraintsByTaskConflicts] Conflict found between goto_cons at location " << loc << " with conflict count " << conflict_count << endl;
            }
        }
    }


    if(sum_conflict_count>30) {
        goto_cons[max_effect_loc] = 0;
        cout << "[FilterConstraintsByTaskConflicts] Discarded goto_cons at location " << max_effect_loc << " due to " << max_effect << " conflicts." << endl;
    }
    else{
        for (auto it = discarded_goto.rbegin(); it != discarded_goto.rend(); ++it) {
            goto_cons[it->first] = 0;
            cout << "[FilterConstraintsByTaskConflicts] Discarded goto_cons at location " << it->first << " due to " << it->second << " conflicts." << endl;
        }
    }

    // 2. 处理open_cons，逻辑同goto_cons
    vector<pair<int, int>> discarded_open;
    int sum_conflict_count_open=0;
    int max_effect_open=0;
    int max_effect_id=0;
    for (int id = 0; id < (int)open_cons.size(); ++id) {
        if (open_cons[id] > 0) {
            int conflict_count = 0;
            for (const auto& task : tasks) {
                // 判断任务是否会与open约束冲突
                // 这里假设冲突定义为：任务涉及该id，且是open/putin/takeout/puton/pickup/give等
                if (task.isEnable && task.IsUsable()) {
                    const bool has_x = !task.X.empty() && task.X[0];
                    const bool has_y = !task.Y.empty() && task.Y[0];
                    if ((task.behave == "open" && has_x && task.X[0]->id == id) ||
                        (task.behave == "putin" && has_y && task.Y[0]->id == id) ||
                        (task.behave == "putin" && has_x && task.X[0]->id == id)||
                        (task.behave == "takeout" && has_y && task.Y[0]->id == id)||
                        (task.behave == "puton" && has_y && task.Y[0]->id == id)||
                        (task.behave == "pickup" && has_x && task.X[0]->id == id)||
                        (task.behave == "give" && has_x && task.X[0]->id == id)
                    ) {
                        conflict_count++;
                    }
                }
            }
            if (conflict_count - open_cons[id] > 2) {
                discarded_open.push_back(make_pair(id, conflict_count));
                sum_conflict_count_open += conflict_count;
                if (conflict_count > max_effect_open) {
                    max_effect_open = conflict_count;
                    max_effect_id = id;
                }
                cout << "[FilterConstraintsByTaskConflicts] Conflict found between open_cons for id " << id << " with conflict count " << conflict_count << endl;
            }
        }
    }

    if(sum_conflict_count_open>30) {
        open_cons[max_effect_id] = 0;
        cout << "[FilterConstraintsByTaskConflicts] Discarded open_cons for id " << max_effect_id << " due to " << max_effect_open << " conflicts." << endl;
    }
    else{
        for (const auto& item : discarded_open) {
            open_cons[item.first] = 0;
            cout << "[FilterConstraintsByTaskConflicts] Discarded open_cons for id " << item.first << " due to " << item.second << " conflicts." << endl;
        }
    }



}


/**====================== 任务优化 =========================== */

//任务优化函数
vector<Instruction> RDFW::TaskOptimization()
{
    auto taskEvaluate = [](const string &behave) -> int
    {
        if (behave == "putin" || behave == "puton" || behave == "give")
            return 0;
        else if (behave == "takeout" ||behave == "putdown")
            return 1;
        else if (behave == "open" || behave == "close")
            return 2;
        else if ( behave == "pickup")
            return 3;
        else if (behave == "goto")
            return 4;
        else
            return 5;
    };
    /*
    auto TaskEquel =[](Instruction task1,Instruction task2) ->bool{
          if(task1.behave==task2.behave&&task1.behave!="pickup"&&task1.behave!="goto")
                if(task1.conditionX.sort==task2.conditionX.sort)
                    return true;
        if(task1.behave==task2.behave){
                if(task1.behave=="pickup"||task1.behave=="goto")
                return true;
            }

          return false;
    }*/
    // Sort tasks based on behavior evaluation and container grouping for takeout tasks
    std::sort(tasks.begin(), tasks.end(), [&](const Instruction &a, const Instruction &b)
              {
                  int priority_a = taskEvaluate(a.behave);
                  int priority_b = taskEvaluate(b.behave);

                  // If different task types, sort by priority
                  if (priority_a != priority_b) {
                      return priority_a < priority_b;
                  }

                  // If both are takeout tasks, group by container (Y[0]->id)
                  if (a.behave == "takeout" && b.behave == "takeout") {
                      if (!a.Y.empty() && !b.Y.empty()) {
                          return a.Y[0]->id < b.Y[0]->id;
                      }
                  }

                  // For other cases, maintain original order (stable sort)
                  return false;
              });


    vector<Instruction> optimizedTasks;
    optimizedTasks.reserve(tasks.size());

    bool hasGoto = false;
    for(int i=0;i<tasks.size();i++){
        // 跳过包含不存在物体的任务
        if (!tasks[i].IsUsable()) {
            continue;
        }

        optimizedTasks.push_back(tasks[i]);
      }

return optimizedTasks;
}

bool RDFW::HasRequestedTask(const string &behave, unsigned int object_id,
                            unsigned int target_id) const
{
    for (const auto &task : tasks) {
        // 与原索引一致：不按 isEnable/isfalse 过滤，只匹配 X[0]/Y[0]。
        // 扩展到全部候选对象会改变冲突策略，不属于本次结构简化。
        if (!task.IsUsable() || task.behave != behave ||
            task.X.empty() || !task.X[0] || task.X[0]->id != object_id) {
            continue;
        }
        if (target_id != NONE &&
            (task.Y.empty() || !task.Y[0] || task.Y[0]->id != target_id)) {
            continue;
        }
        return true;
    }
    return false;
}




/**====================== 任务执行 =========================== */

/*========1)风险预判 ===============*/

//计算任务风险
int RDFW::CalculateTaskRisk(Instruction &t){
    t.risk=0;
    if (!t.IsUsable() || t.X.empty() || !t.X[0] ||
        !IsValidObjectId(t.X[0]->id)) {
        t.risk = std::numeric_limits<int>::max() / 4;
        return t.risk;
    }
    const bool needs_y = t.behave == "takeout" || t.behave == "putin" ||
                         t.behave == "puton";
    if (needs_y && (t.Y.empty() || !t.Y[0] ||
                    !IsValidObjectId(t.Y[0]->id))) {
        t.risk = std::numeric_limits<int>::max() / 4;
        return t.risk;
    }
    auto goto_risk = [&](int loc) {
        if (loc < 0 || !EnsureLocationCapacity(loc)) return 0;
        return goto_cons[loc];
    };
   if(t.behave=="takeout")
   {
     auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
    if (!small) return std::numeric_limits<int>::max() / 4;
    if(small->inside!=t.Y[0]->id) return 0;//如果任务满足
     t.risk+=takeout_cons[t.X[0]->id][t.Y[0]->id]+goto_risk(t.Y[0]->location);
     t.risk+=open_cons[t.Y[0]->id];
    }

     else if(t.behave=="putin") {
          auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
          if (!small) return std::numeric_limits<int>::max() / 4;
           if(small->inside==t.Y[0]->id) return 0;
            t.risk+=putin_cons[t.X[0]->id][t.Y[0]->id]+open_cons[t.Y[0]->id];
            if (t.Y[0]->location >= 0 && EnsureLocationCapacity(t.Y[0]->location))
                t.risk += move_cons[t.X[0]->id][t.Y[0]->location];
            if(t.X[0]->location!=t.Y[0]->location) t.risk+=goto_risk(t.Y[0]->location);
            CalculateStepRisk(t);

      }
      else if(t.behave=="puton") {
          if (t.Y[0]->location >= 0 && EnsureLocationCapacity(t.Y[0]->location))
              t.risk+= putdown_cons[t.X[0]->id][t.Y[0]->location]+move_cons[t.X[0]->id][t.Y[0]->location];
          t.risk += putdown1_cons[t.X[0]->id];
        if(t.X[0]->location!=t.Y[0]->location) t.risk+=goto_risk(t.Y[0]->location);
        CalculateStepRisk(t);
      }
      else if (t.behave == "goto") {
          int loc = t.X[0]->location;
          t.risk += goto_risk(loc);
           // 调试日志，明确 t.risk 的组成
          std::cout << "[DBG] goto risk@loc=" << loc
                    << " bool=" << goto_risk(loc)
                   << std::endl;
      }

      else if(t.behave=="open") t.risk+=open_cons[t.X[0]->id]+goto_risk(t.X[0]->location);
      else if(t.behave=="close") t.risk+=close_cons[t.X[0]->id]+goto_risk(t.X[0]->location);
      else if(t.behave=="pickup") {
         CalculateStepRisk(t);
      }
      else if(t.behave=="give") {
         CalculateStepRisk(t);
         if (!human) return std::numeric_limits<int>::max() / 4;
         t.risk+=givehuman_cons[t.X[0]->id]+putdown1_cons[t.X[0]->id];
         if (human->location >= 0 && EnsureLocationCapacity(human->location))
             t.risk += move_cons[t.X[0]->id][human->location];
         if(t.X[0]->location!=human->location) t.risk+=goto_risk(human->location);
         auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
      }
      else if(t.behave == "putdown")t.risk+=putdown1_cons[t.X[0]->id];
    //  t.risk+=t.X[0]->is_keep;
    // goto targets an object's location; the target object itself is untouched.
    // Any carried object's move risk remains covered by move_cons/goto_cons.
    int keep_penalty = (t.behave == "goto" || t.behave == "move")
        ? 0 : t.X[0]->is_keep;
    // 特例：仅对 pickup，且与 near/next-to 的“对端对象”仍在同一位置时，
    // 视为 pickup 不破坏 near/next-to —— 不计入这部分 keep 惩罚。
    if (t.behave == "pickup") {
        int near_keep = 0;
        for (const auto &cons : notnot_infoConstrains) {
            if (cons.behave == "near") { // 你的“next to”在解析里等同于 near
                const auto aId = t.X[0]->id;
                bool a_is_X = (!cons.X.empty() && cons.X[0] && cons.X[0]->id == aId);
                bool a_is_Y = (!cons.Y.empty() && cons.Y[0] && cons.Y[0]->id == aId);
                if (!(a_is_X || a_is_Y)) continue;

                // 找到与 t.X[0] 成 near 关系的“另一端对象”
                shared_ptr<Object> other =
                    a_is_X ? (cons.Y.size() ? cons.Y[0] : nullptr)
                           : (cons.X.size() ? cons.X[0] : nullptr);

                // 如果二者当前确实在同一位置，则“原地 pickup”不会破坏 near
                if (other && other->location == t.X[0]->location && t.X[0]->location != UNKNOWN) {
                    near_keep++;
                }
            }
        }
        // 只剔除 near/next-to 导致的 keep 惩罚，其它类型的 keep 仍然有效
        if (near_keep > 0) {
            keep_penalty = std::max(0, keep_penalty - near_keep);
        }
    }
    t.risk += keep_penalty;
      return t.risk;
}


//计算步骤风险------计算获取物体的约束值
int RDFW:: CalculateStepRisk(Instruction &t){
    if (t.X.empty() || !t.X[0] || !IsValidObjectId(t.X[0]->id)) return 0;
    if(t.X[0]->location!=location && t.X[0]->location >= 0) {
        if (!EnsureLocationCapacity(t.X[0]->location)) return 0;
        t.risk+=goto_cons[t.X[0]->location];
    }
    auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
    if (!small) return 0;
    if(small->inside!=UNKNOWN&&small->inside!=NONE &&
       IsValidObjectId(small->inside))
        t.risk+=open_cons[small->inside]+takeout_cons[small->id][small->inside];
    else if(small->inside==NONE) t.risk+=pickup_cons[small->id];
    return 1;
}

// ==== helpers for capacity & existence ====
bool RDFW::IsValidObjectId(int id) const {
    return id > 0 && id <= static_cast<int>(MAX_OBJECT_ID) &&
           static_cast<std::size_t>(id) < objects.size() && objects[id];
}

shared_ptr<Object> RDFW::GetObject(int id) const {
    return IsValidObjectId(id) ? objects[static_cast<std::size_t>(id)] : nullptr;
}

bool RDFW::EnsureLocationCapacity(int loc) {
    if (loc < 0 || loc > MAX_LOCATION_ID) {
        LOG_ERROR("Location id %d is outside the supported range [0,%d]",
                  loc, MAX_LOCATION_ID);
        return false;
    }

    // Normal inputs stay inside the preallocated range.  Keep this hot path
    // constant-time; full row scans are needed only when a new location column
    // is actually requested.
    const std::size_t required = static_cast<std::size_t>(loc) + 1;
    if (required <= posCorrectFlag.size() &&
        required <= posSensedFlag.size() &&
        required <= locationSensedObjects.size() &&
        required <= goto_cons.size() &&
        required <= rightlocation.size() &&
        (putdown_cons.empty() || required <= putdown_cons.front().size()) &&
        (move_cons.empty() || required <= move_cons.front().size())) {
        return true;
    }

    // 扩展位置相关的动态数组
    if (loc >= (int)posCorrectFlag.size()) posCorrectFlag.resize(loc + 1, true); // 位置维度
    if (loc >= (int)posSensedFlag.size()) posSensedFlag.resize(loc + 1, false);
    if (loc >= (int)locationSensedObjects.size()) locationSensedObjects.resize(loc + 1);

    // 扩展约束数组
    if (loc >= (int)goto_cons.size()) goto_cons.resize(loc + 1, 0);
    if (loc >= (int)rightlocation.size()) rightlocation.resize(loc + 1, false);

    // putdown/move 的行是对象 id，列是位置；位置扩容必须扩每一行，
    // 不能把位置误当成对象 id 去新增行。
    for (auto& row : putdown_cons)
        if (row.size() <= static_cast<std::size_t>(loc)) row.resize(loc + 1, 0);
    for (auto& row : move_cons)
        if (row.size() <= static_cast<std::size_t>(loc)) row.resize(loc + 1, 0);
    return true;
}

bool RDFW::EnsureObjectExists(unsigned id, bool prefer_small) {
    if (id == 0 || id > MAX_OBJECT_ID) {
        LOG_ERROR("Object id %u is outside the supported range [1,%u]",
                  id, MAX_OBJECT_ID);
        return false;
    }
    if (id >= objects.size()) {
        size_t last = objects.size();
        objects.resize(id + 1);
        for (size_t i = last; i < objects.size(); ++i) {
            objects[i] = std::make_shared<Object>((unsigned)i);
        }
    }
    if (prefer_small) {
        if (!std::dynamic_pointer_cast<SmallObject>(objects[id])) {
            objects[id] = std::make_shared<SmallObject>(objects[id]); // 基于已有 Object 包装
            smallObjects.push_back(std::dynamic_pointer_cast<SmallObject>(objects[id]));
        }
    }
    const size_t object_count = objects.size();

    goto_cons.resize(std::max(goto_cons.size(), rightlocation.size()), 0);
    putdown1_cons.resize(object_count, 0);
    open_cons.resize(object_count, 0);
    close_cons.resize(object_count, 0);
    pickup_cons.resize(object_count, 0);
    givehuman_cons.resize(object_count, 0);
    fromplate_cons.resize(object_count, 0);
    toplate_cons.resize(object_count, 0);

    putin_cons.resize(object_count);
    takeout_cons.resize(object_count);
    for (auto& row : putin_cons) row.resize(object_count, 0);
    for (auto& row : takeout_cons) row.resize(object_count, 0);

    const std::size_t location_count = std::max<std::size_t>(100, rightlocation.size());
    putdown_cons.resize(object_count);
    move_cons.resize(object_count);
    for (auto& row : putdown_cons) row.resize(location_count, 0);
    for (auto& row : move_cons) row.resize(location_count, 0);

    if (mustnear_cons.size() < object_count) mustnear_cons.resize(object_count);
    for (auto& row : mustnear_cons) {
        if (row.size() < object_count) row.resize(object_count, 0);
    }
    if (mustNearComponent.size() < object_count)
        mustNearComponent.resize(object_count, UNKNOWN);
    if (lock_by_mustnear.size() < object_count)
        lock_by_mustnear.resize(object_count, false);
    return EnsureEvidenceCapacity(id);
}

bool RDFW::EnsureEvidenceCapacity(unsigned int id) {
    if (id > MAX_OBJECT_ID) return false;
    const size_t required = static_cast<size_t>(id) + 1;
    if (objectLocationVerified.size() < required)
        objectLocationVerified.resize(required, false);
    if (objectLocationInferredByMustNear.size() < required)
        objectLocationInferredByMustNear.resize(required, false);
    if (objectInsideVerified.size() < required)
        objectInsideVerified.resize(required, false);
    if (containerStateVerified.size() < required)
        containerStateVerified.resize(required, false);
    if (objectLocationSource.size() < required)
        objectLocationSource.resize(required, EvidenceSource::UNKNOWN);
    if (objectInsideSource.size() < required)
        objectInsideSource.resize(required, EvidenceSource::UNKNOWN);
    if (containerStateSource.size() < required)
        containerStateSource.resize(required, EvidenceSource::UNKNOWN);
    return true;
}

void RDFW::MarkDirectLocationEvidence(unsigned int id, bool verified,
                                      EvidenceSource source) {
    if (!EnsureEvidenceCapacity(id)) return;
    objectLocationVerified[id] = verified;
    objectLocationSource[id] = source;
    objectLocationInferredByMustNear[id] =
        source == EvidenceSource::CONSTRAINT_DERIVED ||
        source == EvidenceSource::CONSTRAINT_HEURISTIC;
}

void RDFW::SetInsideEvidence(unsigned int id, bool verified, EvidenceSource source) {
    if (!EnsureEvidenceCapacity(id)) return;
    objectInsideVerified[id] = verified;
    objectInsideSource[id] = source;
}

void RDFW::SetContainerEvidence(unsigned int id, bool verified, EvidenceSource source) {
    if (!EnsureEvidenceCapacity(id)) return;
    containerStateVerified[id] = verified;
    containerStateSource[id] = source;
}

EvidenceSource RDFW::LocationSource(unsigned int id) const {
    return id < objectLocationSource.size() ? objectLocationSource[id] : EvidenceSource::UNKNOWN;
}
EvidenceSource RDFW::InsideSource(unsigned int id) const {
    return id < objectInsideSource.size() ? objectInsideSource[id] : EvidenceSource::UNKNOWN;
}
EvidenceSource RDFW::ContainerSource(unsigned int id) const {
    return id < containerStateSource.size() ? containerStateSource[id] : EvidenceSource::UNKNOWN;
}

void RDFW::InvalidateSenseAtLocation(int loc) {
    if (loc < 0) return;
    if (!EnsureLocationCapacity(loc)) return;
    posSensedFlag[loc] = false;
    locationSensedObjects[loc].object_ids.clear();
    locationSensedObjects[loc].container_id = NONE;
    locationSensedObjects[loc].has_container = false;
}

bool RDFW::IsLocationVerified(unsigned int id) const {
    return id < objectLocationVerified.size() && objectLocationVerified[id];
}

bool RDFW::IsInsideVerified(unsigned int id) const {
    return id < objectInsideVerified.size() && objectInsideVerified[id];
}

bool RDFW::IsContainerStateVerified(unsigned int id) const {
    return id < containerStateVerified.size() && containerStateVerified[id];
}

bool RDFW::IsAbsentFromSensedLocation(unsigned int id, int loc) const {
    if (stage != 2 || tasks.size() > 24 || loc < 0 ||
        static_cast<std::size_t>(loc) >= posSensedFlag.size() ||
        !posSensedFlag[loc] || !IsValidObjectId(static_cast<int>(id)) ||
        hold_id == static_cast<int>(id) || plate_id == static_cast<int>(id))
        return false;
    const LocationSensedInfo& sensed = locationSensedObjects[loc];
    if (std::find(sensed.object_ids.begin(), sensed.object_ids.end(), id) !=
        sensed.object_ids.end()) return false;
    // A closed container can hide a small object at this location.
    if (std::dynamic_pointer_cast<SmallObject>(objects[id]) && sensed.has_container)
        return false;
    return true;
}


/*========2)任务顺序执行 ===============*/

/*======a)做任务（边执行边判断）==========*/


//执行任务----主要是看有没有任务对象，服务stage2
bool RDFW::SolveTask(const Instruction &task) // 改
{
    bool success = false;//是否成功执行

    if (!task.IsUsable()) {
        LogInstructionError(task);
        return false;
    }

    if (task.behave == "puton" || task.behave == "putin" || task.behave == "takeout")
    {
        if (task.X.empty() || task.Y.empty())
        {
            LogInstructionError(task);
            return false;
        }
        for (const auto &a : task.X)
        {
            if (!a || !IsValidObjectId(a->id) || !task.Y[0]) return false;
            cout<<task.behave<<" "<<a->sort<<"的风险系数是："<<task.risk<<endl;
            success = DoBehavious(task.behave, a->id, task.Y[0]->id);
            if (!success)
                break;
        }
    }
    else if (task.X.empty())
    {
        LogInstructionError(task);
        success = false;
    }
    else
    {
        for (const auto &a : task.X)
        {
            if (!a || !IsValidObjectId(a->id)) return false;
            cout<<task.behave<<" "<<a->sort<<"的风险系数是："<<task.risk<<endl;
            success = DoBehavious(task.behave,a->id);
            if (!success)
                break;
        }
    }
    return success;
}

//执行行为---一个对象
bool RDFW::DoBehavious(const string &behavious, unsigned int x)
{
    if (x == 0 || x > MAX_OBJECT_ID || !IsValidObjectId(static_cast<int>(x)))
        return false;
    if (behavious == "move" || behavious == "Move" || behavious == "Goto" || behavious == "goto")
        return SolveTask_Goto(x);
    else if (behavious == "pickup" || behavious == "PickUp")
        return SolveTask_PickUp(x);
    else if (behavious == "close" || behavious == "Close")
    {
        return SolveTask_Close(x);
    }

    else if (behavious == "open" || behavious == "Open")
    {
        return SolveTask_Open(x);
    }
    else if (behavious == "putdown" || behavious == "PutDown")
        return SolveTask_PutDown(x);
    else if (behavious == "give" || behavious == "Give")
        return SolveTask_Give(x);
    else
    {
           LOG("error task error task error task");
           return false;
    }
}

//执行行为---两个对象
bool RDFW::DoBehavious(const string &behavious, unsigned int a, unsigned int b) //dobehavious函数
{
    if (a == 0 || b == 0 || a > MAX_OBJECT_ID || b > MAX_OBJECT_ID ||
        !IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b))) return false;
    if (behavious == "putin" || behavious == "PutIn")
    {
        return SolveTask_Putin(a,b);
    }

    else if (behavious == "takeout" || behavious == "TakeOut")
    {
       return SolveTask_TakeOut(a,b);
    }

    else if (behavious == "puton" || behavious == "PutOn")
    {
        return SolveTask_PutOn(a,b);
    }
    else
    {
        LOG("error task error task error task");
        return false;
    }
}

//拿起物体的逻辑
bool RDFW::HoldSmallObject(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    int t = 0;
    auto target_small = dynamic_pointer_cast<SmallObject>(objects[a]);
    ///这是stage1的逻辑
     if(stage==1)
    {
      if (plate_id == a)
        {
        if (hold_id != NONE && !PutDown(hold_id)) return false;
        return FromPlate(a);
        }
       else if(hold_id==a) return true;
    if (hold_id != NONE && !PutDown(hold_id)) return false;
    if(location!=target_small->location && !Move(target_small->location)) return false;
    if(target_small->inside==NONE) return PickUp(a);
    else if(target_small->inside!=UNKNOWN)//说明小物体在容器里面
    {
       if (!IsValidObjectId(target_small->inside)) return false;
       auto target_cont = dynamic_pointer_cast<Container>(objects[target_small->inside]);
       if (!target_cont) return false;
       if(!target_cont->isOpen && !Open(target_cont->id)) return false;
       return  TakeOut(a,target_cont->id);
    }
    return false;
    }
    //这是stage2的逻辑
    if (hold_id == static_cast<int>(a)) {
        if (IsInsideVerified(a)) return true;
        // 初始 hold 事实可能是错的。用一次可观察动作建立本地事实；失败则清除猜测。
        if (PutDown(a)) return HoldSmallObject(a);
        SetHold(nullptr);
    }
    if (plate_id == static_cast<int>(a) && !IsInsideVerified(a)) {
        if (FromPlate(a)) return true;
        SetPlate(nullptr);
    }
    if (hold_id != a)
    {
        if (hold_id != NONE && !PutDown(hold_id)) return false; //如果拿着物体，先放下
        if (plate_id == a)
        {
        return FromPlate(a);
        }

        while (1)
        {
            t++;
            if (target_small->location != UNKNOWN)
            {
                if (location != target_small->location)
                    if(Move(target_small->location)!=1)
                    {
                        if (t >= 2)  return 0;
                        GetSmallObjectStatus(a);
                        if(!IsKeepingGoing(task_index)) return 0;
                        continue;
                    }
            //Airong:Move后感知，位置可能重新标记为UNKNOWN
                if(target_small->location == UNKNOWN ){
                    if (t >= 2)  return 0;
                    GetSmallObjectStatus(a);
                    if(!IsKeepingGoing(task_index)) return 0;
                    continue;
                }


                 if (target_small->inside == NONE||target_small->inside == UNKNOWN) //这里我想了想，可能不会有UNKOWN的情况
                {

                     if (target_small->location == location && PickUp(a)) return 1;

                     // 先保证容量（这是“语句”，必须放在 if 条件外执行）
                     if (!EnsureLocationCapacity(location)) return false;

                     // 然后再按条件判断
                     if ( posSensedFlag[location]
                          && target_small->location == location
                          && HasContainerAtLocation(location)
                          && [&]{
                                 unsigned int cont_id = GetContainerAtLocation(location);
                                 if (IsValidObjectId(static_cast<int>(cont_id))) {
                                     auto cont = dynamic_pointer_cast<Container>(objects[cont_id]);
                                     return (cont && cont->isOpen);
                                 }
                                 return false;
                             }() )
                     {
                         if (TakeOut(a, GetContainerAtLocation(location))) return 1;
                         if (t >= 2) return 0;
                     }
                     else {
                         if (plate_id == UNKNOWN && FromPlate(a)) return 1;
                         target_small->location = UNKNOWN;
                         MarkDirectLocationEvidence(a, false, EvidenceSource::ACTION_FAILURE);
                         if (t >= 2) return 0;
                         GetSmallObjectStatus(a);
                         if (!IsKeepingGoing(task_index)) return 0;
                         continue;
                     }


                }


                else
                {
                    int initial_cont_id=target_small->inside;
                    if (!IsValidObjectId(initial_cont_id) ||
                        !dynamic_pointer_cast<Container>(objects[initial_cont_id])) {
                        target_small->inside = UNKNOWN;
                        continue;
                    }
                    TakeOutResult result = TakeOutLogic(a,target_small->inside);
                    if(result == TakeOutResult::Success) return true;
                    else
                    {

                        if(result == TakeOutResult::NeedObjectLocation)  {if (t >= 2)  return 0;GetSmallObjectStatus(a);}
                        else if(result == TakeOutResult::NeedContainerLocation) {if (t >= 2)  return 0;GetBigObjectStatus(target_small->inside);}
                        else if(result == TakeOutResult::VerifyObjectRelation)
                        {
                            if (t >= 2)  return 0;
                            GetSmallObjectStatus(a);
                            if(Isinside(a,initial_cont_id))//a就在一开始容器里面
                            {
                                    if(Open(initial_cont_id)) return TakeOut(a,initial_cont_id); //认为骗我是关的
                                    else  GetBigObjectStatus(initial_cont_id);//open失败，本身不可能是open的，直接问容器
                            }
                            else
                            {
                                //小物体不在容器里了
                            }
                        }
                        if(!IsKeepingGoing(task_index)) return 0;
                        continue;
                    }

                }
            }
            else{
                  if (t >= 2)  return 0;
                GetSmallObjectStatus(a);
                if(!IsKeepingGoing(task_index)) return 0;
                continue;
            }

        }
    }
    return 1;
}

// 从容器拿出物体的逻辑。显式结果类型避免数字返回码在调用处被误解。
RDFW::TakeOutResult RDFW::TakeOutLogic(unsigned int small,unsigned int cont){
    if (!IsValidObjectId(static_cast<int>(small)) ||
        !IsValidObjectId(static_cast<int>(cont))) {
        return TakeOutResult::NeedContainerLocation;
    }
    auto target_cont = dynamic_pointer_cast<Container>(objects[cont]);
    if (!target_cont) return TakeOutResult::NeedContainerLocation;
    auto verified_absent_from_open_container = [&]() -> bool {
        if (!target_cont->isOpen || !IsContainerStateVerified(cont)) return false;
        SenseCurrentLocationOnly(true);
        const bool absent = HasObjectAtLocation(location, cont) &&
                            !HasObjectAtLocation(location, small);
        if (absent && small < objects.size()) {
            auto small_object = dynamic_pointer_cast<SmallObject>(objects[small]);
            if (small_object && small_object->inside == static_cast<int>(cont)) {
                target_cont->DeleteObjectInside(small_object);
                small_object->inside = UNKNOWN;
                EnsureEvidenceCapacity(small);
                SetInsideEvidence(small, false, EvidenceSource::SENSE);
            }
        }
        return absent;
    };
    //Airong:
    if(target_cont->location==UNKNOWN)return TakeOutResult::NeedContainerLocation;

    if(!target_cont->isOpen)
    {
     if(Open(cont))
        if(!TakeOut(small,cont)) {
            if (verified_absent_from_open_container()) return TakeOutResult::Success;
            return TakeOutResult::NeedObjectLocation;
        }
        else return TakeOutResult::Success;
    else
    {
      if(sense(cont))
      {
        target_cont->isOpen = true;
        EnsureEvidenceCapacity(cont);
        SetContainerEvidence(cont, true, EvidenceSource::ACTION_FAILURE);
        if(!TakeOut(small,cont)) {
            if (verified_absent_from_open_container()) return TakeOutResult::Success;
            return TakeOutResult::NeedObjectLocation;
        }
        else return TakeOutResult::Success;
      }
      else return TakeOutResult::NeedContainerLocation;
    }
    }
    else //不需要打开容器
    {
         if(!TakeOut(small,cont))
         {
            if (verified_absent_from_open_container()) return TakeOutResult::Success;
            return TakeOutResult::VerifyObjectRelation;
         }
         else return TakeOutResult::Success;
    }
}


/*===============
  solve task流程
  ==============*/

//pickup任务
bool RDFW::SolveTask_PickUp(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    if(HasRequestedTask("putdown", a)){
        if(hold_id==a||plate_id==a) return true;
        else {
            cout<<"there is putdown task,no need to do this!"<<endl;
            return false;
        }
    }
    if(hold_id==a ||plate_id==a) return true;
    else return HoldSmallObject(a);
}

bool RDFW::SolveTask_PutDown(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    if(HasRequestedTask("pickup", a)){
        if(hold_id!=a&&plate_id!=a) return true;
        else {
            cout<<"there is pickup task,no need to do this!"<<endl;
            return false;
        }
    }
    if(hold_id==a){
        if (location < 0 || !EnsureLocationCapacity(location)) return false;
        if(putdown_cons[a][location]) {
            int safe_location = findrightlocation(a);
            if (safe_location == UNKNOWN || !Move(safe_location)) return false;
        }
        return PutDown(a);
    }
    else if(plate_id==a){
        if (location < 0 || !EnsureLocationCapacity(location)) return false;
        if(hold_id>0) {
            if (!PutDown(hold_id)) return false;
            if(putdown_cons[a][location]) {
                int safe_location = findrightlocation(a);
                if (safe_location == UNKNOWN || !Move(safe_location)) return false;
            }
        }
        if (!FromPlate(a)) return false;
        if(putdown_cons[a][location]) {
            int safe_location = findrightlocation(a);
            if (safe_location == UNKNOWN || !Move(safe_location)) return false;
        }
       return PutDown(a);
    }
    else {
        auto small = dynamic_pointer_cast<SmallObject>(objects[a]);
        if (!small) return false;
        if (stage == 1 && small->inside == NONE) return true;
        if (stage == 2 && IsInsideVerified(a) && IsLocationVerified(a) && small->inside == NONE)
            return true;
        if (!HoldSmallObject(a)) return false;
        if (location < 0) return false;
        if (!EnsureLocationCapacity(location)) return false;
        if (putdown_cons[a][location]) {
            int safe_location = findrightlocation(a);
            if (safe_location == UNKNOWN || !Move(safe_location)) return false;
        }
        return PutDown(a);
    }
}

//goto任务
bool RDFW::SolveTask_Goto(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a))) return false;
    if(stage==1)
    {
        if(location==objects[a]->location){
        return true;
    }
    else return Move(objects[a]->location);
    }

    //stage2的情况
    if(location==objects[a]->location && IsLocationVerified(a)) return true;
    const bool is_small = dynamic_pointer_cast<SmallObject>(objects[a]) != nullptr;
    if(objects[a]->location==UNKNOWN)
    {
        if(dynamic_pointer_cast<SmallObject>(objects[a]) != nullptr)
        {
               GetSmallObjectStatus(a);
               if(!IsKeepingGoing(task_index)) return 0;
        }
        else
        {
        GetBigObjectStatus(a);
        if(!IsKeepingGoing(task_index)) return 0;
        }
    }
    int t=0;
    while(1)
    {
        t++;
    if(location==objects[a]->location) {
        SenseCurrentLocationOnly(true);
        if (HasObjectAtLocation(location, a)) return true;
        auto small = dynamic_pointer_cast<SmallObject>(objects[a]);
        if (small && small->inside > 0 && HasObjectAtLocation(location, small->inside)) {
            MarkDirectLocationEvidence(a, true, EvidenceSource::SENSE);
            return true;
        }
    }
    else if(!Move(objects[a]->location))
    {
          if(t>=2) return false;
          if(is_small) {GetSmallObjectStatus(a);if(!IsKeepingGoing(task_index)) return 0;}
          else {GetBigObjectStatus(a);if(!IsKeepingGoing(task_index)) return 0;}
    }
    else {
        SenseCurrentLocationOnly(true);
        if (HasObjectAtLocation(location, a)) return true;
        auto small = dynamic_pointer_cast<SmallObject>(objects[a]);
        if (small && small->inside > 0 && HasObjectAtLocation(location, small->inside)) {
            MarkDirectLocationEvidence(a, true, EvidenceSource::SENSE);
            return true;
        }
        if(t>=2) return false;
        if(is_small) GetSmallObjectStatus(a); else GetBigObjectStatus(a);
    }
    }
    return false;
}

//open任务
bool RDFW::SolveTask_Open(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<Container>(objects[a])) return false;
    auto cnt=dynamic_pointer_cast<Container>(objects[a]);
    if(HasRequestedTask("close", a)){
        if(cnt->isOpen && (stage == 1 || IsContainerStateVerified(a))) return true;
        else {
            cout<<"there is close task,no need to do this!"<<endl;
            return false;
        }
    }
    if(cnt->isOpen && (stage == 1 || IsContainerStateVerified(a))) return true;
    if(hold_id!=NONE && !PutDown(hold_id)) return false;
    if(stage==1)
    {
        if (location != objects[a]->location && !Move(objects[a]->location)) return false;
        return Open(a);
    }

    //这是stage2
    if(objects[a]->location==UNKNOWN)
    {
        GetBigObjectStatus(a);
        if(!IsKeepingGoing(task_index)) return 0;
    }
     int t=0;
    while(1)
    {
        t++;
    if (location != objects[a]->location)
        if(!Move(objects[a]->location))
        {
            if(t>=2) return false;
            GetBigObjectStatus(a);
             if(!IsKeepingGoing(task_index)) return 0;
        }
    if(!Open(a))
    {
       if (sense(a)) {
            cnt->isOpen = true;
            EnsureEvidenceCapacity(a);
            SetContainerEvidence(a, true, EvidenceSource::ACTION_FAILURE);
            MarkDirectLocationEvidence(a, true, EvidenceSource::SENSE);
            return true;
       }
       if(t>=2) return false;
            GetBigObjectStatus(a);
             if(!IsKeepingGoing(task_index)) return 0;
    }
    else return true;
    }
    return false;
}

//close任务
bool RDFW::SolveTask_Close(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<Container>(objects[a])) return false;
    auto cnt=dynamic_pointer_cast<Container>(objects[a]);
    if(HasRequestedTask("open", a))
    {
        if(!cnt->isOpen && (stage == 1 || IsContainerStateVerified(a))) return true;
        else {
            cout<<"there is open task,no need to do this!"<<endl;
            return false;
        }
    }
    if(!cnt->isOpen && (stage == 1 || IsContainerStateVerified(a))) return true;
    if(hold_id!=NONE && !PutDown(hold_id)) return false;
    if(stage==1)
    {
    if (location != objects[a]->location && !Move(objects[a]->location)) return false;
    return Close(a);
    }

   //这是stage2

   if(objects[a]->location==UNKNOWN)
    {
        GetBigObjectStatus(a);
        if(!IsKeepingGoing(task_index)) return 0;
    }
     int t=0;
    while(1)
    {
        t++;
    if (location != objects[a]->location)
        if(!Move(objects[a]->location))
        {
            if(t>=2) return false;
            GetBigObjectStatus(a);
             if(!IsKeepingGoing(task_index)) return 0;
        }
    if(!Close(a))
    {
       if (sense(a)) {
            cnt->isOpen = false;
            EnsureEvidenceCapacity(a);
            SetContainerEvidence(a, true, EvidenceSource::ACTION_FAILURE);
            MarkDirectLocationEvidence(a, true, EvidenceSource::SENSE);
            return true;
       }
       if(t>=2) return false;
            GetBigObjectStatus(a);
             if(!IsKeepingGoing(task_index)) return 0;
    }
    else return true;
    }
    return false;
}

//give任务
bool RDFW::SolveTask_Give(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a]) || !human) return false;
        if (human != nullptr){
            if(human->location==UNKNOWN)
             {
        GetBigObjectStatus(human->id);
        if(!IsKeepingGoing(task_index)) return 0;
        }
            if(objects[a]->location==human->location && plate_id!=a && hold_id!=a) return true;
            else return SolveTask_PutOn(a, human->id);
            }
        else
            LOG_ERROR("There are not human in Scene");

    return false;
}


//putin任务
bool RDFW::SolveTask_Putin(unsigned int a, unsigned int b)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a]) ||
        !dynamic_pointer_cast<Container>(objects[b])) return false;
    if(HasRequestedTask("takeout", a, b))
    {
        if(Isinside(a,b) && (stage == 1 || IsInsideVerified(a))) return true;
               else
               {
            cout<<"there is takeout task,no need to do this!"<<endl;
            return false;
               }
    }
    if(Isinside(a,b) && (stage == 1 || IsInsideVerified(a))) return true;
    auto target_cont = ObjectPtrCast<Container>(objects[b]);
    if(stage==1)
    {
        //优化了一下规划，如果目标物体和容器在一起，先打开再picku
        if(objects[a]->location==objects[b]->location)
        {
        if (location != target_cont->location && !Move(target_cont->location)) return false;
        if (target_cont->isOpen != 1 && !Open(b)) return false;
        if (!HoldSmallObject(a)) return false;
        return PutIn(a,b);
        }
        else
        {
        if(!HoldSmallObject(a)) return false;
        if (location != target_cont->location && !Move(target_cont->location)) return false;
        if (target_cont->isOpen != 1)
        {
        if (!PutDown(a)) return false;
        if (!Open(b)) return false;
        if (!PickUp(a)) return false;
        }
        return PutIn(a,b);
        }
       return false;
    }
    //stage2的情况
  //open如果false可能的情况有两种：1.容器本身就是开着的，他骗我没开 2.b容器就不在这个位置
  //putin a b 如果false的情况有两种： 1.容器是关着的，骗我是开着的，我没有打开 2.b容器就不在这个位置上
    if(objects[b]->location==UNKNOWN)
    {
        GetBigObjectStatus(b);
        if(!IsKeepingGoing(task_index)) return 0;
    }
    if(!HoldSmallObject(a)) return false;

    int try_times=0;
    while(1)
    {
      try_times++;
    if (location != target_cont->location)
        if(!Move(target_cont->location))
        {
            if(try_times>=2) return false;
            GetBigObjectStatus(b);
            if(!IsKeepingGoing(task_index)) return false;
            continue;
        }

    if (!target_cont->isOpen)
    {
        if (!PutDown(a)) return false;
        if(Open(b))
        {
             if (!PickUp(a)) return false;
             return PutIn(a,b);
        }
        else
        {
            if(sense(b)) {
                target_cont->isOpen = true;
                EnsureEvidenceCapacity(b);
                SetContainerEvidence(b, true, EvidenceSource::ACTION_FAILURE);
                if (!PickUp(a)) return false;
                return PutIn(a,b);
            }
            else
            {
            if(try_times>=2) return false;
            if (!PickUp(a)) return false;
            GetBigObjectStatus(b);
            if(!IsKeepingGoing(task_index)) return false;
            continue;
            }
        }

    }
    else
    {
    if(!PutIn(a, b))
    {
        if (!PutDown(a)) return false;
       if(Open(b)){
           if (!PickUp(a)) return false;
           return PutIn(a,b);
       }
       else
       {
        if(try_times>=2) return false;
        if (!PickUp(a)) return false;
        GetBigObjectStatus(b);
        if(!IsKeepingGoing(task_index)) return false;
        continue;
        }
    //    if(sense(b))
    //    {
    //     PutDown(hold_id);
    //     Open(b);
    //     PickUp(a);
    //     return PutIn(a,b);
    //    }
    //    else
    //    {
    //      if(try_times>=2) return false;
    //      GetBigObjectStatus(b);
    //      if(!IsKeepingGoing(task_index)) return false;
    //      continue;
    //    }
    }
    else return true;
    }
    }
}


//takeout任务
bool RDFW::SolveTask_TakeOut(unsigned int a, unsigned int b)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a]) ||
        !dynamic_pointer_cast<Container>(objects[b])) return false;
    if(HasRequestedTask("putin", a, b)){
         if(Isinside(a,b)==0) return true;
        else {
            cout<<"there is putin task,no need to do this!"<<endl;
            return false;
        }
    }
    auto small = ObjectPtrCast<SmallObject>(objects[a]);


    auto target_cont = ObjectPtrCast<Container>(objects[b]);
    // Stage 2 location facts and AskLoc replies may be misleading. A known
    // "at" reply therefore does not prove that the object is outside this
    // container; verify by trying the target container instead. Stage 1 has
    // reliable state and can keep the zero-cost shortcut.
    if (stage == 1 && small->inside != target_cont->id && small->inside != UNKNOWN)
    {
         return true;
    }


    if(stage==1)
    {
        if (hold!= nullptr && !PutDown(hold->id)) return false;
        if (location != target_cont->location && !Move(target_cont->location)) return false;
        if (target_cont->isOpen != 1 && !Open(target_cont->id)) return false;
        return TakeOut(a, target_cont->id);
    }
   ///这是stage2的逻辑 //对于takeout任务，inside未知，location未知，先去容器尝试takeout；或者先询问小物体，再判断任务是否已经完成
    if(target_cont->location==UNKNOWN){GetBigObjectStatus(b);if(!IsKeepingGoing(task_index)) return false;}
    if (hold!= nullptr && !PutDown(hold->id)) return false;
    int t = 0;
    while(1)
    {
        t++;
    if (location != target_cont->location)
        if(!Move(target_cont->location))
        {
        if (t >= 2)  return 0;
        GetBigObjectStatus(b);
        if(!IsKeepingGoing(task_index)) return false;
        continue;
        }
    TakeOutResult result = TakeOutLogic(a,b);
    if(result == TakeOutResult::Success) return true;
    else if(result == TakeOutResult::NeedContainerLocation) {if (t >= 2)  return 0;GetBigObjectStatus(b);}
    else if(result == TakeOutResult::NeedObjectLocation) {
        if (t >= 2) return false;
        GetSmallObjectStatus(a);
        if (!Isinside(a,b) && IsInsideVerified(a)) return true;
    }
    else if(result == TakeOutResult::VerifyObjectRelation)
    {
    if (t >= 2)  return 0;
    GetSmallObjectStatus(a);
    if(Isinside(a,b))//a就在一开始容器里面
    {
     if(Open(b)) return TakeOut(a,b); //认为骗我是关的
     else  GetBigObjectStatus(b);//open失败，本身不可能是open的，直接问容器
    }
    else
    {
    return true; //小物体不在容器里了
    }
    }
    if(!IsKeepingGoing(task_index)) return false;continue;
    }
}


//puton任务
bool RDFW::SolveTask_PutOn(unsigned int a, unsigned int b)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    //10.27
    auto small=dynamic_pointer_cast<SmallObject>(objects[a]);
    if(small && small->inside==NONE && small->location==objects[b]->location &&
       plate_id!=a && hold_id!=a &&
       (stage == 1 || (IsInsideVerified(a) && IsLocationVerified(a) && IsLocationVerified(b))))
        return true;
    //
    if(objects[b]->location==UNKNOWN)
    {
        GetBigObjectStatus(b);
         if(!IsKeepingGoing(task_index)) return false;
    }
    if (!HoldSmallObject(a)) return false;
    int t=0;
    while(1)
    {
        t++;
        if (location != objects[b]->location)
        {
            if(!Move(objects[b]->location))
            {
            if (t >= 3)  return 0;
            GetBigObjectStatus(b);
            if(!IsKeepingGoing(task_index)) return false;
            continue;
            }
        }
        if (stage == 2 && (!IsLocationVerified(b) || objects[b]->location != location)) {
            SenseCurrentLocationOnly(true);
            if(!HasObjectAtLocation(location, b)) {
                if (t >= 3) return false;
                GetBigObjectStatus(b);
                if(!IsKeepingGoing(task_index)) return false;
                continue;
            }
        }
        return PutDown(a);
    }
    return true;
}



/*============== b)mustchooseone ==============*/

//必须要做一个任务
void RDFW::MustChooseOne(void){
    std::vector<CandidatePlan> candidates = EvaluateShadowCandidates(
        "must-choose-one", true, std::numeric_limits<std::size_t>::max());
    if (StopGate("must-choose-one", true, candidates)) return;
    if(stage==1){
        cout<<"stage=1"<<endl;
        int flag=0;
         for(int i=0;i<tasks.size();i++){
                   if(tasks[i].risk<tasks[flag].risk){
                    flag=i;
                   }
               }
               task_index=flag;
               CandidatePlan candidate = BuildCandidatePlan(task_index);
               if (!CanStartPlan(candidate, "must-choose-one")) return;
               if (!ShouldStartConstraintTrade(candidate, candidates, "must-choose-one"))
                   return;
               cout<<"Must Choose one:"<<tasks[flag].behave<<endl;
               BeginCandidateExecution(candidate, "legacy_must_choose_one_risk");
               const bool solved = SolveTask(tasks[task_index]);
               EndCandidateExecution(solved);
               if (solved)
                {
                    stringstream ss;
                    ss << tasks[task_index];
                    LOG(GREEN "Task done\n %s" RESET, ss.str().c_str());
                    SetSolvedTaskNum(solved_task_num + 1);
                    tasks[task_index].isEnable = false;
                    AfterSolveTask(tasks[task_index]);
                }
    }
    if(stage==2){
        cout<<"stage=2"<<endl;
        // —— 新增：若剩余启用的 goto ≥ 2，则不要在这里做选择（留给 Final-GOTO）
        size_t remain_goto = 0;
        for (auto &t : tasks) if (t.isEnable && t.behave == "goto") ++remain_goto;
        if (remain_goto >= 2) {
            LOG(YELLOW "[Defer] MustChooseOne returns early due to multi-goto=%zu\n" RESET, remain_goto);
            return;
        }
          int t=0;
        while(solved_task_num==0){
        int flag=0;

        t++;
        candidates = EvaluateShadowCandidates(
            "must-choose-one", true, std::numeric_limits<std::size_t>::max());
        if (StopGate("must-choose-one", true, candidates)) return;
        for(int i=0;i<tasks.size();i++){
            // 跳过包含不存在物体的任务
            if (!tasks[i].IsUsable()) {
                continue;
            }

                   if((tasks[i].ask_times==0?(double)tasks[i].risk/2:(tasks[i].is_cheat?1000:tasks[i].risk))
                        <(tasks[flag].ask_times==0?(double)tasks[flag].risk/2:(tasks[flag].is_cheat?1000:tasks[flag].risk))){
                    flag=i;
                   }
               }
                task_index=flag;
               CandidatePlan candidate = BuildCandidatePlan(task_index);
               if (!CanStartPlan(candidate, "must-choose-one")) return;
               if (!ShouldStartConstraintTrade(candidate, candidates, "must-choose-one"))
                   return;
               cout<<"Must Choose one:"<<tasks[flag].behave<<endl;
               if(t>3){
                BeginCandidateExecution(candidate, "legacy_must_choose_one_risk");
                const bool solved = SolveTask(tasks[flag]);
                EndCandidateExecution(solved);
                break;
               }
               if(tasks[flag].risk>=4){   //怀疑是否有陷阱
             if(tasks[flag].behave!="open"||tasks[flag].behave!="close") {
                 if (!tasks[flag].X.empty()) {
                     GetSmallObjectStatus(tasks[flag].X[0]->id);
                 }
             }
                if(IsKeepingGoing(flag)!=1) continue;
               }
               if(tasks[flag].isfalse) {
                if (!tasks[flag].X.empty()) {
                    GetSmallObjectStatus(tasks[flag].X[0]->id);
                }
                if(IsKeepingGoing(flag)!=1) continue;
               }

            // ===【新增】零动作预验证：先试；失败(含 fake)立刻回落到 SolveTask ===
            bool zero_ok = ZeroActionPreCheck(tasks[task_index]);
            if (zero_ok) {
                BeginCandidateExecution(candidate, "legacy_must_choose_one_risk");
                std::stringstream ss; ss << tasks[task_index];
                LOG(GREEN "Task done (zero-action prevalidated)\n %s" RESET, ss.str().c_str());
                SetSolvedTaskNum(solved_task_num + 1);
                tasks[task_index].isEnable = false;
                AfterSolveTask(tasks[task_index]);
                EndCandidateExecution(true);
                break;
            }

            candidate = BuildCandidatePlan(task_index);
            if (!CanStartPlan(candidate, "must-choose-one-post-check")) return;
            candidates = EvaluateShadowCandidates(
                "must-choose-one-post-check", true, task_index);
            if (!ShouldStartConstraintTrade(candidate, candidates,
                                            "must-choose-one-post-check")) return;
            BeginCandidateExecution(candidate, "legacy_must_choose_one_risk");
            const bool solved = SolveTask(tasks[task_index]);
            EndCandidateExecution(solved);
            if (solved)
                {
                    stringstream ss;
                    ss << tasks[task_index];
                    LOG(GREEN "Task done\n %s" RESET, ss.str().c_str());
                    SetSolvedTaskNum(solved_task_num + 1);
                    tasks[task_index].isEnable = false;
                    AfterSolveTask(tasks[task_index]);
                    break;
                }
        }
    }
}




/**============== 附：goto任务处理============== */

//检查是否有多goto任务
bool RDFW::CheckAndDeferMultiGoto()
{
    size_t goto_count_enabled = 0;
    for (auto &t : tasks) if (t.behave == "goto") ++goto_count_enabled;
    const bool defer_multi_goto = (goto_count_enabled >= 2);
    if (defer_multi_goto)
        LOG(YELLOW "[Defer] multi-goto detected: %zu tasks; skip in main loop, handle at final aggregation\n" RESET, goto_count_enabled);
    return defer_multi_goto;
}

//执行多goto任务
void RDFW::ExecuteMultiGotoAggregation()
{
    // =========================
    // [FINAL] Multi-GOTO 聚合（低风险 hub + 候选筛选 + 上限 10 + 最终停留）
    // =========================

    const std::vector<CandidatePlan> candidates = EvaluateShadowCandidates(
        "multi-goto", true, std::numeric_limits<std::size_t>::max());
    if (StopGate("multi-goto", true, candidates)) return;

    // 确保多goto模式标志已设置，跳过Sense操作
    isMultiGotoMode = true;
    LOG(GREEN "[MultiGoto] Executing multi-goto aggregation - skipping Sense operations\n" RESET);

    // 1) 收集启用中的 goto 任务对应的小物体
    // 分类收集 goto 任务中的大物体和小物体
    std::vector<unsigned> small_cand_ids;
    std::vector<unsigned> big_cand_ids;

    std::vector<unsigned> small_cand_ids_disable;
    small_cand_ids.reserve(32);
    big_cand_ids.reserve(32);
    for (auto &t : tasks) {
        if(t.behave == "goto" && t.IsUsable()){
            if(t.isEnable){
                for (auto &x : t.X) {
                    if (!x || !IsValidObjectId(static_cast<int>(x->id))) continue;
                    if (std::dynamic_pointer_cast<SmallObject>(x))
                        small_cand_ids.push_back(x->id);
                    else if (std::dynamic_pointer_cast<BigObject>(x))
                        big_cand_ids.push_back(x->id);}
                }else{
                    for (auto &x : t.X) {
                        if (!x || !IsValidObjectId(static_cast<int>(x->id))) continue;
                        if (std::dynamic_pointer_cast<SmallObject>(x))
                            small_cand_ids_disable.push_back(x->id);
                    }
                }
            }
        }


    // 删掉small_cand_ids_disable中goto约束大于2的小物体
    small_cand_ids_disable.erase(
        std::remove_if(
            small_cand_ids_disable.begin(),
            small_cand_ids_disable.end(),
            [&](unsigned id) {
                if (!IsValidObjectId(static_cast<int>(id))) return true;
                int loc = (objects[id] ? objects[id]->location : UNKNOWN);
                int risk = (loc >= 0 && loc < static_cast<int>(goto_cons.size()))
                    ? goto_cons[loc] : 99;
                return risk > 2;
            }
        ),
        small_cand_ids_disable.end()
    );


    /*不需要去重，不能有重复的任务---比赛规则
    // 去重
    std::sort(small_cand_ids.begin(), small_cand_ids.end());
    small_cand_ids.erase(std::unique(small_cand_ids.begin(), small_cand_ids.end()), small_cand_ids.end());
    std::sort(big_cand_ids.begin(), big_cand_ids.end());
    big_cand_ids.erase(std::unique(big_cand_ids.begin(), big_cand_ids.end()), big_cand_ids.end());
    // 计算每个小物体：去到其位置 + 拿起小物体 + 移动小物体违反的约束值（不考虑能否移动到 hub）
    // 如果小物体有 near 约束（即 near_cons[id] > 0），则视为不能移动，风险值设为极大
    */


    // 得到约束值小于2的小物体
    struct SmallGotoPickupInfo {
        unsigned id;
        int goto_risk = 0;      // 去到小物体位置的约束
        int pickup_risk = 0;    // 拿起小物体的约束
        int mustnear_risk = 0;  // mustnear 约束
        int total_risk = 0;     // 总约束
        bool can_move = true;   // 是否能移动
    };
    std::vector<SmallGotoPickupInfo> small_goto_infos;
    std::vector<unsigned> small_lowrisk_ids; // 约束值小于2的小物体id
    // 小物体：计算 goto/pickup/mustnear
    for (auto id : small_cand_ids) {
        if (!IsValidObjectId(static_cast<int>(id))) continue;
        SmallGotoPickupInfo info;
        info.id = id;
        // 2. 去到小物体当前位置
        int loc = (objects[id] ? objects[id]->location : UNKNOWN);
        info.goto_risk = (loc >= 0 && loc < static_cast<int>(goto_cons.size()))
            ? goto_cons[loc] : 99;
        // 3. 拿起小物体的约束
        info.pickup_risk = pickup_cons[id];
        // 4. mustnear 约束（第二维同样是对象 id，不是位置）
        int mustnear_sum = 0;
        for (int i = 0; i < (int)mustnear_cons[id].size(); ++i) mustnear_sum += mustnear_cons[id][i];
        info.mustnear_risk = mustnear_sum;
        // 5. 总约束
        info.total_risk = info.goto_risk + info.pickup_risk + info.mustnear_risk;
        info.can_move = (info.total_risk < 2);
        LOG(GREEN "[MultiGoto] Object[%u]: goto_risk=%d, pickup_risk=%d, mustnear_risk=%d, total_risk=%d, can_move=%s\n" RESET,
            id, info.goto_risk, info.pickup_risk, info.mustnear_risk, info.total_risk, info.can_move ? "true" : "false");
        if (info.total_risk < 3) {
            small_lowrisk_ids.push_back(id);
        }
        small_goto_infos.push_back(info);
    }
    // 大物体：仅统计 goto 约束值
    struct BigGotoInfo {
        unsigned id;
        int goto_risk = 0;
    };
    std::vector<BigGotoInfo> big_goto_infos;
    for (auto id : big_cand_ids) {
        if (!IsValidObjectId(static_cast<int>(id))) continue;
        BigGotoInfo info;
        info.id = id;
        int loc = (objects[id] ? objects[id]->location : UNKNOWN);
        info.goto_risk = (loc >= 0 && loc < static_cast<int>(goto_cons.size()))
            ? goto_cons[loc] : 99;
        LOG(GREEN "[MultiGoto] BigObject[%u]: goto_risk=%d\n" RESET, id, info.goto_risk);
        big_goto_infos.push_back(info);
    }


    LOG(GREEN "[MultiGoto] small_cand_id_disable: ");
    for(auto id : small_cand_ids_disable){
        LOG(GREEN "%u ", id);
    }
    LOG(GREEN "\n" RESET);
    LOG(GREEN "[MultiGoto] small_cand_ids: ");
    for(auto id : small_cand_ids){
        LOG(GREEN "%u ", id);
    }
    LOG(GREEN "\n" RESET);
    LOG(GREEN "[MultiGoto] big_cand_ids: ");
    for(auto id : big_cand_ids){
        LOG(GREEN "%u ", id);
    }
    LOG(GREEN "\n" RESET);
    LOG(GREEN "[MultiGoto] small_cand_ids.size()=%zu, big_cand_ids.size()=%zu\n" RESET,
        small_cand_ids.size(), big_cand_ids.size());
    LOG(GREEN "[MultiGoto] small_lowrisk_ids.size()=%zu\n" RESET, small_lowrisk_ids.size());

    if(small_lowrisk_ids.size()>0 || big_cand_ids.size()>0){
        LOG(GREEN "[MultiGoto] Entering main aggregation logic\n" RESET);

    // 只收集有物体的位置，避免遍历所有空位置
    std::set<int> occupied_locations;

    // 收集所有小物体和大物体的位置
    for (auto id : small_lowrisk_ids) {
        if (id < objects.size() && objects[id]) {
            int loc = objects[id]->location;
            if (loc >= 0 && loc < (int)rightlocation.size()) {
                occupied_locations.insert(loc);
            }
        }
    }
    for (auto id : big_cand_ids) {
        if (id < objects.size() && objects[id]) {
            int loc = objects[id]->location;
            if (loc >= 0 && loc < (int)rightlocation.size()) {
                occupied_locations.insert(loc);
            }
        }
    }
    for (auto id : small_cand_ids_disable) {
        if (id < objects.size() && objects[id]) {
            int loc = objects[id]->location;
            if (loc >= 0 && loc < (int)rightlocation.size()) {
                occupied_locations.insert(loc);
            }
        }
    }

    // 转换为vector，只包含有物体的位置
    std::vector<int> valid_locations(occupied_locations.begin(), occupied_locations.end());

    LOG(GREEN "[MultiGoto] Performance optimization: Only checking %zu occupied locations instead of %zu total locations\n" RESET,
        valid_locations.size(), rightlocation.size());

    // 记录每个位置的约束统计
    struct LocationRiskInfo {
        int loc;
        int goto_risk;
        std::vector<int> move_cons_risks;     // 对每个小物体
        std::vector<int> putdown_cons_risks;  // 对每个小物体
        int total_risk;
    };
    std::vector<LocationRiskInfo> location_risks;

    for (int loc : valid_locations) {
        LocationRiskInfo info;
        info.loc = loc;
        info.goto_risk = goto_cons[loc];
        info.move_cons_risks.reserve(small_lowrisk_ids.size());
        info.putdown_cons_risks.reserve(small_lowrisk_ids.size());
        int sum_risk = info.goto_risk;
        for (auto id : small_lowrisk_ids) {
            int move_risk = move_cons[id][loc];
            int putdown_risk = putdown_cons[id][loc];
            info.move_cons_risks.push_back(move_risk);
            info.putdown_cons_risks.push_back(putdown_risk);
            sum_risk += move_risk + putdown_risk;
        }
        info.total_risk = sum_risk;
        location_risks.push_back(info);
    }

    // 1. 选出约束值小于3且可达的位置
    std::vector<int> low_risk_locs;
    int min_risk = 1000000;
    for (const auto& info : location_risks) {
        // 检查位置是否可达（在rightlocation中且不是UNKNOWN）
        if (info.loc >= 0 && info.loc < (int)rightlocation.size() && rightlocation[info.loc]) {
            if (info.total_risk < 3) {
                low_risk_locs.push_back(info.loc);
            }
            if (info.total_risk < min_risk) {
                min_risk = info.total_risk;
            }
        }
    }

    // 统计选出来的小物体在每个位置的数量（只统计有物体的位置）
    std::map<int, int> location_smallobjloc_counts;
    // 合并两个vector
    std::vector<unsigned int> combined_ids = small_lowrisk_ids;
    combined_ids.insert(combined_ids.end(), small_cand_ids_disable.begin(), small_cand_ids_disable.end());

    for (auto id : combined_ids) {
        if (id < objects.size() && objects[id]) {
            int loc = objects[id]->location;
            if (loc >= 0 && loc < (int)rightlocation.size()) {
                location_smallobjloc_counts[loc]++;
            }
        }
    }

    int chosen_loc = -1;

    // 优先选择大物体中约束值最小且小于2的位置，若没有，选择约束值最小的位置
    int best_big_risk = 1000000;
    int best_big_loc = -1;

    // 直接使用小物体聚集数量大于2且数量最多的位置作为 chosen_loc

    int most_smallobj_loc = -1;
    int most_smallobj_count = 0;


    for (const auto& pair : location_smallobjloc_counts) {
        int loc = pair.first;
        int count = pair.second;
        if (count >= 2) {
            cout << "[MultiGoto] location " << loc << " has " << count << " small objects" << endl;
            // 找到对应的location_risks信息
            int total_risk = 99; // 默认高风险
            for (const auto& risk_info : location_risks) {
                if (risk_info.loc == loc) {
                    total_risk = risk_info.total_risk;
                    break;
                }
            }
            if (count > most_smallobj_count && (count - total_risk) > 2) {
                // 检查位置可达
                if (loc >= 0 && loc < (int)rightlocation.size()) {
                    most_smallobj_count = count;
                    most_smallobj_loc = loc;
                    cout << "[MultiGoto] most_smallobj_loc " << most_smallobj_loc << endl;
                }
            }
        }
    }


    if (most_smallobj_loc != -1) {
        chosen_loc = most_smallobj_loc;
        rightlocation[chosen_loc] = true;
        LOG(YELLOW "[MultiGoto] smallobj_loc is found: %d\n" RESET, most_smallobj_loc);
    }
    else{
        LOG(YELLOW "[MultiGoto] smallobj_loc is not found: %d\n" RESET, most_smallobj_loc);
    }


    // 1. 在 big_cand_ids 中找约束值最小且小于2的位置

    if(chosen_loc == -1){
    for (auto big_id : big_cand_ids) {
        if (IsValidObjectId(static_cast<int>(big_id))) {
            int loc = objects[big_id]->location;
            // 确保位置可达
            if (loc >= 0 && loc < (int)rightlocation.size() && rightlocation[loc]) {
                for (const auto& info : location_risks) {
                    if (info.loc == loc && info.total_risk < 2) {
                        if (info.total_risk < best_big_risk) {
                            best_big_risk = info.total_risk;
                            best_big_loc = loc;}
                            }
                        }
                    }
                }
            }
        }


    if (best_big_loc != -1 ) {
        chosen_loc = best_big_loc;
    }
    else if(chosen_loc == -1){
        LOG(YELLOW "[MultiGoto] bigobj_loc is not found: %d\n" RESET, best_big_loc);
        // 没有满足条件的大物体位置，选择所有可达位置中约束值最小的位置
        int best_risk = 1000000;
        for (const auto& info : location_risks) {
            // 确保位置可达
            if (info.loc >= 0 && info.loc < (int)rightlocation.size() && rightlocation[info.loc]) {
                if (info.total_risk < best_risk) {
                    best_risk = info.total_risk;
                    chosen_loc = info.loc;
                }
            }
        }
        LOG(YELLOW "[MultiGoto] No optimal location found, using first available: %d\n" RESET, chosen_loc);
    }

    // 如果仍然没有选择到位置，选择第一个可达的位置
    if (chosen_loc == -1) {
        for (int loc = 0; loc < (int)rightlocation.size(); ++loc) {
            if (rightlocation[loc]) {
                chosen_loc = loc;
                LOG(YELLOW "[MultiGoto] No optimal location found, using first available: %d\n" RESET, loc);
                break;
            }
        }
    }


    // 验证选择的位置是否可达
    if (chosen_loc == -1 || !rightlocation[chosen_loc]) {
        LOG(RED "[MultiGoto] ERROR: No valid location found! chosen_loc=%d, rightlocation[%d]=%d\n" RESET,
            chosen_loc, chosen_loc, chosen_loc >= 0 && chosen_loc < (int)rightlocation.size() ? rightlocation[chosen_loc] : -1);
        // 选择机器人当前位置作为备选
        chosen_loc = location;
        LOG(YELLOW "[MultiGoto] Using current location as fallback: %d\n" RESET, chosen_loc);
    }

    LOG(GREEN "Final-GOTO: choose hub=%d\n" RESET,chosen_loc);

        // chosen_loc is a location, while SolveTask_PutOn expects an object id
        // whose location is the destination.  Keep the legacy hub selection
        // unchanged and map that location to a stable representative object.
        unsigned int chosen_target_id = NONE;
        for (std::size_t i = 1; i < objects.size(); ++i) {
            if (!objects[i] || objects[i]->location != chosen_loc) continue;
            if (chosen_target_id == NONE) chosen_target_id = static_cast<unsigned int>(i);
            if (std::dynamic_pointer_cast<BigObject>(objects[i])) {
                chosen_target_id = static_cast<unsigned int>(i);
                break;
            }
        }
        if (chosen_target_id == NONE && chosen_loc > 0 &&
            static_cast<std::size_t>(chosen_loc) < objects.size() && objects[chosen_loc])
            chosen_target_id = static_cast<unsigned int>(chosen_loc);
        LOG(GREEN "[MultiGoto] hub location=%d representative_object=%u\n" RESET,
            chosen_loc, chosen_target_id);

        // 4) 两层过滤得到最终搬运集合 move_set
        std::vector<unsigned> move_set;
        move_set.reserve(small_lowrisk_ids.size());

        // 使用 small_lowrisk_ids 作为搬运集合
        for (auto id : small_lowrisk_ids) {
            // 重新定义lambda函数，因为作用域问题
            auto ViolationsIfGoto = [&](unsigned id) -> int {
                if (id >= objects.size() || !objects[id]) return 99;
                int loc = objects[id]->location;
                if (loc < 0 || loc >= static_cast<int>(goto_cons.size())) return 99;
                return goto_cons[loc];
            };
            auto SafeForIdAt = [&](unsigned id, int h) -> bool {
                if (!IsValidObjectId(static_cast<int>(id)) || h < 0 ||
                    id >= putdown_cons.size() || id >= move_cons.size() ||
                    static_cast<std::size_t>(h) >= putdown_cons[id].size() ||
                    static_cast<std::size_t>(h) >= move_cons[id].size()) return false;
                return putdown_cons[id][h] == 0 && move_cons[id][h] == 0;
            };
            if (ViolationsIfGoto(id) < 2 && SafeForIdAt(id, chosen_loc)) {
                move_set.push_back(id);
            }
        }

        // 没有可搬的，就至少停在 chosen_loc
        if (move_set.empty()) {
            if (location != chosen_loc) {
                const CandidatePlan final_move = PreviewFinalMove(chosen_loc);
                if (final_move.marginal_score > 0 &&
                    CanStartPlan(final_move, "multi-goto-final-move")) {
                    BeginCandidateExecution(final_move, "multi_goto_hub_final_move");
                    const bool moved_to_hub = Move(chosen_loc);
                    EndCandidateExecution(moved_to_hub);
                }
            }
        } else {
            auto in_set = [&](unsigned x){
                return std::find(move_set.begin(), move_set.end(), x) != move_set.end();
            };

            // 护栏：hold/plate 不在集合且跨区会触发 move 约束时，先就地处理
            if (IsValidObjectId(hold_id) && !in_set(hold_id) &&
                chosen_loc >= 0 && hold_id < static_cast<int>(move_cons.size()) &&
                static_cast<std::size_t>(chosen_loc) < move_cons[hold_id].size() &&
                move_cons[hold_id][chosen_loc]) {
                PutDown(hold_id);
            }
            if (IsValidObjectId(plate_id) && !in_set(plate_id) &&
                chosen_loc >= 0 && plate_id < static_cast<int>(move_cons.size()) &&
                static_cast<std::size_t>(chosen_loc) < move_cons[plate_id].size() &&
                move_cons[plate_id][chosen_loc]) {
                if (hold_id > 0) PutDown(hold_id);
                FromPlate(plate_id);
                PutDown(plate_id);
            }

            // 若 hold/plate 在集合里，优先处理
            auto promote_front = [&](unsigned x){
                auto it = std::find(move_set.begin(), move_set.end(), x);
                if (it != move_set.end()) std::rotate(move_set.begin(), it, it + 1);
            };
            if (hold_id  > 0) promote_front(hold_id);
            if (plate_id > 0) promote_front(plate_id);

            // 已在 chosen_loc 的优先，其余按 id 升序
            std::stable_sort(move_set.begin(), move_set.end(), [&](unsigned a, unsigned b){
                auto sa = std::dynamic_pointer_cast<SmallObject>(objects[a]);
                auto sb = std::dynamic_pointer_cast<SmallObject>(objects[b]);
                int da = (sa && sa->location == chosen_loc) ? 0 : 1;
                int db = (sb && sb->location == chosen_loc) ? 0 : 1;
                return (da != db) ? (da < db) : (a < b);
            });

            // 实际搬运 ≤ 10 件
            const int MULTI_GOTO_LIMIT = 500;
            int moved = 0;
            for (unsigned id : move_set) {
                if (moved >= MULTI_GOTO_LIMIT) break;
                CandidatePlan relocation = BuildSyntheticPutOnCandidate(
                    id, chosen_target_id);
                LOG(CYAN_BLUE "[3B][Candidate] phase=multi-goto-relocation "
                              "task_index=%zu behave=%s actions=%zu "
                              "duration_ms=%lld action_cost=%d marginal_score=%d "
                              "utility=%d gained_goals=%s lost_goals=%s "
                              "broken_constraints=%s preserved_constraints=%s\n" RESET,
                    relocation.task_index, relocation.task_label.c_str(),
                    relocation.actions.size(),
                    static_cast<long long>(relocation.estimated_duration.count()),
                    relocation.action_cost, relocation.marginal_score,
                    relocation.utility,
                    IndexListString(relocation.gained_goals).c_str(),
                    IndexListString(relocation.lost_goals).c_str(),
                    IndexListString(relocation.broken_constraints).c_str(),
                    IndexListString(relocation.preserved_constraints).c_str());
                if (!CanStartPlan(relocation, "multi-goto-relocation")) break;
                // 直接调用 SolveTask_PutOn ，把小物体移动到 hub
                BeginCandidateExecution(relocation, "multi_goto_hub_relocation");
                const bool relocation_succeeded =
                    chosen_target_id != NONE && SolveTask_PutOn(id, chosen_target_id);
                EndCandidateExecution(relocation_succeeded);
                if (relocation_succeeded) {
                    ++moved;
                    LOG(GREEN "Final-GOTO: moved obj[%u] to hub=%d (%d/%d) (via SolveTask_PutOn)\n" RESET,
                        id, chosen_loc, moved, MULTI_GOTO_LIMIT);
                }
            }

            // 收尾：最终停在 chosen_loc
            if (location != chosen_loc) {
                const CandidatePlan final_move = PreviewFinalMove(chosen_loc);
                if (final_move.marginal_score > 0 &&
                    CanStartPlan(final_move, "multi-goto-final-move")) {
                    BeginCandidateExecution(final_move, "multi_goto_hub_final_move");
                    const bool moved_to_hub = Move(chosen_loc);
                    EndCandidateExecution(moved_to_hub);
                }
            }
            LOG(GREEN "Final-GOTO: aggregation done, stay at hub=%d\n" RESET, chosen_loc);

            // 关闭剩余 goto，避免后续检查阶段拉走
            for (auto &t : tasks) {
                if (t.isEnable && t.behave == "goto") t.isEnable = false;
            }
        }
    } else {
        LOG(YELLOW "[MultiGoto] Skipping aggregation: small_lowrisk_ids.size()=%zu, big_cand_ids.size()=%zu\n" RESET,
            small_lowrisk_ids.size(), big_cand_ids.size());
    }
    // =========================
    // [FINAL] Multi-GOTO 聚合结束
    // =========================
    LOG(GREEN "[MultiGoto] ExecuteMultiGotoAggregation finished\n" RESET);
}



// === Zero-Action Precheck helpers (stage2 only) ===

// 判定当前知识下，该任务是否“无需执行器动作即可满足”
bool RDFW::IsZeroActionSatisfy(const Instruction& t) const
{
    return stage == 2 &&
           terminal_checker.evaluateTask(*this, t) == TerminalStatus::SATISFIED;
}

// 零动作检查必须是纯检查：不得 Ask、Sense、Move 或改变场景。
bool RDFW::ZeroActionPreCheck(Instruction& t)
{
    if (stage != 2) return false;
    if (IsZeroActionSatisfy(t)) {
        stringstream ss; ss << t;
        LOG(GREEN "[Zero-Action] validated as done\n %s" RESET, ss.str().c_str());
        return true;
    }
    return false;
}
// === Zero-Action Precheck helpers (stage2 only) ===




/**============== b)状态判断函数============== */
//小物体、大物体、询问、Sense、inside判断、IsKeepingGoing、IsObjectSatisfy、IsInstructionInvoke、SearchConditionObject


namespace {
bool ParseAskLocationReply(const string& reply, unsigned int expected_id,
                           string& relation, unsigned int& target)
{
    static const regex pattern(
        "^\\s*(at|inside)\\s*\\(\\s*([0-9]+)\\s*,\\s*([0-9]+)\\s*\\)\\s*$");
    smatch match;
    if (!regex_match(reply, match, pattern) || match.size() != 4) return false;
    int reply_id = 0;
    int reply_target = 0;
    if (!ParseBoundedInt(match[2].str(), 1,
                         static_cast<int>(MAX_OBJECT_ID), reply_id) ||
        !ParseBoundedInt(match[3].str(), 0, MAX_LOCATION_ID, reply_target) ||
        static_cast<unsigned int>(reply_id) != expected_id) return false;
    relation = match[1].str();
    if (relation == "inside" && reply_target > static_cast<int>(MAX_OBJECT_ID))
        return false;
    target = static_cast<unsigned int>(reply_target);
    return true;
}
}

// 获取小物体状态。AskLoc 只用于提出待验证的位置假设，因此遇到第一个
// 格式正确的回答即可继续；最多额外重试一次 not_known/非法回答。
bool RDFW::GetSmallObjectStatus(unsigned int a)
{
    if (isPass || a == 0 || a >= objects.size() || !objects[a]) return false;
    auto small = dynamic_pointer_cast<SmallObject>(objects[a]);
    if (!small) return false;

    const int max_attempts = 2;
    string chosen_relation;
    unsigned int chosen_target = 0;
    bool chosen = false;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const string reply = AskLoc(a);
        string relation;
        unsigned int target = 0;
        if (!ParseAskLocationReply(reply, a, relation, target)) continue;
        if (relation == "inside") {
            if (target == 0 || target >= objects.size() || !objects[target] ||
                !dynamic_pointer_cast<Container>(objects[target])) continue;
        } else if (target < posSensedFlag.size() && posSensedFlag[target] &&
                   !HasObjectAtLocation(static_cast<int>(target), a)) {
            continue;
        }
        chosen_relation = relation;
        chosen_target = target;
        chosen = true;
        break;
    }
    if (!chosen) {
        LOG(YELLOW "AskLoc(%u) produced no usable bounded reply\n" RESET, a);
        return false;
    }

    if (IsInsideVerified(a)) {
        const int answer_inside = chosen_relation == "inside"
            ? static_cast<int>(chosen_target) : NONE;
        if (small->inside != answer_inside) {
            LOG(YELLOW "AskLoc(%u) conflicts with trusted inside fact; ignored\n" RESET, a);
            return false;
        }
    }
    if (IsLocationVerified(a)) {
        const int answer_location = chosen_relation == "inside"
            ? objects[chosen_target]->location : static_cast<int>(chosen_target);
        if (objects[a]->location != answer_location) {
            LOG(YELLOW "AskLoc(%u) conflicts with trusted location; ignored\n" RESET, a);
            return false;
        }
    }
    if (IsInsideVerified(a) && IsLocationVerified(a)) return true;

    if (small->inside > 0 && static_cast<size_t>(small->inside) < objects.size()) {
        auto old_cont = dynamic_pointer_cast<Container>(objects[small->inside]);
        if (old_cont) old_cont->DeleteObjectInside(small);
    }

    if (chosen_relation == "inside") {
        auto cont = dynamic_pointer_cast<Container>(objects[chosen_target]);
        if (!cont) return false;
        if (cont->location == UNKNOWN) GetBigObjectStatus(cont->id);
        small->location = cont->location;
        small->inside = cont->id;
        bool already_listed = false;
        for (const auto& item : cont->smallObjectsInside) {
            if (item && item->id == small->id) { already_listed = true; break; }
        }
        if (!already_listed) cont->smallObjectsInside.push_back(small);
    } else {
        if (!EnsureLocationCapacity(static_cast<int>(chosen_target))) return false;
        small->location = static_cast<int>(chosen_target);
        small->inside = NONE;
    }

    if (!IsLocationVerified(a))
        MarkDirectLocationEvidence(a, false, EvidenceSource::ASK_ANSWER);
    if (!IsInsideVerified(a))
        SetInsideEvidence(a, false, EvidenceSource::ASK_ANSWER);
    if (small->location >= 0) {
        if (!EnsureLocationCapacity(small->location)) return false;
        posCorrectFlag[small->location] = false;
    }
    RefreshMustNearConstraintState(false);
    ++world_revision; // accepted AskLoc answer changes a weak hypothesis
    return true;
}

// 获取大物体状态。只接受格式正确且对象 ID 匹配的 at 回答。
bool RDFW::GetBigObjectStatus(unsigned int a)
{
    if (isPass || a == 0 || a >= objects.size() || !objects[a]) return false;
    const int max_attempts = 2;
    unsigned int chosen_location = 0;
    bool chosen = false;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const string reply = AskLoc(a);
        string relation;
        unsigned int target = 0;
        if (!ParseAskLocationReply(reply, a, relation, target) || relation != "at") continue;
        if (target < posSensedFlag.size() && posSensedFlag[target] &&
            !HasObjectAtLocation(static_cast<int>(target), a)) continue;
        chosen_location = target;
        chosen = true;
        break;
    }
    if (!chosen) {
        LOG(YELLOW "AskLoc(%u) produced no usable bounded location reply\n" RESET, a);
        return false;
    }
    if (IsLocationVerified(a)) {
        if (objects[a]->location != static_cast<int>(chosen_location)) {
            LOG(YELLOW "AskLoc(%u) conflicts with trusted location; ignored\n" RESET, a);
            return false;
        }
        return true;
    }

    if (!EnsureLocationCapacity(static_cast<int>(chosen_location))) return false;
    objects[a]->location = static_cast<int>(chosen_location);
    if (auto cont = dynamic_pointer_cast<Container>(objects[a])) {
        for (const auto& item : cont->smallObjectsInside) {
            if (item) {
                // A human answer about the container cannot replace a direct
                // observation or successful action on an individual item.
                if (IsLocationVerified(item->id)) continue;
                item->location = cont->location;
                MarkDirectLocationEvidence(item->id, false, EvidenceSource::ASK_ANSWER);
            }
        }
    }
    MarkDirectLocationEvidence(a, false, EvidenceSource::ASK_ANSWER);
    posCorrectFlag[chosen_location] = false;
    RefreshMustNearConstraintState(false);
    ++world_revision; // accepted AskLoc answer changes a weak hypothesis
    return true;
}

// 单次询问。重试与一致性判断由调用方控制，保证每条调用链都有明确上限。
std::string RDFW::AskLoc(unsigned int a)
{
    if (isPass || a == 0 || a >= objects.size() || !objects[a]) return "";
    RecordAction(ActionCategory::HUMAN_INTERACTION, "AskLoc",
                 std::vector<unsigned int>{a});
    string str;
    if (shadow_dry_run) {
        const std::shared_ptr<SmallObject> small =
            std::dynamic_pointer_cast<SmallObject>(objects[a]);
        if (small && small->inside > 0)
            str = "inside(" + std::to_string(a) + "," +
                  std::to_string(small->inside) + ")";
        else if (objects[a]->location >= 0)
            str = "at(" + std::to_string(a) + "," +
                  std::to_string(objects[a]->location) + ")";
    } else {
        str = Plug::AskLoc(a);
    }
    RecordActionOutcome("AskLoc", !str.empty());
    if (task_index >= 0 && static_cast<size_t>(task_index) < tasks.size())
        tasks[task_index].ask_times++;
    LOG("AskLoc(%d)", a);
    if (str.empty() && !shadow_dry_run)
        LOG_ERROR("AskLoc returned empty string for object (%d,%s)", a, objects[a]->sort.c_str());
    return str;
}

//感知位置---只能判断是否有这个物体，容器中的物体感知不到
void RDFW::Sense()
{
    if(stage==1 || isPass) return;
    // Sense 只观察当前位置，不应把 location 当作对象 ID，也不应隐式开关门。
    SenseCurrentLocationOnly(true);
}


// 只感知当前位置的物体，并更新其感知位置
void RDFW::SenseCurrentLocationOnly(bool force)
{
    unsigned int sensed_container_id = NONE;
    // 获取当前位置
    int curr_loc = location;
    if (curr_loc < 0 || !EnsureLocationCapacity(curr_loc)) return;

    // 检查位置是否已经感知过，避免重复感知
    if (curr_loc >= posSensedFlag.size()) {
        posSensedFlag.resize(curr_loc + 1, false);
        locationSensedObjects.resize(curr_loc + 1);  // 同时扩展物体记录数组
    }

    if (!force && posSensedFlag[curr_loc]) {
        LOG(YELLOW "[SenseCurrentLocationOnly] Location %d already sensed, skipping to avoid redundancy\n" RESET, curr_loc);
        return;
    }

    // 感知当前位置的物体
    vector<unsigned int> sensed_ids;
    RecordAction(ActionCategory::OBSERVATION, "Sense",
                 std::vector<unsigned int>());
    if (shadow_dry_run) DryRunSenseIds(sensed_ids);
    else Plug::Sense(sensed_ids);

    // 标记当前位置已感知
    if (curr_loc >= posSensedFlag.size()) {
        posSensedFlag.resize(curr_loc + 1, false);
        locationSensedObjects.resize(curr_loc + 1);  // 同时扩展物体记录数组
    }
    posSensedFlag[curr_loc] = true;

    // 清空当前位置的感知记录，准备记录新的感知结果
    locationSensedObjects[curr_loc].object_ids.clear();
    locationSensedObjects[curr_loc].container_id = 0;
    locationSensedObjects[curr_loc].has_container = false;

    LOG(GREEN "[SenseCurrentLocationOnly] Location %d marked as sensed\n" RESET, curr_loc);

    // 遍历感知到的物体，更新其位置
    for (auto id : sensed_ids) {
        if (id > 0 && id < objects.size() && objects[id] != nullptr) {
            objects[id]->location = curr_loc;
            MarkDirectLocationEvidence(id, true, EvidenceSource::SENSE);
            if (objects[id]->unable_site == curr_loc) objects[id]->unable_site = UNKNOWN;

            // 记录感知到的物体ID
            locationSensedObjects[curr_loc].object_ids.push_back(id);

            auto cont = std::dynamic_pointer_cast<Container>(objects[id]);
            if (cont) {
                // 记录容器ID和标记有容器（一个位置只能有一个大物体）
                locationSensedObjects[curr_loc].container_id = id;
                locationSensedObjects[curr_loc].has_container = true;
                sensed_container_id = id;
                LOG("[SenseCurrentLocationOnly] Container %d detected at location %d", id, curr_loc);
            } else {
                LOG("[SenseCurrentLocationOnly] Object %d detected at location %d", id, curr_loc);
            }
        }
    }

    // 让位置证据与 inside 关系保持一致。开放容器与其内容同时可见时，
    // 单次 Sense 无法区分“在容器内”和“在容器旁”，因此只清理能够
    // 确定矛盾的旧关系，不把歧义状态升级为已验证。
    for (auto id : sensed_ids) {
        if (id == 0 || id >= objects.size() || !objects[id]) continue;
        auto small = dynamic_pointer_cast<SmallObject>(objects[id]);
        if (!small || static_cast<int>(id) == hold_id || static_cast<int>(id) == plate_id) continue;
        EnsureEvidenceCapacity(id);

        bool visible_container_is_open = false;
        if (sensed_container_id > 0 && sensed_container_id < objects.size()) {
            auto visible_container = dynamic_pointer_cast<Container>(objects[sensed_container_id]);
            visible_container_is_open = visible_container && visible_container->isOpen;
        }

        const bool relation_is_ambiguous =
            small->inside == static_cast<int>(sensed_container_id) && visible_container_is_open;
        if (relation_is_ambiguous) {
            SetInsideEvidence(id, false, EvidenceSource::SENSE);
            continue;
        }

        if (small->inside > 0 && static_cast<size_t>(small->inside) < objects.size()) {
            auto old_container = dynamic_pointer_cast<Container>(objects[small->inside]);
            if (old_container) old_container->DeleteObjectInside(small);
        }
        small->inside = NONE;
        SetInsideEvidence(id, true, EvidenceSource::SENSE);
    }

    // 检查原本应该在这个位置但没被感知到的物体
    // 1. 标记已感知到的物体（包括容器内的物体）
    std::vector<bool> sensed(objects.size(), false);
    for (auto id2 : locationSensedObjects[curr_loc].object_ids) {
        if (id2 > 0 && id2 < objects.size() && objects[id2] != nullptr) {
            sensed[id2] = true;
        }
    }

    // 2.检查这个容器是否开着
    bool container_is_open = false;
    if (sensed_container_id > 0 && sensed_container_id < objects.size() && objects[sensed_container_id] != nullptr) {
        auto cont = std::dynamic_pointer_cast<Container>(objects[sensed_container_id]);
        if (cont) {
            container_is_open = cont->isOpen;
        }
    }

    // 直接按照位置找，找到标记为在这个位置的所谓物体id
    std::vector<unsigned int> ids_at_curr_loc;
    for (unsigned int i = 1; i < objects.size(); ++i) {
        if (!objects[i]) continue;
        if (static_cast<int>(i) == hold_id || static_cast<int>(i) == plate_id) continue;
        if (objects[i]->location == curr_loc) {
            ids_at_curr_loc.push_back(i);
        }
    }

    if(sensed_container_id == NONE || container_is_open == true){
        for (auto id : ids_at_curr_loc) {
            if (!sensed[id]) {
                objects[id]->location = UNKNOWN;
                MarkDirectLocationEvidence(id, false, EvidenceSource::SENSE);
                objects[id]->unable_site = curr_loc;
                LOG(YELLOW "[SenseCurrentLocationOnly] Object id=%u expected at %d but not sensed, set location UNKNOWN\n" RESET, id, curr_loc);
            }
        }
    }
    else{
        for (auto id : ids_at_curr_loc) {
            bool in_container = false;
            // 检查是否是小物体且在容器内
            auto small = std::dynamic_pointer_cast<SmallObject>(objects[id]);
            if (small && small->inside == sensed_container_id) {
                in_container = true;
            }

            if (!sensed[id] && !in_container) {
                objects[id]->location = UNKNOWN;
                MarkDirectLocationEvidence(id, false, EvidenceSource::SENSE);
                objects[id]->unable_site = curr_loc;
                LOG(YELLOW "[SenseCurrentLocationOnly] Object id=%u expected at %d but not sensed, set location UNKNOWN\n" RESET, id, curr_loc);
            }
        }
    }
    RefreshMustNearConstraintState(true);
    UpdateConstraintLedger("Sense", std::vector<unsigned int>());
}
// 位置感知物体记录访问函数实现
const RDFW::LocationSensedInfo& RDFW::GetLocationSensedInfo(int location) const {
    static RDFW::LocationSensedInfo empty_info;  // 返回空结构体作为默认值
    if (location >= 0 && location < locationSensedObjects.size()) {
        return locationSensedObjects[location];
    }
    return empty_info;
}

bool RDFW::HasObjectAtLocation(int location, unsigned int object_id) const {
    if (location >= 0 && location < locationSensedObjects.size()) {
        const auto& info = locationSensedObjects[location];
        for (auto id : info.object_ids) {
            if (id == object_id) return true;
        }
    }
    return false;
}

bool RDFW::HasContainerAtLocation(int location) const {
    if (location >= 0 && location < locationSensedObjects.size()) {
        return locationSensedObjects[location].has_container;
    }
    return false;
}

vector<unsigned int> RDFW::GetObjectsAtLocation(int location) const {
    if (location >= 0 && location < locationSensedObjects.size()) {
        return locationSensedObjects[location].object_ids;
    }
    return vector<unsigned int>();
}

unsigned int RDFW::GetContainerAtLocation(int location) const {
    if (location >= 0 && location < locationSensedObjects.size()) {
        return locationSensedObjects[location].container_id;
    }
    return 0;
}


//感知大物体是否在当前位置
bool RDFW::sense(unsigned int t) //返回值表示t对应的大物体是否在当前位置
{

    vector<unsigned int> A_;

    RecordAction(ActionCategory::OBSERVATION, "Sense",
                 std::vector<unsigned int>());
    if (shadow_dry_run) DryRunSenseIds(A_);
    else Plug::Sense(A_);
    LOG("Sense");
    int flagg = 0;
    // 用“感知到的对象 id”安全地更新
    for (auto id : A_) {
        if (t == id) flagg = 1;
        if (id < objects.size() && objects[id]) {
            if (objects[id]->location != location) {
                objects[id]->location = location;
                if (auto cont = std::dynamic_pointer_cast<Container>(objects[id])) {
                    for (auto &sp : cont->smallObjectsInside) {
                        if (sp) {
                            sp->location = location;
                            const bool entailed = IsInsideVerified(sp->id);
                            MarkDirectLocationEvidence(sp->id, entailed,
                                entailed ? EvidenceSource::CONSTRAINT_DERIVED
                                         : EvidenceSource::CONSTRAINT_HEURISTIC);
                        }
                    }
                }
                // 小物体分支无需强制改 inside，这里保持原有逻辑不动
            }
            MarkDirectLocationEvidence(id, true, EvidenceSource::SENSE);
            if (objects[id]->unable_site == location) objects[id]->unable_site = UNKNOWN;
        }
    }
    RefreshMustNearConstraintState(true);
    UpdateConstraintLedger("Sense", std::vector<unsigned int>());
    return flagg;


}

//查找合适的位置
int RDFW::findrightlocation(unsigned int a)
{
    if (a >= move_cons.size() || a >= putdown_cons.size()) return UNKNOWN;
    for(int i=0;i<(int)rightlocation.size();i++){
        if (!rightlocation[i]) continue;
        if (i >= (int)move_cons[a].size() || i >= (int)putdown_cons[a].size()) continue;
        if (i >= (int)goto_cons.size()) continue;
        if (goto_cons[i] == 0 && move_cons[a][i] == 0 && putdown_cons[a][i] == 0)
            return i;
    }
	return UNKNOWN;
}

//判断小物体是否在容器里面
bool RDFW::Isinside(unsigned int a, unsigned int b){
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a]) ||
        !dynamic_pointer_cast<Container>(objects[b])) return false;
    auto small = ObjectPtrCast<SmallObject>(objects[a]);
    if(small->inside==objects[b]->id) return true;
    else return false;
}


//判断任务是否继续---跟违反的约束有关
bool RDFW::IsKeepingGoing(unsigned int index){
    if (index >= tasks.size() || !tasks[index].IsUsable() ||
        tasks[index].X.empty() || !tasks[index].X[0] ||
        !IsValidObjectId(tasks[index].X[0]->id)) return false;
    auto &t=tasks[index];
    const bool needs_y = t.behave == "takeout" || t.behave == "putin" ||
                         t.behave == "puton";
    if (needs_y && (t.Y.empty() || !t.Y[0] ||
                    !IsValidObjectId(t.Y[0]->id))) return false;
    t.risk=0;
      if(t.behave=="takeout")
      {
        auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
        if (!small) return false;
        if(small->inside==t.Y[0]->id){ //如果任务没有满足
            const int target_location = t.Y[0]->location;
            if (target_location >= 0 && !EnsureLocationCapacity(target_location)) return false;
        t.risk+=takeout_cons[t.X[0]->id][t.Y[0]->id];
        if (target_location >= 0) t.risk+=goto_cons[target_location];
        t.risk+=open_cons[t.Y[0]->id];
            }
            }

        else if(t.behave=="putin") {
            auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
             if (!small) return false;
             if(small->inside!=t.Y[0]->id) //如果任务没有满足
             {
                const int target_location = t.Y[0]->location;
                if (target_location >= 0 && !EnsureLocationCapacity(target_location)) return false;
               t.risk+=putin_cons[t.X[0]->id][t.Y[0]->id]+open_cons[t.Y[0]->id];
               if (target_location >= 0) {
                   t.risk+=move_cons[t.X[0]->id][target_location];
                   if(t.X[0]->location!=target_location) t.risk+=goto_cons[target_location];
               }
               CalculateStepRisk(t);
              }
         }
         else if(t.behave=="puton") {
             const int target_location = t.Y[0]->location;
             if (target_location >= 0 && !EnsureLocationCapacity(target_location)) return false;
             t.risk+=putdown1_cons[t.X[0]->id];
             if (target_location >= 0) {
                 t.risk+= putdown_cons[t.X[0]->id][target_location]+move_cons[t.X[0]->id][target_location];
                 if(t.X[0]->location!=target_location) t.risk+=goto_cons[target_location];
             }
           CalculateStepRisk(t);
         }
         else if(t.behave=="goto" && t.X[0]->location >= 0 &&
                 EnsureLocationCapacity(t.X[0]->location)) t.risk+=goto_cons[t.X[0]->location];
         else if(t.behave=="open") {
             t.risk+=open_cons[t.X[0]->id];
             if (t.X[0]->location >= 0 && EnsureLocationCapacity(t.X[0]->location))
                 t.risk+=goto_cons[t.X[0]->location];
         }
         else if(t.behave=="close") {
             t.risk+=close_cons[t.X[0]->id];
             if (t.X[0]->location >= 0 && EnsureLocationCapacity(t.X[0]->location))
                 t.risk+=goto_cons[t.X[0]->location];
         }
         else if(t.behave=="pickup") {
            CalculateStepRisk(t);
         }
         else if(t.behave=="give") {
            CalculateStepRisk(t);
            t.risk+=givehuman_cons[t.X[0]->id]+putdown1_cons[t.X[0]->id];
            if (human && human->location >= 0) {
                if (!EnsureLocationCapacity(human->location)) return false;
                t.risk+=move_cons[t.X[0]->id][human->location];
                if(t.X[0]->location!=human->location) t.risk+=goto_cons[human->location];
            }
            auto small=dynamic_pointer_cast<SmallObject>(t.X[0]);
         }
         else if(t.behave == "putdown")t.risk+=putdown1_cons[t.X[0]->id];
         if (t.behave != "goto" && t.behave != "move")
             t.risk+=t.X[0]->is_keep;
         if(t.risk>=2)
         {
            cout<<"cheating task!!"<<endl;
            cout<<"real cons num is "<<t.risk<<endl;
            t.is_cheat=0;
            return false;
         }
         else return true;
}

//判断物体是否满足条件
bool Condition::IsObjectSatisfy(const shared_ptr<Object> &target) const
 {
     if (!target)
         return false;
     if (has_explicit_id && target->id != object_id)
         return false;
     if (!has_explicit_id && sort.empty()) return false;
     if (!sort.empty() && sort != target->sort) return false;
     if (declared_type == "small" && !IsSmallObject(target)) return false;
     if (declared_type == "big" && !IsBigObject(target)) return false;
     if (declared_type == "container" && !IsContainerObject(target)) return false;
     if (!declared_type.empty() && declared_type != "object" &&
         declared_type != "small" && declared_type != "big" &&
         declared_type != "container") return false;
     if (color != "") // target maybe point to SmallObject
     {
         auto tem = dynamic_pointer_cast<SmallObject>(target); // Convert type safely
         if (!tem || tem->color != color) return false;
     }
     return true;
 }

 //判断任务是否可执行
 bool Instruction::IsInstructionInvoke(const string &behave, const shared_ptr<Object> &x, const shared_ptr<Object> &y)
 {
     if (isEnable && behave == this->behave && conditionX.IsObjectSatisfy(x) && (y == nullptr || conditionY.IsObjectSatisfy(y)))
         return true;
     return false;
 }

//搜索条件物体
 void Instruction::SearchConditionObject(const shared_ptr<RDFW> &rdfw, bool is_every)
 {
     X.clear();
     Y.clear();
     hasMissingObjects = false;
     if (!rdfw || !syntaxValid) {
         hasMissingObjects = true;
         return;
     }
     for (auto v : rdfw->objects)
     {
         if (v && v->id > 0 && conditionX.IsObjectSatisfy(v))
         {
             if (is_every == true || X.size() == 0)
                 X.push_back(v);
         }
         if (v && v->id > 0 && isUseY && conditionY.IsObjectSatisfy(v))
         {
             if (is_every == true || Y.size() == 0)
                 Y.push_back(v);
         }
     }

     // 验证是否找到了必要的物体
     if (X.empty()) {
         LOG_ERROR("Instruction Error: No object found matching conditionX (sort=%s, color=%s)",
                   conditionX.sort.c_str(), conditionX.color.c_str());
         hasMissingObjects = true;  // 标记包含不存在的物体
     }
     if (isUseY && Y.empty()) {
         LOG_ERROR("Instruction Error: No object found matching conditionY (sort=%s, color=%s)",
                   conditionY.sort.c_str(), conditionY.color.c_str());
         hasMissingObjects = true;  // 标记包含不存在的物体
     }
 }


/**====================== 原子动作 =========================== */
bool RDFW::TakeOut(unsigned int a, unsigned int b)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a]) ||
        !dynamic_pointer_cast<Container>(objects[b])) return false;
    auto small = ObjectPtrCast<SmallObject>(objects[a]);
    auto cont = ObjectPtrCast<Container>(objects[b]);
    LOG("TakeOut(%d,%s)(%d,%s)", a, small->sort.c_str(), b, cont->sort.c_str());
    const std::vector<unsigned int> arguments{a, b};
    RecordAction(ActionCategory::PHYSICAL, "TakeOut", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("TakeOut", arguments) : Plug::TakeOut(a, b);
    RecordActionOutcome("TakeOut", action_succeeded);
    if (action_succeeded)
    {
        small->inside = NONE;
        small->location = location;
        cont->DeleteObjectInside(small);
        cont->isOpen=1;
        SetHold(small);
        EnsureEvidenceCapacity(a);
        EnsureEvidenceCapacity(b);
        MarkDirectLocationEvidence(a, true);
        SetInsideEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        MarkDirectLocationEvidence(b, true);
        SetContainerEvidence(b, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        UpdateTaskList("takeout", objects[a], objects[b]);
        takeout_cons[a][b]=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("TakeOut", arguments);
        return 1;
    }
    return 0;
}

bool RDFW::PutIn(unsigned int a, unsigned int b)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !IsValidObjectId(static_cast<int>(b)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a]) ||
        !dynamic_pointer_cast<Container>(objects[b])) return false;
    auto cont = ObjectPtrCast<Container>(objects[b]);
    auto small = ObjectPtrCast<SmallObject>(objects[a]);
    if (!cont || !small) return 0; // 直接早退，避免 LOG 解引用空指针
    LOG("PutIn(%d,%s)(%d,%s)", a,small->sort.c_str() , b, cont->sort.c_str());
    const std::vector<unsigned int> arguments{a, b};
    RecordAction(ActionCategory::PHYSICAL, "PutIn", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("PutIn", arguments) : Plug::PutIn(a, b);
    RecordActionOutcome("PutIn", action_succeeded);
    if (action_succeeded)
    {

        small->inside = b;
        small->location = location;
        SetHold(nullptr);
        bool already_listed = false;
        for (const auto& item : cont->smallObjectsInside) {
            if (item && item->id == small->id) { already_listed = true; break; }
        }
        if (!already_listed) cont->smallObjectsInside.push_back(small);
        cont->isOpen=1;
        EnsureEvidenceCapacity(a);
        EnsureEvidenceCapacity(b);
        MarkDirectLocationEvidence(a, true);
        SetInsideEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        MarkDirectLocationEvidence(b, true);
        SetContainerEvidence(b, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        UpdateTaskList("putin", objects[a], objects[b]);
        putin_cons[a][b]=0;
        open_cons[b]=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("PutIn", arguments);
        return 1;
    }
    return 0;
}
bool RDFW::Close(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<Container>(objects[a])) return false;
    shared_ptr<Container> container = ObjectPtrCast<Container>(objects[a]);
    if (!container) return 0;
    LOG("(%d,%s) has closed", a, container->sort.c_str());
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::PHYSICAL, "Close", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("Close", arguments) : Plug::Close(a);
    RecordActionOutcome("Close", action_succeeded);
    if (action_succeeded)
    {
        container->isOpen = false;
        MarkDirectLocationEvidence(a, true);
        SetContainerEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        UpdateTaskList("close", objects[a]);
        objects[a]->is_keep=0;
        close_cons[a]=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("Close", arguments);
        return 1;
    }
    return 0;
}
bool RDFW::Open(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<Container>(objects[a])) return false;
    shared_ptr<Container> container = ObjectPtrCast<Container>(objects[a]);
    if (!container) return 0;
    LOG("Open(%d,%s)", a, container->sort.c_str());
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::PHYSICAL, "Open", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("Open", arguments) : Plug::Open(a);
    RecordActionOutcome("Open", action_succeeded);
    if (action_succeeded)
    {
        container->isOpen = true;
        MarkDirectLocationEvidence(a, true);
        SetContainerEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        UpdateTaskList("open", objects[a]);
        open_cons[a]=0;
        objects[a]->is_keep=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("Open", arguments);
        return 1;
    }
    return 0;
}
bool RDFW::FromPlate(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    LOG("FromPlate(%d,%s)", a, objects[a]->sort.c_str());
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::PHYSICAL, "FromPlate", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("FromPlate", arguments) : Plug::FromPlate(a);
    RecordActionOutcome("FromPlate", action_succeeded);
    if (action_succeeded)
    {
        SetHold(plate);
        SetPlate(nullptr);
        MarkDirectLocationEvidence(a, true);
        SetInsideEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("FromPlate", arguments);
        return 1;
    }
    fromplate_cons[a]=0;
    return 0;
}
bool RDFW::ToPlate(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    LOG("ToPlate(%d,%s)", a, objects[a]->sort.c_str());
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::PHYSICAL, "ToPlate", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("ToPlate", arguments) : Plug::ToPlate(a);
    RecordActionOutcome("ToPlate", action_succeeded);
    if (action_succeeded)
    {
        SetPlate(hold);
        SetHold(nullptr);
        MarkDirectLocationEvidence(a, true);
        SetInsideEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        toplate_cons[a]=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("ToPlate", arguments);
        return 1;
    }
    return 0;
}

bool RDFW::PutDown(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    LOG("PutDown(%d,%s)", a, objects[a]->sort.c_str());
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::PHYSICAL, "PutDown", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("PutDown", arguments) : Plug::PutDown(a);
    RecordActionOutcome("PutDown", action_succeeded);
    if (action_succeeded)
    {
        SetHold(nullptr);
        auto small = dynamic_pointer_cast<SmallObject>(objects[a]);
        if (small) {
            small->location = location;
            small->inside = NONE;
        }
        MarkDirectLocationEvidence(a, true);
        SetInsideEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        UpdateTaskList("putdown", objects[a]);
        putdown1_cons[a]=0;
        if (location >= 0 && EnsureLocationCapacity(location))
            putdown_cons[a][location]=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("PutDown", arguments);
        return 1;
    }
    return 0;
}
bool RDFW::PickUp(unsigned int a)
{
    if (!IsValidObjectId(static_cast<int>(a)) ||
        !dynamic_pointer_cast<SmallObject>(objects[a])) return false;
    auto small = ObjectPtrCast<SmallObject>(objects[a]);
    if (!small) return 0;
    LOG("PickUp(%d,%s)", small->id, small->sort.c_str());
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::PHYSICAL, "PickUp", arguments);
    const bool action_succeeded = shadow_dry_run ? DryRunActionSucceeds("PickUp", arguments) : Plug::PickUp(a);
    RecordActionOutcome("PickUp", action_succeeded);
    if (action_succeeded)
    {
        SetHold(small);
        small->location = location;
        small->inside = NONE;
        MarkDirectLocationEvidence(a, true);
        SetInsideEvidence(a, true, EvidenceSource::ACTION_SUCCESS);
        InvalidateSenseAtLocation(location);
        UpdateTaskList("pickup", objects[a]);
        pickup_cons[a]=0;
        RefreshMustNearConstraintState(false);
        UpdateConstraintLedger("PickUp", arguments);
        return 1;
    }
    return 0;
}


bool RDFW::Move(unsigned int a)
{
    LOG("Move(%d)", a);

    // 1) 位置边界检查（使用动态数组大小）
    if (a > static_cast<unsigned int>(MAX_LOCATION_ID)) {
        LOG(RED "Move: invalid target loc=%d (UNKNOWN or negative)\n" RESET, (int)a);
        return 0;
    }

    // 2) 确保数组容量足够，动态扩展
    if (!EnsureLocationCapacity(static_cast<int>(a))) return false;

    // 3) 当前任务 X[0] 是否存在（聚合阶段常常不存在）
    const bool has_taskX0 =
        (task_index < tasks.size() &&
         !tasks[task_index].X.empty() &&
         tasks[task_index].X[0] != nullptr);

    auto safe_idx = [&](unsigned id)->bool {
        return (id > 0 && id < objects.size() && objects[id] != nullptr);
    };
    auto hit_move_cons = [&](unsigned id, unsigned loc)->bool {
        if (!safe_idx(id)) return false;
        if ((int)loc < 0 || (int)loc >= (int)rightlocation.size()) return false;
        // 检查二维数组边界
        if (id >= move_cons.size() || loc >= move_cons[id].size()) return false;
        return move_cons[id][loc] != 0;
    };

    // 4) 跨区前的"自清理"：手持/托盘若会触发 move 约束，先放下/取下
    if (hold_id > 0 && hit_move_cons(hold_id, a)) {
        if (!has_taskX0 || tasks[task_index].X[0]->id != hold_id) {
            PutDown(hold_id);
        }
    }
    if (plate_id > 0 && hit_move_cons(plate_id, a)) {
        if (hold_id > 0) PutDown(hold_id);
        FromPlate(plate_id);
        if (hold_id > 0) PutDown(hold_id);
    }

    // 5) 真正移动
    const int previous_location = location;
    const std::vector<unsigned int> arguments{a};
    RecordAction(ActionCategory::MOVE, "Move", arguments);
    const bool move_succeeded = shadow_dry_run
        ? DryRunActionSucceeds("Move", arguments) : Plug::Move(a);
    RecordActionOutcome("Move", move_succeeded);
    if (!move_succeeded) {
        LOG(RED "Move: Plug::Move(%d) failed\n" RESET, (int)a);
        return 0;
    }

    // 6) 成功后的状态更新（全部带边界/判空）
    location = a;
    if (hold)  hold->location  = a;
    if (plate) plate->location = a;
    InvalidateSenseAtLocation(previous_location);
    InvalidateSenseAtLocation(static_cast<int>(a));
    if (hold_id > 0) {
        MarkDirectLocationEvidence(hold_id, true);
    }
    if (plate_id > 0) {
        MarkDirectLocationEvidence(plate_id, true);
    }

    /*
    // 6) 移动到新位置后立即进行感知，更新物体位置信息
    // 改进：每次移动后都进行感知，更新物体位置和状态信息
    // 在多goto任务模式下跳过Sense操作
    if (!isMultiGotoMode) {
        SenseCurrentLocationOnly();
    } else {

    }
    */
    if (!EnsureLocationCapacity(static_cast<int>(a))) return false;
    goto_cons[a] = 0;                          // a 边界在上面已保证
    if (hold_id > 0 && safe_idx(hold_id)) {
        move_cons[hold_id][a] = 0;
        objects[hold_id]->is_keep = 0;
    }

    // 正确地把“当前任务的目标对象”传给 UpdateTaskList
    if (has_taskX0) {
        UpdateTaskList("goto", tasks[task_index].X[0]);  // ✅ 用任务里的那个对象
    }
    RefreshMustNearConstraintState(false);
    UpdateConstraintLedger("Move", arguments);
    return 1;
}






/*====================== 解析环境 =========================== */


void RDFW::PrintEnv()
 {
 #ifdef __DEBUG__
     vector<vector<shared_ptr<Object>>> objPos;  // 2-dim shared_ptr vector
                                                 // Each position contains all objects_ptrs
     vector<shared_ptr<Object>> unknownPos;      // 1-dim shared_ptr vector

     // Check each object's position and put into objPos
     for (auto v : objects)
     {
         if (!v) continue;
         if (v->location == UNKNOWN)
         {
             unknownPos.push_back(v);
             continue;
         }
         if (v->location < 0 || v->location > MAX_LOCATION_ID) {
             unknownPos.push_back(v);
             continue;
         }
         if (static_cast<std::size_t>(v->location) >= objPos.size())
             objPos.resize(v->location + 1); // expand objPos
         objPos[v->location].push_back(v);
     }
 //把“位置正确性”的调试打印改为“按位置维度”访问
    if ((int)posCorrectFlag.size() < (int)objPos.size()) {
        posCorrectFlag.resize(objPos.size(), true);  // 统一按“位置维度”
    }

     for (int i = 0; i < objPos.size(); i++)
     {
         // print: position and is_correct
         bool is_corr = (i >= 0 && i < (int)posCorrectFlag.size()) ? (posCorrectFlag[i] == true) : true;
         cout << "Pos " << (i < 10 ? " " : "") << i << ":" << (is_corr ? "(T)" : "(F)") << ":";

         // print objects info in this position
         for (auto v : objPos[i])
         {
             // print robot info (green) (hold, plate)
             if (v->sort == "robot")
                 cout << GREEN << "(" << v->sort << " hold:" << hold_id << " plate:" << plate_id << ")" << RESET;

             // print Big Object info (yellow) (id, sort)
             else if (dynamic_pointer_cast<BigObject>(v))
             {
                 auto p = dynamic_pointer_cast<BigObject>(v);
                 cout << YELLOW << "(" << p->id << " " << p->sort;
                 // check container
                 if (dynamic_pointer_cast<Container>(v))
                 {
                     auto p = dynamic_pointer_cast<Container>(v);
                     cout << " inside:[";
                     for (int c = 0; c < p->smallObjectsInside.size(); c++)
                         cout << (c == 0 ? "" : ",") << p->smallObjectsInside[c]->id;
                     cout << "] " << (p->isOpen ? "Open" : "Closed");
                 }
                 cout << ")" << RESET;
             }
             else
                 cout << "(" << v->id << " " << v->sort << ")";
         }
         cout << endl;
     }

     // print Unknown Position (id, sort)
     cout << "UnknownPos:" << endl;
     for (auto v : unknownPos)
     {
         cout << BLUE << "(" << v->id << " " << v->sort << ")" << RESET << " ";
     }

     cout << endl;
 #endif
 }


bool RDFW::ParseInstruction(const string &taskDis) // 改并且新增两个函数
{
    if (taskDis.empty() || taskDis.size() > MAX_INPUT_BYTES) {
        LOG_ERROR("Instruction input is empty or oversized");
        return false;
    }

    // A fully balanced but invalid constraint (e.g. two payload children) is
    // quarantined as one wrapper. Only a broken bracket stream needs marker
    // resynchronization: never execute a second child of a closed bad wrapper.
    int input_depth = 0;
    bool balanced_input = true;
    for (char ch : taskDis) {
        if (ch == '(') ++input_depth;
        else if (ch == ')' && --input_depth < 0) balanced_input = false;
    }
    balanced_input = balanced_input && input_depth == 0;

    std::size_t parsed_forms = 0;
    std::size_t cursor = 0;
    while (cursor < taskDis.size()) {
        std::size_t start = string::npos;
        std::string marker;
        for (std::size_t i = cursor; i < taskDis.size(); ++i) {
            if (taskDis[i] == '(' && InstructionMarkerAt(taskDis, i, &marker)) {
                start = i;
                break;
            }
        }
        if (start == string::npos) break;

        const bool simple_form = marker == "(:task" || marker == "(:info";
        bool constraint_child_seen = false;
        bool opaque_invalid_child = false;
        int depth = 0;
        std::size_t end = string::npos;
        std::size_t recovery = string::npos;
        for (std::size_t i = start; i < taskDis.size(); ++i) {
            if (i > start && !simple_form && !constraint_child_seen &&
                taskDis[i] == '(' && depth == 1) {
                std::string child_marker;
                InstructionMarkerAt(taskDis, i, &child_marker);
                opaque_invalid_child = child_marker != "(:task" && child_marker != "(:info";
                constraint_child_seen = true;
                ++depth;
                continue;
            }
            if (i > start && taskDis[i] == '(' &&
                InstructionMarkerAt(taskDis, i) &&
                (simple_form || !balanced_input) &&
                !(opaque_invalid_child && depth > 1)) {
                // A constraint owns exactly one direct task/info child. Never
                // promote that child to an executable task during recovery,
                // even if its kind is illegal (e.g. cons_notnot + task).
                recovery = i;
                break;
            }
            if (taskDis[i] == '(') ++depth;
            else if (taskDis[i] == ')') {
                if (--depth == 0) {
                    end = i + 1;
                    break;
                }
                if (depth < 0) break;
            }
        }

        if (end == string::npos) {
            // A complete action + condition with only the outer task/info ')'
            // missing is unambiguous at a sibling marker. Recover that payload
            // through the same schema checks. Never do this to a constraint:
            // its child must not escape the original polarity.
            if (simple_form && recovery != string::npos && depth == 1) {
                shared_ptr<SyntaxNode> repaired;
                string repair_error;
                if (ParseInstructionFormTree(taskDis.substr(start, recovery - start) + ")",
                                             repaired, repair_error)) {
                    ExtractInstructions(vector<shared_ptr<SyntaxNode>>(1, repaired));
                    ++parsed_forms;
                    LOG("[InputSafety] recovered missing outer close at byte %zu", start);
                    cursor = recovery;
                    continue;
                }
            }
            ++discarded_instruction_count;
            LOG_ERROR("Isolated malformed instruction form at byte %zu", start);
            cursor = recovery == string::npos ? taskDis.size() : recovery;
            continue;
        }

        shared_ptr<SyntaxNode> node;
        string error;
        const string form = taskDis.substr(start, end - start);
        if (!ParseInstructionFormTree(form, node, error)) {
            ++discarded_instruction_count;
            LOG_ERROR("Isolated malformed instruction (%s): %s",
                      error.c_str(), form.c_str());
        } else {
            ExtractInstructions(vector<shared_ptr<SyntaxNode>>(1, node));
            ++parsed_forms;
        }
        cursor = end;
    }
    return parsed_forms > 0;
}



string RDFW::ExtractValue(const string &taskDis, int tag1, int tag2)
{
    if (tag1 < 0 || tag2 < tag1 || static_cast<std::size_t>(tag2) > taskDis.size())
        return "";
    const string value = taskDis.substr(tag1, tag2 - tag1);
    if (value.empty()) return value;
    return (value.back() == ' ' ? value.substr(0, value.size() - 1) : value);
}



void RDFW::ExtractInstructions(const vector<shared_ptr<SyntaxNode>> &nodes)
{
    for (const auto &node : nodes)
    {
        if (!node) {
            ++discarded_instruction_count;
            LOG_ERROR("Ignoring null instruction syntax node");
            continue;
        }
        string instructionType = node->value;

        if (instructionType == ":task")
        {
            AddValidatedInstruction(node, InstructionKind::TASK, tasks);
        }
        else if (instructionType == ":cons_not")
        {
            if (node->sons.size() != 1 || !node->sons[0]) {
                ++discarded_instruction_count;
                LOG_ERROR("Ignoring malformed :cons_not wrapper");
                continue;
            }
            const auto& child = node->sons[0];
            if (child->value == ":info")
                AddValidatedInstruction(child, InstructionKind::NOT_INFO_CONSTRAINT,
                                        not_infoConstrains);
            else if (child->value == ":task")
                AddValidatedInstruction(child, InstructionKind::NOT_TASK_CONSTRAINT,
                                        not_taskConstrains);
            else {
                ++discarded_instruction_count;
                LOG_ERROR("Ignoring :cons_not with invalid child %s",
                          child->value.c_str());
            }
        }
        else if (instructionType == ":cons_notnot") {
            if (node->sons.size() != 1 || !node->sons[0] ||
                node->sons[0]->value != ":info") {
                ++discarded_instruction_count;
                LOG_ERROR("Ignoring malformed :cons_notnot wrapper");
                continue;
            }
            AddValidatedInstruction(node->sons[0],
                                    InstructionKind::MUST_INFO_CONSTRAINT,
                                    notnot_infoConstrains);
        }
        else if (instructionType == ":info")
            AddValidatedInstruction(node, InstructionKind::INFO, infos);
        else {
            ++discarded_instruction_count;
            LOG_ERROR("Ignoring unknown instruction node %s", instructionType.c_str());
        }
    }
}

bool RDFW::AddValidatedInstruction(const shared_ptr<SyntaxNode>& node,
                                   InstructionKind kind,
                                   vector<Instruction>& destination) {
    try {
        Instruction instruction(node, shared_from_this());
        if (!ValidateInstruction(instruction, kind)) {
            ++discarded_instruction_count;
            LOG_ERROR("[InputSafety] isolated instruction behave=%s reason=%s",
                      instruction.behave.c_str(),
                      instruction.validationMessage.c_str());
            return false;
        }
        destination.push_back(instruction);
        return true;
    } catch (const std::exception& error) {
        ++discarded_instruction_count;
        LOG_ERROR("[InputSafety] isolated instruction exception: %s", error.what());
        return false;
    }
}

bool RDFW::ValidateInstruction(Instruction &instruction, InstructionKind kind) {
    enum class RequiredType { ANY, SMALL, BIG, CONTAINER };
    struct Schema {
        std::size_t parameter_count;
        bool uses_y;
        RequiredType x_type;
        RequiredType y_type;
        bool task_form;
        bool info_form;
        bool give_form;
    };

    auto reject = [&](const string& message) {
        instruction.validationStatus = Instruction::ValidationStatus::REJECTED;
        instruction.validationMessage = message;
        instruction.syntaxValid = false;
        return false;
    };

    if (!instruction.syntaxValid) {
        return reject(instruction.validationMessage.empty()
                          ? "invalid syntax tree" : instruction.validationMessage);
    }

    Schema schema = {0, false, RequiredType::ANY, RequiredType::ANY,
                     false, false, false};
    const string& behave = instruction.behave;
    if (behave == "goto")
        schema = {1, false, RequiredType::ANY, RequiredType::ANY, true, false, false};
    else if (behave == "pickup" || behave == "putdown")
        schema = {1, false, RequiredType::SMALL, RequiredType::ANY, true, false, false};
    else if (behave == "open" || behave == "close")
        schema = {1, false, RequiredType::CONTAINER, RequiredType::ANY, true, false, false};
    else if (behave == "give")
        schema = {2, false, RequiredType::SMALL, RequiredType::ANY, true, false, true};
    else if (behave == "putin" || behave == "takeout")
        schema = {2, true, RequiredType::SMALL, RequiredType::CONTAINER, true, false, false};
    else if (behave == "puton")
        schema = {2, true, RequiredType::SMALL, RequiredType::BIG, true, false, false};
    else if (behave == "opened" || behave == "closed")
        schema = {1, false, RequiredType::CONTAINER, RequiredType::ANY, false, true, false};
    else if (behave == "plate")
        schema = {1, false, RequiredType::SMALL, RequiredType::ANY, false, true, false};
    else if (behave == "inside" || behave == "in")
        schema = {2, true, RequiredType::SMALL, RequiredType::CONTAINER, false, true, false};
    else if (behave == "on")
        schema = {2, true, RequiredType::SMALL, RequiredType::BIG, false, true, false};
    else if (behave == "near" || behave == "nextto")
        schema = {2, true, RequiredType::ANY, RequiredType::ANY, false, true, false};
    else
        return reject("unknown behavior");

    // NL "give X to the human" carries an explicit human Y; IT encodes the
    // same target as the literal in (give human X). Preserve both legal forms.
    if (schema.give_form && !instruction.structuredSource &&
        (instruction.isUseY || !instruction.Y.empty())) {
        schema.uses_y = true;
        schema.y_type = RequiredType::BIG;
        for (const auto& target : instruction.Y)
            if (!target || target->sort != "human")
                return reject("give Y must be a human");
    }

    const bool wants_task = kind == InstructionKind::TASK ||
                            kind == InstructionKind::NOT_TASK_CONSTRAINT;
    if ((wants_task && !schema.task_form) || (!wants_task && !schema.info_form))
        return reject("behavior is not valid for this instruction kind");

    if (instruction.structuredSource) {
        if (instruction.declaredParameters.size() != schema.parameter_count)
            return reject("action parameter count does not match schema");
        if (schema.give_form) {
            if (instruction.declaredParameters[0] != "human" ||
                instruction.declaredParameters[1] != "X")
                return reject("give must bind as (give human X)");
        } else {
            if (instruction.declaredParameters.empty() ||
                instruction.declaredParameters[0] != "X")
                return reject("first action parameter must bind X");
            if (schema.uses_y && instruction.declaredParameters[1] != "Y")
                return reject("second action parameter must bind Y");
        }
    }

    bool repaired = false;
    if (schema.uses_y && !instruction.isUseY && !instruction.Y.empty()) {
        instruction.isUseY = true;
        repaired = true;
    }
    if (!schema.uses_y && (instruction.isUseY || !instruction.Y.empty()))
        return reject("unary instruction contains a Y binding");
    if (instruction.X.empty()) return reject("X binding resolved to an empty set");
    if (schema.uses_y && instruction.Y.empty())
        return reject("Y binding resolved to an empty set");

    auto registered = [&](const shared_ptr<Object>& object) {
        return object && object->id > 0 &&
               object->id <= static_cast<int>(MAX_OBJECT_ID) &&
               static_cast<std::size_t>(object->id) < objects.size() &&
               objects[object->id] == object;
    };
    auto type_matches = [](const shared_ptr<Object>& object, RequiredType type) {
        switch (type) {
            case RequiredType::SMALL: return IsSmallObject(object);
            case RequiredType::BIG: return IsBigObject(object);
            case RequiredType::CONTAINER: return IsContainerObject(object);
            case RequiredType::ANY: return static_cast<bool>(object);
        }
        return false;
    };
    auto declared_type_matches = [](const shared_ptr<Object>& object,
                                    const string& declared) {
        if (declared.empty() || declared == "object") return true;
        if (declared == "small") return IsSmallObject(object);
        if (declared == "big") return IsBigObject(object);
        if (declared == "container") return IsContainerObject(object);
        return false;
    };

    bool inferred = false;
    for (const auto& object : instruction.X) {
        if (!registered(object)) return reject("X contains an invalid object reference");
        if (!type_matches(object, schema.x_type))
            return reject("X actual object type violates schema");
        if (!declared_type_matches(object, instruction.conditionX.declared_type))
            return reject("X declared type conflicts with actual object type");
        if (schema.x_type != RequiredType::ANY &&
            instruction.conditionX.declared_type.empty()) inferred = true;
    }
    for (const auto& object : instruction.Y) {
        if (!registered(object)) return reject("Y contains an invalid object reference");
        if (!type_matches(object, schema.y_type))
            return reject("Y actual object type violates schema");
        if (!declared_type_matches(object, instruction.conditionY.declared_type))
            return reject("Y declared type conflicts with actual object type");
        if (schema.y_type != RequiredType::ANY &&
            instruction.conditionY.declared_type.empty()) inferred = true;
    }

    if (schema.give_form) {
        bool valid_human = false;
        for (const auto& object : objects) {
            if (object && object->sort == "human" && IsBigObject(object)) {
                valid_human = true;
                break;
            }
        }
        if (!valid_human) return reject("give has no valid human target");
    }

    instruction.hasMissingObjects = false;
    instruction.validationStatus = repaired
        ? Instruction::ValidationStatus::REPAIRED
        : (inferred ? Instruction::ValidationStatus::INFERRED
                    : Instruction::ValidationStatus::VALID);
    instruction.validationMessage = inferred
        ? "required runtime type inferred from bound object"
        : (repaired ? "binding metadata repaired from resolved objects" : "schema valid");
    // Inferred types are ordinary legal input, often checked twice. Avoid
    // synchronous per-item success logging inside the real-time budget.
    if (instruction.validationStatus == Instruction::ValidationStatus::REPAIRED) {
        LOG("[InputSafety] instruction %s status=%s",
            behave.c_str(),
            Instruction::ValidationStatusName(instruction.validationStatus));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief   Parse Natural Language
 * @param   src (const): All env strings in natural language
 * @returns None
 */
void RDFW::ParseNaturalLanguage(const string &src) //
{
    if (src.size() > MAX_INPUT_BYTES) {
        LOG_ERROR("Natural-language instruction input is oversized");
        return;
    }
    std::size_t begin = 0;
    while (begin < src.size()) {
        const std::size_t delimiter = src.find('.', begin);
        const std::size_t end = delimiter == string::npos ? src.size() : delimiter + 1;
        const string sentence = TrimCopy(src.substr(begin, end - begin));
        if (!sentence.empty()) {
            LOG(GREEN "%s" RESET, sentence.c_str());
            if (!ParseNaturalLanguageSentence(sentence)) {
                ++discarded_instruction_count;
                LOG_ERROR("[InputSafety] isolated malformed NL instruction: %s",
                          sentence.c_str());
            }
        }
        if (delimiter == string::npos) break;
        begin = delimiter + 1;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief   Parse Natural Language (Single Sentence)
 * @param   s (string): single sentence to parse
 * @return  if Parse successfully
 */
bool RDFW::ParseNaturalLanguageSentence(const string &s) // 改
{
    if (!nlp_parser || s.empty() || s.size() > 64 * 1024 ||
        !nlp_parser->parse(s))  // Fail to parse
    {
        errorlist.push_back(s);
        return false;
    }

    auto tree = nlp_parser->root;
    if (!tree) {
        errorlist.push_back(s);
        return false;
    }
    vector<Instruction> *list_p = nullptr;
    bool is_task = true;
    InstructionKind instruction_kind = InstructionKind::TASK;

    switch (tree->sons.size())
    {
    case 1:
    {
        if (tree->sons[0]->token.type == VP)
        {
            if (nlp_parser->is_not)
            {
                list_p = &not_taskConstrains;
                instruction_kind = InstructionKind::NOT_TASK_CONSTRAINT;
            }
            else
            {
                list_p = &tasks;
                instruction_kind = InstructionKind::TASK;
            }
        }
        break;
    }
    case 2:
    case 3:
    {
        is_task = false;

        // 定位指令类被
        if (nlp_parser->is_must)
        {
            if (nlp_parser->is_not)
            {
                list_p = &not_infoConstrains;
                instruction_kind = InstructionKind::NOT_INFO_CONSTRAINT;
            }
            else
            {
                list_p = &notnot_infoConstrains;
                instruction_kind = InstructionKind::MUST_INFO_CONSTRAINT;
            }
        }
        else
        {
            list_p = &infos;
            instruction_kind = InstructionKind::INFO;
        }
        break;
    }
    default:
        break;
    }

    if (list_p == nullptr)
    {
        LOG_ERROR("NLP Parse Error");
        return false;
    }
    else
    {
        Instruction instr;
        // 捕获task或info, 封装进Instruction
        if (is_task)
            instr = nlp_parser->get_task_instruction();
        else
            instr = nlp_parser->get_info_instruction();

        instr.SearchConditionObject(shared_from_this(), nlp_parser->is_every);
        if (!ValidateInstruction(instr, instruction_kind)) {
            LOG_ERROR("[InputSafety] rejected NL instruction: %s",
                      instr.validationMessage.c_str());
            return false;
        }
        list_p->push_back(instr);
    }

    return true;
}



bool RDFW::ParseEnvSentence(const string &sentence) // 改
{
    if (sentence.size() < 3 || sentence.front() != '(' || sentence.back() != ')') {
        LOG_ERROR("Env sentence has invalid delimiters: %s", sentence.c_str());
        return false;
    }
    const vector<string> tokenList =
        SplitWhitespace(sentence.substr(1, sentence.size() - 2));
    if (tokenList.empty()) return false;

    const string &firstToken = tokenList[0];
    const bool unary = firstToken == "hold" || firstToken == "plate" ||
                       firstToken == "opened" || firstToken == "closed";
    const bool ternary = firstToken == "at" || firstToken == "sort" ||
                         firstToken == "size" || firstToken == "color" ||
                         firstToken == "inside" || firstToken == "type";
    const std::size_t expected_tokens = unary ? 2 : (ternary ? 3 : 0);
    if (expected_tokens == 0 || tokenList.size() != expected_tokens) {
        LOG_ERROR("Env sentence has an unknown predicate or wrong field count: %s",
                  sentence.c_str());
        return false;
    }

    int index = 0;
    if (!ParseBoundedInt(tokenList[1], 0, static_cast<int>(MAX_OBJECT_ID), index)) {
        LOG_ERROR("Env sentence has an invalid object id: %s", sentence.c_str());
        return false;
    }

    // set robot status
    if (firstToken == "hold")
    {
        if (index < 0 || index > static_cast<int>(MAX_OBJECT_ID)) return false;
        this->hold_id = index;
        return true;
    }
    else if (firstToken == "plate")
    {
        this->plate_id = index;
        return true;
    }

    if (index == 0 && firstToken != "at") {
        LOG_ERROR("Only an at fact may target robot id 0: %s", sentence.c_str());
        return false;
    }
    if (index > 0 && !EnsureObjectExists(static_cast<unsigned int>(index))) return false;
    if (static_cast<std::size_t>(index) >= objects.size() || !objects[index]) return false;
    auto &obj = objects[index];

    auto remember_small = [&](const shared_ptr<SmallObject>& small) {
        for (const auto& existing : smallObjects)
            if (existing && existing->id == small->id) return;
        smallObjects.push_back(small);
    };

    if (firstToken == "opened" || firstToken == "closed") {
        if (dynamic_pointer_cast<SmallObject>(obj)) {
            LOG_ERROR("Container state applied to small object id %d", index);
            return false;
        }
        auto containerPtr = dynamic_pointer_cast<Container>(obj);
        if (!containerPtr) {
            containerPtr = make_shared<Container>(obj);
            obj = containerPtr;
        }
        containerPtr->isOpen = (firstToken == "opened");
        SetContainerEvidence(index, stage == 1, EvidenceSource::INITIAL);
    } else if (firstToken == "at") {
        int location_id = 0;
        if (!ParseBoundedInt(tokenList[2], 0, MAX_LOCATION_ID, location_id) ||
            !EnsureLocationCapacity(location_id)) {
            LOG_ERROR("Env sentence has an invalid location: %s", sentence.c_str());
            return false;
        }
        obj->location = location_id;
        if (index > 0)
            MarkDirectLocationEvidence(index, stage == 1, EvidenceSource::INITIAL);
        rightlocation[location_id] = true;
    } else if (firstToken == "sort") {
        if (tokenList[2].empty()) return false;
        obj->sort = tokenList[2];
    } else if (firstToken == "size") {
        if (tokenList[2] == "big") {
            if (dynamic_pointer_cast<SmallObject>(obj)) return false;
            if (!dynamic_pointer_cast<BigObject>(obj)) obj = make_shared<BigObject>(obj);
        } else if (tokenList[2] == "small") {
            if (dynamic_pointer_cast<BigObject>(obj)) return false;
            auto small = dynamic_pointer_cast<SmallObject>(obj);
            if (!small) {
                small = make_shared<SmallObject>(obj);
                obj = small;
            }
            remember_small(small);
        } else {
            return false;
        }
    } else if (firstToken == "color" || firstToken == "inside") {
        if (dynamic_pointer_cast<BigObject>(obj)) return false;
        auto small = dynamic_pointer_cast<SmallObject>(obj);
        if (!small) {
            small = make_shared<SmallObject>(obj);
            obj = small;
        }
        remember_small(small);
        if (firstToken == "color") {
            small->color = tokenList[2];
        } else {
            int container_id = 0;
            if (!ParseBoundedInt(tokenList[2], 1,
                                 static_cast<int>(MAX_OBJECT_ID), container_id)) {
                LOG_ERROR("Env sentence has an invalid inside reference: %s",
                          sentence.c_str());
                return false;
            }
            small->inside = container_id;
            SetInsideEvidence(index, stage == 1, EvidenceSource::INITIAL);
        }
    } else if (firstToken == "type") {
        if (tokenList[2] != "container" || dynamic_pointer_cast<SmallObject>(obj))
            return false;
        if (!dynamic_pointer_cast<Container>(obj)) obj = make_shared<Container>(obj);
    }

    return true;
}



bool RDFW::ParseEnv(const string &env) // 没改
{
    if (env.empty() || env.size() > MAX_INPUT_BYTES || objects.empty() || !objects[0]) {
        LOG_ERROR("WorldState input is empty, oversized, or lacks robot object 0");
        return false;
    }

    // The platform wraps leaf facts in (:domain ...).  Parse only innermost
    // parenthesized forms so the wrapper can never be mistaken for a fact.
    struct EnvOpen { std::size_t offset; bool has_child; };
    vector<EnvOpen> open_forms;
    std::size_t valid_facts = 0;
    for (std::size_t cursor = 0; cursor < env.size(); ++cursor) {
        if (env[cursor] == '(') {
            if (!open_forms.empty()) open_forms.back().has_child = true;
            open_forms.push_back(EnvOpen{cursor, false});
            continue;
        }
        if (env[cursor] != ')') continue;
        if (open_forms.empty()) {
            LOG_ERROR("Ignoring unmatched environment ')' at byte %zu", cursor);
            continue;
        }
        const EnvOpen current = open_forms.back();
        open_forms.pop_back();
        if (current.has_child) continue;
        const string fact = env.substr(current.offset, cursor - current.offset + 1);
        if (ParseEnvSentence(fact)) {
            ++valid_facts;
        } else {
            LOG_ERROR("Ignoring invalid environment fact: %s", fact.c_str());
        }
    }
    if (!open_forms.empty())
        LOG_ERROR("Ignoring %zu unterminated environment form(s)", open_forms.size());

    // set robot status
    if (hold_id > 0)
    {
        auto held = std::dynamic_pointer_cast<SmallObject>(GetObject(hold_id));
        if (!held) {
            LOG_ERROR("Ignoring invalid hold reference %d", hold_id);
            hold_id = NONE;
            SetHold(nullptr);
        } else {
            SetHold(held);
            MarkDirectLocationEvidence(hold_id, stage == 1, EvidenceSource::INITIAL);
            SetInsideEvidence(hold_id, stage == 1, EvidenceSource::INITIAL);
        }
    }
    if (plate_id > 0)
    {
        auto plated = std::dynamic_pointer_cast<SmallObject>(GetObject(plate_id));
        if (!plated || plate_id == hold_id) {
            LOG_ERROR("Ignoring invalid plate reference %d", plate_id);
            plate_id = NONE;
            SetPlate(nullptr);
        } else {
            SetPlate(plated);
            MarkDirectLocationEvidence(plate_id, stage == 1, EvidenceSource::INITIAL);
            SetInsideEvidence(plate_id, stage == 1, EvidenceSource::INITIAL);
        }
    }

    for (const auto &s : smallObjects)
    {
        if (s == plate || s == hold)
            continue;
        if (s->inside != UNKNOWN && s->inside != NONE)
        {
            auto p = dynamic_pointer_cast<Container>(GetObject(s->inside));
            if (p != nullptr)
            {
                p->smallObjectsInside.push_back(s);
                s->location = p->location;
            }
            else
            {
                s->location = UNKNOWN;
                s->inside = UNKNOWN;
            }
        }
        else if (s->location != UNKNOWN)
            s->inside = NONE;
        if (s->inside != UNKNOWN && s != plate && s != hold)
            SetInsideEvidence(s->id, stage == 1, EvidenceSource::INITIAL);
        if (s->location != UNKNOWN && LocationSource(s->id) == EvidenceSource::UNKNOWN)
            MarkDirectLocationEvidence(s->id, stage == 1, EvidenceSource::INITIAL);
    }

    if (valid_facts == 0) {
        LOG_ERROR("WorldState contains no usable facts");
        return false;
    }
    return true;
}



void RDFW::ParseInfo(const Instruction &info) // 改
{
    // 跳过包含不存在物体的info
    if (!info.IsUsable() || info.X.empty() || !info.X[0] ||
        (info.isUseY && (info.Y.empty() || !info.Y[0]))) {
        LOG_ERROR("[InputSafety] skipped unusable info %s", info.behave.c_str());
        return;
    }

    const string &behave = info.behave;

    if (behave == "on")
    {
        int thelocation = info.Y[0]->location;
        for (auto v : info.X)
        {
            v->location = thelocation;
            MarkDirectLocationEvidence(v->id, IsLocationVerified(info.Y[0]->id),
                                       EvidenceSource::EXPLICIT_INFO);
            auto small=dynamic_pointer_cast<SmallObject>(v);
            if(small!=nullptr) {
                small->inside=NONE;
                small->on = info.Y[0]->id;
                SetInsideEvidence(v->id, true, EvidenceSource::EXPLICIT_INFO);
            }
        }
    }
    else if (behave == "near")
    {
        int yLocation = info.Y[0]->location;
        int xLocation = info.X[0]->location;

        if (yLocation != UNKNOWN)
        {
            for (auto v : info.X)
            {
            v->location = yLocation;
            MarkDirectLocationEvidence(v->id, IsLocationVerified(info.Y[0]->id),
                                       EvidenceSource::EXPLICIT_INFO);
            auto small=dynamic_pointer_cast<SmallObject>(v);
            if(small!=nullptr) {
                small->inside=NONE;
                SetInsideEvidence(v->id, true, EvidenceSource::EXPLICIT_INFO);
            }
            }

        }
        else if (xLocation != UNKNOWN)
        {
            for (auto v : info.Y)
            {
            v->location = xLocation;
            MarkDirectLocationEvidence(v->id, IsLocationVerified(info.X[0]->id),
                                       EvidenceSource::EXPLICIT_INFO);
            auto small=dynamic_pointer_cast<SmallObject>(v);
            if(small!=nullptr) {
                small->inside=NONE;
                SetInsideEvidence(v->id, true, EvidenceSource::EXPLICIT_INFO);
            }
            }
        }
    }
    else if (behave == "plate")
    {
        if (plate == nullptr)
        {
            auto small = dynamic_pointer_cast<SmallObject>(info.X[0]);
            if (!small) return;
            SetPlate(small);
            SetInsideEvidence(small->id, true, EvidenceSource::EXPLICIT_INFO);
        }
        else
        {
            LOG_ERROR("The plate already has a small object (%d %s)", plate->id, plate->sort.c_str());
        }
    }
    else if (behave == "inside" || behave == "in")
    {
        auto c = dynamic_pointer_cast<Container>(info.Y[0]);
        if (!c) return;
        int cId = c->id;

        for (auto v : info.X)
        {
            auto p = dynamic_pointer_cast<SmallObject>(v);
            if (!p) continue;
            p->inside = cId;
            p->location=c->location;
            SetInsideEvidence(p->id, true, EvidenceSource::EXPLICIT_INFO);
            MarkDirectLocationEvidence(p->id, IsLocationVerified(cId),
                                       EvidenceSource::EXPLICIT_INFO);
            c->smallObjectsInside.push_back(p);
        }
    }
    else if (behave == "opened")
    {
        for (auto v : info.X)
        {
            auto p = dynamic_pointer_cast<Container>(v);
            if (!p) continue;
            p->isOpen = true;
            SetContainerEvidence(p->id, true, EvidenceSource::EXPLICIT_INFO);
        }
    }
    else if (behave == "closed")
    {
        for (auto v : info.X)
        {
            auto p = dynamic_pointer_cast<Container>(v);
            if (!p) continue;
            p->isOpen = false;
            SetContainerEvidence(p->id, true, EvidenceSource::EXPLICIT_INFO);
        }
    }
}






/*====================== 工具函数=========================== */
//UpdateTaskList、LogInstructionError、AfterSolveTask、PrintInstruction、


//更新任务列表
void RDFW::UpdateTaskList(const string& behave,
        const shared_ptr<Object>& x,
        const shared_ptr<Object>& y)
    {
    for (auto& t : tasks) {
    if (!t.isEnable) continue;

    if (behave == "goto") {
    // 仅当“同一物体”时才去重（严格按对象 id）
    if (t.behave == "goto" && t.X.size() > 0 && t.X[0] && x && (t.X[0]->id == x->id)) {
    t.isEnable = false;
    }
    // 注意：这里直接 continue，避免 goto 走到下面“通用等价”分支
    continue;
    }

    // 非 goto：保持你原有的等价判定（条件/对象等）
    if (t.IsInstructionInvoke(behave, x, y)) {
    t.isEnable = false;
    }
    }
}

//记录错误任务
void RDFW::LogInstructionError(const Instruction &task) // 新增
{
    stringstream ss;
    ss << task;
    LOG_ERROR("Instruction Error\n %s", ss.str().c_str());
}

//执行完任务后更新约束
void RDFW::AfterSolveTask(const Instruction &task){
    if (!task.IsUsable() || task.X.empty() || !task.X[0] ||
        !IsValidObjectId(task.X[0]->id)) return;
    if(task.behave=="pickup") for(int i=0;i<(int)pickup_cons.size();i++) pickup_cons[i]+=2;
    else if(task.behave=="putdown") pickup_cons[task.X[0]->id]+=2;
    else if(task.behave=="takeout" && !task.Y.empty() && task.Y[0] &&
            IsValidObjectId(task.Y[0]->id)) putin_cons[task.X[0]->id][task.Y[0]->id]+=2;
    else if(task.behave=="putin" && !task.Y.empty() && task.Y[0] &&
            IsValidObjectId(task.Y[0]->id)) takeout_cons[task.X[0]->id][task.Y[0]->id]+=2;
//这里要删除
//    else if(task.behave=="goto") for(int i=0;i<50;i++) goto_cons[i]+=2;
    else task.X[0]->is_keep+=2;
}//我还是想把这个改一下

/**
 * @brief   Print Instruction Infomation
 * @param   None
 * @note    task, info, constrains are stored as "Instruction class",
 *          include bahave, conditionX, conditionY (if it has)
 */
 void RDFW::PrintInstruction()
 {
 #ifdef __DEBUG__
     // print tasks (Do)
     cout << "Task:\n";
     for (auto v : tasks) {
         cout << v;
     }

     // print infos
     cout << "\nInfo:\n";
     for (auto v : infos) {
         cout << v;
     }

     // print not_infos (constrains)
     cout << "\nNot_Info:\n";
     for (auto v : not_infoConstrains) {
         cout << v;
     }

     // print not_tasks (constrains)
     cout << "\nNot_Task:\n";
     for (auto v : not_taskConstrains)
         {
         cout << v;
     }

     // print notnot_infos (Must keep)
     cout << "\nNotNot_Info:\n";
     for (auto v : notnot_infoConstrains)
         {
         cout << v;
     }
 #endif
 }



 ////////////////////////////////////////////////////////////////////////////////////////////////////////
 /**
  * @brief   Print Environment Infomation
  * @param   None
  * @note    All Env Infomations are stored in objects, include id, sort, position...
  *          we define a 2D vector to store each objects' position and its info
  *
  */


void RDFW::Fini()
{
    preflight_report = QuestionPreflightReport();
    cout << "#(RDFW): Fini - Comprehensive state cleanup for next test" << endl;

    // ==================== 机器人状态重置 ====================
    location = UNKNOWN;
    hold = nullptr;
    hold_id = 0;

    // ==================== 对象引用清理 ====================
    human = nullptr;
    plate = nullptr;
    plate_id = 0;
    task_index = 0;

    // ==================== 状态变量重置 ====================
    solved_task_num = 0;
    err_times = 0;
    isPass = false;
    isKeepConstrain = false;
    isMultiGotoMode = false;
    isAutoConstrain = false;
    isAskTwice = false;

    // ==================== 容器完全清理 ====================
    objects.clear();
    smallObjects.clear();
    tasks.clear();
    infos.clear();
    not_infoConstrains.clear();
    not_taskConstrains.clear();
    notnot_infoConstrains.clear();
    posCorrectFlag.clear();
    posSensedFlag.clear();
    objectLocationVerified.clear();
    objectLocationInferredByMustNear.clear();
    objectInsideVerified.clear();
    containerStateVerified.clear();
    objectLocationSource.clear();
    objectInsideSource.clear();
    containerStateSource.clear();
    failed_task_revision.clear();
    world_revision = 0;
    stop_rescan_count = 0;
    errorlist.clear();
    lock_by_mustnear.clear();
    mustNearComponent.clear();

    // ==================== 内存优化清理 ====================
    // 强制释放vector内存
    objects.shrink_to_fit();
    smallObjects.shrink_to_fit();
    tasks.shrink_to_fit();
    infos.shrink_to_fit();
    not_infoConstrains.shrink_to_fit();
    not_taskConstrains.shrink_to_fit();
    notnot_infoConstrains.shrink_to_fit();
    posCorrectFlag.shrink_to_fit();
    posSensedFlag.shrink_to_fit();
    objectLocationVerified.shrink_to_fit();
    objectLocationInferredByMustNear.shrink_to_fit();
    objectInsideVerified.shrink_to_fit();
    containerStateVerified.shrink_to_fit();
    errorlist.shrink_to_fit();
    lock_by_mustnear.shrink_to_fit();
    mustNearComponent.shrink_to_fit();

    // ==================== 数组完全重置 ====================
    // 重置并查集数组
    memset(uf_parent, -1, sizeof(uf_parent));
    memset(uf_size, 0, sizeof(uf_size));
    memset(uf_groupLoc, -1, sizeof(uf_groupLoc));

    // 重置配置标志
    enable_near_correction = true;
    enable_must_lock = true;
    sense_cb = nullptr;

    // ==================== 动态数组深度清理 ====================
    // 清理一维数组
    fill(goto_cons.begin(), goto_cons.end(), 0);
    fill(putdown1_cons.begin(), putdown1_cons.end(), 0);
    fill(open_cons.begin(), open_cons.end(), 0);
    fill(close_cons.begin(), close_cons.end(), 0);
    fill(pickup_cons.begin(), pickup_cons.end(), 0);
    fill(givehuman_cons.begin(), givehuman_cons.end(), 0);
    fill(fromplate_cons.begin(), fromplate_cons.end(), 0);
    fill(toplate_cons.begin(), toplate_cons.end(), 0);
    fill(rightlocation.begin(), rightlocation.end(), false);

    // 清理二维数组
    for (auto& row : putin_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : takeout_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : putdown_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : move_cons) fill(row.begin(), row.end(), 0);
    for (auto& row : mustnear_cons) fill(row.begin(), row.end(), 0);

    // ==================== 感知状态完全重置 ====================
    posSensedFlag.resize(100, false);
    objectLocationVerified.resize(100, false);
    objectLocationInferredByMustNear.resize(100, false);
    objectInsideVerified.resize(100, false);
    containerStateVerified.resize(100, false);
    locationSensedObjects.resize(100);

    // 深度清理位置感知数据
    for (auto& loc_info : locationSensedObjects) {
        loc_info.object_ids.clear();
        loc_info.object_ids.shrink_to_fit();
        loc_info.container_id = 0;
        loc_info.has_container = false;
    }

    // ==================== 解析器状态清理 ====================
    if (nlp_parser) {
        parser::clear_static_state();
    }

    // ==================== 重新初始化基础状态 ====================
    objects.push_back(shared_ptr<Object>(static_cast<Object*>(this),
                                         [](Object*) {}));
    if (isErrorCorrection)
        posCorrectFlag.push_back(false);
    else
        posCorrectFlag.push_back(true);

    // ==================== 内存优化 ====================
    OptimizeMemoryUsage();

    cout << "#(RDFW): Comprehensive state cleanup completed - ready for next test" << endl;
}

void RDFW::OptimizeMemoryUsage() {
    cout << "#(RDFW): Optimizing memory usage..." << endl;

    // 强制释放所有vector的未使用内存
    objects.shrink_to_fit();
    smallObjects.shrink_to_fit();
    tasks.shrink_to_fit();
    infos.shrink_to_fit();
    not_infoConstrains.shrink_to_fit();
    not_taskConstrains.shrink_to_fit();
    notnot_infoConstrains.shrink_to_fit();
    posCorrectFlag.shrink_to_fit();
    posSensedFlag.shrink_to_fit();
    objectLocationVerified.shrink_to_fit();
    objectLocationInferredByMustNear.shrink_to_fit();
    objectInsideVerified.shrink_to_fit();
    containerStateVerified.shrink_to_fit();
    errorlist.shrink_to_fit();
    lock_by_mustnear.shrink_to_fit();
    mustNearComponent.shrink_to_fit();

    // 优化动态数组内存
    goto_cons.shrink_to_fit();
    putdown1_cons.shrink_to_fit();
    open_cons.shrink_to_fit();
    close_cons.shrink_to_fit();
    pickup_cons.shrink_to_fit();
    givehuman_cons.shrink_to_fit();
    fromplate_cons.shrink_to_fit();
    toplate_cons.shrink_to_fit();
    rightlocation.shrink_to_fit();

    // 优化二维数组内存
    for (auto& row : putin_cons) row.shrink_to_fit();
    for (auto& row : takeout_cons) row.shrink_to_fit();
    for (auto& row : putdown_cons) row.shrink_to_fit();
    for (auto& row : move_cons) row.shrink_to_fit();
    for (auto& row : mustnear_cons) row.shrink_to_fit();

    // 优化位置感知数据内存
    for (auto& loc_info : locationSensedObjects) {
        loc_info.object_ids.shrink_to_fit();
    }
    locationSensedObjects.shrink_to_fit();

    cout << "#(RDFW): Memory optimization completed" << endl;
}



 void split_string(vector<string> &out, const string &str_source, char mark)
 {
     int last = 0;
     for (int i = 0; i < str_source.size(); i++)
     {
         if (str_source[i] == mark)
         {
             out.push_back(str_source.substr(last, i - last));
             last = i + 1;
         }
     }
     if (last != str_source.size())
         out.push_back(str_source.substr(last));
 }





 Instruction::Instruction() {}

 const char* Instruction::ValidationStatusName(ValidationStatus status)
 {
     switch (status) {
         case ValidationStatus::UNCHECKED: return "UNCHECKED";
         case ValidationStatus::VALID: return "VALID";
         case ValidationStatus::REPAIRED: return "REPAIRED";
         case ValidationStatus::INFERRED: return "INFERRED";
         case ValidationStatus::REJECTED: return "REJECTED";
     }
     return "UNKNOWN";
 }

 Instruction::Instruction(const shared_ptr<SyntaxNode> &node, const shared_ptr<RDFW> &rdfw) // 改
 {
     structuredSource = true;
     auto invalidate = [&](const string& message) {
         syntaxValid = false;
         validationStatus = ValidationStatus::REJECTED;
         validationMessage = message;
     };

     if (!node || node->sons.size() != 2 || !node->sons[0] || !node->sons[1]) {
         invalidate("instruction must contain one action and one :cond node");
         return;
     }
     if (node->sons[1]->value != ":cond") {
         invalidate("instruction has no valid :cond node");
         return;
     }

     const vector<string> action = SplitWhitespace(node->sons[0]->value);
     if (action.empty()) {
         invalidate("instruction action is empty");
         return;
     }
     behave = action[0];
     declaredParameters.assign(action.begin() + 1, action.end());

     for (const auto &n : node->sons[1]->sons) // sons[1]是cond节点
     {
         if (!n || !n->sons.empty()) {
             invalidate("condition contains an empty or nested node");
             return;
         }
         const vector<string> fields = SplitWhitespace(n->value);
         if (fields.size() != 3) {
             invalidate("condition must contain exactly three fields");
             return;
         }
         if (fields[1] != "X" && fields[1] != "Y") {
             invalidate("condition binds an unknown variable");
             return;
         }
         if (std::find(declaredParameters.begin(), declaredParameters.end(),
                       fields[1]) == declaredParameters.end()) {
             invalidate("condition variable is not declared by the action");
             return;
         }

         Condition *condition = &conditionX;
         if (fields[1] == "Y") {
             isUseY = true;
             condition = &conditionY;
         }

         if (fields[0] == "color") {
             if (!condition->color.empty() && condition->color != fields[2]) {
                 invalidate("conflicting color conditions");
                 return;
             }
             condition->color = fields[2];
         } else if (fields[0] == "sort") {
             if (!condition->sort.empty() && condition->sort != fields[2]) {
                 invalidate("conflicting sort conditions");
                 return;
             }
             condition->sort = fields[2];
         } else if (fields[0] == "type") {
             if (fields[2] != "object" && fields[2] != "small" &&
                 fields[2] != "big" && fields[2] != "container") {
                 invalidate("unknown condition object type");
                 return;
             }
             if (!condition->declared_type.empty() &&
                 condition->declared_type != fields[2]) {
                 invalidate("conflicting type conditions");
                 return;
             }
             condition->declared_type = fields[2];
         } else if (fields[0] == "id") {
             int object_id = 0;
             if (!ParseBoundedInt(fields[2], 1,
                                  static_cast<int>(MAX_OBJECT_ID), object_id)) {
                 invalidate("condition contains an invalid object id");
                 return;
             }
             if (condition->has_explicit_id && condition->object_id != object_id) {
                 invalidate("conflicting id conditions");
                 return;
             }
             condition->has_explicit_id = true;
             condition->object_id = object_id;
         } else {
             invalidate("unknown condition predicate");
             return;
         }
     }

     SearchConditionObject(rdfw);
 }


 ostream &operator<<(ostream &os, const Instruction &instr)
 {
     os << instr.ToString();
     return os;
 }

 ostream &operator<<(ostream &os, shared_ptr<SyntaxNode> sn)
 {
     if (!sn) return os << "<null syntax node>|" << endl;
     static int layer = 0;
     for (int i = 0; i < layer; i++)
         os << "-";
     os << sn->value << '|' << endl;
     layer++;
     for (int i = 0; i < sn->sons.size(); i++)
     {
         os << sn->sons[i];
     }
     layer--;
     return os;
 }

 ostream &operator<<(ostream &os, shared_ptr<Object> obj)
 {
     if (dynamic_pointer_cast<SmallObject>(obj) != nullptr)
     {
         os << "SmallObject " << dynamic_pointer_cast<SmallObject>(obj)->ToString();
     }
     else if (dynamic_pointer_cast<Robot>(obj) != nullptr)
     {
         os << "this " << dynamic_pointer_cast<Robot>(obj)->ToString();
     }
     else if (dynamic_pointer_cast<BigObject>(obj) != nullptr)
     {
         if (dynamic_pointer_cast<Container>(obj) != nullptr)
         {
             os << "Container " << dynamic_pointer_cast<Container>(obj)->ToString();
         }
         else
         {
             os << "BigObject  " << dynamic_pointer_cast<BigObject>(obj)->ToString();
         }
    }
    return os;
}

 /**
  * @brief Must Near 纠错与补全（极简版）
  * - 用并查集把 near 约束形成的对象连通分量合并
  * - 对每个分量按“已知位置”的多数票决定组位置
  * - 如果启用上锁，则把组内所有对象位置改为该“组位置”，并标记为锁定
  * 依赖成员：
  *   objects, notnot_infoConstrains, lock_by_mustnear, enable_near_correction, enable_must_lock, UNKNOWN
  */
void RDFW::BuildMustNearRelations() {
    const size_t object_count = objects.size();
    mustnear_cons.assign(object_count, vector<int>(object_count, 0));
    mustNearComponent.assign(object_count, UNKNOWN);
    lock_by_mustnear.assign(object_count, false);
    hold_mustnear = false;
    plate_mustnear = false;

    if (object_count == 0) return;

    vector<int> parent(object_count);
    vector<int> component_size(object_count, 1);
    std::iota(parent.begin(), parent.end(), 0);

    auto find_root = [&](int id) {
        while (parent[id] != id) {
            parent[id] = parent[parent[id]];
            id = parent[id];
        }
        return id;
    };
    auto unite = [&](int lhs, int rhs) {
        lhs = find_root(lhs);
        rhs = find_root(rhs);
        if (lhs == rhs) return;
        if (component_size[lhs] < component_size[rhs]) std::swap(lhs, rhs);
        parent[rhs] = lhs;
        component_size[lhs] += component_size[rhs];
    };

    for (const auto& cons : notnot_infoConstrains) {
        if (!cons.IsUsable() ||
            (cons.behave != "near" && cons.behave != "nextto")) continue;

        // 自然语言中的 every 会让 X/Y 各包含多个对象；must-near 应覆盖
        // 两侧对象集合的笛卡尔积，而不是只读取 X[0]/Y[0]。
        for (const auto& x : cons.X) {
            if (!x || x->id <= 0 || static_cast<size_t>(x->id) >= object_count) continue;
            for (const auto& y : cons.Y) {
                if (!y || y->id <= 0 || static_cast<size_t>(y->id) >= object_count ||
                    x->id == y->id) continue;
                ++mustnear_cons[x->id][y->id];
                ++mustnear_cons[y->id][x->id];
                unite(x->id, y->id);
                LOG(GREEN "[MustNear] relation obj %d <-> obj %d\n" RESET,
                    x->id, y->id);
            }
        }
    }

    for (size_t id = 1; id < object_count; ++id) {
        bool has_relation = false;
        for (size_t other = 1; other < object_count; ++other) {
            if (mustnear_cons[id][other] > 0) {
                has_relation = true;
                break;
            }
        }
        if (has_relation) mustNearComponent[id] = find_root(static_cast<int>(id));
    }

    hold_mustnear = hold_id > 0 && static_cast<size_t>(hold_id) < mustNearComponent.size() &&
                    mustNearComponent[hold_id] != UNKNOWN;
    plate_mustnear = plate_id > 0 && static_cast<size_t>(plate_id) < mustNearComponent.size() &&
                     mustNearComponent[plate_id] != UNKNOWN;
}

void RDFW::RefreshMustNearConstraintState(bool propagate_evidence) {
    const size_t object_count = objects.size();
    if (mustNearComponent.size() != object_count) return;

    lock_by_mustnear.assign(object_count, false);
    unordered_map<int, vector<unsigned int>> groups;
    for (size_t id = 1; id < object_count; ++id) {
        if (mustNearComponent[id] != UNKNOWN && objects[id])
            groups[mustNearComponent[id]].push_back(static_cast<unsigned int>(id));
    }

    for (const auto& entry : groups) {
        const vector<unsigned int>& members = entry.second;
        map<int, int> direct_votes;
        map<int, int> candidate_votes;

        for (unsigned int id : members) {
            EnsureEvidenceCapacity(id);
            const int loc = objects[id]->location;
            if (loc == UNKNOWN) continue;
            if (objectLocationVerified[id] && !objectLocationInferredByMustNear[id])
                ++direct_votes[loc];
            if (!objectLocationInferredByMustNear[id]) ++candidate_votes[loc];
        }

        const map<int, int>& votes = direct_votes.empty() ? candidate_votes : direct_votes;
        int chosen_location = UNKNOWN;
        int best_count = 0;
        bool tied = false;
        for (const auto& vote : votes) {
            if (vote.second > best_count) {
                chosen_location = vote.first;
                best_count = vote.second;
                tied = false;
            } else if (vote.second == best_count) {
                tied = true;
            }
        }
        if (tied) chosen_location = UNKNOWN;

        bool conflicts_with_evidence = false;
        if (chosen_location != UNKNOWN) {
            for (unsigned int id : members) {
                // Sense 未发现对象形成明确的反证，不能再由 must-near 把它
                // 强行写回同一位置。
                if (objects[id]->unable_site == chosen_location) {
                    conflicts_with_evidence = true;
                    break;
                }
                if (objectLocationVerified[id] && !objectLocationInferredByMustNear[id] &&
                    objects[id]->location != UNKNOWN && objects[id]->location != chosen_location) {
                    conflicts_with_evidence = true;
                    break;
                }
            }
        }

        if (propagate_evidence && chosen_location != UNKNOWN && !conflicts_with_evidence) {
            const bool anchored = !direct_votes.empty();
            for (unsigned int id : members) {
                if (objects[id]->location == chosen_location &&
                    objectLocationVerified[id] && !objectLocationInferredByMustNear[id]) continue;

                objects[id]->location = chosen_location;
                MarkDirectLocationEvidence(id, anchored,
                    anchored ? EvidenceSource::CONSTRAINT_DERIVED
                             : EvidenceSource::CONSTRAINT_HEURISTIC);

                // 容器位置变化时，只同步确定在该容器内的物体；不修改
                // SmallObject::inside/on，near 本身不代表包含或承载关系。
                auto container = dynamic_pointer_cast<Container>(objects[id]);
                if (container) {
                    for (const auto& item : container->smallObjectsInside) {
                        if (!item || item->inside != container->id) continue;
                        item->location = chosen_location;
                        const bool location_entailed = anchored && IsInsideVerified(item->id);
                        MarkDirectLocationEvidence(item->id, location_entailed,
                            location_entailed ? EvidenceSource::CONSTRAINT_DERIVED
                                             : EvidenceSource::CONSTRAINT_HEURISTIC);
                    }
                }
                LOG(GREEN "[MustNear] inferred obj %u at location %d\n" RESET,
                    id, chosen_location);
            }
        }

        int component_location = UNKNOWN;
        bool component_consistent = !conflicts_with_evidence;
        for (unsigned int id : members) {
            const int loc = objects[id]->location;
            if (loc == UNKNOWN) {
                component_consistent = false;
                break;
            }
            if (component_location == UNKNOWN) component_location = loc;
            else if (component_location != loc) {
                component_consistent = false;
                break;
            }
        }

        if (enable_must_lock && component_consistent && component_location != UNKNOWN) {
            for (unsigned int id : members) lock_by_mustnear[id] = true;
        } else if (chosen_location == UNKNOWN || conflicts_with_evidence) {
            LOG(YELLOW "[MustNear] component %d has conflicting/insufficient evidence; not propagated\n" RESET,
                entry.first);
        }
    }
}

void RDFW::ApplyMustNearConstraintCorrection() {
    if (!enable_near_correction) {
        LOG(YELLOW "[MustNear] disabled\n" RESET);
        return;
    }
    BuildMustNearRelations();
    RefreshMustNearConstraintState(true);
    LOG(GREEN "[MustNear] correction done.\n" RESET);
}


void RDFW::ApplyMustInConstraintCorrection() {
    const int numObjs = static_cast<int>(objects.size());
    if (numObjs <= 0) return;

    // 采集 mustin（或同义）约束：记录每个物体唯一的容器
    std::vector<int> obj_to_cont(numObjs, -1);
    for (const auto& cons : notnot_infoConstrains) {
        if (cons.IsUsable() && (cons.behave == "inside" || cons.behave == "in") &&
            !cons.X.empty() && cons.X[0] && !cons.Y.empty() && cons.Y[0])
        {
            const int x = static_cast<int>(cons.X[0]->id);
            const int y = static_cast<int>(cons.Y[0]->id);
            if (x < 0 || x >= numObjs || y < 0 || y >= numObjs) continue;
            cout<<"x: "<<x<<" y: "<<y<<endl;

            obj_to_cont[x] = y;

            cout<<"obj_to_cont[x]: "<<obj_to_cont[x]<<endl;

            // 直接同步smallObject和container信息
            if (objects[x]) {
                if (auto sm = std::dynamic_pointer_cast<SmallObject>(objects[x])) {
                    cout<<"sm->inside: "<<sm->inside<<endl;
                    // 移除在原容器中的记录
                    if (sm->inside != UNKNOWN && sm->inside != y) {
                        if (sm->inside >= 0 && sm->inside < numObjs) {
                            if (auto old_cont = std::dynamic_pointer_cast<Container>(objects[sm->inside])) {
                                auto& vec = old_cont->smallObjectsInside;
                                vec.erase(std::remove_if(vec.begin(), vec.end(),
                                    [&](const std::shared_ptr<SmallObject>& ptr) {
                                        return ptr && ptr->id == sm->id;
                                    }), vec.end());
                            }
                        }
                    }
                    sm->inside = y;
                    SetInsideEvidence(x, true, EvidenceSource::CONSTRAINT_DERIVED);
                    // 添加到目标容器（避免重复）
                    if (objects[y]) {
                        if (auto cont = std::dynamic_pointer_cast<Container>(objects[y])) {
                            bool exists = false;
                            for (auto&& v : cont->smallObjectsInside) {
                                if (v && v->id == sm->id) { exists = true; break; }
                            }
                            if (!exists)
                                cont->smallObjectsInside.push_back(sm);
                        }
                    }
                }
            }
            // 同步位置（取容器已知位置为准，否则不强制）
            if (objects[x] && objects[y]) {
                int y_loc = objects[y]->location;
                if (y_loc != UNKNOWN) {
                    objects[x]->location = y_loc;
                    const bool location_entailed = IsLocationVerified(y);
                    MarkDirectLocationEvidence(x, location_entailed,
                        location_entailed ? EvidenceSource::CONSTRAINT_DERIVED
                                         : EvidenceSource::CONSTRAINT_HEURISTIC);
                    LOG(GREEN "[MustIn] (direct) set obj %d @ %d (in %d)\n" RESET, x, y_loc, y);
                }
            }
        }
    }

    // INSERT_YOUR_CODE
    // 处理 notinside 约束的纠错与补全
    // 遍历所有 notnot_infoConstrains，查找 behave == "notinside" 的约束
    for (const auto& cons : not_infoConstrains) {
        if (cons.behave != "inside"&&cons.behave != "in") continue;
        if (cons.X.empty() || cons.Y.empty()) continue;
        int x = static_cast<int>(cons.X[0]->id);   // smallObject id
        int y = static_cast<int>(cons.Y[0]->id);   // container id
        if (x < 0 || x >= numObjs || y < 0 || y >= numObjs) continue;
        // 防御性检查
        auto smObj = std::dynamic_pointer_cast<SmallObject>(objects[x]);
        if (!smObj) continue;
        // 如果x当前就在y里，需要移除
        if (smObj->inside == y) {
            // 修改smallObject的inside信息
            smObj->inside = UNKNOWN;
            int old_loc = smObj->location;
            smObj->location = UNKNOWN;
            // 同时尝试从container的smallObjectsInside中移除
            auto cont = std::dynamic_pointer_cast<Container>(objects[y]);
            if (cont) {
                auto& vec = cont->smallObjectsInside;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                    [&](const std::shared_ptr<SmallObject>& ptr){
                        return ptr && ptr->id == smObj->id;
                    }), vec.end());
            }
            LOG(GREEN "[NotInside] remove obj %d from container %d\n" RESET, x, y);
        }
        // 如果inside本来就不是y，例如 UNKNOWN 或其他容器，则无需操作
    }

    LOG(GREEN "[MustIn] correction done.\n" RESET);
}

// open和close的纠正和补全，只处理not_infoConstrains和notnot_infoConstrains

void RDFW::ApplyOpenCloseCorrection() {
    // 遍历所有容器，对opened/closed的约束进行处理
    for (const auto& obj : objects) {
        if (!obj) continue;
        auto cont = std::dynamic_pointer_cast<Container>(obj);
        if (!cont) continue;

        // 收集是否存在opened/closed的约束
        bool must_open = false;
        bool must_closed = false;
        bool cons_not_open = false;
        bool cons_not_closed = false;

        // notnot_infoConstrains：必须成立
        for (const auto& cons : notnot_infoConstrains) {
            if (cons.X.empty()) continue;
            if (cons.X[0]->id != obj->id) continue;
            if (cons.behave == "opened") must_open = true;
            else if (cons.behave == "closed") must_closed = true;
        }
        // not_infoConstrains：必须不成立
        for (const auto& cons : not_infoConstrains) {
            if (cons.X.empty()) continue;
            if (cons.X[0]->id != obj->id) continue;
            if (cons.behave == "opened") cons_not_open = true;
            else if (cons.behave == "closed") cons_not_closed = true;
        }

        // 优先满足冲突最小原则
        // 如果必须open且不能closed
        if (must_open && !cons_not_open && !must_closed) {
            if (!cont->isOpen) {
                cont->isOpen = true;
                LOG(GREEN "[AutoOpenClose] set container %d open (by notnot)\n" RESET, cont->id);
            }
        }
        // 如果必须closed且不能open
        else if (must_closed && !cons_not_closed && !must_open) {
            if (cont->isOpen) {
                cont->isOpen = false;
                LOG(GREEN "[AutoOpenClose] set container %d closed (by notnot)\n" RESET, cont->id);
            }
        }
        // 如果not要求not opened，则要关闭
        else if (cons_not_open && !must_open) {
            if (cont->isOpen) {
                cont->isOpen = false;
                LOG(GREEN "[AutoOpenClose] set container %d closed (by not constraint)\n" RESET, cont->id);
            }
        }
        // 如果not要求not closed，则要打开
        else if (cons_not_closed && !must_closed) {
            if (!cont->isOpen) {
                cont->isOpen = true;
                LOG(GREEN "[AutoOpenClose] set container %d open (by not constraint)\n" RESET, cont->id);
            }
        }
        // 冲突：有not和notnot都要求open/closed
        else if ((must_open && cons_not_open) || (must_closed && cons_not_closed) || (must_open && must_closed)) {
            // 默认优先closed
            cont->isOpen = false;
            LOG(YELLOW "[AutoOpenClose] conflict: container %d open/close constraints conflict, set to closed\n" RESET, cont->id);
        }
        // 无约束不处理
        if (must_open || must_closed || cons_not_open || cons_not_closed) {
            const bool contradictory = (must_open && must_closed) ||
                (must_open && cons_not_open) || (must_closed && cons_not_closed);
            SetContainerEvidence(cont->id, !contradictory,
                contradictory ? EvidenceSource::CONSTRAINT_HEURISTIC
                              : EvidenceSource::CONSTRAINT_DERIVED);
        }
    }
}
