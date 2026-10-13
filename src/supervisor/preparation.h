#pragma once
#include "paths.h"

namespace x360n {
// A receipt binds one native build to its content and build dependency closure.
// No timestamp cache: replacement with preserved size/mtime must be detected.
std::filesystem::path PreparationReceipt(const std::filesystem::path& exe);
std::optional<std::string> CheckPreparation(const ProjectPaths& paths,
    const std::string& title, const std::filesystem::path& manifest,
    const std::filesystem::path& exe);
// Title launches use the same check even through the diagnostic `run` command.
// Non-title diagnostic tools retain their existing launch policy.
std::optional<std::string> CheckPreparedLaunch(const ProjectPaths& paths,
    const std::filesystem::path& exe);
}
