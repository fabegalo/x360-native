#include "preparation.h"
#include "action_trace.h"
#include "digest_batch.h"
#include "sha256.h"
#include "host_platform.h"
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>
#include <fstream>
#include <cctype>
#include <regex>
#include <algorithm>
#include <unordered_set>
#include <map>
#include <locale>

namespace x360n {
namespace fs = std::filesystem;
namespace {
fs::path Normal(const fs::path& p) { return VerificationPath(p); }
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
  ActionTrace trace("receipt.verify." + title);
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
    std::unique_ptr<FileLease> native_lease;
    if (fs::exists(lease_path)) {
#ifndef _WIN32
      native_lease = std::make_unique<FileLease>(lease_path, false, false);
#endif
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
    auto sdk_source=paths.sdk();
    bool native_image_required=receipt.contains("native_identity");
#if !defined(_WIN32) || defined(_M_ARM64) || defined(__aarch64__)
    // A legacy Windows receipt must not invoke WSL's PE interop as if it were
    // a native Linux build, or count x64 emulation as a native ARM64 build.
    native_image_required=true;
#endif
    if (native_image_required) {
      try { ValidateNativeImage(exe); }
      catch (const std::exception& error) { return fail(error.what()); }
    }
    if (receipt.contains("native_identity")) {
      if (receipt.at("native_identity").at("platform")!=HostPlatform()) return fail("O produto pertence a outro sistema/arquitetura.");
      if (const auto source=receipt.at("native_identity").value("sdk_source",std::string{}); !source.empty()) {
        sdk_source=PathFromUtf8(source);
        if (!sdk_source.is_absolute()) return fail("Caminho do SDK nativo inválido.");
      }
    }
    if (sdk_commit.size() != 40 || sdk_commit != ReadHead(sdk_source))
      return fail("A revisão do ReXGlue mudou.");
    std::unordered_set<std::string> inputs, artifacts;
    for (auto group : {"inputs", "artifacts"}) {
      const auto& entries = receipt.at(group);
      if (!entries.is_array() || entries.empty()) return fail("Recibo incompleto.");
      auto& seen = std::string(group) == "inputs" ? inputs : artifacts;
      seen.reserve(entries.size());
      ActionTrace entries_trace(std::string("receipt.") + group);
      std::vector<fs::path> files;
      std::vector<std::string> hashes;
      files.reserve(entries.size());
      hashes.reserve(entries.size());
      for (const auto& entry : entries) {
        CheckVerificationCancellation();
        const auto file = PathFromUtf8(entry.at("path").get<std::string>());
        const auto hash = entry.at("sha256").get<std::string>();
        if (!file.is_absolute() || hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos ||
            !seen.insert(Key(file)).second) return fail("Registro de arquivo inválido.");
        files.push_back(file); hashes.push_back(hash);
      }
      PrimeVerificationDigests(files);
      for (size_t i = 0; i < files.size(); ++i) {
        const auto digest = DigestFile(files[i]);
        if (!digest || digest->sha256 != hashes[i]) return fail("Arquivo alterado ou ausente: " + PathUtf8(files[i]));
      }
    }
    for (auto role : {"compiler", "codegen", "build_config"})
      if (!inputs.contains(Key(PathFromUtf8(receipt.at("roles").at(role).get<std::string>()))))
        return fail("Identidade das ferramentas e configuração incompleta.");
    // CMake's CONFIGURE_DEPENDS inventory also detects newly added sources or
    // templates; hashes of previously existing files alone cannot do that.
    {
    ActionTrace globs_trace("receipt.globs");
    std::map<std::pair<fs::path, bool>, std::vector<fs::directory_entry>> inventories;
    for (const auto& glob : receipt.at("globs")) {
      CheckVerificationCancellation();
      const auto glob_pattern = glob.at("pattern").get<std::string>();
      std::string expression = glob.at("recursive").get<bool>() ? "([^/]*/)*" : "";
      for (const auto c : glob_pattern) {
        if (c == '*') expression += "[^/]*";
        else if (c == '?') expression += "[^/]";
        else { if (std::string(".\\+(){}[]^$|").find(c) != std::string::npos) expression += '\\'; expression += c; }
      }
      // CMake predominantly emits *.c/*.h/*.cpp. Their exact equivalent is a
      // case-insensitive suffix on the final component, including recursive
      // globs. Keep the original regex for every other pattern.
      const bool suffix_pattern = glob_pattern.starts_with("*.") &&
          glob_pattern.find_first_of("*?/\\", 1) == std::string::npos &&
          std::all_of(glob_pattern.begin(), glob_pattern.end(), [](unsigned char c) { return c < 128; });
      std::optional<std::regex> pattern;
      if (!suffix_pattern) pattern.emplace(expression, std::regex::icase);
      const auto directory = PathFromUtf8(glob.at("directory").get<std::string>());
      const auto include_directories = glob.at("directories").get<bool>();
      const std::locale glob_locale;
      const auto& character = std::use_facet<std::ctype<char>>(glob_locale);
      auto matches = [&](const fs::path& file) {
        if (suffix_pattern) {
          const auto name = PathUtf8(file.filename());
          const auto suffix = std::string_view(glob_pattern).substr(1);
          if (name.size() < suffix.size()) return false;
          const auto tail = std::string_view(name).substr(name.size() - suffix.size());
          for (size_t i = 0; i < suffix.size(); ++i)
            if (character.tolower(tail[i]) != character.tolower(suffix[i])) return false;
          return true;
        }
        auto relative = PathUtf8(file.lexically_relative(directory));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        return std::regex_match(relative, *pattern);
      };
      std::unordered_set<std::string> expected, actual;
      expected.reserve(glob.at("files").size());
      for (const auto& file : glob.at("files")) expected.insert(Key(PathFromUtf8(file.get<std::string>())));
      const bool recursive = glob.at("recursive").get<bool>() || glob_pattern.find('/') != std::string::npos;
      const auto inventory_key = std::make_pair(directory, recursive);
      auto [inventory, inserted] = inventories.try_emplace(inventory_key);
      if (inserted) {
        if (recursive) for (const auto& file : fs::recursive_directory_iterator(directory)) {
          CheckVerificationCancellation(); inventory->second.push_back(file);
        }
        else for (const auto& file : fs::directory_iterator(directory)) {
          CheckVerificationCancellation(); inventory->second.push_back(file);
        }
      }
      auto collect = [&](const auto& file) {
        CheckVerificationCancellation();
        if (matches(file.path()) && (include_directories || file.is_regular_file()))
          actual.insert(Key(file.path()));
      };
      for (const auto& file : inventory->second) collect(file);
      if (actual != expected) return fail("A lista de fontes ou templates mudou: " + PathUtf8(directory));
    }
    }
    // Required anchors prevent an empty/partial receipt from blessing an exe.
    std::ifstream profile_file(manifest, std::ios::binary);
    const auto profile = toml::parse(profile_file, PathUtf8(manifest));
    auto xex = profile["entrypoint"]["file_path"].value<std::string>();
    if (!xex || !inputs.contains(Key(manifest)) || !inputs.contains(Key(paths.root / "deps.lock.json")) ||
        !inputs.contains(Key(manifest.parent_path() / PathFromUtf8(*xex))) || !artifacts.contains(Key(exe)))
      return fail("Entradas obrigatórias ausentes do recibo.");
    if (auto modules = profile["modules"].as_array()) {
      for (const auto& module : *modules) {
        const auto* table = module.as_table();
        auto file = table ? (*table)["file_path"].value<std::string>() : std::nullopt;
        if (!file || !inputs.contains(Key(manifest.parent_path() / PathFromUtf8(*file))))
          return fail("Dependência AOT ausente do recibo.");
      }
    }
    const auto registration = manifest.parent_path() / "preparation.toml";
    if (fs::is_regular_file(registration) && !inputs.contains(Key(registration)))
      return fail("O registro do título mudou.");
    if (IsWithin(exe, paths.build() / "aot") || IsWithin(exe, paths.build()/HostPlatform()/"aot")) {
      const auto owner = exe.parent_path() / ".x360n-aot-owner";
      if (!inputs.contains(Key(owner))) return fail("Propriedade do build AOT ausente do recibo.");
      std::ifstream owner_input(owner); std::string owner_title;
      std::getline(owner_input, owner_title);
      if (owner_title != title) return fail("O build AOT pertence a outro título.");
    }
    for (const auto& file : fs::directory_iterator(exe.parent_path())) {
      if (IsNativeModule(file.path()) && !artifacts.contains(Key(file.path())))
        return fail("Módulo nativo sem identidade verificada: " + PathUtf8(file.path()));
      // The supervisor runs the Vulkan preflight from this directory.
      if (Key(file.path()) == Key(exe.parent_path() / ("x360n_gpu_preflight" + ExecutableSuffix())) && !artifacts.contains(Key(file.path())))
        return fail("Preflight Vulkan sem identidade verificada: " + PathUtf8(file.path()));
    }
    if (fs::exists(paths.local() / "preparation" / (title + ".incomplete")))
      return fail("A preparação ainda não terminou.");
    return std::nullopt;
  } catch (const VerificationCancelled&) { throw; }
    catch (const std::exception&) { return fail("Não foi possível verificar o recibo e suas dependências."); }
}
ReceiptHint ReceiptHints::Check(const std::string& title, const fs::path& manifest, const fs::path& exe) {
  ActionTrace trace("receipt.hint." + title);
  // Lexical keys: canonicalizing or opening each recorded file is the cost a
  // listing must avoid. Every directory is enumerated once; its entries carry
  // their modification times, and a recorded file absent from it is missing.
  const auto key = [](const fs::path& path) {
    auto s = PathUtf8(fs::absolute(path).lexically_normal().make_preferred());
#ifdef _WIN32
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
    return s;
  };
  try {
    std::error_code ec;
    const auto receipt_file = PreparationReceipt(exe);
    const auto written = fs::last_write_time(receipt_file, ec);
    if (ec) return ReceiptHint::Stale;
    std::ifstream input(receipt_file, std::ios::binary);
    const auto receipt = nlohmann::json::parse(input);
    if (receipt.at("schema") != 1 || receipt.at("title") != title ||
        key(PathFromUtf8(receipt.at("manifest").get<std::string>())) != key(manifest) ||
        key(PathFromUtf8(receipt.at("exe").get<std::string>())) != key(exe)) return ReceiptHint::Stale;
    if (!head_) head_ = ReadHead(paths_.sdk());
    if (receipt.at("sdk_commit").get<std::string>() != *head_) return ReceiptHint::Stale;
    for (auto group : {"inputs", "artifacts"}) {
      const auto& entries = receipt.at(group);
      if (!entries.is_array() || entries.empty()) return ReceiptHint::Stale;
      for (const auto& entry : entries) {
        const auto file = PathFromUtf8(entry.at("path").get<std::string>());
        const auto name = key(file);
        auto time = times_.find(name);
        if (time == times_.end()) {
          const auto directory = file.parent_path();
          if (listed_.insert(key(directory)).second) {
            std::error_code list;
            for (fs::directory_iterator it(directory, list), end; !list && it != end; it.increment(list)) {
              std::error_code stat;
              const auto modified = it->last_write_time(stat);
              if (!stat) times_.emplace(key(it->path()), modified);
            }
          }
          time = times_.try_emplace(name).first;  // still absent: missing
        }
        if (!time->second || *time->second > written) return ReceiptHint::Stale;
      }
    }
    return ReceiptHint::Current;
  } catch (const std::exception&) { return ReceiptHint::Unknown; }
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
        auto target = configured ? ResolveNativeExecutable(paths, id, dir.path() / PathFromUtf8(*configured)) : NativeTitleExecutable(paths, id);
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
    if (fs::exists(PreparationReceipt(exe)) || IsWithin(exe, paths.build() / "titles") ||
        IsWithin(exe, paths.build() / "aot"))
      return "Preparação inválida; prepare novamente. Executável sem título correspondente.";
    return std::nullopt;
  } catch (const std::exception&) { return "Preparação inválida; prepare novamente. Não foi possível verificar o título."; }
}
}
