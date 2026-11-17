#include <print>
// Shared by the diagnostic tool and the desktop extractor; keep C1/C3 guards here.
#include "extraction.h"
#include "paths.h"
#include "sha256.h"
#include "extraction_manifest.h"
#include <algorithm>
#include <fstream>
#include <vector>
#include <format>
#include <rex/filesystem/file_system_factory.h>
#include <rex/filesystem/stfs.h>
#include <rex/init.h>
#include <rex/bit.h>
#include <stdexcept>
#if defined(X360N_STATIC_FILESYSTEM)
extern "C" void* ReXCreateInterface(const char*, uint32_t);
#endif
namespace fs = std::filesystem;
namespace x360n::content {
using rex::filesystem::IContainerImage;
using rex::filesystem::ContainerNode;
using rex::filesystem::FileAttributes;
rex::filesystem::IFileSystemFactory& FileSystem() {
  using Factory = rex::filesystem::IFileSystemFactory;
  if (auto* factory = REX_INTERFACE(Factory)) return *factory;
  struct Services {
    Services() {
      rex::Config config;
#if defined(X360N_STATIC_FILESYSTEM)
      using Module = rex::IModule;
      auto* module = static_cast<Module*>(ReXCreateInterface(Module::kInterface.name, Module::kInterface.version));
      if (!module) throw std::runtime_error("Static content filesystem module is unavailable");
      config.code_modules.push_back(module);
#else
      config.modules.emplace_back("rexfilesystem");
#endif
      const auto result = rex::Init(config);
      if (!result) throw std::runtime_error(result.error().GetDescription());
    }
    ~Services() { rex::Shutdown(); }
  };
  static Services services;
  auto* factory = REX_INTERFACE(Factory);
  if (!factory) throw std::runtime_error("Content filesystem provider is unavailable");
  return *factory;
}
namespace {
std::string PathText(const fs::path& path) { auto s = path.generic_u8string(); return {s.begin(), s.end()}; }
fs::path Utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
}
struct Extractor {
  const IContainerImage& image;
  fs::path dest;
  std::ostream& manifest;
  uint64_t files = 0, bytes = 0;
  int errors = 0;
  // Nonzero for a disc image: every node extent must lie
  // inside the first disc_size bytes of the image.
  uint64_t disc_size = 0;
  uint64_t entries = 0, declared_bytes = 0;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(4 << 20);

  void Walk(const ContainerNode& entry, const fs::path& rel, size_t depth = 0) {
    if (depth > 64) throw std::runtime_error("container directory depth limit exceeded");
    const auto nodes = image.GetNodes();
    for (const auto index : entry.children) {
      if (index >= nodes.size()) throw std::runtime_error("container child index outside node table");
      const auto& child = nodes[index];
      if (!WithinExtractionLimits(++entries, child.info.size, declared_bytes))
        throw std::runtime_error("container extraction limit exceeded");
      if (!(rex::HasFlags(child.info.attributes, FileAttributes::kDirectory))) declared_bytes += child.info.size;
      const std::string& name = child.info.name;
      if (!x360n::IsSafePackageEntryName(name)) {
        std::print(stderr, "extract: refusing unsafe entry name '{}' under '{}'\n", name,
                   PathText(rel));
        ++errors;
        continue;
      }
      fs::path child_rel = rel / Utf8Path(name);
      fs::path out = dest / child_rel;
      if (rex::HasFlags(child.info.attributes, FileAttributes::kDirectory)) {
        if (!io::create_directory(out))
          throw std::runtime_error("duplicate/conflicting container directory");
        Walk(child, child_rel, depth+1);
        continue;
      }
      Copy(child, child_rel, out);
    }
  }

  void Copy(const ContainerNode& entry, const fs::path& rel, const fs::path& out) {
    std::error_code ec;
    const auto& output = out;
    if (io::exists(output, ec)) {
      std::print(stderr, "extract: refusing to overwrite {}\n", PathText(rel));
      ++errors;
      return;
    }
    if (ec) {
      std::print(stderr, "extract: cannot inspect destination {}: {}\n",
                 PathText(rel), ec.message());
      ++errors;
      return;
    }
    if (disc_size) {
      uint64_t extent_bytes = 0;
      for (const auto& extent : entry.extents) {
        if (extent.file != 0 || extent.offset > disc_size || extent.length > disc_size - extent.offset ||
            extent_bytes > entry.info.size || extent.length > entry.info.size - extent_bytes) {
          std::print(stderr, "extract: {} has data outside the disc image\n", PathText(rel));
          ++errors; return;
        }
        extent_bytes += extent.length;
      }
      if (extent_bytes != entry.info.size) {
        std::print(stderr, "extract: {} has incomplete disc extents\n", PathText(rel));
        ++errors; return;
      }
    }
    // Windows' default MAX_PATH policy can reject a nested output file even
    // though the source entry is readable. Never label an output-open failure
    // as a corrupt source or "short read".
    io::ofstream o(output, std::ios::binary);
    if (!o) {
      std::print(stderr, "extract: cannot create output {} (destination path {} characters)\n",
                 PathText(rel), PathText(out).size());
      ++errors;
      return;
    }
    x360n::Sha256 h;
    size_t offset = 0, total = entry.info.size;
    bool read_ok = true, write_ok = true;
    uint32_t read_status = 0;
    size_t read_count = 0;
    while (offset < total) {
      size_t want = std::min(buffer.size(), total - offset);
      const auto status = image.Read(entry, offset, std::span<uint8_t>(buffer.data(), want));
      const size_t got = status ? *status : 0;
      if (!status || got == 0 || got > want) {
        read_ok = false;
        read_status = status ? 0 : uint32_t(status.error().code);
        read_count = got;
        break;
      }
      o.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(got));
      if (!o) {
        write_ok = false;
        break;
      }
      h.Update({buffer.data(), got});
      offset += got;
    }
    o.close();
    if (!write_ok || !o) {
      std::print(stderr, "extract: cannot write output {} ({} of {} bytes)\n",
                 PathText(rel), offset, total);
      ++errors;
      return;
    }
    if (!read_ok || offset != total) {
      std::print(stderr,
                 "extract: short read from source {} ({} of {} bytes; status {:#x}, last read {})\n",
                 PathText(rel), offset, total, read_status, read_count);
      ++errors;
      return;
    }
    auto digest = h.Finish();
    std::string hex = x360n::ToHex(digest);
    manifest << hex << " *" << PathText(rel) << "\n";
    if (!manifest) {
      std::print(stderr, "extract: failed to write manifest\n");
      ++errors;
      return;
    }
    std::print("{:>12}  {}  {}\n", total, hex, PathText(rel));
    ++files;
    bytes += total;
  }
};

int ExtractTree(const IContainerImage& image, const fs::path& dest, uint64_t disc_size) {
  if (image.GetNodes().empty()) {
    std::print(stderr, "extract: source has no root entry\n");
    return 1;
  }
  try {
    io::create_directories(dest.parent_path());
    auto manifest_path = dest;
    manifest_path += ".sha256";
    x360n::content::ExtractionManifest manifest(manifest_path);
    if (!io::create_directory(dest))
      throw std::runtime_error("destination already exists; not extracting");
    Extractor x{image, dest, manifest.stream()};
    x.disc_size = disc_size;
    x.Walk(image.GetRoot(), {});
    if (x.errors) {
      std::print(stderr, "extract: {} error(s); manifest not published\n", x.errors);
      return 1;
    }
    manifest.Publish();
    std::print("extracted {} file(s), {} bytes, 0 error(s); manifest {}\n", x.files, x.bytes,
               PathText(manifest_path));
    return 0;
  } catch (const std::exception& error) {
    std::print(stderr, "extract: {}; extraction incomplete\n", error.what());
    return 1;
  }
}

bool DestinationFree(const fs::path& dest) {
  auto manifest = dest;
  manifest += ".sha256";
  for (const auto& path : {dest, manifest}) {
    std::error_code ec;
    const auto status = io::symlink_status(path, ec);
    if ((ec && ec != std::errc::no_such_file_or_directory) || io::exists(status)) {
      std::print(stderr, "extract: destination exists or cannot be inspected: {}\n", PathText(path));
      return false;
    }
  }
  return true;
}

fs::path ExtractionDestination(const fs::path& input) {
  auto dest = fs::absolute(input).lexically_normal();
  // lexically_normal preserves the terminal empty component in "out/" and
  // "out/.". Use the same adjacent manifest and exclusive directory in all
  // spellings, without stripping the root directory of an absolute path.
  if (dest.filename().empty() && dest != dest.root_path()) dest = dest.parent_path();
  return dest;
}

int Extract(const fs::path& package, const fs::path& dest_in) {
  fs::path dest = ExtractionDestination(dest_in);
  if (!DestinationFree(dest)) return 3;
  auto image = FileSystem().OpenContainer(package);
  if (!image || (*image)->GetType() == rex::filesystem::ContainerType::kGDFX) {
    std::print(stderr, "extract: package reader failed for {}\n", PathText(package));
    return 1;
  }
  return ExtractTree(**image, dest, 0);
}

int ExtractDisc(const fs::path& image, const fs::path& dest_in) {
  fs::path dest = ExtractionDestination(dest_in);
  if (!DestinationFree(dest)) return 3;
  std::error_code ec;
  const uint64_t size = io::file_size(image, ec);
  if (ec || size == 0) {
    std::print(stderr, "extract-disc: cannot read the size of {}\n", PathText(image));
    return 1;
  }
  auto disc = FileSystem().OpenContainer(image);
  if (!disc || (*disc)->GetType() != rex::filesystem::ContainerType::kGDFX) {
    std::print(stderr, "extract-disc: GDFX reader failed for {}: {}\n", PathText(image),
               disc ? "not a GDFX game partition" : disc.error().GetDescription());
    return 1;
  }
  return ExtractTree(**disc, dest, size);
}


}
