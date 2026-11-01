#pragma once
#include "../pipeline/deferred_commands.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <utility>
#include <stdexcept>
#include <vector>
namespace x360n::native {
// The ID3D12GraphicsCommandList subset Native records, through
// DeferredCommands. Pointer arguments are copied: deferred commands outlive
// the caller's frame. Referenced D3D12 objects already live until the
// submission's fence (the same lifetime the GPU needs), which is after replay.
//
// Graphics state equal to what the list already holds is not recorded again
// (draws re-specify their whole state; most of it repeats). Every command
// goes through this class and the cache is cleared with each list reset, so
// it always mirrors the list: replay keeps the recording order. D3D12 rules
// kept: a different graphics root signature invalidates the root arguments,
// different descriptor heaps invalidate the descriptor tables, and the
// pipeline state is shared with compute (dispatches update it here too).
template <class L>
class BasicD3D12Commands {
 public:
  using Pipeline = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
  void Attach(L* list) { commands_.Attach(list); bound_ = {}; }
  bool deferred() const { return commands_.deferred(); }
  void Replay() { commands_.Replay(); }
  bool empty() const { return empty_; }
  // Only after resetting the command list, past the optional timestamp
  // preamble: the reset list holds no state.
  void ResetWorkTracking() { empty_ = true; bound_ = {}; }
  // native_redundant_state_filter: off records every call (A/B, diagnosis).
  void SetStateFilter(bool enabled) { filter_ = enabled; bound_ = {}; }
  uint64_t filtered() const { return filtered_; }

  void SetPipelineState(ID3D12PipelineState* p) {
    if (Same(bound_.pipeline, static_cast<const void*>(p))) return;
    Rec([=](L* l) { l->SetPipelineState(p); });
  }
  // `identity` names the pending pipeline (stable for its whole lifetime).
  void SetPipelineState(const std::shared_future<Pipeline>& p, const void* identity) {
    if (Same(bound_.pipeline, identity)) return;
    empty_ = false;
    commands_.BindPending(p, [](L* l, const Pipeline& pipeline) { l->SetPipelineState(pipeline.Get()); });
  }
  void SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap* const* heaps) {
    if (count > 2) throw std::invalid_argument("Native D3D12: more than two descriptor heaps");
    std::array<ID3D12DescriptorHeap*, 2> copy{};
    std::copy_n(heaps, count, copy.begin());
    if (Same(bound_.heaps, std::pair{count, copy})) return;
    bound_.tables = 0;
    Rec([=](L* l) { l->SetDescriptorHeaps(count, copy.data()); });
  }
  void SetGraphicsRootSignature(ID3D12RootSignature* r) {
    if (Same(bound_.root, r)) return;
    bound_.tables = bound_.views = 0;
    Rec([=](L* l) { l->SetGraphicsRootSignature(r); });
  }
  void SetComputeRootSignature(ID3D12RootSignature* r) { Rec([=](L* l) { l->SetComputeRootSignature(r); }); }
  void SetGraphicsRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
    if (Argument(bound_.tables, i, h.ptr)) return;
    Rec([=](L* l) { l->SetGraphicsRootDescriptorTable(i, h); });
  }
  void SetComputeRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
    Rec([=](L* l) { l->SetComputeRootDescriptorTable(i, h); });
  }
  void SetGraphicsRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
    if (Argument(bound_.views, i, a)) return;
    Rec([=](L* l) { l->SetGraphicsRootConstantBufferView(i, a); });
  }
  void SetComputeRoot32BitConstants(UINT i, UINT count, const void* data, UINT offset) {
    if (!commands_.deferred()) { Direct()->SetComputeRoot32BitConstants(i, count, data, offset); return; }
    std::vector<uint32_t> copy(count);
    std::memcpy(copy.data(), data, size_t(count) * 4);
    Rec([=, copy = std::move(copy)](L* l) { l->SetComputeRoot32BitConstants(i, count, copy.data(), offset); });
  }
  void RSSetViewports(UINT count, const D3D12_VIEWPORT* v) {
    if (count == 1 && Same(bound_.viewport, Bytes(*v))) return;
    if (count != 1) bound_.viewport.reset();
    if (!commands_.deferred()) { Direct()->RSSetViewports(count, v); return; }
    std::vector<D3D12_VIEWPORT> copy(v, v + count);
    Rec([copy = std::move(copy)](L* l) { l->RSSetViewports(UINT(copy.size()), copy.data()); });
  }
  void RSSetScissorRects(UINT count, const D3D12_RECT* r) {
    if (count == 1 && Same(bound_.scissor, Bytes(*r))) return;
    if (count != 1) bound_.scissor.reset();
    if (!commands_.deferred()) { Direct()->RSSetScissorRects(count, r); return; }
    std::vector<D3D12_RECT> copy(r, r + count);
    Rec([copy = std::move(copy)](L* l) { l->RSSetScissorRects(UINT(copy.size()), copy.data()); });
  }
  void OMSetRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* targets, BOOL single,
                          const D3D12_CPU_DESCRIPTOR_HANDLE* depth) {
    // Only "no render targets" (the ROV path) is remembered.
    const bool none = !count && !targets && !depth;
    if (none && Same(bound_.no_targets, true)) return;
    if (!none) bound_.no_targets.reset();
    if (!commands_.deferred()) { Direct()->OMSetRenderTargets(count, targets, single, depth); return; }
    // A single-handle range passes one descriptor for all targets.
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> copy;
    if (targets) copy.assign(targets, targets + (single ? std::min(count, 1u) : count));
    std::optional<D3D12_CPU_DESCRIPTOR_HANDLE> ds;
    if (depth) ds = *depth;
    Rec([=, copy = std::move(copy)](L* l) {
      l->OMSetRenderTargets(count, copy.empty() ? nullptr : copy.data(), single, ds ? &*ds : nullptr);
    });
  }
  void IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY t) {
    if (Same(bound_.topology, t)) return;
    Rec([=](L* l) { l->IASetPrimitiveTopology(t); });
  }
  void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) {
    std::optional<D3D12_INDEX_BUFFER_VIEW> copy;
    if (view) copy = *view;
    if (view ? Same(bound_.index_buffer, Bytes(*view)) : (bound_.index_buffer.reset(), false)) return;
    Rec([=](L* l) { l->IASetIndexBuffer(copy ? &*copy : nullptr); });
  }
  void DrawInstanced(UINT v, UINT i, UINT sv, UINT si) { Rec([=](L* l) { l->DrawInstanced(v, i, sv, si); }); }
  void DrawIndexedInstanced(UINT n, UINT i, UINT si, INT bv, UINT sinst) {
    Rec([=](L* l) { l->DrawIndexedInstanced(n, i, si, bv, sinst); });
  }
  void Dispatch(UINT x, UINT y, UINT z) { Rec([=](L* l) { l->Dispatch(x, y, z); }); }
  void ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* b) {
    if (!commands_.deferred()) { Direct()->ResourceBarrier(count, b); return; }
    std::vector<D3D12_RESOURCE_BARRIER> copy(b, b + count);
    Rec([copy = std::move(copy)](L* l) { l->ResourceBarrier(UINT(copy.size()), copy.data()); });
  }
  void CopyBufferRegion(ID3D12Resource* dst, UINT64 dst_offset, ID3D12Resource* src, UINT64 src_offset, UINT64 bytes) {
    Rec([=](L* l) { l->CopyBufferRegion(dst, dst_offset, src, src_offset, bytes); });
  }
  void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst, UINT x, UINT y, UINT z,
                         const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* box) {
    if (!commands_.deferred()) { Direct()->CopyTextureRegion(dst, x, y, z, src, box); return; }
    const auto d = *dst, s = *src;
    std::optional<D3D12_BOX> b;
    if (box) b = *box;
    Rec([=](L* l) { l->CopyTextureRegion(&d, x, y, z, &s, b ? &*b : nullptr); });
  }
  void EndQuery(ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT index) {
    Rec([=](L* l) { l->EndQuery(heap, type, index); });
  }
  void ResolveQueryData(ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT start, UINT count,
                        ID3D12Resource* destination, UINT64 offset) {
    Rec([=](L* l) { l->ResolveQueryData(heap, type, start, count, destination, offset); });
  }
 private:
  template <class T> static std::array<uint8_t, sizeof(T)> Bytes(const T& value) {
    std::array<uint8_t, sizeof(T)> bytes;
    std::memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
  }
  // True (skip the call) when the list already holds `value`; else remembers it.
  template <class T, class V> bool Same(std::optional<T>& slot, const V& value) {
    if (filter_ && slot && *slot == value) { ++filtered_; return true; }
    slot = value;
    return false;
  }
  bool Argument(uint32_t& valid, UINT index, uint64_t value) {
    if (index >= kRootArguments) return false;
    if (filter_ && (valid >> index & 1) && bound_.arguments[index] == value) { ++filtered_; return true; }
    // A root index is either a table or a view: setting one form drops the other.
    bound_.tables &= ~(1u << index);
    bound_.views &= ~(1u << index);
    valid |= 1u << index;
    bound_.arguments[index] = value;
    return false;
  }
  static constexpr UINT kRootArguments = 16;
  struct Bound {
    std::optional<const void*> pipeline;
    std::optional<ID3D12RootSignature*> root;
    std::optional<std::pair<UINT, std::array<ID3D12DescriptorHeap*, 2>>> heaps;
    uint32_t tables = 0, views = 0;  // Root indices holding a known table/view.
    std::array<uint64_t, kRootArguments> arguments{};
    std::optional<std::array<uint8_t, sizeof(D3D12_VIEWPORT)>> viewport;
    std::optional<std::array<uint8_t, sizeof(D3D12_RECT)>> scissor;
    std::optional<D3D12_PRIMITIVE_TOPOLOGY> topology;
    std::optional<std::array<uint8_t, sizeof(D3D12_INDEX_BUFFER_VIEW)>> index_buffer;
    std::optional<bool> no_targets;
  };
  template <class F> void Rec(F&& f) { empty_ = false; commands_.Record(std::forward<F>(f)); }
  // Only while nothing is deferred: the caller's arguments are used at once.
  L* Direct() { empty_ = false; return commands_.list(); }
  DeferredCommands<L> commands_;
  Bound bound_;
  bool empty_ = true, filter_ = true;
  uint64_t filtered_ = 0;
};
using D3D12Commands = BasicD3D12Commands<ID3D12GraphicsCommandList>;
}
