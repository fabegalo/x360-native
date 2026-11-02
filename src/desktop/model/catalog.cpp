#include "catalog.h"
#include "catalog_paths.h"
#include "action_trace.h"
#include "digest_batch.h"
#include "preparation.h"
#include "sha256.h"
#include "workspace_lease.h"
#include "host_platform.h"
#include "xex_identity.h"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sstream>
#include <toml++/toml.hpp>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace x360n::desktop {
std::string Utf8(const fs::path& path) {
  auto s = path.u8string();
  return {reinterpret_cast<const char*>(s.data()), s.size()};
}
namespace {
thread_local bool launch_verification = false;
toml::table Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Não foi possível ler: " + Utf8(path));
  return toml::parse(stream, Utf8(path));
}
std::string Text(const auto& node) {
  auto s = node.template value<std::string>();
  if (!s || s->empty()) throw std::runtime_error("Perfil de preparação incompleto.");
  return *s;
}
fs::path Resolve(const fs::path& parent, const std::string& value) {
  return CatalogVerificationPath(parent / fs::path(std::u8string(value.begin(), value.end())));
}
void Inside(const fs::path& path, const fs::path& base) {
  // Includes non-existing descendants; existing links are resolved before comparison.
  auto p = CatalogVerificationPath(path), b = CatalogVerificationPath(base);
  auto rel = p.lexically_relative(b);
  if (rel.empty() || rel.is_absolute() || *rel.begin() == "..")
    throw std::runtime_error("Caminho fora da pasta autorizada: " + Utf8(path));
}
bool Matches(const fs::path& path, const std::string& hash) {
  auto d = DigestFile(path);
  return d && d->sha256 == hash;
}
std::string FileKey(const fs::path& path) {
  auto value = path.generic_u8string(); std::string key(value.begin(), value.end());
  for (auto& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return key;
}
fs::path ImportIoPath(const fs::path& path) {
#ifdef _WIN32
  // The final game filename can fit MAX_PATH while its verified staging path
  // does not. Extended paths work without registry or process-manifest changes.
  auto value = fs::absolute(path).lexically_normal().make_preferred().native();
  if (value.starts_with(L"\\\\?\\")) return fs::path(value);
  if (value.starts_with(L"\\\\")) return fs::path(L"\\\\?\\UNC\\" + value.substr(2));
  return fs::path(L"\\\\?\\" + value);
#else
  return path;
#endif
}
}
CatalogLaunchVerification::CatalogLaunchVerification() : previous_(launch_verification) { launch_verification = true; }
CatalogLaunchVerification::~CatalogLaunchVerification() { launch_verification = previous_; }
bool CatalogLaunchVerificationActive() { return launch_verification; }
fs::path CatalogVerificationPath(const fs::path& path) {
  return launch_verification ? VerificationPath(path) : fs::weakly_canonical(path);
}
std::vector<Target> ReadCatalog(const ProjectPaths& paths) {
  ActionTrace trace("catalog.read");
  std::vector<Target> result;
  if (fs::is_directory(paths.root / "titles")) for (const auto& dir : fs::directory_iterator(paths.root / "titles")) {
    CheckVerificationCancellation();
    auto setup = dir.path() / "preparation.toml";
    if (!fs::is_regular_file(setup)) continue;
    auto info = Read(setup);
    Target t;
    t.id = Utf8(dir.path().filename());
    ActionTrace target_trace("catalog.recipe." + t.id);
    if (!IsSafeLabel(t.id)) throw std::runtime_error("Identificador inválido.");
    t.label = Text(info["label"]);
    t.limitations = Text(info["limitations"]);
    t.label_en = info["label_en"].value_or(t.label);
    t.limitations_en = info["limitations_en"].value_or(t.limitations);
    t.session = info["session"].value_or(false);
    t.kind = info["kind"].value_or(std::string("game"));
    t.importable = info["importable"].value_or(true);
    if (auto content = info["library"].as_table()) {
      t.library = {Text((*content)["title_id"]), Text((*content)["content_type"]),
                   Text((*content)["file_name"]), Text((*content)["display_name"]),
                   Text((*content)["source_sha256"])};
      for (const auto& value : {t.library.title_id, t.library.content_type})
        if (value.size() != 8 || value.find_first_not_of("0123456789ABCDEF") != std::string::npos)
          throw std::runtime_error("Identidade de conteúdo inválida.");
      if (!IsSafePackageEntryName(t.library.file_name) || t.library.file_name.size() > 42 ||
          t.library.source_sha256.size() != 64 || t.library.source_sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Procedência de conteúdo inválida.");
    }
    t.manifest = dir.path() / (t.id + "_manifest.toml");
    auto profile = Read(t.manifest);
    t.launch_library = profile["x360n"]["launch_library"].value_or(false);
    t.game_root = Resolve(dir.path(), Text(profile["project"]["game_root"]));
    // A codegen VFS root wider than the content (embedded system modules).
    if (profile["x360n"]["game_data_root"]) t.game_root = Resolve(dir.path(), Text(profile["x360n"]["game_data_root"]));
    Inside(t.game_root, paths.inputs());
    const bool absent_content = !fs::exists(t.game_root);
    CatalogPaths content_paths(t.game_root);
    t.exe = NativeTitleExecutable(paths,t.id);
    if (auto value = profile["x360n"]["native_exe"].value<std::string>())
      t.exe = ResolveNativeExecutable(paths,t.id,Resolve(dir.path(), *value));
    Inside(t.exe, paths.build());
    if (const auto* runtime = info["runtime_files"].as_array())
      for (const auto& file : *runtime) {
        auto path = Resolve(t.exe.parent_path(), PathUtf8(NativeRuntimeFilename(PathFromUtf8(Text(file)))));
        Inside(path, t.exe.parent_path());
        t.runtime_files.push_back(path);
      }
    if (auto value = profile["x360n"]["user_data_root"].value<std::string>()) {
      t.user_root = Resolve(dir.path(), *value);
      Inside(t.user_root, paths.local() / "user");
      t.user_root = UserDataRoot(paths, t.user_root);
    }
    const auto* files = info["files"].as_array();
    if (!files || files->empty()) throw std::runtime_error("Perfil sem componentes verificados.");
    for (const auto& file : *files) {
      const auto* fields = file.as_table();
      if (!fields) throw std::runtime_error("Componente inválido no perfil.");
      Input i;
      i.destination = Resolve(t.game_root, Text((*fields)["path"]));
      Inside(i.destination, t.game_root);
      i.source_name = Text((*fields)["source_name"]);
      i.sha256 = Text((*fields)["sha256"]);
      i.optional = (*fields)["optional"].value_or(false);
      if (!IsSafePackageEntryName(i.source_name) || i.sha256.size() != 64 ||
          i.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Identidade de componente inválida.");
      t.inputs.push_back(std::move(i));
    }
    if (auto inventory = info["inventory"].value<std::string>()) {
      auto list = Resolve(dir.path(), *inventory);
      Inside(list, dir.path());
      std::ifstream stream(list, std::ios::binary);
      if (!stream) throw std::runtime_error("Inventário de componentes ausente: " + Utf8(list));
      for (std::string line; std::getline(stream, line);) {
        CheckVerificationCancellation();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() < 67 || line[64] != '\t' || line.substr(0, 64).find_first_not_of("0123456789abcdef") != std::string::npos)
          throw std::runtime_error("Inventário de componentes inválido.");
        auto relative = fs::path(std::u8string(line.begin() + 65, line.end()));
        for (const auto& component : relative)
          if (!IsSafePackageEntryName(Utf8(component))) throw std::runtime_error("Caminho inválido no inventário.");
        // This is metadata construction, never an authorization to copy/launch.
        // With an absent, already-contained root there are no descendants to
        // canonicalize. Every component above is still validated; Import and
        // launch preflight revalidate the filesystem at their trust boundary.
        auto fast = absent_content ? std::optional<fs::path>(t.game_root / relative)
                                   : content_paths.Resolve(t.game_root / relative);
        auto destination = fast ? *fast : Resolve(t.game_root, Utf8(relative));
        if (!fast) Inside(destination, t.game_root);
        t.inputs.push_back({destination, Utf8(relative.filename()), line.substr(0, 64), relative});
      }
    }
    if (const auto* deps = info["dependencies"].as_array())
      for (const auto& dep : *deps) {
        auto id = Text(dep);
        if (!IsSafeLabel(id)) throw std::runtime_error("Dependência inválida.");
        t.dependencies.push_back(id);
      }
    result.push_back(std::move(t));
  }
  ReadImportedGames(paths, result);
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  return result;
}
std::vector<std::string> DetectImportTargets(const std::vector<Target>& targets, const fs::path& source,
                                            const std::function<bool()>& cancelled) {
  if (!fs::is_directory(source)) throw std::runtime_error("Selecione uma pasta de conteúdo extraído.");
  std::vector<std::string> matches;
  for (auto it = fs::recursive_directory_iterator(source); it != fs::recursive_directory_iterator(); ++it) {
    if (cancelled && cancelled()) throw std::runtime_error("Importação cancelada.");
    if (it->is_symlink() || !IsWithin(it->path(), source)) { it.disable_recursion_pending(); continue; }
    if (!it->is_regular_file()) continue;
    for (const auto& t : targets) {
      if (t.inputs.empty() || std::find(matches.begin(), matches.end(), t.id) != matches.end()) continue;
      const auto& anchor = t.inputs.front();
      if (it->path().filename() == PathFromUtf8(anchor.source_name) && Matches(it->path(), anchor.sha256)) matches.push_back(t.id);
    }
  }
  return matches;
}
fs::path PreparationMarker(const ProjectPaths& paths, const Target& t) {
  if (!IsSafeLabel(t.id)) throw std::runtime_error("Identificador inválido.");
  return paths.local() / "preparation" / (t.id + ".incomplete");
}
void ApplyReceiptHint(Inspection& state, ReceiptHint hint) {
  state.receipt_current = hint == ReceiptHint::Current;
  state.receipt_stale = hint == ReceiptHint::Stale;
  if (!state.receipt_stale) return;
  state.launch_candidate = false;
  state.issues.push_back("Arquivos mudaram desde a última preparação (por exemplo, uma atualização do aplicativo); prepare novamente.");
}
Inspection Inspect(const ProjectPaths& paths, const Target& t, InspectionMode mode, ReceiptHints* hints) {
  ActionTrace trace("content.inspect." + t.id);
  Inspection r;
  if (!t.import_error.empty()) { r.inputs_ok = false; r.issues.push_back(t.import_error); }
  if (mode == InspectionMode::Preview) {
    // Opening the launcher lists metadata. Only an explicit check/preparation/
    // launch authenticates the complete inventory with Full (the default).
    r.inputs_ok = false;
    if (!t.import_error.empty() || t.inputs.empty()) return r;
    const auto& anchor = t.inputs.front();
    const auto file = t.preparation_source.empty() ? anchor.destination :
        t.preparation_source / anchor.destination.lexically_relative(t.game_root);
    r.verification_pending = fs::is_regular_file(file);
    r.interrupted = false;
    // Both markers are small; their first lines name the failed step and log.
    for (const auto& marker : {PreparationMarker(paths, t), paths.local() / "preparation" / (t.id + ".runtime-error")}) {
      if (!fs::exists(marker)) continue;
      r.interrupted = true;
      std::ifstream log(marker); std::string line;
      for (unsigned n = 0; n < 30 && std::getline(log, line); ++n) r.issues.push_back(line);
    }
    r.launch_candidate = r.verification_pending && t.aot && !r.interrupted &&
        fs::is_regular_file(t.exe) && fs::is_regular_file(PreparationReceipt(t.exe));
    for (const auto& runtime : t.runtime_files) r.launch_candidate &= fs::is_regular_file(runtime);
    if (r.launch_candidate && hints) ApplyReceiptHint(r, hints->Check(t.id, t.manifest, t.exe));
    if (!r.verification_pending) r.issues.push_back("Falta componente: " + Utf8(anchor.destination));
    return r;
  }
  for (const auto& i : t.inputs) {
    CheckVerificationCancellation();
    auto file = t.preparation_source.empty() ? i.destination : t.preparation_source / i.destination.lexically_relative(t.game_root);
    if (!fs::is_regular_file(file)) {
      // Absent optional components are checked by whoever needs them.
      if (i.optional) continue;
      r.inputs_ok = false;
      r.issues.push_back("Falta componente: " + Utf8(i.destination));
    } else if (!Matches(file, i.sha256)) {
      r.inputs_ok = false;
      r.issues.push_back("Arquivo diferente do conjunto verificado; preserve-o e selecione a versão correta: " + Utf8(i.destination));
    }
  }
  if (!t.aot) return r;
  if (auto error = CheckLaunchable(paths, t.exe)) {
    r.issues.push_back("Precisa preparar: " + Utf8(t.exe));
  } else {
    r.prepared = r.inputs_ok;
    if (auto error = CheckPreparation(paths, t.id, t.manifest, t.exe)) {
      r.prepared = false;
      r.issues.push_back(*error);
    }
  }
  for (const auto& file : t.runtime_files) {
    if (!fs::is_regular_file(file)) {
      r.prepared = false;
      r.issues.push_back("Falta dependência da preparação; prepare novamente: " + Utf8(file));
    }
  }
  if (fs::exists(PreparationMarker(paths, t))) {
    r.prepared = false;
    r.interrupted = true;
    r.issues.push_back("Preparação anterior incompleta. Clique em Preparar para retomar; o executável antigo não valida essa tentativa.");
    std::ifstream log(PreparationMarker(paths, t));
    std::string line;
    for (unsigned n = 0; n < 30 && std::getline(log, line); ++n) r.issues.push_back(line);
  }
  const auto runtime_error = paths.local() / "preparation" / (t.id + ".runtime-error");
  if (fs::is_regular_file(runtime_error)) {
    r.prepared = false; r.interrupted = true;
    std::ifstream in(runtime_error); std::string line;
    for (unsigned n = 0; n < 30 && std::getline(in, line); ++n) r.issues.push_back(line);
  }
  return r;
}
Target VerifyCatalogLaunch(const ProjectPaths& paths, const std::vector<Target>& catalog,
    const std::string& id, const fs::path& manifest) {
  CatalogLaunchVerification launch_paths;
  ActionTrace trace("launch.catalog-preflight");
  const auto find = [&](const std::string& name) -> const Target& {
    const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto& t) { return t.id == name; });
    if (it == catalog.end()) throw std::runtime_error("Dependência de lançamento ausente: " + name);
    return *it;
  };
  const auto& selected = find(id);
  if (selected.kind == "game" && !InGameLibrary(selected))
    throw std::runtime_error("Adicione o jogo à biblioteca antes de iniciar: " + id);
  if (!selected.aot || fs::weakly_canonical(selected.manifest) != fs::weakly_canonical(manifest))
    throw std::runtime_error("O manifesto de lançamento não corresponde ao título selecionado.");
  const auto check = [&](const Target& t) {
    CheckVerificationCancellation();
    const auto state = Inspect(paths, t);
    if (!state.inputs_ok || !state.prepared) {
      std::string reason = "Conteúdo ou preparação inválida: " + t.id;
      for (const auto& issue : state.issues) reason += "\n" + issue;
      throw std::runtime_error(reason);
    }
    const auto issues = InspectLibrary(paths, t, catalog);
    if (!issues.empty()) throw std::runtime_error(issues.front());
  };
  check(selected);
  for (const auto& dep : selected.dependencies) check(find(dep));
  return selected;
}
size_t Import(const ProjectPaths& paths, const Target& t, const fs::path& source,
              const std::function<bool()>& cancelled, bool allow_partial) {
  Inside(t.game_root, paths.inputs());
  if (!t.importable) throw std::runtime_error("Importação guiada ainda não disponível para este alvo.");
  auto stop = [&] { if (cancelled && cancelled()) throw std::runtime_error("Importação cancelada; arquivos já verificados foram preservados."); };
  const bool single = fs::is_regular_file(source);
  if (!single && !fs::is_directory(source)) throw std::runtime_error("Selecione um arquivo ou uma pasta de conteúdo extraído.");
  std::map<fs::path, fs::path> copies;
  // A renamed standalone file is compared against the whole inventory. Hash it
  // once, not once per asset; the staged copy is independently verified below.
  const auto single_digest = single ? DigestFile(source) : std::optional<FileDigest>{};
  std::map<std::string, std::vector<const Input*>> by_name;
  for (const auto& i : t.inputs) {
    by_name[FileKey(PathFromUtf8(i.source_name))].push_back(&i);
    if (FileKey(PathFromUtf8(i.source_name)) != FileKey(i.destination.filename())) by_name[FileKey(i.destination.filename())].push_back(&i);
  }
  auto consider = [&](const fs::path& file) {
    if (!single && !by_name.contains(FileKey(file.filename()))) return;
    const auto digest = single ? single_digest : DigestFile(file);
    if (!digest) throw std::runtime_error("Could not read import source: " + Utf8(file));
    auto check = [&](const Input& i) {
      if (!single && !i.source_relative.empty() && FileKey(file.lexically_relative(LongPath(source))) != FileKey(i.source_relative)) return;
      if (digest->sha256 == i.sha256) copies[i.destination] = file;
    };
    if (single) for (const auto& i : t.inputs) check(i);
    else if (auto it = by_name.find(FileKey(file.filename())); it != by_name.end()) for (const auto* i : it->second) check(*i);
  };
  if (single) { stop(); consider(source); }
  // Search only the user-selected folder, never follow directory symlinks.
  for (auto it = single ? fs::recursive_directory_iterator{} : fs::recursive_directory_iterator(LongPath(source)); it != fs::recursive_directory_iterator(); ++it) {
    stop();
    const auto& entry = *it;
    if (entry.is_symlink() || !IsWithin(entry.path(), source)) { it.disable_recursion_pending(); continue; }
    if (!entry.is_regular_file()) continue;
    consider(entry.path());
  }
  uintmax_t needed = 0;
  for (const auto& i : t.inputs) {
    Inside(i.destination, t.game_root);
    if (fs::exists(LongPath(i.destination))) {
      if (!Matches(i.destination, i.sha256))
        throw std::runtime_error("Destino já contém outro arquivo; nada será sobrescrito: " + Utf8(i.destination));
      copies.erase(i.destination);
    } else if (!copies.contains(i.destination)) {
      if (allow_partial || i.optional) continue;
      throw std::runtime_error("Conjunto incompleto. Falta o arquivo verificado: " + i.source_name);
    } else needed += fs::file_size(LongPath(copies.at(i.destination)));
  }
  WorkspaceLease lease(paths);
  fs::create_directories(paths.inputs());
  if (fs::space(paths.inputs()).available < needed)
    throw std::runtime_error("Espaço insuficiente. Libere espaço na pasta de conteúdo e tente novamente.");
  size_t count = 0;
  auto records = paths.local() / "logs/imports";
  fs::create_directories(records);
  if (!IsSafeLabel(t.id)) throw std::runtime_error("Identificador inválido.");
  std::ofstream log(records / (t.id + ".tsv"), std::ios::binary | std::ios::app);
  if (!log) throw std::runtime_error("Não foi possível registrar a procedência local da importação.");
  std::map<fs::path, const Input*> expected_files;
  for (const auto& i : t.inputs) expected_files.emplace(i.destination, &i);
  for (const auto& [destination, input] : copies) {
    stop();
    Inside(destination, paths.inputs());
    fs::create_directories(LongPath(destination.parent_path()));
    // Unique temporary name; never truncate an existing original or destination.
    const auto staging = ImportIoPath(destination.parent_path() / (".x360n-import-" + Utf8(destination.filename())));
    const auto temporary = staging / "verified.tmp";
    const auto expected = expected_files.at(destination);
    if (fs::exists(staging)) {
      if (!fs::is_directory(staging) || fs::is_symlink(staging) || !IsWithin(staging, ImportIoPath(destination.parent_path())))
        throw std::runtime_error("Pasta temporária inválida; conteúdo preservado.");
      for (const auto& entry : fs::directory_iterator(staging))
        if (entry.path() != temporary || entry.is_symlink() || !entry.is_regular_file())
          throw std::runtime_error("Pasta temporária contém arquivos desconhecidos; conteúdo preservado.");
      if (fs::exists(temporary) && !Matches(temporary, expected->sha256)) {
        auto saved = staging; saved += ".interrupted-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        // Same verified parent; retain interrupted bytes rather than deleting them.
        fs::rename(staging, saved);
      }
    }
    fs::create_directory(staging);
    try {
      if (!fs::exists(temporary)) fs::copy_file(ImportIoPath(input), temporary, fs::copy_options::none);
      if (!Matches(temporary, expected->sha256))
        throw std::runtime_error("O conteúdo mudou durante a cópia. Tente novamente.");
      // Publish complete bytes atomically and never replace a concurrent file.
#ifdef _WIN32
      if (!MoveFileExW(temporary.c_str(), ImportIoPath(destination).c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Não foi possível publicar o componente; erro Windows " + std::to_string(GetLastError()));
#else
      fs::create_hard_link(temporary, destination);
      fs::remove(temporary);
#endif
      // Antivirus/indexing may keep the now-empty directory briefly busy after
      // publication. Cleanup failure cannot invalidate an already verified file.
      std::error_code cleanup;
      fs::remove(staging, cleanup);
      log << expected->sha256 << '\t' << Utf8(input) << '\t' << Utf8(destination) << '\n';
      log.flush();
      if (!log) throw std::runtime_error("Componente copiado, mas falhou o registro local da procedência.");
      ++count;
    } catch (...) {
      std::error_code ec; fs::remove(temporary, ec); fs::remove(staging, ec); throw;
    }
  }
  return count;
}
}
