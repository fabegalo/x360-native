#include "avatar_aot_assets.h"

#include <rex/memory.h>
#include <rex/system/thread_state.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace x360n::avatar {
namespace {
constexpr uint32_t kGuard = 0x1000, kScratch = 0x10000, kLdi = 0x1CA0F;
constexpr uint32_t kProtocol = 0x81B4B238, kCallbacks = 0x81A975CC;
uint32_t Read(const uint8_t* p) {
  return rex::memory::load_and_swap<uint32_t>(p);
}
void Put(uint8_t* p, uint32_t n) {
  rex::memory::store_and_swap<uint32_t>(p, n);
}

// Caller must serialize all 17559 avatar calls sharing these guest globals.
// The title service holds its mutex for the entire request; probes are exclusive.
struct ScopedXamProfile {
  rex::memory::Memory& memory;
  std::array<uint8_t, 20> callbacks;
  uint32_t protocol, coordinates;
  ScopedXamProfile(rex::memory::Memory& m, uint32_t table) : memory(m) {
    protocol = Read(m.TranslateVirtual<const uint8_t*>(kProtocol));
    coordinates = Read(m.TranslateVirtual<const uint8_t*>(0x81A975E0));
    Put(m.TranslateVirtual<uint8_t*>(0x81A975E0), 1);
    std::memcpy(callbacks.data(), m.TranslateVirtual(kCallbacks), 20);
    Put(m.TranslateVirtual<uint8_t*>(kProtocol), 4);
    std::memcpy(m.TranslateVirtual(kCallbacks), m.TranslateVirtual(table), 20);
  }
  ~ScopedXamProfile() {
    Put(memory.TranslateVirtual<uint8_t*>(kProtocol), protocol);
    Put(memory.TranslateVirtual<uint8_t*>(0x81A975E0), coordinates);
    std::memcpy(memory.TranslateVirtual(kCallbacks), callbacks.data(), 20);
  }
};

class AotAssetsBackend final : public AssetsBackend {
 public:
  AotAssetsBackend(rex::memory::Memory& memory, AvatarAotCall call,
                   const AvatarToc& toc, std::vector<AssetSelection> selection,
                   uint32_t protocol, uint32_t body, uint32_t callbacks,
                   AotAssetsStats& stats, float height = 0, float weight = 0,
                   AssetsBuffers destination = {})
      : memory_(memory),
        call_(std::move(call)),
        toc_(toc),
        selection_(std::move(selection)),
        protocol_(protocol),
        body_(body),
        callbacks_(callbacks),
        stats_(stats),
        height_(height),
        weight_(weight),
        destination_(destination) {}
  ~AotAssetsBackend() override { Reset(); }
  size_t Count() const override { return selection_.size(); }

  uint32_t Begin() override {
    if (!blocks_.empty() || protocol_ != 4 || (body_ != 1 && body_ != 2) ||
        !callbacks_ || selection_.empty() || selection_.size() > 32)
      return kAssetsInvalid;
    uint16_t mask = 0;
    for (const auto& s : selection_) {
      std::string error;
      auto index = ResolveAsset(toc_, s.id, &error);
      const auto cat = s.id.category;
      // Special component 0x1000 and multi-category records are a separate
      // contract; reject rather than publish an incomplete special object.
      if (!index || !cat || (cat & (cat - 1)) ||
          !(s.id.compatibility & body_) || s.kind < 1 || s.kind > 3 ||
          (s.kind == 1 && ((cat & ~0xFFFu) || (cat & mask))) ||
          (s.kind == 2 && s.modifier_slot >= 6) ||
          (s.kind == 3 && s.modifier_slot >= 3))
        return kAssetsInvalid;
      const auto blob = toc_.Blob(*index);
      if (blob.size() < 32 || blob.size() > 0x400000 ||
          std::memcmp(blob.data(), "STRB", 4) || blob[4] != 1 || blob[5] != 0 ||
          blob[22] != 1 || blob[23] != 4 || blob[26] != 4)
        return kAssetsInvalid;
      // Bounded field framing, without decompressing a second time. AOT remains
      // responsible for the document GUID, body/category and compressed data.
      size_t p = 32;
      bool mesh = false, modifier = false;
      while (p < blob.size()) {
        if (blob.size() - p < 12) return kAssetsInvalid;
        const uint64_t length = Read(blob.data() + p + 1);
        const uint64_t padded = (length + 3) & ~uint64_t(3);
        if (padded > blob.size() - p - 12) return kAssetsInvalid;
        mesh |= blob[p] == 3;
        modifier |= blob[p] == (s.kind == 2 ? 2 : 4);
        p += 12 + size_t(padded);
      }
      if (s.kind == 1 ? !mesh : !modifier) return kAssetsInvalid;
      for (const auto& material : s.materials)
        for (float value : material.rgba)
          if (!std::isfinite(value)) return kAssetsInvalid;
      if (s.kind == 1) mask |= uint16_t(cat);
    }
    const auto sizes = ResultSizeFor(mask);  // already validated C++ sizing
    if (destination_.result || destination_.gpu) {
      if (!destination_.result || !destination_.gpu ||
          (destination_.result & 15) || (destination_.gpu & 4095) ||
          destination_.result_size < sizes.result ||
          destination_.gpu_size < sizes.gpu_resources)
        return kAssetsInvalid;
      buffers_ = destination_;
      std::memset(Host(buffers_.result), 0, sizes.result);
      std::memset(Host(buffers_.gpu), 0, sizes.gpu_resources);
    } else {
      buffers_ = {Allocate(sizes.result), sizes.result,
                  Allocate(sizes.gpu_resources), sizes.gpu_resources};
    }
    control_ = Allocate(0x2000);
    scratch_ = Allocate(kScratch);
    ldi_ = Allocate(kLdi);
    if (!buffers_.result || !buffers_.gpu || !control_ || !scratch_ || !ldi_)
      return kAssetsNoMemory;
    for (const auto& s : selection_) {
      auto source = toc_.Blob(s.id.index);
      const auto p = Allocate(uint32_t(source.size()));
      if (!p) return kAssetsNoMemory;
      std::memcpy(Host(p), source.data(), source.size());
      sources_.push_back(p);
    }
    ScopedXamProfile profile(memory_, callbacks_);
    call_(0x81965FD0, {control_});
    const std::array<uint32_t, 10> allocator = {sizes.result,
                                                sizes.gpu_resources,
                                                kScratch,
                                                kLdi,
                                                buffers_.result,
                                                buffers_.gpu,
                                                scratch_,
                                                ldi_,
                                                0,
                                                0};
    for (size_t i = 0; i < allocator.size(); ++i)
      Put(Host(control_) + 4 * i, allocator[i]);
    // Original aggregate layout: header, skeleton, 13 refs (32 B) and 13
    // descriptors (52 B). Also initializes the CPU/GPU bump cursors.
    const auto hr = call_(0x81967628, {control_, mask});
    return hr ? hr : (CheckGuards() ? 0 : kAssetsFailure);
  }

  uint32_t Decode(size_t index, bool& needs_apply) override {
    if (index >= sources_.size()) return kAssetsInvalid;
    PrepareSelection(index);
    ScopedXamProfile profile(memory_, callbacks_);
    const auto hr = call_(
        0x81966FC8, {control_, control_ + 0x1000, body_, 1, control_ + 0x1200});
    needs_apply = Host(control_)[0x1200] != 0;
    ++stats_.decoded;
    if (!CheckGuards()) return kAssetsFailure;
    return hr;
  }

  uint32_t Apply(size_t index) override {
    if (index >= sources_.size()) return kAssetsInvalid;
    PrepareSelection(index);
    ScopedXamProfile profile(memory_, callbacks_);
    // Apply field 4 only after all assets have populated the shared aggregate.
    const auto hr = call_(0x819672F8, {control_, control_ + 0x1000});
    ++stats_.applied;
    if (!CheckGuards()) return kAssetsFailure;
    return hr;
  }

  uint32_t Finish() override {
    ScopedXamProfile profile(memory_, callbacks_);
    auto* ctx = rex::runtime::ThreadState::Get()->context();
    const auto f1 = ctx->f1, f2 = ctx->f2;
    ctx->f1.f64 = height_;
    ctx->f2.f64 = weight_;
    call_(0x819660A0, {control_, body_});
    ctx->f1 = f1;
    ctx->f2 = f2;
    call_(0x81966348, {control_});
    ++stats_.finalized;
    finished_ = CheckGuards() && ValidateResult();
    return finished_ ? 0 : kAssetsFailure;
  }

  AssetsBuffers Buffers() const override { return buffers_; }
  void Reset() noexcept override {
    CheckGuards();
    if (destination_.result && buffers_.result && !finished_)
      std::memset(Host(buffers_.result), 0, 20);
    for (const auto& b : blocks_) {
      memory_.SystemHeapFree(b.base);
      ++stats_.frees;
    }
    blocks_.clear();
    sources_.clear();
    buffers_ = {};
    control_ = scratch_ = ldi_ = 0;
  }

 private:
  struct Block {
    uint32_t base, size;
  };
  uint8_t* Host(uint32_t p) { return memory_.TranslateVirtual<uint8_t*>(p); }
  uint32_t Allocate(uint32_t size) {
    if (!size) return 0;
    auto p = memory_.SystemHeapAlloc(size + 2 * kGuard, kGuard);
    if (!p) return 0;
    blocks_.push_back({p, size});
    ++stats_.allocations;
    std::memset(Host(p), 0xCD, size + 2 * kGuard);
    std::memset(Host(p + kGuard), 0, size);
    return p + kGuard;
  }
  bool CheckGuards() noexcept {
    for (const auto& b : blocks_)
      for (uint32_t i = 0; i < kGuard; ++i)
        stats_.guards &= Host(b.base)[i] == 0xCD &&
                         Host(b.base + kGuard + b.size)[i] == 0xCD;
    return stats_.guards;
  }
  void PrepareSelection(size_t index) {
    auto* record = Host(control_ + 0x1000);
    std::memset(record, 0, 416);
    const auto& s = selection_[index];
    const auto id = s.id.ToBytes();
    Put(record + 16, s.kind);
    std::memcpy(record + 32, id.data(), 16);
    rex::memory::store_and_swap<uint16_t>(record + 48, uint16_t(s.id.category));
    for (size_t i = 0; i < s.materials.size(); ++i) {
      for (size_t j = 0; j < 4; ++j)
        rex::memory::store_and_swap<float>(record + 64 + i * 32 + j * 4,
                                           s.materials[i].rgba[j]);
      Put(record + 80 + i * 32, s.materials[i].id);
    }
    for (size_t i = 0; i < 3; ++i) Put(record + 52 + i * 4, s.colors[i]);
    Put(record + (s.kind == 2 ? 384 : 388), s.modifier_slot);
    Put(record + 400, sources_[index]);
    Put(record + 404, uint32_t(toc_.Blob(s.id.index).size()));
    Host(control_)[0x1200] = 0;
  }
  static bool Within(uint32_t p, uint64_t size, uint32_t base, uint32_t cap) {
    return p >= base && uint64_t(p) + size <= uint64_t(base) + cap;
  }
  bool Cpu(uint32_t p, uint64_t size) const {
    return Within(p, size, buffers_.result, buffers_.result_size);
  }
  bool Gpu(uint32_t p, uint64_t size) const {
    return Within(p, size, buffers_.gpu, buffers_.gpu_size);
  }
  bool ValidateResult() {
    const auto* root = Host(buffers_.result);
    const auto skeleton = Read(root), count = Read(root + 8),
               refs = Read(root + 12), descriptors = Read(root + 16);
    if (!Cpu(skeleton, 8) || Read(root + 4) != 0 || !count ||
        count > selection_.size() || !Cpu(refs, uint64_t(count) * 32) ||
        !Cpu(descriptors, uint64_t(count) * 52))
      return false;
    const auto bones = Read(Host(skeleton));
    if (!bones || bones > 72 ||
        !Cpu(Read(Host(skeleton) + 4), uint64_t(bones) * 96))
      return false;
    std::vector<bool> seen(selection_.size());
    for (uint32_t i = 0; i < count; ++i) {
      const auto* ref = Host(refs + i * 32);
      size_t found = selection_.size();
      for (size_t j = 0; j < selection_.size(); ++j) {
        auto id = selection_[j].id.ToBytes();
        if (!std::memcmp(ref, id.data(), 16)) found = j;
      }
      if (found == selection_.size() || seen[found] ||
          rex::memory::load_and_swap<uint16_t>(ref + 16) !=
              selection_[found].id.category)
        return false;
      seen[found] = true;
      const auto* d = Host(descriptors + i * 52);
      const auto cpu = Read(d + 28), gpu = Read(d + 32);
      if (!Cpu(cpu, Read(d)) || !Gpu(gpu, Read(d + 4)) ||
          !Within(Read(d + 44), uint64_t(Read(d + 20)) * 512, cpu, Read(d)) ||
          !Within(Read(d + 48), uint64_t(Read(d + 24)) * 44, cpu, Read(d)))
        return false;
    }
    // Both cursors must still belong to their original, shared allocation.
    if (!Cpu(Read(Host(control_) + 32), 0) ||
        !Gpu(Read(Host(control_) + 36), 0))
      return false;
    for (size_t i = 0; i < sources_.size(); ++i) {
      auto source = toc_.Blob(selection_[i].id.index);
      if (std::memcmp(Host(sources_[i]), source.data(), source.size()))
        return false;
    }
    return true;
  }
  rex::memory::Memory& memory_;
  AvatarAotCall call_;
  const AvatarToc& toc_;
  std::vector<AssetSelection> selection_;
  uint32_t protocol_, body_, callbacks_;
  AotAssetsStats& stats_;
  float height_, weight_;
  std::vector<Block> blocks_;
  std::vector<uint32_t> sources_;
  AssetsBuffers buffers_, destination_;
  bool finished_ = false;
  uint32_t control_ = 0, scratch_ = 0, ldi_ = 0;
};
}  // namespace

std::unique_ptr<AssetsBackend> MakeAotAssetsBackend(
    rex::memory::Memory& memory, AvatarAotCall call, const AvatarToc& toc,
    std::vector<AssetSelection> selection, uint32_t protocol,
    uint32_t body_type, uint32_t callbacks, AotAssetsStats& stats,
    AssetsBuffers destination) {
  return std::make_unique<AotAssetsBackend>(
      memory, std::move(call), toc, std::move(selection), protocol, body_type,
      callbacks, stats, 0, 0, destination);
}
std::unique_ptr<AssetsBackend> MakeAotMetadataAssetsBackend(
    rex::memory::Memory& memory, AvatarAotCall call, const AvatarToc& toc,
    std::span<const uint8_t, kMetadataSize> metadata, uint16_t mask,
    uint32_t callbacks, AotAssetsStats& stats, AssetsBuffers destination) {
  const auto body = BodyType(metadata);
  const auto height = rex::memory::load_and_swap<float>(metadata.data() + 4);
  const auto weight = rex::memory::load_and_swap<float>(metadata.data() + 8);
  if (!body || !mask || (mask & ~0x1FFFu) || !std::isfinite(height) ||
      !std::isfinite(weight) || std::abs(height) > 1 || std::abs(weight) > 1)
    return nullptr;
  auto color = [](uint32_t argb) {
    return std::array<float, 4>{
        float((argb >> 16) & 255) / 255, float((argb >> 8) & 255) / 255,
        float(argb & 255) / 255, float(argb >> 24) / 255};
  };
  const auto skin = Read(metadata.data() + 252),
             hair = Read(metadata.data() + 256);
  // Original nearest-palette skin material in v1, used by the game's shaders.
  call(0x81967E08, {skin});
  std::array<float, 4> skin_material;
  for (size_t i = 0; i < 4; ++i)
    skin_material[i] =
        rex::runtime::ThreadState::Get()->context()->v1.f32[3 - i];
  std::vector<AssetSelection> selections;
  auto add = [&](size_t offset, uint32_t kind, uint32_t slot) {
    auto id = AssetId::FromBytes(
        std::span<const uint8_t, 16>(metadata.data() + offset, 16));
    if (id.IsZero() || (kind == 1 && !(id.category & mask))) return;
    AssetSelection s;
    s.id = id;
    s.kind = kind;
    s.modifier_slot = slot;
    if (kind == 1) {
      for (size_t i = 0; i < 3; ++i) {
        s.colors[i] = Read(metadata.data() + offset + 20 + i * 4);
        s.materials[i] = {uint32_t(22 + i), color(s.colors[i])};
      }
      s.materials[3] = {27, skin_material};
      if (offset == 288) {
        s.materials = {};
        s.materials[0] = {22, color(skin)};
        s.materials[1] = {27, skin_material};
      } else if (offset == 320) {
        s.materials[0] = {27, skin_material};
        for (size_t i = 0; i < 9; ++i)
          s.materials[i + 1] = {uint32_t(13 + i),
                                color(Read(metadata.data() + 252 + i * 4))};
      } else if (id.category == 4)
        s.materials[0] = {22, color(hair)};
    }
    selections.push_back(s);
  };
  add(288, 1, 0);
  add(320, 1, 0);
  for (size_t i = 0; i < 13; ++i) add(352 + i * 32, 1, 0);
  if (mask & 1) {
    for (size_t i = 0; i < 6; ++i) add(60 + i * 32, 2, uint32_t(i));
    for (size_t i = 0; i < 3; ++i) add(12 + i * 16, 3, uint32_t(i));
  }
  return std::make_unique<AotAssetsBackend>(
      memory, std::move(call), toc, std::move(selections), 4, body, callbacks,
      stats, height, weight, destination);
}

}  // namespace x360n::avatar
