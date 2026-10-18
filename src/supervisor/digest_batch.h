#pragma once
#include "sha256.h"
#include <functional>
#include <memory>
#include <stdexcept>

namespace x360n {
class VerificationCancelled : public std::runtime_error {
 public:
  VerificationCancelled() : std::runtime_error("Verification cancelled; original files preserved.") {}
};
void CheckVerificationCancellation();
// Ordinary paths may reuse pinned ancestor identities within the batch. Links
// and hosts without equivalent exclusion retain full filesystem resolution.
std::filesystem::path VerificationPath(const std::filesystem::path& path);
// Reuse only within one synchronous verification transaction. On Windows each
// cached digest owns a read handle denying writes/deletion until destruction.
// No timestamp cache, persisted approvals, or reuse across process boundaries.
// Other platforms stream every call until an equivalent exclusion exists.
class DigestBatch {
 public:
  explicit DigestBatch(std::function<bool()> cancelled = {});
  ~DigestBatch();
  DigestBatch(const DigestBatch&) = delete;
  DigestBatch& operator=(const DigestBatch&) = delete;
  uint64_t files = 0, bytes = 0, hits = 0;
  struct State;
 private:
  std::unique_ptr<State> state_;
  friend std::optional<FileDigest> DigestFile(const std::filesystem::path&);
};
}
