#pragma once
// Application self-update from the project's GitHub Releases. Platform-neutral:
// downloading and extraction run as separate processes chosen by the frontend.
#include <filesystem>
#include <optional>
#include <string>
#include <functional>

namespace x360n::desktop {
inline constexpr const char* kReleasesApi = "https://api.github.com/repos/fabegalo/x360-native/releases?per_page=30";
// Negative, zero or positive by Semantic Versioning precedence; nullopt if either is invalid.
std::optional<int> CompareVersions(const std::string& left, const std::string& right);
struct AvailableUpdate {
  std::string version, package, archive_url, checksum_url, page_url;
  // SHA-256 obtained from GitHub's signed HTTPS release-asset metadata.
  // Empty for older Releases that require a standalone .sha256 sidecar.
  std::string archive_sha256;
};
// Package suffix of this build: <system>-<architecture>, e.g. windows-x64,
// linux-x64, macos-arm64. Each published package serves exactly one of them.
std::string PackagePlatform();
// Archive of a published package: .tar.gz on Linux (extracted by the system tar),
// .zip elsewhere.
std::string PackageArchiveExtension(const std::string& platform = PackagePlatform());
// Highest published (non-draft) release above `current` whose tag is v<version>.
// A valid GitHub asset digest is required for archive-only releases; legacy
// releases may use the separately published .sha256 instead. URLs are rebuilt
// from the pinned repository, never taken from untrusted API response fields.
enum class UpdateChannel { Stable, Preview };
std::optional<AvailableUpdate> SelectUpdate(const std::string& releases_json, const std::string& current,
                                           UpdateChannel channel = UpdateChannel::Stable,
                                           const std::string& platform = PackagePlatform());
// The checksum file must read "<sha256>  <archive name>" and match the archive.
void VerifyArchive(const std::filesystem::path& archive, const std::filesystem::path& checksum);
// Modern GitHub Release assets expose SHA-256 directly via assets[].digest.
// Both paths fail closed before extraction or any application file replacement.
void VerifyArchiveDigest(const std::filesystem::path& archive, const std::string& trusted_sha256);
// Persistent transaction in a new direct child of package/data/updates. The old
// package and recovery supervisor survive until explicit recovery/confirmation.
// checkpoint is optional fault instrumentation; production leaves it empty.
using UpdateCheckpoint = std::function<void(const char*)>;
void ApplyUpdate(const std::filesystem::path& package, const std::filesystem::path& staged,
                 const std::string& version, const std::filesystem::path& transaction,
                 const UpdateCheckpoint& checkpoint = {});
void RecoverUpdate(const std::filesystem::path& package, const std::filesystem::path& transaction,
                   const UpdateCheckpoint& checkpoint = {});
void ConfirmUpdates(const std::filesystem::path& package, const std::string& version);
// Recovery entry written beside the package during an update: recover-update.cmd
// on Windows, recover-update.sh elsewhere.
std::string RecoveryScriptName();
}
