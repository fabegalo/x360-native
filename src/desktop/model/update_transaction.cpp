#include "update.h"
#include "catalog.h"
#include "sha256.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
using Json = nlohmann::json;
using Index = std::map<std::string, std::string>;
std::string Text(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Arquivo de transação ausente: " + Utf8(file));
  return {std::istreambuf_iterator<char>(in), {}};
}
std::string Lower(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return s;
}
void CheckPath(const fs::path& path) {
  // Reject every redirect, including junctions and ancestors of the root.
  fs::path prefix;
  for (const auto& part : fs::absolute(path).lexically_normal()) {
    prefix /= part;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(prefix.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Caminho redirecionado na atualização: " + Utf8(prefix));
#else
    if (fs::is_symlink(prefix)) throw std::runtime_error("Caminho redirecionado na atualização");
#endif
  }
}
fs::path Under(const fs::path& root, const std::string& relative) {
  const auto rel = fs::u8path(relative);
  if (relative.empty() || rel.is_absolute() || rel.has_root_path() || relative.find('\\') != std::string::npos)
    throw std::runtime_error("Caminho inválido no inventário da atualização");
  for (const auto& part : rel)
    if (!IsSafePackageEntryName(Utf8(part))) throw std::runtime_error("Componente inválido na atualização");
  const auto top = Lower(Utf8(*rel.begin()));
  if (top == "data" || top == ".local" || top == ".git" || top == "recover-update.cmd" || top == "recover-update.sh")
    throw std::runtime_error("Inventário da atualização tentou incluir dados locais ou recuperação");
  const auto result = root / rel;
  CheckPath(result);
  return result;
}
bool HashMatches(const fs::path& path, const std::string& expected) {
  if (expected.empty()) return !fs::exists(path);
  const auto hash = DigestFile(path);
  return hash && hash->sha256 == expected;
}
void DurableMove(const fs::path& from, const fs::path& to) {
  CheckPath(from); CheckPath(to);
  fs::create_directories(to.parent_path());
  if (fs::exists(to)) throw std::runtime_error("Destino de recuperação já existe: " + Utf8(to));
#ifdef _WIN32
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Falha ao mover arquivo da atualização (Windows " + std::to_string(GetLastError()) + "): " + Utf8(from));
#else
  fs::rename(from, to);
  for (const auto& dir : {from.parent_path(), to.parent_path()}) {
    int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY); if (fd < 0 || fsync(fd)) { if (fd >= 0) close(fd); throw std::runtime_error("fsync directory failed"); } close(fd);
  }
#endif
}
void DurableWrite(const fs::path& path, const std::string& data) {
  CheckPath(path); fs::create_directories(path.parent_path());
  const auto temp = fs::path(path.native() + fs::path(".new").native());
  CheckPath(temp);
#ifdef _WIN32
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Não foi possível gravar o diário de atualização");
  DWORD written = 0;
  const bool ok = WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Não foi possível persistir o diário de atualização");
#else
  int fd = open(temp.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
  if (fd < 0) throw std::runtime_error("Cannot open update journal");
  const bool ok = write(fd, data.data(), data.size()) == ssize_t(data.size()) && fsync(fd) == 0;
  close(fd); if (!ok) throw std::runtime_error("Cannot flush update journal");
  fs::rename(temp, path);
  fd = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0 || fsync(fd)) { if (fd >= 0) close(fd); throw std::runtime_error("Cannot flush journal directory"); } close(fd);
#endif
}
struct UpdateLock {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  explicit UpdateLock(const fs::path& package) {
    const auto path = package / "data/updates/update.lock"; CheckPath(path);
    fs::create_directories(path.parent_path());
    handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Outra atualização ou recuperação está em andamento");
  }
  ~UpdateLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
  int fd;
  explicit UpdateLock(const fs::path& package) {
    const auto path = package / "data/updates/update.lock"; CheckPath(path); fs::create_directories(path.parent_path());
    fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB)) { if (fd >= 0) close(fd); throw std::runtime_error("Update already active"); }
  }
  ~UpdateLock() { close(fd); }
#endif
};
Index Inventory(const fs::path& package, bool incoming) {
  CheckPath(package);
  VerifyPackage(package);
  Index result;
  std::set<std::string> folded;
  std::istringstream lines(Text(package / "package.sha256"));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 66 || line[64] != '\t' || line.substr(0, 64).find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("Índice inválido na atualização");
    const auto name = line.substr(65);
    Under(package, name);
    if (Lower(name) == "package.sha256" || !folded.insert(Lower(name)).second)
      throw std::runtime_error("Índice duplicado ou autorreferente na atualização");
    result.emplace(name, line.substr(0, 64));
  }
  result.emplace("package.sha256", DigestFile(package / "package.sha256")->sha256);
  for (auto it = fs::recursive_directory_iterator(package); it != fs::recursive_directory_iterator(); ++it) {
    const auto relative = it->path().lexically_relative(package);
    if (Lower(Utf8(*relative.begin())) == "data" && !incoming) { it.disable_recursion_pending(); continue; }
    CheckPath(it->path());
    auto name = Utf8(relative); std::replace(name.begin(), name.end(), '\\', '/');
    if (incoming && !it->is_directory() && !result.contains(name))
      throw std::runtime_error("Arquivo não inventariado no pacote novo");
    if (incoming && Lower(Utf8(*relative.begin())) == "data") throw std::runtime_error("Dados de usuário no pacote novo");
  }
  return result;
}
void ValidateTransaction(const fs::path& package, const fs::path& transaction) {
  CheckPath(package); CheckPath(transaction);
  const auto parent = fs::absolute(package / "data/updates").lexically_normal();
  const auto name = Utf8(transaction.filename());
  if (fs::absolute(transaction.parent_path()).lexically_normal() != parent ||
      name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
    throw std::runtime_error("A transação deve estar diretamente em data/updates");
}
void FlushCopy(const fs::path& source, const fs::path& destination) {
  CheckPath(source); CheckPath(destination); fs::create_directories(destination.parent_path());
  fs::copy_file(source, destination);
#ifdef _WIN32
  HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Não foi possível persistir a cópia de recuperação");
  const bool ok = FlushFileBuffers(file); CloseHandle(file);
#else
  int file = open(destination.c_str(), O_WRONLY);
  const bool ok = file >= 0 && fsync(file) == 0; if (file >= 0) close(file);
#endif
  if (!ok) throw std::runtime_error("Falha ao persistir cópia da atualização");
}
Json ReadPlan(const fs::path& package, const fs::path& transaction) {
  ValidateTransaction(package, transaction);
  const auto plan = Json::parse(Text(transaction / "plan.json"));
  if (plan.value("schema", 0) != 1 || !plan.contains("files") || !plan["files"].is_array() || plan["files"].empty())
    throw std::runtime_error("Diário de atualização inválido; recuperação interrompida");
  std::set<std::string> names;
  for (const auto& file : plan["files"]) {
    const auto name = file.at("path").get<std::string>(); Under(package, name);
    if (!names.insert(Lower(name)).second) throw std::runtime_error("Diário duplicado");
    for (auto side : {"old", "new"}) {
      const auto hash = file.at(side).get<std::string>();
      if (!hash.empty() && (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)) throw std::runtime_error("Hash inválido no diário");
    }
    if (file["old"] == "" && file["new"] == "") throw std::runtime_error("Operação vazia no diário");
  }
  return plan;
}
void Checkpoint(const UpdateCheckpoint& checkpoint, const char* step) { if (checkpoint) checkpoint(step); }
void Rollback(const fs::path& package, const fs::path& transaction, const UpdateCheckpoint& checkpoint) {
  const auto plan = ReadPlan(package, transaction);
  if (fs::exists(transaction / "rolled-back")) { VerifyPackage(package); return; }
  // Authenticate every reachable copy before making any recovery move.
  for (const auto& item : plan["files"]) {
    const auto name = item["path"].get<std::string>(), old_hash = item["old"].get<std::string>(), new_hash = item["new"].get<std::string>();
    const auto live = Under(package, name), backup = Under(transaction / "backup", name);
    if (fs::exists(backup) && (old_hash.empty() || !HashMatches(backup, old_hash))) throw std::runtime_error("Backup de atualização alterado");
    if (fs::exists(live) && !HashMatches(live, old_hash) && !HashMatches(live, new_hash)) throw std::runtime_error("Arquivo instalado alterado; recuperação preservada para diagnóstico");
    if (!old_hash.empty() && !fs::exists(backup) && !HashMatches(live, old_hash)) throw std::runtime_error("Versão anterior incompleta; rollback não foi confirmado");
  }
  for (const auto& item : plan["files"]) {
    const auto name = item["path"].get<std::string>(), old_hash = item["old"].get<std::string>();
    const auto live = Under(package, name), backup = Under(transaction / "backup", name);
    const bool saved = fs::exists(backup);
    if (fs::exists(live) && (saved || old_hash.empty())) {
      Checkpoint(checkpoint, "before-remove-new");
      DurableMove(live, Under(transaction / "rejected", name));
      Checkpoint(checkpoint, "after-remove-new");
    }
    if (saved) {
      Checkpoint(checkpoint, "before-restore-old"); DurableMove(backup, live); Checkpoint(checkpoint, "after-restore-old");
    }
  }
  VerifyPackage(package);
  DurableWrite(transaction / "rolled-back", "1\n");
  Checkpoint(checkpoint, "rolled-back");
}
}

void RecoverUpdate(const fs::path& package, const fs::path& transaction, const UpdateCheckpoint& checkpoint) {
  ValidateTransaction(package, transaction);
  UpdateLock lock(package);
  Rollback(package, transaction, checkpoint);
}

void ApplyUpdate(const fs::path& package, const fs::path& staged, const std::string& version,
                 const fs::path& transaction, const UpdateCheckpoint& checkpoint) {
  ValidateTransaction(package, transaction);
  CheckPath(staged);
  // Never consume staging from inside an application component or a previous transaction.
  const auto stage_relative = fs::absolute(staged).lexically_normal().lexically_relative(fs::absolute(package).lexically_normal());
  if (!stage_relative.empty() && *stage_relative.begin() != ".." && *stage_relative.begin() != "data")
    throw std::runtime_error("Área de staging sobreposta ao aplicativo");
  UpdateLock lock(package);
  if (fs::exists(transaction)) throw std::runtime_error("Transação já existe; recupere-a ou escolha outra pasta");
  for (const auto& entry : fs::directory_iterator(package / "data/updates"))
    if (entry.is_directory() && fs::exists(entry.path() / "plan.json") &&
        !fs::exists(entry.path() / "rolled-back") && !fs::exists(entry.path() / "confirmed"))
      throw std::runtime_error("Atualização anterior pendente; inicie-a ou execute " + RecoveryScriptName());
  const auto old = Inventory(package, false), incoming = Inventory(staged, true);
#ifdef _WIN32
  constexpr const char* recovery_header = "@echo off\r\n:: x360-native update recovery v1\r\n";
#else
  constexpr const char* recovery_header = "#!/bin/sh\n# x360-native update recovery v1\n";
#endif
  const auto script = package / RecoveryScriptName();
  if (fs::exists(script) && !Text(script).starts_with(recovery_header))
    throw std::runtime_error(RecoveryScriptName() + " já existe e não pertence ao atualizador; arquivo preservado");
  if (Json::parse(Text(staged / "package.json")).value("version", std::string()) != version)
    throw std::runtime_error("Pacote não corresponde à versão anunciada");
  for (const auto& [name, hash] : incoming) {
    const auto destination = Under(package, name);
    if (!old.contains(name) && fs::exists(destination)) throw std::runtime_error("Atualização colide com arquivo local não gerenciado");
    if (!old.contains(name)) for (auto parent = destination.parent_path(); parent != package && parent != parent.root_path(); parent = parent.parent_path())
      if (fs::exists(parent) && !fs::is_directory(parent)) throw std::runtime_error("Atualização colide com diretório local");
  }
  Json plan{{"schema", 1}, {"version", version}, {"files", Json::array()}};
  std::set<std::string> names;
  for (const auto& [name, hash] : old) names.insert(name);
  for (const auto& [name, hash] : incoming) names.insert(name);
  for (const auto& name : names) plan["files"].push_back({{"path", name}, {"old", old.contains(name) ? old.at(name) : ""}, {"new", incoming.contains(name) ? incoming.at(name) : ""}});
  fs::create_directories(transaction / "incoming");
  for (const auto& [name, hash] : incoming) {
    const auto to = Under(transaction / "incoming", name); fs::create_directories(to.parent_path());
    FlushCopy(Under(staged, name), to);
    if (!HashMatches(to, hash)) throw std::runtime_error("Staging mudou durante a cópia");
  }
  // Recovery survives a missing launcher/app directory and a broken new build.
  // This is the old supervisor, copied outside all application moves.
  FlushCopy(package / PackageExecutable("app/x360n"), transaction / PackageExecutable("recovery"));
#ifdef _WIN32
  const auto relative = "data\\updates\\" + Utf8(transaction.filename());
  DurableWrite(script, std::string(recovery_header) + "\"%~dp0" + relative + "\\recovery.exe\" recover-update --package \"%~dp0.\" --transaction \"%~dp0" + relative + "\"\r\nif errorlevel 1 pause\r\n");
#else
  const auto relative = "data/updates/" + Utf8(transaction.filename());
  DurableWrite(script, std::string(recovery_header) + "here=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nexec \"$here/" + relative +
               "/recovery\" recover-update --package \"$here\" --transaction \"$here/" + relative + "\"\n");
  fs::permissions(script, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add);
#endif
  DurableWrite(transaction / "plan.json", plan.dump(2) + "\n");
  try {
    Checkpoint(checkpoint, "journal");
    for (const auto& [name, hash] : old) {
      if (!HashMatches(Under(package, name), hash)) throw std::runtime_error("Instalação mudou durante atualização");
      Checkpoint(checkpoint, "before-backup");
      DurableMove(Under(package, name), Under(transaction / "backup", name));
      Checkpoint(checkpoint, "after-backup");
    }
    for (const auto& [name, hash] : incoming) {
      Checkpoint(checkpoint, "before-install");
      DurableMove(Under(transaction / "incoming", name), Under(package, name));
      Checkpoint(checkpoint, "after-install");
    }
    VerifyPackage(package);
    DurableWrite(transaction / "installed", "1\n");
    Checkpoint(checkpoint, "installed");
  } catch (const std::exception& error) {
    const auto cause = std::string(error.what());
    try { Rollback(package, transaction, {}); }
    catch (const std::exception& recovery) {
      throw std::runtime_error(cause + "\nRollback pendente: " + recovery.what() + "\nFeche o aplicativo e execute " + RecoveryScriptName() + ".");
    }
    throw std::runtime_error(cause + "\nVersão anterior restaurada e verificada.");
  }
}

std::string RecoveryScriptName() {
#ifdef _WIN32
  return "recover-update.cmd";
#else
  return "recover-update.sh";
#endif
}
void ConfirmUpdates(const fs::path& package, const std::string& version) {
  const auto updates = package / "data/updates";
  if (!fs::exists(updates)) return;
  CheckPath(updates); UpdateLock lock(package);
  for (const auto& entry : fs::directory_iterator(updates)) {
    if (!entry.is_directory() || !fs::exists(entry.path() / "plan.json") || fs::exists(entry.path() / "rolled-back") || fs::exists(entry.path() / "confirmed")) continue;
    const auto plan = ReadPlan(package, entry.path());
    if (!fs::exists(entry.path() / "installed") || plan.at("version") != version)
      throw std::runtime_error("Atualização interrompida. Feche o aplicativo e execute " + RecoveryScriptName() + ".");
    VerifyPackage(package);
    DurableWrite(entry.path() / "confirmed", "1\n");
  }
}
}
