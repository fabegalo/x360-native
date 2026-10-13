#include "import_source.h"
#include "catalog.h"
#include "child_process.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace x360n::desktop {
ImportSource::ImportSource(const fs::path& original, const fs::path& extractor,
    const fs::path& logs, const std::function<bool()>& cancelled,
    const std::function<void()>& extracting) : source_(original) {
  if (cancelled && cancelled()) throw std::runtime_error("Importação cancelada. Seus originais foram preservados.");
  if (!fs::is_regular_file(original)) return;
  char header[4]{};
  std::ifstream input(original, std::ios::binary); input.read(header, sizeof(header));
  const std::string magic(header, sizeof(header));
  const bool package = magic == "CON " || magic == "LIVE" || magic == "PIRS";
  auto extension = original.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
  if (!package && extension != L".iso") return;
  if (!fs::is_regular_file(extractor))
    throw std::runtime_error("Falta o componente de extração. Extraia novamente o ZIP completo do aplicativo; seus jogos e dados serão preservados.");
  fs::create_directories(logs);
  const auto temporary = logs / "extraction";
  if (!fs::create_directory(temporary))
    throw std::runtime_error("Esta extração foi interrompida. Tente importar novamente para iniciar uma nova tentativa segura.");
  temporary_ = temporary;
  try {
    if (extracting) extracting();
    source_ = temporary_ / "content";
    ProcessSpec spec;
    spec.exe = extractor; spec.args = {package ? "extract" : "extract-disc", Utf8(fs::absolute(original)), Utf8(fs::absolute(source_))};
    spec.working_dir = temporary_; spec.stdout_file = logs / "extraction.log";
    spec.stderr_file = logs / "extraction.error.log"; spec.no_console = true; spec.should_cancel = cancelled;
    spec.timeout = std::chrono::minutes(30);
    std::ofstream(logs / "source.txt", std::ios::binary) << Utf8(original) << '\n';
    const auto result = RunProcess(spec);
    if (result.cancelled || (cancelled && cancelled()))
      throw std::runtime_error("Importação cancelada. O arquivo original e seus dados foram preservados.");
    if (!result.launched || result.exit_code != 0 || !fs::is_directory(source_) ||
        !fs::is_regular_file(temporary_ / "content.sha256"))
      throw std::runtime_error("Não foi possível extrair o jogo. Confira se a imagem ISO ou o pacote Xbox 360 está completo e se há espaço livre. Nada foi importado; o original foi preservado. Consulte Detalhes para o diagnóstico.");
  } catch (...) {
    std::error_code ec; fs::remove_all(temporary_, ec); throw;
  }
}
ImportSource::~ImportSource() {
  if (!temporary_.empty()) { std::error_code ec; fs::remove_all(temporary_, ec); }
}
fs::path ImportSource::identity_path() const {
  // Disc update partitions and auxiliary XEX modules are not the selected game.
  if (extracted() && fs::is_regular_file(source_ / "default.xex")) return source_ / "default.xex";
  return source_;
}
}
