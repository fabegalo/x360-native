#include "system_update.h"
#include "safe_zip.h"
#include "extraction.h"
#include "extraction_manifest.h"
#include "paths.h"
#include "sha256.h"
#include "xex_patch.h"
#include "update_container.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace x360n::content {
namespace {
std::string Text(const fs::path& p) { auto s=p.generic_u8string(); return {s.begin(),s.end()}; }
std::string Lower(std::string s) {
  std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));}); return s;
}
void NoLinks(const fs::path& path) {
  fs::path parent;
  for (const auto& part : fs::absolute(path)) {
    parent /= part;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(parent.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
#else
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(parent, ec)))
#endif
      throw std::runtime_error("System Update: refusing reparse point/link: " + Text(parent));
  }
}
struct Stage {
  fs::path path;
  ~Stage() { std::error_code ec; fs::remove_all(path,ec); }
};
}
int ExtractSystemUpdate(const fs::path& source, const fs::path& destination) {
  try {
    const auto dest = fs::absolute(destination).lexically_normal();
    auto manifest_path=dest; manifest_path += ".sha256";
    auto report_path=dest; report_path += ".report.txt";
    NoLinks(source); NoLinks(dest);
    if (fs::exists(dest) || fs::exists(manifest_path))
      throw std::runtime_error("System Update: destination already exists; nothing overwritten");
    fs::create_directories(dest.parent_path());
    auto staging = dest; staging += ".staging";
    if (!fs::create_directory(staging)) throw std::runtime_error("System Update: interrupted staging exists; retry with a new operation directory");
    Stage cleanup{staging};
    auto folder = source;
    fs::path package;
    if (fs::is_regular_file(source)) {
      if (HasZipSignature(source)) {
        folder = staging / "zip";
        ExtractUpdateZip(source,folder);
        package = FindSystemUpdatePackage(folder);
      } else if (IsSystemUpdatePackage(source)) { package = source; folder.clear(); }
      else throw std::runtime_error("System Update: expected ZIP or PIRS system update content");
    } else package = FindSystemUpdatePackage(folder);
    if (package.empty()) throw std::runtime_error("System Update: no PIRS system update package found in the selected content");
    if (!folder.empty()) folder = package.parent_path();
    std::cout << "X360N_STAGE PIRS System Update\n" << std::flush;
    ValidateUpdateContainer(package);
    if (Extract(package,staging/"flash")) throw std::runtime_error("System Update: invalid/incomplete PIRS");
    const auto flash = staging/"flash";
    // This exact Retail image identifies the profile, independently of names
    // and the update container's historical metadata version (0x20076000).
    const auto anchor = DigestFile(flash/"$flash_dash.xex");
    if (!anchor || anchor->sha256 != dash17559::kOriginalAssets[0].sha256)
      throw std::runtime_error("System Update: $flash_dash.xex does not match Retail 17559 SHA-256; no components imported");
    const auto output = staging/"verified";
    fs::create_directory(output);
    ExtractionManifest manifest(manifest_path);
    std::ofstream report(report_path,std::ios::binary | std::ios::out | std::ios::noreplace);
    if (!report) throw std::runtime_error("System Update: cannot create provenance report");
    report << "dashboard-version\t2.0.17559.0\n";
    auto record = [&](const fs::path& path, const std::string& origin) {
      const auto digest=DigestFile(path);
      if (!digest) throw std::runtime_error("System Update: cannot hash source");
      report << "source\t" << digest->sha256 << '\t' << origin << '\n';
    };
    if (fs::is_regular_file(source)) record(source,Text(source));
    record(package,Text(package.lexically_relative(staging)));
    std::map<std::string,fs::path> inputs;
    auto index = [&](const fs::path& root, bool strict) {
      size_t count = 0;
      for (const auto& entry:fs::directory_iterator(root)) {
        if (++count > 4096) throw std::runtime_error("System Update: directory limit exceeded");
        NoLinks(entry.path());
        if (!entry.is_regular_file()) continue;
        auto name=Lower(Text(entry.path().filename()));
        if (strict && name.starts_with("$flash_")) name.erase(0,7);
        // Flash images have precedence over the uncompressed USB variants.
        if (!strict && inputs.contains(name)) continue;
        if (!inputs.emplace(name,entry.path()).second) throw std::runtime_error("System Update: conflicting component names");
      }
    };
    index(flash,true); if (!folder.empty()) index(folder,false);
    auto copy = [&](const fs::path& from,std::string_view relative,std::string_view expected) {
      const auto digest=DigestFile(from);
      if (!digest || digest->sha256 != expected)
        throw std::runtime_error("System Update: incorrect/corrupt Retail 17559 component: " + Text(from.filename()));
      const auto out=output/PathFromUtf8(std::string(relative));
      fs::create_directories(out.parent_path()); fs::copy_file(from,out,fs::copy_options::none);
      const auto copied=DigestFile(out);
      if (!copied || copied->sha256 != expected) throw std::runtime_error("System Update: component changed during copy");
      manifest.stream() << expected << " *" << relative << '\n';
      report << "verified\t" << expected << '\t' << relative << '\t' << Text(from.lexically_relative(staging)) << '\n';
      std::cout << "X360N_STAGE Verificado 17559: " << relative << '\n' << std::flush;
    };
    size_t missing=0;
    for (const auto& asset:dash17559::kOriginalAssets) {
      auto name=Lower(Text(PathFromUtf8(std::string(asset.path)).filename()));
      if (name=="xam_ui.xex") name="xam.xex";
      if (const auto it=inputs.find(name);it!=inputs.end()) copy(it->second,asset.path,asset.sha256);
      else if (name=="shrdres.xzp") {
        // The inventory orders shrdres before XAM. Authenticate the original
        // flash XAM explicitly before decoding its embedded XUIZ section.
        const auto original=inputs.find("xam.xex");
        if (original==inputs.end()) {
          ++missing; report << "missing\t" << asset.path << '\t' << asset.sha256 << "\tXAM 17559 ausente; recurso embutido indisponível\n";continue;
        }
        const auto xam_digest=DigestFile(original->second);
        if (!xam_digest || xam_digest->sha256 != "78b3dbd8ae866f4258f5444205486ea9f8451afaa30f469c875dcf345f828467")
          throw std::runtime_error("System Update: verified XAM 17559 required for shrdres resource");
        auto bytes=ReadUnencryptedXexResource(original->second,"shrdres");
        if (Sha256Hex(bytes)!=asset.sha256) throw std::runtime_error("System Update: embedded shrdres SHA-256 mismatch");
        const auto recovered=staging/"shrdres.xzp";
        std::ofstream file(recovered,std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        file.close(); if (!file) throw std::runtime_error("System Update: cannot write embedded resource");
        copy(recovered,asset.path,asset.sha256);
      } else {
        ++missing;
        report << "missing\t" << asset.path << '\t' << asset.sha256;
        const auto patch=inputs.find(name+"p");
        if (patch!=inputs.end()) {
          report << "\tpatch " << Text(patch->second.filename()) << ": base original correta e validada necessária; ";
          report << (name.ends_with(".xtt") ? "patch de fonte XTTP não suportado; forneça a fonte completa com o SHA-256 indicado"
                                              : "forneça o XEX completo com o SHA-256 indicado; patch não equivale ao arquivo completo");
        }
        else report << "\tausente no pacote selecionado";
        report << '\n';
      }
    }
    // Additional avatar storage pack, confined to the same selected update.
    if (const auto it=inputs.find("fffe07df00000002");it!=inputs.end()) {
      record(it->second,"FFFE07DF00000002");
      std::cout << "X360N_STAGE PIRS AvatarAsset\n" << std::flush;
      ValidateUpdateContainer(it->second);
      if (Extract(it->second,staging/"avatar")) throw std::runtime_error("System Update: invalid avatar PIRS");
      copy(staging/"avatar/AvatarAssetPack.toc","avatar/AvatarAssetPack.toc",
           "a8c90b3db5c6349a55fcb72eda47e08e4940f88fabda4ff73025d9728832daa8");
    } else report << "optional-missing\tavatar/AvatarAssetPack.toc\n";
    // Report all patch-only content. ApplyXexPatch's outputs are decoded
    // header/image pairs, not the byte-exact XEX files required by this profile.
    for (const auto& [name,path]:inputs) if (name.ends_with(".xexp") || name.ends_with(".xttp")) {
      record(path,Text(path.filename()));
      report << "patch\t" << Text(path.filename()) << "\tbase original validada necessária; não publicado como arquivo completo\n";
    }
    report << "required-missing\t" << missing << '\n'; report.close();
    if (!report) throw std::runtime_error("System Update: cannot flush provenance report");
    fs::rename(output,dest); // exclusive destination in this operation's staging
    manifest.Publish();
    std::cout << "X360N_STAGE Dashboard Xbox 360 — Retail 17559; faltam " << missing << " componente(s) obrigatório(s)\n" << std::flush;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "System Update: " << error.what() << '\n'; return 1;
  }
}
}
