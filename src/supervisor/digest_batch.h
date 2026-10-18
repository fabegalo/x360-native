#pragma once
#include "sha256.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <span>

namespace x360n {
class VerificationCancelled : public std::runtime_error {
 public:
  VerificationCancelled() : std::runtime_error("Verification cancelled; original files preserved.") {}
};
void CheckVerificationCancellation();
// Ordinary paths may reuse pinned ancestor identities within the batch. Links
// and hosts without equivalent exclusion retain full filesystem resolution.
std::filesystem::path VerificationPath(const std::filesystem::path& path);
// True only while this exact ordinary name and all ancestors are held against
// writes/replacement. Does not authenticate bytes or cache absent paths.
bool HasProtectedVerificationPath(const std::filesystem::path& path);
// Read ahead only inside the active transaction. Each result retains the same
// write/delete exclusion as DigestFile; callers must still compare its SHA-256.
// At most four workers; non-Windows hosts keep the serial verification path.
void PrimeVerificationDigests(std::span<const std::filesystem::path> paths, unsigned workers = 4);
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
