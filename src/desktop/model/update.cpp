#include "update.h"
#include "catalog.h"
#include "sha256.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
constexpr const char* kRepository = "https://github.com/fabegalo/x360-native";
const std::regex& VersionPattern() {
  static const std::regex pattern(R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z.-]+)?)");
  return pattern;
}
bool Numeric(const std::string& s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; }
int CompareNumbers(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  return a.compare(b) < 0 ? -1 : a.compare(b) > 0 ? 1 : 0;
}
std::vector<std::string> Split(const std::string& s) {
  std::vector<std::string> parts; size_t start = 0;
  for (size_t dot; (dot = s.find('.', start)) != std::string::npos; start = dot + 1) parts.push_back(s.substr(start, dot - start));
  parts.push_back(s.substr(start)); return parts;
}
std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Arquivo da atualização ausente: " + Utf8(file.filename()));
  return {std::istreambuf_iterator<char>(in), {}};
}
}
std::optional<int> CompareVersions(const std::string& left, const std::string& right) {
  std::smatch a, b;
  if (!std::regex_match(left, a, VersionPattern()) || !std::regex_match(right, b, VersionPattern())) return std::nullopt;
  for (int i = 1; i <= 3; ++i) if (int c = CompareNumbers(a[i], b[i])) return c;
  // A prerelease precedes its release; identifiers compare numerically, then by ASCII.
  if (!a[4].matched || !b[4].matched) return a[4].matched == b[4].matched ? 0 : a[4].matched ? -1 : 1;
  const auto x = Split(a[4]), y = Split(b[4]);
  for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
    const bool nx = Numeric(x[i]), ny = Numeric(y[i]);
    const int c = nx && ny ? CompareNumbers(x[i], y[i]) : nx != ny ? (nx ? -1 : 1) : x[i].compare(y[i]) < 0 ? -1 : x[i].compare(y[i]) > 0 ? 1 : 0;
    if (c) return c;
  }
  return x.size() == y.size() ? 0 : x.size() < y.size() ? -1 : 1;
}
std::string PackagePlatform() {
#if defined(_WIN32)
  const std::string system = "windows";
#elif defined(__APPLE__)
  const std::string system = "macos";
#else
  const std::string system = "linux";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
  return system + "-arm64";
#else
  return system + "-x64";
#endif
}
std::string PackageArchiveExtension(const std::string& platform) { return platform.starts_with("linux-") ? ".tar.gz" : ".zip"; }
std::optional<AvailableUpdate> SelectUpdate(const std::string& releases_json, const std::string& current, UpdateChannel channel,
                                           const std::string& platform) {
  if (platform.empty() || platform.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
    throw std::runtime_error("Plataforma de pacote inválida.");
  const auto releases = nlohmann::json::parse(releases_json);
  if (!releases.is_array()) throw std::runtime_error("Resposta inesperada da lista de versões.");
  std::optional<AvailableUpdate> best;
  for (const auto& release : releases) {
    if (!release.is_object() || release.value("draft", true) || !release.contains("tag_name") || !release["tag_name"].is_string()) continue;
    const std::string tag = release["tag_name"];
    if (tag.size() < 2 || tag[0] != 'v') continue;
    const auto version = tag.substr(1);
    if (channel == UpdateChannel::Stable &&
        (release.value("prerelease", false) || version.find('-') != std::string::npos)) continue;
    // Build metadata would put '+' in file names and URLs; published versions never use it.
    if (version.find('+') != std::string::npos) continue;
    const auto newer = CompareVersions(version, current);
    if (!newer || *newer <= 0 || (best && CompareVersions(version, best->version).value_or(0) <= 0)) continue;
    const std::string package = "x360-native-" + version + "-" + platform;
    const auto archive = PackageArchiveExtension(platform);
    const std::string archive_name = package + archive;
    const nlohmann::json* archive_asset = nullptr;
    bool legacy_checksum = false;
    if (release.contains("assets") && release["assets"].is_array())
      for (const auto& asset : release["assets"]) {
        if (!asset.is_object() || !asset.contains("name") || !asset["name"].is_string()) continue;
        const std::string name = asset["name"];
        if (name == archive_name) archive_asset = &asset;
        if (name == archive_name + ".sha256") legacy_checksum = true;
      }
    if (!archive_asset) continue;
    std::string trusted_sha256;
    if (archive_asset->contains("digest") && !(*archive_asset)["digest"].is_null()) {
      if (!(*archive_asset)["digest"].is_string()) continue;
      const std::string field = (*archive_asset)["digest"];
      if (!field.empty()) {
        if (field.size() != 71 || !field.starts_with("sha256:") ||
            field.substr(7).find_first_not_of("0123456789abcdef") != std::string::npos)
          continue;  // Reject an invalid GitHub digest; never install unchecked bytes.
        trusted_sha256 = field.substr(7);
      }
    }
    // Modern Releases publish only the archive. Older Releases have a .sha256
    // sidecar and can still be used if the API does not provide an asset digest.
    if (trusted_sha256.empty() && !legacy_checksum) continue;
    const std::string base = std::string(kRepository) + "/releases/download/" + tag + "/" + package;
    best = AvailableUpdate{version, package, base + archive,
                           trusted_sha256.empty() ? base + archive + ".sha256" : "",
                           std::string(kRepository) + "/releases/tag/" + tag, trusted_sha256};
  }
  return best;
}
void VerifyArchive(const fs::path& archive, const fs::path& checksum) {
  auto text = ReadText(checksum);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  const auto digest = DigestFile(archive);
  if (!digest || text != digest->sha256 + "  " + Utf8(archive.filename()))
    throw std::runtime_error("O download da atualização não confere com o SHA-256 publicado. Nada foi alterado.");
}
void VerifyArchiveDigest(const fs::path& archive, const std::string& trusted_sha256) {
  if (trusted_sha256.size() != 64 ||
      trusted_sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("SHA-256 inválido nos metadados da atualização. Nada foi alterado.");
  const auto digest = DigestFile(archive);
  if (!digest || digest->sha256 != trusted_sha256)
    throw std::runtime_error("O download da atualização não confere com o SHA-256 do GitHub. Nada foi alterado.");
}
}
