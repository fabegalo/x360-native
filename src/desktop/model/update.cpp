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
std::optional<AvailableUpdate> SelectUpdate(const std::string& releases_json, const std::string& current) {
  const auto releases = nlohmann::json::parse(releases_json);
  if (!releases.is_array()) throw std::runtime_error("Resposta inesperada da lista de versões.");
  std::optional<AvailableUpdate> best;
  for (const auto& release : releases) {
    if (!release.is_object() || release.value("draft", true) || !release.contains("tag_name") || !release["tag_name"].is_string()) continue;
    const std::string tag = release["tag_name"];
    if (tag.size() < 2 || tag[0] != 'v') continue;
    const auto version = tag.substr(1);
    // Build metadata would put '+' in file names and URLs; published versions never use it.
    if (version.find('+') != std::string::npos) continue;
    const auto newer = CompareVersions(version, current);
    if (!newer || *newer <= 0 || (best && CompareVersions(version, best->version).value_or(0) <= 0)) continue;
    const std::string package = "x360-native-" + version + "-windows-x64";
    std::set<std::string> names;
    if (release.contains("assets") && release["assets"].is_array())
      for (const auto& asset : release["assets"]) if (asset.is_object() && asset.contains("name") && asset["name"].is_string()) names.insert(asset["name"].get<std::string>());
    if (!names.contains(package + ".zip") || !names.contains(package + ".zip.sha256")) continue;
    const std::string base = std::string(kRepository) + "/releases/download/" + tag + "/" + package;
    best = AvailableUpdate{version, package, base + ".zip", base + ".zip.sha256", std::string(kRepository) + "/releases/tag/" + tag};
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
void ApplyUpdate(const fs::path& package, const fs::path& staged, const std::string& version, const fs::path& rollback) {
  VerifyPackage(staged);
  const auto metadata = nlohmann::json::parse(ReadText(staged / "package.json"));
  if (!metadata.is_object() || metadata.value("version", std::string()) != version)
    throw std::runtime_error("O pacote baixado não corresponde à versão anunciada. Nada foi alterado.");
  // Current application entries: the top-level components of its own index.
  std::set<fs::path> current{"package.sha256"};
  {
    std::ifstream in(package / "package.sha256", std::ios::binary);
    if (!in) throw std::runtime_error("Esta cópia não é um pacote instalado; atualize extraindo o ZIP.");
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() > 65) current.insert(*fs::path(std::u8string(line.begin() + 65, line.end())).begin());
    }
  }
  std::vector<fs::path> incoming;
  for (const auto& entry : fs::directory_iterator(staged)) incoming.push_back(entry.path().filename());
  for (const auto& name : incoming) if (name == "data") throw std::runtime_error("O pacote baixado contém dados de usuário. Nada foi alterado.");
  fs::remove_all(rollback);
  fs::create_directories(rollback);
  std::vector<std::pair<fs::path, fs::path>> moved;
  auto move = [&](const fs::path& from, const fs::path& to) { fs::rename(from, to); moved.emplace_back(from, to); };
  try {
    // Renaming keeps the running launcher valid; nothing is deleted.
    for (const auto& name : current) if (fs::exists(package / name)) move(package / name, rollback / name);
    for (const auto& name : incoming) {
      if (fs::exists(package / name)) move(package / name, rollback / name);
      move(staged / name, package / name);
    }
  } catch (...) {
    for (auto it = moved.rbegin(); it != moved.rend(); ++it) { std::error_code ec; fs::rename(it->second, it->first, ec); }
    throw;
  }
}
}
