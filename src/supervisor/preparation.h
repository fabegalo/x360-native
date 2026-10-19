#pragma once
#include "paths.h"
#include <unordered_map>
#include <unordered_set>

namespace x360n {
// Presentation only, so a listing can tell ready from stale without hashing:
// the receipt's identity and SDK revision, and every recorded file's presence
// and modification time against the receipt's own. It never authorizes a
// preparation or launch; CheckPreparation (hashes, no timestamps) does.
enum class ReceiptHint { Unknown, Current, Stale };
class ReceiptHints {
 public:
  explicit ReceiptHints(ProjectPaths paths) : paths_(std::move(paths)) {}
  ReceiptHint Check(const std::string& title, const std::filesystem::path& manifest,
                    const std::filesystem::path& exe);
 private:
  ProjectPaths paths_;
  std::optional<std::string> head_;
  // Receipts share most of the SDK closure; each file is examined once.
  std::unordered_map<std::string, std::optional<std::filesystem::file_time_type>> times_;
  std::unordered_set<std::string> listed_;
};
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
