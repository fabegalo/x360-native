#include "catalog.h"
#include "sha256.h"
#include "xex_identity.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace x360n::desktop {
std::vector<ContentIdentity> IdentifyContent(const std::vector<Target>& targets,
    const fs::path& source, const std::function<bool()>& cancelled) {
  std::vector<ContentIdentity> result;
  const bool single = fs::is_regular_file(source);
  auto inspect = [&](const fs::path& file) {
    if (cancelled && cancelled()) throw std::runtime_error("Identificação cancelada.");
    // Ignore unrelated assets in a folder, while still accepting any single file.
    bool candidate = single || file.extension() == ".xex";
    for (const auto& t : targets) for (const auto& i : t.inputs)
      if (i.source_relative.empty() && (file.filename() == PathFromUtf8(i.source_name) || file.filename() == i.destination.filename())) candidate = true;
    if (!candidate) return;
    ContentIdentity item; item.file = file;
    auto digest = DigestFile(file);
    if (!digest) throw std::runtime_error("Não foi possível ler: " + Utf8(file.filename()));
    for (const auto& t : targets) {
      const auto match = std::find_if(t.inputs.begin(), t.inputs.end(), [&](const auto& i) { return i.sha256 == digest->sha256; });
      if (match != t.inputs.end()) {
        item.recognized = true; item.target_id = t.id; item.label = t.label; item.kind = t.kind;
        result.push_back(item);
        return;
      }
    }
    // Bound header allocation, exactly as the supervisor's identity reader does.
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> header(24); in.read(reinterpret_cast<char*>(header.data()), header.size());
    header.resize(size_t(in.gcount()));
    if (header.size() >= 12 && header[0] == 'X' && header[1] == 'E' && header[2] == 'X' && header[3] == '2') {
      const uint32_t size = uint32_t(header[8]) << 24 | uint32_t(header[9]) << 16 | uint32_t(header[10]) << 8 | header[11];
      if (size >= 24 && size <= 16 * 1024 * 1024 && size <= digest->size) {
        header.resize(size); in.clear(); in.seekg(0); in.read(reinterpret_cast<char*>(header.data()), size);
        header.resize(size_t(in.gcount()));
      }
    }
    auto parsed = ParseXexIdentity(header);
    if (parsed.identity) {
      const auto& identity = *parsed.identity;
      item.kind = IsDashboardTitleId(identity.title_id) ? "dashboard" : "game";
      item.label = item.kind == "dashboard" ? "Dashboard Xbox 360" : "Título Xbox 360";
      if (identity.has_execution_info) item.version = identity.version.ToString();
      item.problem = "Versão identificada, mas sem preparação disponível neste catálogo. Seus arquivos foram preservados.";
    } else if (parsed.format == XexFormat::kXex1 || parsed.format == XexFormat::kXexOtherVariant) {
      item.label = "Formato antigo do Xbox 360";
      item.problem = "Formato reconhecido. Leitura de versão e preparação ainda não disponíveis.";
    } else if (parsed.format == XexFormat::kXex2) {
      item.label = "Arquivo incompleto ou inválido";
      item.problem = "Não foi possível ler a identidade. Selecione novamente o arquivo original completo.";
    } else {
      item.label = "Arquivo não reconhecido";
      item.problem = "Selecione uma ISO, um pacote Xbox 360 (CON/LIVE/PIRS), um arquivo XEX ou uma pasta extraída. Arquivos ZIP, RAR e 7z precisam ser descompactados primeiro.";
    }
    result.push_back(std::move(item));
  };
  if (single) inspect(source);
  else if (fs::is_directory(source)) {
    for (auto it = fs::recursive_directory_iterator(source); it != fs::recursive_directory_iterator(); ++it) {
      if (cancelled && cancelled()) throw std::runtime_error("Identificação cancelada.");
      if (it->is_symlink() || !IsWithin(it->path(), source)) { it.disable_recursion_pending(); continue; }
      if (it->is_regular_file()) inspect(it->path());
    }
  } else throw std::runtime_error("O arquivo ou a pasta selecionada não está disponível.");
  return result;
}
}
