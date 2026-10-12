#include "catalog.h"
#include "sha256.h"
#include <chrono>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace x360n::desktop {
void InitializeEnvironment(const fs::path& payload, const fs::path& destination) {
  std::ifstream index(payload / "payload.sha256", std::ios::binary);
  if (!index) throw std::runtime_error("Pacote incompleto: falta workspace/payload.sha256.");
  struct File { fs::path source, output; std::string hash; };
  std::vector<File> copies;
  bool root_found = false;
  uintmax_t bytes = 0;
  // Validate every path, source and existing destination before any copy.
  for (std::string line; std::getline(index, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 67 || line[64] != '\t' || line.substr(0, 64).find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("Índice do pacote inválido.");
    auto rel = fs::path(std::u8string(line.begin() + 65, line.end()));
    for (auto& part : rel)
      if (!IsSafePackageEntryName(Utf8(part))) throw std::runtime_error("Caminho inválido no pacote.");
    if (rel.is_absolute() || *rel.begin() == ".local" || *rel.begin() == ".git" || *rel.begin() == "external")
      throw std::runtime_error("O pacote tentou incluir dados locais ou dependências não verificadas.");
    auto source = payload / rel, output = destination / rel;
    auto normalized = fs::weakly_canonical(output).lexically_relative(fs::weakly_canonical(destination));
    if (normalized.empty() || normalized.is_absolute() || *normalized.begin() == ".." || !IsWithin(source, payload))
      throw std::runtime_error("Caminho do pacote fora do ambiente selecionado.");
    auto expected = line.substr(0, 64);
    auto digest = DigestFile(source);
    if (!digest || digest->sha256 != expected) throw std::runtime_error("Pacote incompleto ou alterado: " + Utf8(rel));
    if (rel == "deps.lock.json") root_found = true;
    if (fs::exists(output)) {
      auto current = DigestFile(output);
      if (!current || current->sha256 != expected)
        throw std::runtime_error("Esta pasta contém outra versão. Ela foi preservada; selecione uma pasta vazia: " + Utf8(output));
    } else { bytes += digest->size; copies.push_back({source, output, expected}); }
  }
  if (!root_found) throw std::runtime_error("Pacote sem identidade de dependências.");
  fs::create_directories(destination);
  if (fs::space(destination).available < bytes) throw std::runtime_error("Espaço insuficiente para preparar o ambiente.");
  // Copy without overwrite. A partial copy is detected by its hash on retry.
  // Publish through the same no-replace mechanism as the content importer.
  for (const auto& file : copies) {
    fs::create_directories(file.output.parent_path());
    auto staging = file.output; staging += ".x360n-tmp";
    if (fs::exists(staging)) {
      if (fs::is_symlink(staging) || !IsWithin(staging, destination))
        throw std::runtime_error("Cópia temporária redirecionada; arquivos preservados.");
      auto previous = DigestFile(staging);
      if (!previous || previous->sha256 != file.hash) {
        auto interrupted = staging; interrupted += ".interrupted-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::rename(staging, interrupted);
      }
    }
    if (!fs::exists(staging)) fs::copy_file(file.source, staging);
    auto copied = DigestFile(staging);
    if (!copied || copied->sha256 != file.hash) throw std::runtime_error("Falha ao verificar a cópia do pacote.");
#ifdef _WIN32
    if (!MoveFileExW(staging.c_str(), file.output.c_str(), MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("Não foi possível concluir a cópia do pacote: " + std::to_string(GetLastError()));
#else
    fs::create_hard_link(staging, file.output); fs::remove(staging);
#endif
  }
}
}
