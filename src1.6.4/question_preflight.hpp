#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace _home {

struct PreflightCounts {
    std::size_t raw = 0;
    std::size_t unique = 0;
    std::size_t duplicates = 0;
    std::size_t rejected = 0;
};

struct QuestionPreflightReport {
    PreflightCounts tasks;
    PreflightCounts constraints;
    std::size_t rejected_infos = 0;
    std::vector<std::string> world_errors;
    std::vector<std::string> world_warnings;
    bool safe() const { return world_errors.empty(); }
};

} // namespace _home
