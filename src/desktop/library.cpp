#include "catalog.h"
#include "sha256.h"
#include "workspace_lease.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
bool WithinNew(const fs::path& file, const fs::path& root) {
  auto relative = fs::weakly_canonical(file).lexically_relative(fs::weakly_canonical(root));
  return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
const Target& Dependency(const std::vector<Target>& catalog, const std::string& id) {
  auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto& t) { return t.id == id; });
  if (it == catalog.end()) throw std::runtime_error("Perfil de dependência ausente: " + id);
  return *it;
}
struct Entry { fs::path folder, header; std::string bytes; };
Entry Describe(const Target& session, const Target& game) {
  const auto& c = game.library;
  for (const auto& s : {c.title_id, c.content_type})
    if (s.size() != 8 || s.find_first_not_of("0123456789ABCDEF") != std::string::npos)
      throw std::runtime_error("Identidade de biblioteca inválida.");
  if (session.user_root.empty() || !IsSafePackageEntryName(c.file_name) || c.file_name.size() > 42)
    throw std::runtime_error("Destino de biblioteca inválido.");
  // ReXGlue's XCONTENT_AGGREGATE_DATA ABI (content_manager.h): BE fields,
  // 128 UTF-16 units at 8, 42 filename bytes at 0x108, aligned XUID at 0x138,
  // title ID at 0x140; total 0x148. Zero XUID means shared installed content.
  // No license-mask trailer is created: extraction does not grant a license.
  std::string bytes(0x148, '\0');
  auto be32 = [&](size_t offset, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = char(v >> (24 - i * 8));
  };
  be32(0, 1); // SDK DummyDeviceId::HDD
  be32(4, uint32_t(std::stoul(c.content_type, nullptr, 16)));
  be32(0x140, uint32_t(std::stoul(c.title_id, nullptr, 16)));
  const auto name = PathFromUtf8(c.display_name).u16string();
  if (name.empty() || name.size() >= 128) throw std::runtime_error("Nome de conteúdo inválido.");
  for (size_t i = 0; i < name.size(); ++i) {
    bytes[8 + 2 * i] = char(name[i] >> 8); bytes[9 + 2 * i] = char(name[i]);
  }
  bytes.replace(0x108, c.file_name.size(), c.file_name);
  auto base = session.user_root / "0000000000000000" / c.title_id;
  return {base / c.content_type / c.file_name,
          base / "Headers" / c.content_type / (c.file_name + ".header"), bytes};
}
bool Same(const fs::path& file, const std::string& hash) {
  auto d = DigestFile(file); return d && d->sha256 == hash;
}
std::string Hash(const std::string& bytes) {
  return Sha256Hex({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
}
void CheckSource(const Target& game) {
  if (game.inputs.empty()) throw std::runtime_error("Conteúdo sem inventário verificado.");
  for (const auto& input : game.inputs)
    if (!IsWithin(input.destination, game.game_root) || !Same(input.destination, input.sha256))
      throw std::runtime_error("Importe o conjunto completo antes de registrar na biblioteca: " + Utf8(input.destination));
}
std::vector<std::string> InstalledGames(const Target& session, const std::vector<Target>& catalog) {
  auto ids = session.dependencies;
  for (const auto& id : session.library_targets) {
    const auto& game = Dependency(catalog, id);
    // Optional games are advertised only after their complete contents and build
    // exist. A dashboard can be installed with an empty game library.
    if (!fs::is_regular_file(game.exe) || game.inputs.empty()) continue;
    if (std::all_of(game.inputs.begin(), game.inputs.end(), [](const auto& i) { return Same(i.destination, i.sha256); }))
      ids.push_back(id);
  }
  return ids;
}
}
std::vector<std::string> InspectLibrary(const Target& session, const std::vector<Target>& catalog) {
  std::vector<std::string> issues;
  if (!session.session) return issues;
  for (const auto& id : InstalledGames(session, catalog)) {
    const auto& game = Dependency(catalog, id);
    if (game.library.title_id.empty()) continue;
    const auto entry = Describe(session, game);
    bool valid = Same(entry.header, Hash(entry.bytes));
    for (const auto& input : game.inputs) {
      auto destination = entry.folder / input.destination.lexically_relative(game.game_root);
      valid = valid && Same(destination, input.sha256);
    }
    if (!valid) issues.push_back("Prepare para registrar o conteúdo verificado na My Games: " + id);
  }
  return issues;
}
void PrepareLibrary(const ProjectPaths& paths, const Target& session, const std::vector<Target>& catalog,
                    const std::function<bool()>& cancelled) {
  if (!session.session) return;
  WorkspaceLease lease(paths);
  auto stop = [&] { if (cancelled && cancelled()) throw std::runtime_error("Registro cancelado. Conteúdo e saves existentes foram preservados."); };
  for (const auto& id : InstalledGames(session, catalog)) {
    stop();
    const auto& game = Dependency(catalog, id);
    if (game.library.title_id.empty()) continue;
    CheckSource(game);
    const auto entry = Describe(session, game);
    // Preflight the complete installed set before writing. An existing junction
    // from the historical manual install is accepted only for identical bytes;
    // never write through it. New installs use ordinary files, without elevation.
    uintmax_t required = entry.bytes.size();
    if (!WithinNew(entry.header, session.user_root)) throw std::runtime_error("Catálogo fora da pasta de dados selecionada.");
    if (fs::exists(entry.header) && !Same(entry.header, Hash(entry.bytes)))
      throw std::runtime_error("Registro existente diferente; preservado: " + Utf8(entry.header));
    for (const auto& input : game.inputs) {
      auto dest = entry.folder / input.destination.lexically_relative(game.game_root);
      if (fs::exists(dest)) {
        if (!Same(dest, input.sha256)) throw std::runtime_error("Conteúdo instalado diferente; preservado: " + Utf8(dest));
      } else {
        if (!WithinNew(dest, session.user_root)) throw std::runtime_error("Destino redirecionado; importação preservada.");
        required += fs::file_size(input.destination);
      }
    }
    fs::create_directories(session.user_root);
    if (fs::space(session.user_root).available < required) throw std::runtime_error("Espaço insuficiente na pasta de dados para a biblioteca.");
    for (const auto& input : game.inputs) {
      stop();
      auto dest = entry.folder / input.destination.lexically_relative(game.game_root);
      if (fs::exists(dest)) continue;
      fs::create_directories(dest.parent_path());
      auto tmp = dest; tmp += ".x360n-library-tmp";
      if (fs::exists(tmp) && !Same(tmp, input.sha256)) {
        if (!IsWithin(tmp, session.user_root) || fs::is_symlink(tmp)) throw std::runtime_error("Cópia temporária redirecionada; preservada.");
        auto interrupted = tmp; interrupted += ".interrupted-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::rename(tmp, interrupted);
      }
      if (!fs::exists(tmp)) fs::copy_file(input.destination, tmp, fs::copy_options::none);
      if (!Same(tmp, input.sha256)) throw std::runtime_error("Falha na verificação da cópia da biblioteca.");
      fs::rename(tmp, dest);
    }
    stop();
    // Publish the enumeration header last: interrupted copies aren't advertised.
    if (!fs::exists(entry.header)) {
      fs::create_directories(entry.header.parent_path());
      auto tmp = entry.header; tmp += ".x360n-library-tmp";
      if (fs::exists(tmp) && !Same(tmp, Hash(entry.bytes))) throw std::runtime_error("Registro temporário diferente; preservado.");
      if (!fs::exists(tmp)) {
        std::ofstream out(tmp, std::ios::binary); out.write(entry.bytes.data(), entry.bytes.size());
        out.close(); if (!out) throw std::runtime_error("Não foi possível gravar o catálogo.");
      }
      fs::rename(tmp, entry.header);
    }
  }
}
}
