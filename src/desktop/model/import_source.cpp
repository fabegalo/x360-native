#include "import_source.h"
#include "catalog.h"
#include "child_process.h"
#include "../../content/system_update.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace x360n::desktop {
ImportSource::ImportSource(const fs::path& original, const fs::path& extractor,
    const fs::path& logs, const std::function<bool()>& cancelled,
    const std::function<void()>& extracting,
    const std::function<void(const std::string&)>& progress) : source_(original) {
  if (cancelled && cancelled()) throw std::runtime_error("Importação cancelada. Seus originais foram preservados.");
  system_update_ = fs::is_directory(original)
      ? !content::FindSystemUpdatePackage(original).empty()
      : content::HasZipSignature(original) || content::IsSystemUpdatePackage(original);
  if (!fs::is_regular_file(original) && !system_update_) return;
  char header[4]{};
  std::ifstream input(original, std::ios::binary); input.read(header, sizeof(header));
  const std::string magic(header, sizeof(header));
  const bool package = magic == "CON " || magic == "LIVE" || magic == "PIRS";
  auto extension = original.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
  bool disc = extension == L".iso" || extension == L".gdfx";
  // GDFX may be supplied without an extension, at any supported disc offset.
  for (uint64_t offset : {0ull, 0xFB20ull, 0x20600ull, 0xFD90000ull, 0x2080000ull}) {
    char signature[20]{}; input.clear(); input.seekg(offset + 32 * 2048);
    input.read(signature, 20);
    if (std::string_view(signature, 20) == std::string_view("MICROSOFT*XBOX*MEDIA", 20)) disc = true;
  }
  if (!package && !disc && !system_update_) return;
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
    spec.exe = extractor; spec.args = {system_update_ ? "extract-system-update" : package ? "extract" : "extract-disc", Utf8(fs::absolute(original)), Utf8(fs::absolute(source_))};
    spec.working_dir = temporary_; spec.stdout_file = logs / "extraction.log";
    spec.stderr_file = logs / "extraction.error.log"; spec.no_console = true; spec.should_cancel = cancelled;
    spec.timeout = system_update_ ? std::chrono::minutes(5) : std::chrono::minutes(30);
    auto next_sample = std::chrono::steady_clock::now();
    std::string previous_stage;
    if (progress) spec.on_tick = [&] {
      const auto now=std::chrono::steady_clock::now();
      if (now<next_sample) return;
      next_sample=now+std::chrono::milliseconds(250);
      std::ifstream in(spec.stdout_file,std::ios::binary | std::ios::ate);
      if (!in) return;
      in.seekg(std::max<std::streamoff>(0,std::streamoff(in.tellg())-16384));
      std::string last;
      for (std::string line;std::getline(in,line);) if (line.starts_with("X360N_STAGE ")) last=line.substr(12);
      if (!last.empty() && last!=previous_stage) { previous_stage=last; progress(last); }
    };
    std::ofstream(logs / "source.txt", std::ios::binary) << Utf8(original) << '\n';
    const auto result = RunProcess(spec);
    if (result.cancelled || (cancelled && cancelled()))
      throw std::runtime_error("Importação cancelada. O arquivo original e seus dados foram preservados.");
    if (!result.launched || result.exit_code != 0 || !fs::is_directory(source_) ||
        !fs::is_regular_file(temporary_ / "content.sha256")) {
      std::string detail;
      if (system_update_) {
        std::ifstream error(spec.stderr_file,std::ios::binary);
        detail.resize(4096); error.read(detail.data(),detail.size()); detail.resize(size_t(error.gcount()));
      }
      throw std::runtime_error(system_update_
        ? "Não foi possível importar o System Update. Nenhum componente foi instalado; originais preservados.\n" + detail
        : "Não foi possível extrair o jogo. Confira se a imagem ISO ou o pacote Xbox 360 está completo e se há espaço livre. Nada foi importado; o original foi preservado. Consulte Detalhes para o diagnóstico.");
    }
    if (system_update_) {
      std::ifstream report(temporary_/"content.report.txt",std::ios::binary);
      report_.resize(65536); report.read(report_.data(),report_.size()); report_.resize(size_t(report.gcount()));
      if (report_.empty()) throw std::runtime_error("System Update: falta o registro de procedência.");
      fs::copy_file(temporary_/"content.report.txt",logs/"system-update-17559.tsv",fs::copy_options::none);
    }
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
