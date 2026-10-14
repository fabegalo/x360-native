#include "preparation.h"
#include "sha256.h"
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>
#include <fstream>
#include <set>
#include <cctype>
#include <regex>
#include <algorithm>

namespace x360n {
namespace fs = std::filesystem;
namespace {
fs::path Normal(const fs::path& p) { return fs::weakly_canonical(p); }
std::string Key(const fs::path& p) {
  auto s = PathUtf8(Normal(p));
#ifdef _WIN32
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
  return s;
}
std::string ReadHead(const fs::path& sdk) {
  auto git = sdk / ".git";
  if (fs::is_regular_file(git)) {
    std::ifstream in(git); std::string line; std::getline(in, line);
    if (!line.starts_with("gitdir: ")) return {};
    git = sdk / PathFromUtf8(line.substr(8));
  }
  std::ifstream in(git / "HEAD"); std::string head; std::getline(in, head);
  if (head.starts_with("ref: ")) {
    const auto ref = head.substr(5);
    std::ifstream branch(git / PathFromUtf8(ref));
    if (branch) std::getline(branch, head);
    else {
      std::ifstream packed(git / "packed-refs"); head.clear(); std::string line;
      while (std::getline(packed, line))
        if (line.size() > 41 && line.substr(41) == ref) { head = line.substr(0, 40); break; }
    }
  }
  return head;
}
}
fs::path PreparationReceipt(const fs::path& exe) { auto p = exe; p += ".preparation.json"; return p; }
std::optional<std::string> CheckPreparation(const ProjectPaths& paths,
    const std::string& title, const fs::path& manifest, const fs::path& exe) {
  const auto fail = [](const std::string& reason) -> std::optional<std::string> {
    return "Preparação inválida; prepare novamente. " + reason;
  };
  try {
    if (fs::exists(paths.local() / "payload-update/pending") || fs::exists(paths.local() / "dependency-update.pending"))
      return fail("A atualização do aplicativo foi interrompida. Reabra o aplicativo para concluí-la; seus dados foram preservados.");
    if (!IsSafeLabel(title) || manifest.empty()) return fail("Identidade do título ausente.");
    // On Windows the preparation/Ninja producer owns this file with no sharing.
    // Keep a reader open for the entire inspection, excluding replacement while
    // hashes are collected. Legacy receipts still follow all C2 validations.
    const auto lease_path = paths.local() / "preparation" / (title + ".lock");
    std::ifstream build_lease;
    if (fs::exists(lease_path)) {
      build_lease.open(lease_path, std::ios::binary);
      if (!build_lease) return fail("O título está sendo compilado; aguarde a preparação.");
    }
    if (fs::exists(paths.local() / "preparation" / (title + ".incomplete")))
      return fail("A preparação anterior não terminou.");
    std::ifstream input(PreparationReceipt(exe), std::ios::binary);
    if (!input) return fail("Recibo de preparação ausente.");
    const auto receipt = nlohmann::json::parse(input);
    if (receipt.at("schema") != 1 || receipt.at("title") != title ||
        Key(PathFromUtf8(receipt.at("manifest").get<std::string>())) != Key(manifest) ||
        Key(PathFromUtf8(receipt.at("exe").get<std::string>())) != Key(exe))
      return fail("O recibo pertence a outro título ou build.");
    const auto sdk_commit = receipt.at("sdk_commit").get<std::string>();
    if (sdk_commit.size() != 40 || sdk_commit != ReadHead(paths.sdk()))
      return fail("A revisão do ReXGlue mudou.");
    std::set<std::string> inputs, artifacts;
    for (auto group : {"inputs", "artifacts"}) {
      const auto& entries = receipt.at(group);
      if (!entries.is_array() || entries.empty()) return fail("Recibo incompleto.");
      auto& seen = std::string(group) == "inputs" ? inputs : artifacts;
      for (const auto& entry : entries) {
        const auto file = PathFromUtf8(entry.at("path").get<std::string>());
        const auto hash = entry.at("sha256").get<std::string>();
        if (!file.is_absolute() || hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos ||
            !seen.insert(Key(file)).second) return fail("Registro de arquivo inválido.");
        const auto digest = DigestFile(file);
        if (!digest || digest->sha256 != hash) return fail("Arquivo alterado ou ausente: " + PathUtf8(file));
      }
    }
    for (auto role : {"compiler", "codegen", "build_config"})
      if (!inputs.contains(Key(PathFromUtf8(receipt.at("roles").at(role).get<std::string>()))))
        return fail("Identidade das ferramentas e configuração incompleta.");
    // CMake's CONFIGURE_DEPENDS inventory also detects newly added sources or
    // templates; hashes of previously existing files alone cannot do that.
    for (const auto& glob : receipt.at("globs")) {
      const auto glob_pattern = glob.at("pattern").get<std::string>();
      std::string expression = glob.at("recursive").get<bool>() ? "([^/]*/)*" : "";
      for (const auto c : glob_pattern) {
        if (c == '*') expression += "[^/]*";
        else if (c == '?') expression += "[^/]";
        else { if (std::string(".\\+(){}[]^$|").find(c) != std::string::npos) expression += '\\'; expression += c; }
      }
      const std::regex pattern(expression, std::regex::icase);
      std::set<std::string> expected, actual;
      for (const auto& file : glob.at("files")) expected.insert(Key(PathFromUtf8(file.get<std::string>())));
      const auto directory = PathFromUtf8(glob.at("directory").get<std::string>());
      auto collect = [&](const auto& file) {
        auto relative = PathUtf8(file.path().lexically_relative(directory));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if ((glob.at("directories").get<bool>() || file.is_regular_file()) &&
            std::regex_match(relative, pattern)) actual.insert(Key(file.path()));
      };
      if (glob.at("recursive").get<bool>() || glob_pattern.find('/') != std::string::npos)
        for (const auto& file : fs::recursive_directory_iterator(directory)) collect(file);
      else for (const auto& file : fs::directory_iterator(directory)) collect(file);
      if (actual != expected) return fail("A lista de fontes ou templates mudou: " + PathUtf8(directory));
    }
    // Required anchors prevent an empty/partial receipt from blessing an exe.
    std::ifstream profile_file(manifest, std::ios::binary);
    const auto profile = toml::parse(profile_file, PathUtf8(manifest));
    auto xex = profile["entrypoint"]["file_path"].value<std::string>();
    if (!xex || !inputs.contains(Key(manifest)) || !inputs.contains(Key(paths.root / "deps.lock.json")) ||
        !inputs.contains(Key(manifest.parent_path() / PathFromUtf8(*xex))) || !artifacts.contains(Key(exe)))
      return fail("Entradas obrigatórias ausentes do recibo.");
    const auto registration = manifest.parent_path() / "preparation.toml";
    if (fs::is_regular_file(registration) && !inputs.contains(Key(registration)))
      return fail("O registro do título mudou.");
    for (const auto& file : fs::directory_iterator(exe.parent_path())) {
      auto ext = file.path().extension().string();
      for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (ext == ".dll" && !artifacts.contains(Key(file.path())))
        return fail("DLL sem identidade verificada: " + PathUtf8(file.path()));
    }
    if (fs::exists(paths.local() / "preparation" / (title + ".incomplete")))
      return fail("A preparação ainda não terminou.");
    return std::nullopt;
  } catch (const std::exception&) { return fail("Não foi possível verificar o recibo e suas dependências."); }
}
std::optional<std::string> CheckPreparedLaunch(const ProjectPaths& paths, const fs::path& exe) {
  try {
    const auto titles = paths.root / "titles";
    for (const auto& profiles : {titles, paths.local() / "aot"}) {
    if (fs::is_directory(profiles)) for (const auto& dir : fs::directory_iterator(profiles)) {
      if (!dir.is_directory()) continue;
      const auto id = PathUtf8(dir.path().filename());
      const auto manifest = dir.path() / PathFromUtf8(id + "_manifest.toml");
      if (!fs::is_regular_file(manifest)) continue;
      try {
        std::ifstream in(manifest, std::ios::binary); auto table = toml::parse(in, PathUtf8(manifest));
        auto configured = table["x360n"]["native_exe"].value<std::string>();
        auto target = configured ? dir.path() / PathFromUtf8(*configured) : paths.build() / "titles" / dir.path().filename() / PathFromUtf8(id + ".exe");
        if (Key(target) == Key(exe)) return CheckPreparation(paths, id, manifest, exe);
      } catch (const toml::parse_error&) {
        // An unrelated broken registration cannot invalidate this title. A title
        // with no matching valid registration is still rejected below.
        continue;
      }
    }
    }
    if (fs::exists(PreparationReceipt(exe))) {
      // build-title -BuildDir may produce an explicitly selected diagnostic
      // build elsewhere under .local/build, still bound to an existing title.
      std::ifstream in(PreparationReceipt(exe), std::ios::binary);
      const auto receipt = nlohmann::json::parse(in);
      const auto id = receipt.at("title").get<std::string>();
      if (IsSafeLabel(id)) {
        const auto manifest = titles / PathFromUtf8(id) / PathFromUtf8(id + "_manifest.toml");
        if (fs::is_regular_file(manifest)) return CheckPreparation(paths, id, manifest, exe);
      }
    }
    if (fs::exists(PreparationReceipt(exe)) || IsWithin(exe, paths.build() / "titles"))
      return "Preparação inválida; prepare novamente. Executável sem título correspondente.";
    return std::nullopt;
  } catch (const std::exception&) { return "Preparação inválida; prepare novamente. Não foi possível verificar o título."; }
}
}
