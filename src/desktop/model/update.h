#pragma once
// Application self-update from the project's GitHub Releases. Platform-neutral:
// downloading and extraction run as separate processes chosen by the frontend.
#include <filesystem>
#include <optional>
#include <string>

namespace x360n::desktop {
inline constexpr const char* kReleasesApi = "https://api.github.com/repos/fabegalo/x360-native/releases?per_page=30";
// Negative, zero or positive by Semantic Versioning precedence; nullopt if either is invalid.
std::optional<int> CompareVersions(const std::string& left, const std::string& right);
struct AvailableUpdate {
  std::string version, package, archive_url, checksum_url, page_url;
};
// Highest published (non-draft) release above `current` whose tag is v<version> and
// that carries both x360-native-<version>-windows-x64.zip and its .sha256. URLs are
// rebuilt from the pinned repository, never taken from the response.
std::optional<AvailableUpdate> SelectUpdate(const std::string& releases_json, const std::string& current);
// The checksum file must read "<sha256>  <archive name>" and match the archive.
void VerifyArchive(const std::filesystem::path& archive, const std::filesystem::path& checksum);
// Replaces the application files of `package` with the verified package extracted at
// `staged` (same version), keeping data/. Current files move to `rollback` (replaced),
// and every move is undone if any step fails.
void ApplyUpdate(const std::filesystem::path& package, const std::filesystem::path& staged,
                 const std::string& version, const std::filesystem::path& rollback);
}
