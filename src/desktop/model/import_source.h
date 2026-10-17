#pragma once
#include <filesystem>
#include <functional>
namespace x360n::desktop {
// Owns only this operation's private extraction. A crash leaves an unused
// staging directory; it can never become installed or prepared content.
class ImportSource {
 public:
  ImportSource(const std::filesystem::path& original,
               const std::filesystem::path& extractor,
               const std::filesystem::path& logs,
               const std::function<bool()>& cancelled = {},
               const std::function<void()>& extracting = {});
  ~ImportSource();
  ImportSource(const ImportSource&) = delete;
  ImportSource& operator=(const ImportSource&) = delete;
  const std::filesystem::path& path() const { return source_; }
  std::filesystem::path identity_path() const;
  bool extracted() const { return !temporary_.empty(); }
 private:
  std::filesystem::path source_, temporary_;
};
}
