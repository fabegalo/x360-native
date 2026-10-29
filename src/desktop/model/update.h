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
};
// Package suffix of this build: <system>-<architecture>, e.g. windows-x64,
// linux-x64, macos-arm64. Each published package serves exactly one of them.
std::string PackagePlatform();
// Highest published (non-draft) release above `current` whose tag is v<version> and
// that carries both x360-native-<version>-<platform>.zip and its .sha256. URLs are
// rebuilt from the pinned repository, never taken from the response.
enum class UpdateChannel { Stable, Preview };
std::optional<AvailableUpdate> SelectUpdate(const std::string& releases_json, const std::string& current,
                                           UpdateChannel channel = UpdateChannel::Stable,
                                           const std::string& platform = PackagePlatform());
// The checksum file must read "<sha256>  <archive name>" and match the archive.
void VerifyArchive(const std::filesystem::path& archive, const std::filesystem::path& checksum);
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
}
