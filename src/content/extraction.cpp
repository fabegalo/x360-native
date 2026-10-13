// Shared by the diagnostic tool and the desktop extractor; keep C1/C3 guards here.
#include "extraction.h"
#include "paths.h"
#include "sha256.h"
#include "extraction_manifest.h"
#include <algorithm>
#include <fstream>
#include <vector>
#include <fmt/format.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/disc_image_entry.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/file.h>
namespace fs = std::filesystem;
namespace x360n::content {
using rex::X_STATUS;
using rex::filesystem::DiscImageDevice;
using rex::filesystem::DiscImageEntry;
using rex::filesystem::StfsContainerDevice;
namespace {
std::string PathText(const fs::path& path) { auto s = path.generic_u8string(); return {s.begin(), s.end()}; }
fs::path Utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
}
struct Extractor {
  fs::path dest;
  std::ostream& manifest;
  uint64_t files = 0, bytes = 0;
  int errors = 0;
  // Nonzero for a disc image: every entry is a DiscImageEntry whose data must lie
  // inside the first disc_size bytes of the image.
  uint64_t disc_size = 0;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(4 << 20);

  void Walk(rex::filesystem::Entry* entry, const fs::path& rel) {
    for (auto& child : entry->children()) {
      const std::string& name = child->name();
      if (!x360n::IsSafePackageEntryName(name)) {
        fmt::print(stderr, "extract: refusing unsafe entry name '{}' under '{}'\n", name,
                   PathText(rel));
        ++errors;
        continue;
      }
      fs::path child_rel = rel / Utf8Path(name);
      fs::path out = dest / child_rel;
      if (child->attributes() & rex::filesystem::kFileAttributeDirectory) {
        fs::create_directories(out);
        Walk(child.get(), child_rel);
        continue;
      }
      Copy(child.get(), child_rel, out);
    }
  }

  void Copy(rex::filesystem::Entry* entry, const fs::path& rel, const fs::path& out) {
    std::error_code ec;
    if (fs::exists(out, ec)) {
      fmt::print(stderr, "extract: refusing to overwrite {}\n", PathText(out));
      ++errors;
      return;
    }
    if (disc_size) {
      auto* d = static_cast<DiscImageEntry*>(entry);
      if (d->data_size() != entry->size() || d->data_offset() > disc_size ||
          d->data_size() > disc_size - d->data_offset()) {
        fmt::print(stderr, "extract: {} lies outside the image (offset {:#x}, {} bytes; image {} bytes)\n",
                   PathText(rel), d->data_offset(), d->data_size(), disc_size);
        ++errors;
        return;
      }
    }
    rex::filesystem::File* in = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in) != X_STATUS_SUCCESS || !in) {
      fmt::print(stderr, "extract: cannot open {} in package\n", PathText(rel));
      ++errors;
      return;
    }
    std::ofstream o(out, std::ios::binary);
    x360n::Sha256 h;
    size_t offset = 0, total = entry->size();
    bool ok = bool(o);
    while (ok && offset < total) {
      size_t want = std::min(buffer.size(), total - offset);
      size_t got = 0;
      if (in->ReadSync(std::span<uint8_t>(buffer.data(), want), offset, &got) != X_STATUS_SUCCESS ||
          got == 0) {
        ok = false;
        break;
      }
      o.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(got));
      h.Update({buffer.data(), got});
      offset += got;
    }
    in->Destroy();
    o.close();
    if (!ok || offset != total || !o) {
      fmt::print(stderr, "extract: short read for {} ({} of {} bytes)\n", PathText(rel),
                 offset, total);
      ++errors;
      return;
    }
    auto digest = h.Finish();
    std::string hex = x360n::ToHex(digest);
    manifest << hex << " *" << PathText(rel) << "\n";
    if (!manifest) {
      fmt::print(stderr, "extract: failed to write manifest\n");
      ++errors;
      return;
    }
    fmt::print("{:>12}  {}  {}\n", total, hex, PathText(rel));
    ++files;
    bytes += total;
  }
};

int ExtractTree(rex::filesystem::Entry* root, const fs::path& dest, uint64_t disc_size) {
  if (!root) {
    fmt::print(stderr, "extract: source has no root entry\n");
    return 1;
  }
  try {
    fs::create_directories(dest.parent_path());
    auto manifest_path = dest;
    manifest_path += ".sha256";
    x360n::content::ExtractionManifest manifest(manifest_path);
    if (!fs::create_directory(dest))
      throw std::runtime_error("destination already exists; not extracting");
    Extractor x{dest, manifest.stream()};
    x.disc_size = disc_size;
    x.Walk(root, {});
    if (x.errors) {
      fmt::print(stderr, "extract: {} error(s); manifest not published\n", x.errors);
      return 1;
    }
    manifest.Publish();
    fmt::print("extracted {} file(s), {} bytes, 0 error(s); manifest {}\n", x.files, x.bytes,
               PathText(manifest_path));
    return 0;
  } catch (const std::exception& error) {
    fmt::print(stderr, "extract: {}; extraction incomplete\n", error.what());
    return 1;
  }
}

bool DestinationFree(const fs::path& dest) {
  auto manifest = dest;
  manifest += ".sha256";
  for (const auto& path : {dest, manifest}) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if ((ec && ec != std::errc::no_such_file_or_directory) || fs::exists(status)) {
      fmt::print(stderr, "extract: destination exists or cannot be inspected: {}\n", PathText(path));
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
  StfsContainerDevice device("", package);
  if (!device.Initialize()) {
    fmt::print(stderr, "extract: StfsContainerDevice::Initialize failed for {}\n", PathText(package));
    return 1;
  }
  return ExtractTree(device.ResolvePath("/"), dest, 0);
}

int ExtractDisc(const fs::path& image, const fs::path& dest_in) {
  fs::path dest = ExtractionDestination(dest_in);
  if (!DestinationFree(dest)) return 3;
  std::error_code ec;
  const uint64_t size = fs::file_size(image, ec);
  if (ec || size == 0) {
    fmt::print(stderr, "extract-disc: cannot read the size of {}\n", PathText(image));
    return 1;
  }
  DiscImageDevice device("", image);
  if (!device.Initialize()) {
    fmt::print(stderr, "extract-disc: DiscImageDevice::Initialize failed for {} (no GDFX game partition)\n",
               PathText(image));
    return 1;
  }
  return ExtractTree(device.ResolvePath("/"), dest, size);
}


}
