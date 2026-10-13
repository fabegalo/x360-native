#pragma once

#include <filesystem>
#include <fstream>

namespace x360n::content {

// Only a complete, closed manifest may be published. Both the staging file and
// the final name are acquired without replacement, including concurrent writers.
class ExtractionManifest {
 public:
  explicit ExtractionManifest(const std::filesystem::path& destination);
  ~ExtractionManifest();
  ExtractionManifest(const ExtractionManifest&) = delete;
  ExtractionManifest& operator=(const ExtractionManifest&) = delete;
  std::ostream& stream() { return stream_; }
  void Publish();

 private:
  std::filesystem::path destination_, temporary_;
  std::ofstream stream_;
};

}  // namespace x360n::content
