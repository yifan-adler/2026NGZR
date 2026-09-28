#include "rdfw.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace _home {
namespace {

std::string IdSet(const std::vector<std::shared_ptr<Object>>& objects) {
    std::set<int> ids;
    for (const auto& object : objects) ids.insert(object->id);
    std::ostringstream out;
    for (int id : ids) out << id << ',';
    return out.str();
}

bool IsModelledObject(const std::shared_ptr<Object>& object) {
    return object && !object->sort.empty() &&
           (std::dynamic_pointer_cast<SmallObject>(object) ||
            std::dynamic_pointer_cast<BigObject>(object));
}

} // namespace

std::string RDFW::SemanticInstructionKey(const Instruction& instruction,
                                        InstructionKind kind) {
    // Only schema-validated, registered bindings may reach this function.
    // Do NOT use positions: two objects at the same location remain two goals.
    std::string predicate = instruction.behave;
    if (predicate == "in") predicate = "inside";
    if (predicate == "nextto") predicate = "near";
    std::string x = IdSet(instruction.X);
    std::string y = instruction.isUseY ? IdSet(instruction.Y) : "";
    if (predicate == "near" && y < x) std::swap(x, y);
    // Keep kind/polarity and the entire quantified binding sets. Never flatten
    // every/all into singleton goals or equate open with not-closed, etc.
    return std::to_string(static_cast<int>(kind)) + ":" + predicate +
           ":X[" + x + "]:Y[" + y + "]";
}

bool RDFW::RunQuestionPreflight() {
    preflight_report = QuestionPreflightReport();
    auto world_error = [&](const std::string& message) {
        preflight_report.world_errors.push_back(message);
        LOG_ERROR("[Preflight][World] %s", message.c_str());
    };
    auto world_warning = [&](const std::string& message) {
        preflight_report.world_warnings.push_back(message);
        LOG("[Preflight][WorldWarning] %s", message.c_str());
    };

    if (objects.empty() || objects[0].get() != this || id != 0)
        world_error("robot identity is not objects[0]");
    if (location < 0 || location > MAX_LOCATION_ID)
        world_error("robot has no usable initial location");

    std::size_t humans = 0;
    std::map<int, int> big_locations;
    for (std::size_t i = 1; i < objects.size(); ++i) {
        const auto& object = objects[i];
        if (!object || object->id != static_cast<int>(i) || i > MAX_OBJECT_ID) {
            world_error("object registration mismatch at " + std::to_string(i));
            continue;
        }
        // A's bounded sparse-ID slots are harmless until referenced. Keep them,
        // but isolate any instruction that tries to use one below.
        const bool small = bool(std::dynamic_pointer_cast<SmallObject>(object));
        const bool big = bool(std::dynamic_pointer_cast<BigObject>(object));
        if (object->sort.empty() && !small && !big && object->location == UNKNOWN)
            continue;
        if (!IsModelledObject(object))
            world_warning("object lacks stable sort/runtime type; isolate bindings: " + std::to_string(i));
        if (object->location < UNKNOWN || object->location > MAX_LOCATION_ID)
            world_error("object location out of bounds: " + std::to_string(i));
        if (object->sort == "human") {
            ++humans;
            if (i != 1 || !big || std::dynamic_pointer_cast<Container>(object))
                world_warning("human differs from conventional object 1 layout");
        }
        // Stage 2 location descriptions can be wrong; collisions there are NOT
        // proof of an illegal question. Unknown locations are allowed in both.
        if (stage == 1 && big && object->location != UNKNOWN &&
            !big_locations.emplace(object->location, object->id).second)
            world_warning("multiple big objects at Stage 1 location " +
                        std::to_string(object->location));
    }
    if (humans != 1) world_warning("human count differs from conventional layout");

    auto normalize = [&](std::vector<Instruction>& list, InstructionKind kind,
                         PreflightCounts& counts, bool deduplicate) {
        counts.raw += list.size();
        std::set<std::string> seen;
        std::vector<Instruction> unique;
        unique.reserve(list.size());
        for (auto& instruction : list) {
            bool valid = ValidateInstruction(instruction, kind);
            for (const auto& object : instruction.X)
                valid = IsModelledObject(object) && valid;
            for (const auto& object : instruction.Y)
                valid = IsModelledObject(object) && valid;
            if (!valid) {
                ++counts.rejected;
                ++discarded_instruction_count;
                LOG_ERROR("[Preflight] isolated unmodelled/invalid binding: %s",
                          instruction.behave.c_str());
                continue;
            }
            const std::string key = SemanticInstructionKey(instruction, kind);
            if (deduplicate && !seen.insert(key).second) {
                ++counts.duplicates;
                if (deduplicate_input) {
                    LOG("[Preflight][Duplicate] %s policy=unique-goals", key.c_str());
                    continue;
                }
            }
            unique.push_back(instruction); // stable first occurrence, metadata intact
        }
        counts.unique += deduplicate ? seen.size() : unique.size();
        list.swap(unique);
    };

    normalize(tasks, InstructionKind::TASK, preflight_report.tasks, true);
    normalize(not_taskConstrains, InstructionKind::NOT_TASK_CONSTRAINT,
              preflight_report.constraints, true);
    normalize(not_infoConstrains, InstructionKind::NOT_INFO_CONSTRAINT,
              preflight_report.constraints, true);
    normalize(notnot_infoConstrains, InstructionKind::MUST_INFO_CONSTRAINT,
              preflight_report.constraints, true);
    PreflightCounts info_counts;
    normalize(infos, InstructionKind::INFO, info_counts, false);
    preflight_report.rejected_infos = info_counts.rejected;
    LOG("[Preflight] task raw=%zu unique=%zu duplicate=%zu rejected=%zu; "
        "constraint raw=%zu unique=%zu duplicate=%zu rejected=%zu; world_errors=%zu",
        preflight_report.tasks.raw, preflight_report.tasks.unique,
        preflight_report.tasks.duplicates, preflight_report.tasks.rejected,
        preflight_report.constraints.raw, preflight_report.constraints.unique,
        preflight_report.constraints.duplicates, preflight_report.constraints.rejected,
        preflight_report.world_errors.size());
    return preflight_report.safe();
}

} // namespace _home
