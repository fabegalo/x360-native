// Adapt the current benchmark/model to the original verifier's path semantics.
// Only the explicit baseline target links this file.
#include "digest_batch.h"
namespace x360n {
std::filesystem::path VerificationPath(const std::filesystem::path& path) {
  return std::filesystem::weakly_canonical(path);
}
bool HasProtectedVerificationPath(const std::filesystem::path&) { return false; }
}
