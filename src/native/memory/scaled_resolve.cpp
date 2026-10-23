#include "scaled_resolve.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <algorithm>
#include <stdexcept>
namespace x360n::native {
ScaledResolveCache::~ScaledResolveCache() { Clear(); }
void ScaledResolveCache::Clear() {
  auto lock = GuestMemoryMirror::Lock();
  if (mirror_) for (auto& [index, page] : pages_) if (page->watch) mirror_->Unwatch(page->watch);
  pages_.clear(); mirror_=nullptr;
}
void ScaledResolveCache::Invalidate(void*, void* data, uint64_t, bool) {
  auto& page = *static_cast<Page*>(data);
  page.valid = false; page.watch = nullptr; ++page.version;
  page.data.reset();
}
bool ScaledResolveCache::Overlaps(uint32_t start, uint32_t length) const {
  if (!enabled() || !length) return false;
  auto lock = GuestMemoryMirror::Lock();
  const uint64_t end = std::min<uint64_t>(uint64_t(start) + length, GuestMemoryMirror::kSize);
  for (uint64_t address = start & ~(kPage - 1); address < end; address += kPage) {
    auto found = pages_.find(uint32_t(address) / kPage);
    if (found != pages_.end() && (found->second->valid || found->second->watch)) return true;
  }
  return false;
}
uint8_t ScaledResolveCache::Read(uint32_t address, unsigned sample, uint8_t fallback) const {
  auto found = pages_.find(address / kPage);
  if (found == pages_.end() || !found->second->valid) return fallback;
  return (*found->second->data)[size_t(sample) * kPage + address % kPage];
}
std::vector<uint8_t> ScaledResolveCache::Hydrate(uint32_t base, uint32_t length,
    unsigned pixel_log2, rex::memory::Memory& memory) const {
  if (uint64_t(base) + length > GuestMemoryMirror::kSize || pixel_log2 > 4 || (base | length) & ((pixel_log2 ? 16u : 8u)-1))
    throw std::runtime_error("Native scaled resolve: invalid extent");
  const unsigned pixel = 1u << pixel_log2, area = x_ * y_;
  std::vector<uint8_t> result(size_t(length) * area);
  const auto* guest = memory.TranslatePhysical<const uint8_t*>(base);
  auto lock = GuestMemoryMirror::Lock();
  for (uint32_t offset = 0; offset < length; offset += pixel)
    for (unsigned sample = 0; sample < area; ++sample)
      for (unsigned b = 0; b < pixel; ++b)
        result[ScaledResolveByteOffset(offset+b,pixel_log2,x_,y_,sample%x_,sample/x_)] = Read(base + offset + b, sample, guest[offset+b]);
  return result;
}
ScaledResolveCache::Ticket ScaledResolveCache::Capture(uint32_t start, uint32_t length) const {
  Ticket ticket{start, length, {}, {}};
  auto lock = GuestMemoryMirror::Lock();
  ticket.cpu_generations=mirror_->CaptureCpuGenerations(start,length);
  for (uint64_t address = start & ~(kPage-1); address < uint64_t(start)+length; address += kPage) {
    auto found = pages_.find(uint32_t(address) / kPage);
    ticket.pages.push_back({uint32_t(address) / kPage, found != pages_.end() ? found->second->version : 0,
        found != pages_.end() && found->second->valid ? found->second->data : nullptr});
  }
  return ticket;
}
void ScaledResolveCache::Begin(Ticket& ticket) {
  auto lock = GuestMemoryMirror::Lock();
  for (auto& pending : ticket.pages) {
    auto& pointer = pages_[pending.index]; if (!pointer) pointer = std::make_unique<Page>();
    auto& page = *pointer;
    if (page.watch) mirror_->Unwatch(page.watch);
    pending.version = ++page.version; page.valid = false; page.watch=nullptr;
    if (!mirror_->ReadbackCurrent(std::max(ticket.start,pending.index*kPage),ticket.start,ticket.cpu_generations)) {
      pending.previous.reset(); continue;
    }
    page.watch = mirror_->Watch(pending.index*kPage, kPage, Invalidate, this, &page, 0);
  }
}
void ScaledResolveCache::Complete(const Ticket& ticket, std::span<const uint8_t> pixels,
    unsigned pixel_log2, rex::memory::Memory& memory) {
  const unsigned area = x_*y_;
  if (((ticket.start | ticket.length) & ((pixel_log2 ? 16u : 8u)-1)) || pixels.size() != size_t(ticket.length)*area) throw std::logic_error("Native scaled readback extent mismatch");
  auto lock = GuestMemoryMirror::Lock();
  for (const auto& pending : ticket.pages) {
    auto& page = *pages_.at(pending.index);
    if (page.version != pending.version || !page.watch) continue;
    if (!mirror_->ReadbackCurrent(std::max(ticket.start,pending.index*kPage),ticket.start,ticket.cpu_generations)) {
      mirror_->Unwatch(page.watch); page.watch=nullptr; page.valid=false; ++page.version;
      continue;
    }
    auto data = std::make_shared<std::vector<uint8_t>>(size_t(kPage)*area);
    const auto* guest = memory.TranslatePhysical<const uint8_t*>(pending.index*kPage);
    if (pending.previous) *data = *pending.previous;
    else for (unsigned sample=0; sample<area; ++sample) std::memcpy(data->data()+size_t(sample)*kPage, guest, kPage);
    const uint32_t begin = std::max(ticket.start, pending.index*kPage);
    const uint32_t end = std::min(ticket.start+ticket.length, (pending.index+1)*kPage);
    for (unsigned sample=0; sample<area; ++sample) for (uint32_t address=begin; address<end; ++address) {
      const uint32_t relative=address-ticket.start;
      (*data)[size_t(sample)*kPage+address%kPage] = pixels[ScaledResolveByteOffset(relative,pixel_log2,x_,y_,sample%x_,sample/x_)];
    }
    page.data=std::move(data); page.valid=true;
  }
}
bool ScaledResolveCache::Scanout(const ScanoutTexture& input, const void* pixels,
    ScanoutTexture& output, std::vector<uint8_t>& bytes) const {
  if (!Overlaps(input.address, input.bytes)) return false;
  const uint64_t width=uint64_t(input.width)*x_,height=uint64_t(input.height)*y_;
  const uint64_t pitch=(width+31)&~uint64_t(31),size=pitch*height*4;
  if (!pixels || !width || !height || size>UINT32_MAX || width>16384 || height>16384)
    throw std::runtime_error("Native: requested scale exceeds scanout texture limits");
  output=input; output.width=uint32_t(width); output.height=uint32_t(height); output.pitch=uint32_t(pitch);
  output.tiled=0; output.bytes=uint32_t(size);
  bytes.resize(output.bytes);
  const auto* source=static_cast<const uint8_t*>(pixels);
  auto lock=GuestMemoryMirror::Lock();
  for (uint32_t y=0; y<output.height; ++y) for (uint32_t x=0; x<output.width; ++x) {
    uint32_t offset=input.tiled ? uint32_t(rex::graphics::texture_util::GetTiledOffset2D(x/x_,y/y_,input.pitch,2)) : ((y/y_)*input.pitch+x/x_)*4;
    if (offset+4>input.bytes) throw std::runtime_error("Native scaled scanout extent overflow");
    unsigned sample=x%x_+(y%y_)*x_;
    for (unsigned b=0; b<4; ++b) bytes[(size_t(y)*output.pitch+x)*4+b]=Read(input.address+offset+b,sample,source[offset+b]);
  }
  return true;
}
}
