#include "catalog.h"
#include "sha256.h"
#include "workspace_lease.h"
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
  return fs::weakly_canonical(parent / fs::path(std::u8string(value.begin(), value.end())));
}
void Inside(const fs::path& path, const fs::path& base) {
  // Includes non-existing descendants; existing links are resolved before comparison.
  auto p = fs::weakly_canonical(path), b = fs::weakly_canonical(base);
  auto rel = p.lexically_relative(b);
  if (rel.empty() || rel.is_absolute() || *rel.begin() == "..")
    throw std::runtime_error("Caminho fora da pasta autorizada: " + Utf8(path));
}
bool Matches(const fs::path& path, const std::string& hash) {
  auto d = DigestFile(path);
  return d && d->sha256 == hash;
}
}
std::vector<Target> ReadCatalog(const ProjectPaths& paths) {
  std::vector<Target> result;
  if (!fs::is_directory(paths.root / "titles")) return result;
  for (const auto& dir : fs::directory_iterator(paths.root / "titles")) {
    auto setup = dir.path() / "preparation.toml";
    if (!fs::is_regular_file(setup)) continue;
    auto info = Read(setup);
    Target t;
    t.id = Utf8(dir.path().filename());
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
    t.game_root = Resolve(dir.path(), Text(profile["project"]["game_root"]));
    Inside(t.game_root, paths.inputs());
    t.exe = paths.build() / "titles" / t.id / (t.id + ".exe");
    if (auto value = profile["x360n"]["native_exe"].value<std::string>())
      t.exe = Resolve(dir.path(), *value);
    Inside(t.exe, paths.build());
    if (const auto* runtime = info["runtime_files"].as_array())
      for (const auto& file : *runtime) {
        auto path = Resolve(t.exe.parent_path(), Text(file));
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
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() < 67 || line[64] != '\t' || line.substr(0, 64).find_first_not_of("0123456789abcdef") != std::string::npos)
          throw std::runtime_error("Inventário de componentes inválido.");
        auto relative = fs::path(std::u8string(line.begin() + 65, line.end()));
        for (const auto& component : relative)
          if (!IsSafePackageEntryName(Utf8(component))) throw std::runtime_error("Caminho inválido no inventário.");
        auto destination = Resolve(t.game_root, Utf8(relative)); Inside(destination, t.game_root);
        t.inputs.push_back({destination, Utf8(relative.filename()), line.substr(0, 64), relative});
      }
    }
    if (const auto* deps = info["dependencies"].as_array())
      for (const auto& dep : *deps) {
        auto id = Text(dep);
        if (!IsSafeLabel(id)) throw std::runtime_error("Dependência inválida.");
        t.dependencies.push_back(id);
      }
    if (const auto* library = info["library_targets"].as_array())
      for (const auto& item : *library) {
        auto id = Text(item);
        if (!IsSafeLabel(id)) throw std::runtime_error("Identificador de biblioteca inválido.");
        t.library_targets.push_back(id);
      }
    result.push_back(std::move(t));
  }
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
Inspection Inspect(const ProjectPaths& paths, const Target& t) {
  Inspection r;
  for (const auto& i : t.inputs) {
    if (!fs::is_regular_file(i.destination)) {
      r.inputs_ok = false;
      r.issues.push_back("Falta componente: " + Utf8(i.destination));
    } else if (!Matches(i.destination, i.sha256)) {
      r.inputs_ok = false;
      r.issues.push_back("Arquivo diferente do conjunto verificado; preserve-o e selecione a versão correta: " + Utf8(i.destination));
    }
  }
  if (auto error = CheckLaunchable(paths, t.exe)) {
    r.issues.push_back("Precisa preparar: " + Utf8(t.exe));
  } else {
    r.prepared = r.inputs_ok;
  }
  for (const auto& file : t.runtime_files) {
    if (!fs::is_regular_file(file)) {
      r.prepared = false;
      r.issues.push_back("Falta dependência da preparação; prepare novamente: " + Utf8(file));
    }
  }
  if (fs::exists(PreparationMarker(paths, t))) {
    r.prepared = false;
    r.issues.push_back("Preparação anterior incompleta. Clique em Preparar para retomar; o executável antigo não valida essa tentativa.");
  }
  return r;
}
size_t Import(const ProjectPaths& paths, const Target& t, const fs::path& source,
              const std::function<bool()>& cancelled, bool allow_partial) {
  Inside(t.game_root, paths.inputs());
  if (!t.importable) throw std::runtime_error("Importação guiada ainda não disponível para este alvo.");
  auto stop = [&] { if (cancelled && cancelled()) throw std::runtime_error("Importação cancelada; arquivos já verificados foram preservados."); };
  const bool single = fs::is_regular_file(source);
  if (!single && !fs::is_directory(source)) throw std::runtime_error("Selecione um arquivo ou uma pasta de conteúdo extraído.");
  std::map<fs::path, fs::path> copies;
  auto consider = [&](const fs::path& file) {
    for (const auto& i : t.inputs) {
      if (!single && !i.source_relative.empty() && file.lexically_relative(source) != i.source_relative) continue;
      if (!single && file.filename() != PathFromUtf8(i.source_name) && file.filename() != i.destination.filename()) continue;
      if (Matches(file, i.sha256)) copies[i.destination] = file;
    }
  };
  if (single) { stop(); consider(source); }
  // Search only the user-selected folder, never follow directory symlinks.
  for (auto it = single ? fs::recursive_directory_iterator{} : fs::recursive_directory_iterator(source); it != fs::recursive_directory_iterator(); ++it) {
    stop();
    const auto& entry = *it;
    if (entry.is_symlink() || !IsWithin(entry.path(), source)) { it.disable_recursion_pending(); continue; }
    if (!entry.is_regular_file()) continue;
    consider(entry.path());
  }
  uintmax_t needed = 0;
  for (const auto& i : t.inputs) {
    Inside(i.destination, t.game_root);
    if (fs::exists(i.destination)) {
      if (!Matches(i.destination, i.sha256))
        throw std::runtime_error("Destino já contém outro arquivo; nada será sobrescrito: " + Utf8(i.destination));
      copies.erase(i.destination);
    } else if (!copies.contains(i.destination)) {
      if (allow_partial) continue;
      throw std::runtime_error("Conjunto incompleto. Falta o arquivo verificado: " + i.source_name);
    } else needed += fs::file_size(copies.at(i.destination));
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
  for (const auto& [destination, input] : copies) {
    stop();
    Inside(destination, paths.inputs());
    fs::create_directories(destination.parent_path());
    // Unique temporary name; never truncate an existing original or destination.
    const auto staging = destination.parent_path() / (".x360n-import-" + Utf8(destination.filename()));
    const auto temporary = staging / "verified.tmp";
    const auto expected = std::find_if(t.inputs.begin(), t.inputs.end(), [&](const auto& i) { return i.destination == destination; });
    if (expected == t.inputs.end()) throw std::runtime_error("Componente sem identidade.");
    if (fs::exists(staging)) {
      if (!fs::is_directory(staging) || fs::is_symlink(staging) || !IsWithin(staging, destination.parent_path()))
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
      if (!fs::exists(temporary)) fs::copy_file(input, temporary, fs::copy_options::none);
      if (!Matches(temporary, expected->sha256))
        throw std::runtime_error("O conteúdo mudou durante a cópia. Tente novamente.");
      // Publish complete bytes atomically and never replace a concurrent file.
#ifdef _WIN32
      if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
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
