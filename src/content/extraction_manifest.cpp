#include "extraction_manifest.h"

#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace x360n::content {

ExtractionManifest::ExtractionManifest(const fs::path& destination)
    : destination_(destination), temporary_(destination) {
  temporary_ += ".partial";
  // noreplace rejects an existing file, directory or dangling symlink. A stale
  // partial file is preserved for inspection, never silently reused/truncated.
  stream_.open(temporary_, std::ios::binary | std::ios::out | std::ios::noreplace);
  if (!stream_) throw std::runtime_error("cannot create exclusive temporary manifest");
}

ExtractionManifest::~ExtractionManifest() {
  stream_.close();
  std::error_code ignored;
  if (!temporary_.empty()) fs::remove(temporary_, ignored);
}

void ExtractionManifest::Publish() {
  if (!stream_.is_open()) throw std::runtime_error("manifest is already closed");
  stream_.flush();
  if (!stream_) throw std::runtime_error("cannot write/flush manifest; not published");
  stream_.close();
  if (!stream_) throw std::runtime_error("cannot close manifest; not published");
#ifdef _WIN32
  if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_WRITE_THROUGH))
    throw std::system_error(GetLastError(), std::system_category(), "cannot publish manifest without replacement");
#else
  // rename() may replace a concurrent destination on POSIX. Linking a closed
  // file exposes complete bytes atomically and fails if the final name exists.
  fs::create_hard_link(temporary_, destination_);
  std::error_code cleanup;
  fs::remove(temporary_, cleanup);
#endif
  // Publication released the staging name. Never remove a subsequent writer's
  // staging file when this object is destroyed.
  temporary_.clear();
}

}  // namespace x360n::content
