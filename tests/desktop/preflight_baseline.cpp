// The 99a7764 verifier has no parallel read-ahead API. Only the explicit
// historical comparison target links these adapters; all original gates run.
#include "launch_preflight.h"
#include "digest_batch.h"
namespace x360n {
void PrimeLaunchVerification(const ProjectPaths&, const std::vector<desktop::Target>&, const std::string&) {}
void PrimeVerificationDigests(std::span<const std::filesystem::path>, unsigned) {}
}
