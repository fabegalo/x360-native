#include "catalog.h"
#include "sha256.h"
#include "workspace_lease.h"
#include "host_platform.h"
#include <chrono>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
using Index = std::map<fs::path, std::string>;
Index ReadIndex(const fs::path& file, bool payload = true) {
  io::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Pacote incompleto. Extraia novamente o ZIP completo em outra pasta.");
  Index result;
  std::set<std::string> names;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 66 || line[64] != '\t' || line.substr(0,64).find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("Índice do pacote inválido.");
    auto rel = fs::path(std::u8string(line.begin()+65, line.end()));
    for (const auto& part : rel) if (!IsSafePackageEntryName(Utf8(part))) throw std::runtime_error("Caminho inválido no pacote.");
    if (rel.is_absolute() || rel.empty() || *rel.begin() == ".local" || *rel.begin() == ".git" || *rel.begin() == "external" || *rel.begin() == "data")
      throw std::runtime_error("O pacote tentou incluir dados locais ou dependências não verificadas.");
    auto key = Utf8(rel); for (auto& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (!names.insert(key).second) throw std::runtime_error("Índice do pacote contém caminhos duplicados.");
    result.emplace(rel, line.substr(0,64));
  }
  if (payload && !result.contains("deps.lock.json")) throw std::runtime_error("Pacote sem identidade de dependências.");
  return result;
}
void Safe(const fs::path& root, const fs::path& rel) {
  const auto relative = io::weakly_canonical(root / rel).lexically_relative(io::weakly_canonical(root));
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    throw std::runtime_error("Pasta redirecionada para fora do ambiente. Arquivos preservados.");
  auto part = root;
  for (const auto& segment : rel) {
    part /= segment;
    if (io::is_redirect(part)) throw std::runtime_error("Arquivo redirecionado. Arquivos preservados.");
  }
}
bool Matches(const fs::path& path, const std::string& hash) {
  const auto d = DigestFile(path); return d && d->sha256 == hash;
}
Index Payload(const fs::path& payload) {
  auto index = ReadIndex(payload / "payload.sha256");
  for (const auto& [rel, hash] : index) {
    Safe(payload, rel);
    if (!Matches(payload / rel, hash)) throw std::runtime_error("Pacote incompleto ou alterado. Extraia novamente o ZIP completo. Arquivo: " + Utf8(rel));
  }
  return index;
}
void Publish(const fs::path& source, const fs::path& dest) {
  std::error_code ec;
  io::rename(source, dest, ec);
  if (ec) throw std::runtime_error("Não foi possível concluir a atualização. Feche outros aplicativos e tente novamente; seus dados foram preservados.");
}
std::string Component(const fs::path& rel) {
  if (rel == "deps.lock.json" || *rel.begin() == "patches") return "SDK e correções";
  if (rel == "scripts/setup/desktop-tools.lock.json") return "ferramentas de preparação";
  if (*rel.begin() == "titles") return "preparações de jogos e dashboards";
  return "componentes do aplicativo";
}
Index Retired(const fs::path& destination, const Index& index) {
  Index old;
  const auto previous = destination / ".local/payload.sha256";
  Safe(destination, ".local/payload.sha256");
  if (io::exists(previous)) old = ReadIndex(previous);
  // Legacy installations did not retain an index. Archive obsolete build inputs
  // in managed source directories, but retain AOT outputs and all user content.
  const std::set<std::string> extensions{".cpp", ".c", ".h", ".hpp", ".cmake", ".toml", ".json", ".ps1", ".cmd", ".py"};
  for (auto top : {"cmake", "src", "scripts", "titles", "tests", "patches"}) {
    Safe(destination, top);
    if (!io::is_directory(destination / top)) continue;
    for (auto it = io::recursive_directory_iterator(destination / top); it != io::recursive_directory_iterator(); ++it) {
      const auto rel = it->path().lexically_relative(destination);
      Safe(destination, rel);
      if (it->is_directory() && it->path().filename() == "generated") { it.disable_recursion_pending(); continue; }
      if (it->is_regular_file() && !index.contains(rel) && (extensions.contains(Utf8(it->path().extension())) || it->path().filename() == "CMakeLists.txt")) old.emplace(rel, "");
    }
  }
  for (const auto& [rel, hash] : index) old.erase(rel);
  return old;
}
}
std::string PackageExecutable(const std::string& name) { return name + ExecutableSuffix(); }
void VerifyPackage(const fs::path& directory, const std::string& expected_payload) {
  if (!expected_payload.empty() && !Matches(directory / "app/workspace/payload.sha256", expected_payload))
    throw std::runtime_error("Os componentes do aplicativo pertencem a versões diferentes. Extraia novamente o ZIP completo; seus dados serão preservados.");
  auto index = ReadIndex(directory / "package.sha256", false);
  for (const auto& required : {PackageExecutable("x360-native"), PackageExecutable("app/x360n"), PackageExecutable("app/x360n-extract"),
                               std::string("app/workspace/payload.sha256"), std::string("package.json")})
    if (!index.contains(required)) throw std::runtime_error("Pacote incompleto. Extraia novamente o ZIP completo.");
  for (const auto& [rel, hash] : index) {
    Safe(directory, rel);
    if (!Matches(directory / rel, hash)) throw std::runtime_error("Esta cópia do aplicativo está incompleta ou foi alterada. Extraia novamente o ZIP completo; seus jogos e dados serão preservados. Arquivo: " + Utf8(rel));
  }
  for (auto it = io::recursive_directory_iterator(directory); it != io::recursive_directory_iterator(); ++it) {
    const auto rel = it->path().lexically_relative(directory);
    if (*rel.begin() == "data") { it.disable_recursion_pending(); continue; }
    auto ext = Utf8(it->path().extension()); for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    // Programs and native modules of any installation (POSIX programs have no extension).
    const bool component = ext == ".dll" || ext == ".exe" || ext == ".so" || ext == ".dylib" ||
        (ext.empty() && it->is_regular_file() && Utf8(it->path().filename()).starts_with("x360"));
    if (component && !index.contains(rel))
      throw std::runtime_error("Há componentes de outra instalação nesta pasta. Extraia o ZIP em uma pasta vazia e selecione seu ambiente anterior.");
  }
  Payload(directory / "app/workspace");
}
PayloadStatus InspectEnvironment(const fs::path& payload, const fs::path& destination) {
  const auto index = Payload(payload);
  PayloadStatus status;
  Safe(destination, ".local/payload-update");
  status.interrupted = io::exists(destination / ".local/payload-update/pending");
  std::set<std::string> components;
  for (const auto& [rel, hash] : index) {
    Safe(destination, rel);
    if (!Matches(destination / rel, hash)) components.insert(Component(rel));
  }
  for (const auto& [rel, hash] : Retired(destination, index)) {
    Safe(destination, rel);
    if (!index.contains(rel) && io::exists(destination / rel)) components.insert(Component(rel));
  }
  status.components.assign(components.begin(), components.end());
  status.current = components.empty() && !status.interrupted;
  return status;
}
void InitializeEnvironment(const fs::path& payload, const fs::path& destination,
                           bool allow_update, const std::function<void()>& checkpoint) {
  const auto index = Payload(payload); // Validate before creating any directory.
  const auto status = InspectEnvironment(payload, destination);
  if (!allow_update && !status.current && io::exists(destination / "deps.lock.json"))
    throw std::runtime_error("Esta instalação precisa ser atualizada para continuar. Seus jogos e dados serão preservados.");
  WorkspaceLease lease(ProjectPaths{destination});
  Safe(destination, ".local/payload.sha256");
  if (status.current && Matches(destination / ".local/payload.sha256", DigestFile(payload / "payload.sha256")->sha256)) return;
  Safe(destination, ".local/payload-backups");
  const auto txn = destination / ".local/payload-update";
  const auto identity = DigestFile(payload / "payload.sha256")->sha256;
  if (io::exists(txn / "pending")) {
    io::ifstream in(txn / "pending"); std::string pending; std::getline(in, pending);
    if (pending != identity) throw std::runtime_error("Uma atualização foi interrompida. Reabra a versão que iniciou a atualização para concluí-la. Seus dados foram preservados.");
  } else {
    if (io::exists(txn)) {
      io::create_directories(destination / ".local/payload-backups");
      io::rename(txn, destination / ".local/payload-backups" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    io::create_directories(txn);
    io::ofstream(txn / "pending.tmp", std::ios::binary) << identity << '\n';
    Publish(txn / "pending.tmp", txn / "pending");
  }
  // Durable marker before managed file changes. Retry is idempotent and the
  // original backup is never overwritten. User directories are not in the index.
  auto backup = [&](const fs::path& rel) {
    Safe(destination, rel); Safe(txn, fs::path("backup") / rel);
    if (io::exists(destination / rel) && !io::exists(txn / "backup" / rel)) {
      io::create_directories((txn / "backup" / rel).parent_path());
      const auto staged = txn / "backup-stage" / rel;
      Safe(txn, fs::path("backup-stage") / rel);
      io::create_directories(staged.parent_path());
      io::copy_file(destination / rel, staged, fs::copy_options::overwrite_existing);
      if (DigestFile(destination / rel)->sha256 != DigestFile(staged)->sha256)
        throw std::runtime_error("Não foi possível preservar os arquivos anteriores. Atualização interrompida com segurança.");
      Publish(staged, txn / "backup" / rel);
    }
  };
  for (const auto& [rel, hash] : index) {
    if (Matches(destination / rel, hash)) continue;
    backup(rel);
    const auto stage = txn / "stage" / rel;
    Safe(txn, fs::path("stage") / rel);
    io::create_directories(stage.parent_path());
    io::copy_file(payload / rel, stage, fs::copy_options::overwrite_existing);
    if (!Matches(stage, hash)) throw std::runtime_error("Falha ao verificar a cópia do pacote.");
    io::create_directories((destination / rel).parent_path());
    Publish(stage, destination / rel);
    if (checkpoint) checkpoint();
  }
  for (const auto& [rel, hash] : Retired(destination, index)) {
    if (io::exists(destination / rel)) { backup(rel); io::remove(destination / rel); if (checkpoint) checkpoint(); }
  }
  for (const auto& [rel, hash] : index) if (!Matches(destination / rel, hash)) throw std::runtime_error("A atualização ainda não foi validada. Tente novamente.");
  io::copy_file(payload / "payload.sha256", txn / "identity", fs::copy_options::overwrite_existing);
  Publish(txn / "identity", destination / ".local/payload.sha256");
  io::remove(txn / "pending"); // Last: only verified environments are ready.
}
}
