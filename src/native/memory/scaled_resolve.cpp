#include "scaled_resolve.h"
#include <algorithm>
namespace x360n::native {
ScaledResolveCache::~ScaledResolveCache() { Clear(); }
void ScaledResolveCache::Clear() {
  auto lock = GuestMemoryMirror::Lock();
  if (mirror_) for (auto& [index, page] : pages_) if (page->watch) mirror_->Unwatch(page->watch);
  pages_.clear(); mirror_ = nullptr;
}
void ScaledResolveCache::Invalidate(void*, void* data, uint64_t, bool) {
  auto& page = *static_cast<Page*>(data);
  page.valid = false; page.watch = nullptr;
}
bool ScaledResolveCache::Overlaps(uint32_t start, uint32_t length) const {
  if (!enabled() || !length) return false;
  auto lock = GuestMemoryMirror::Lock();
  const uint64_t end = std::min<uint64_t>(uint64_t(start) + length, GuestMemoryMirror::kSize);
  for (uint64_t address = start & ~(kPage - 1); address < end; address += kPage) {
    auto found = pages_.find(uint32_t(address / kPage));
    if (found != pages_.end() && found->second->valid) return true;
  }
  return false;
}
std::vector<std::pair<uint32_t, uint32_t>> ScaledResolveCache::Stale(uint32_t start, uint32_t length) const {
  std::vector<std::pair<uint32_t, uint32_t>> runs;
  if (!length) return runs;
  auto lock = GuestMemoryMirror::Lock();
  const uint64_t end = std::min<uint64_t>(uint64_t(start) + length, GuestMemoryMirror::kSize);
  for (uint64_t address = start & ~(kPage - 1); address < end; address += kPage) {
    auto found = pages_.find(uint32_t(address / kPage));
    if (found != pages_.end() && found->second->valid) continue;
    if (!runs.empty() && uint64_t(runs.back().first) + runs.back().second == address) runs.back().second += kPage;
    else runs.push_back({uint32_t(address), kPage});
  }
  return runs;
}
void ScaledResolveCache::MarkCurrent(uint32_t start, uint32_t length) {
  if (!length) return;
  auto lock = GuestMemoryMirror::Lock();
  const uint64_t end = std::min<uint64_t>(uint64_t(start) + length, GuestMemoryMirror::kSize);
  for (uint64_t address = start & ~(kPage - 1); address < end; address += kPage) {
    auto& pointer = pages_[uint32_t(address / kPage)];
    if (!pointer) pointer = std::make_unique<Page>();
    pointer->valid = true;
    if (!pointer->watch) pointer->watch = mirror_->Watch(uint32_t(address), kPage, Invalidate, this, pointer.get(), 0);
  }
}
}
