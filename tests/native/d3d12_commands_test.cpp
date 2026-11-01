// Redundant graphics state filtering in BasicD3D12Commands: with the filter,
// every draw must see exactly the state it sees when every call is recorded.
// The fake list applies the strictest D3D12 invalidation rules (any root
// signature call drops the root arguments, any descriptor heap call drops the
// descriptor tables, the pipeline state is shared with compute) and takes a
// snapshot of the effective state at each draw.
#include "../../src/native/d3d12/d3d12_commands.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <map>
#include <string>
#include <vector>
using namespace x360n::native;
namespace {
struct FakeList {
  struct State {
    const void* pipeline = nullptr;
    const void* root = nullptr;
    std::vector<const void*> heaps;
    std::map<UINT, uint64_t> tables, views;
    std::string viewport, scissor, index_buffer;
    int topology = -1;
    bool no_targets = false;
    bool operator==(const State&) const = default;
  } state;
  std::vector<State> draws;
  size_t calls = 0;
  template <class T> static std::string Bytes(const T& value) { return {reinterpret_cast<const char*>(&value), sizeof(T)}; }
  void SetPipelineState(ID3D12PipelineState* p) { ++calls; state.pipeline = p; }
  void SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap* const* heaps) {
    ++calls; state.heaps.assign(heaps, heaps + count); state.tables.clear();
  }
  void SetGraphicsRootSignature(ID3D12RootSignature* r) { ++calls; state.root = r; state.tables.clear(); state.views.clear(); }
  void SetComputeRootSignature(ID3D12RootSignature*) { ++calls; }
  void SetGraphicsRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) { ++calls; state.views.erase(i); state.tables[i] = h.ptr; }
  void SetComputeRootDescriptorTable(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) { ++calls; }
  void SetGraphicsRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { ++calls; state.tables.erase(i); state.views[i] = a; }
  void RSSetViewports(UINT count, const D3D12_VIEWPORT* v) { ++calls; state.viewport = count ? Bytes(*v) : ""; }
  void RSSetScissorRects(UINT count, const D3D12_RECT* r) { ++calls; state.scissor = count ? Bytes(*r) : ""; }
  void OMSetRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE* d) {
    ++calls; state.no_targets = !count && !d;
  }
  void IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY t) { ++calls; state.topology = int(t); }
  void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) { ++calls; state.index_buffer = view ? Bytes(*view) : ""; }
  void DrawInstanced(UINT, UINT, UINT, UINT) { ++calls; draws.push_back(state); }
  void DrawIndexedInstanced(UINT, UINT, UINT, INT, UINT) { ++calls; draws.push_back(state); }
  void Dispatch(UINT, UINT, UINT) { ++calls; }
  // A reset list holds no state (Reset + ResetWorkTracking in the renderer).
  void Reset() { state = {}; }
};
using Commands = BasicD3D12Commands<FakeList>;
template <class T> T* Fake(uintptr_t value) { return reinterpret_cast<T*>(value); }

// The renderer's draw sequence: the whole state, then the draw.
void Draw(Commands& c, uintptr_t root, const void* pipeline_identity, ID3D12PipelineState* pipeline,
          uint64_t constants, float viewport_x, D3D12_PRIMITIVE_TOPOLOGY topology, bool indexed,
          const std::shared_future<Commands::Pipeline>* pending = nullptr) {
  ID3D12DescriptorHeap* heaps[]{Fake<ID3D12DescriptorHeap>(0x100), Fake<ID3D12DescriptorHeap>(0x200)};
  c.SetDescriptorHeaps(2, heaps);
  c.SetGraphicsRootSignature(Fake<ID3D12RootSignature>(root));
  if (pending) c.SetPipelineState(*pending, pipeline_identity);
  else c.SetPipelineState(pipeline);
  c.SetGraphicsRootDescriptorTable(5, {0x5000});
  const D3D12_VIEWPORT vp{viewport_x, 0, 64, 64, 0, 1};
  c.RSSetViewports(1, &vp);
  const D3D12_RECT rect{0, 0, 64, 64};
  c.RSSetScissorRects(1, &rect);
  c.SetGraphicsRootConstantBufferView(0, constants);
  c.SetGraphicsRootConstantBufferView(1, 0x9000);
  c.SetGraphicsRootDescriptorTable(7, {0x7000});
  c.OMSetRenderTargets(0, nullptr, FALSE, nullptr);
  c.IASetPrimitiveTopology(topology);
  if (indexed) {
    const D3D12_INDEX_BUFFER_VIEW ib{0xA000, 64, DXGI_FORMAT_R32_UINT};
    c.IASetIndexBuffer(&ib);
    c.DrawIndexedInstanced(3, 1, 0, 0, 0);
  } else {
    c.DrawInstanced(3, 1, 0, 0);
  }
}
void Dispatch(Commands& c, ID3D12PipelineState* compute) {
  ID3D12DescriptorHeap* heaps[]{Fake<ID3D12DescriptorHeap>(0x100), Fake<ID3D12DescriptorHeap>(0x200)};
  c.SetDescriptorHeaps(2, heaps);
  c.SetComputeRootSignature(Fake<ID3D12RootSignature>(0x30));
  c.SetPipelineState(compute);
  c.SetComputeRootDescriptorTable(1, {0x1000});
  c.Dispatch(1, 1, 1);
}
// A frame-like sequence touching every invalidation rule.
FakeList Run(bool filter) {
  FakeList list;
  Commands c;
  c.Attach(&list);
  c.SetStateFilter(filter);
  auto* graphics = Fake<ID3D12PipelineState>(0x10);
  auto* other = Fake<ID3D12PipelineState>(0x11);
  auto* compute = Fake<ID3D12PipelineState>(0x12);
  for (int i = 0; i < 4; ++i) Draw(c, 0x20, nullptr, graphics, 0x8000, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
  Draw(c, 0x20, nullptr, graphics, 0x8100, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true);   // New constants, indexed.
  Draw(c, 0x21, nullptr, other, 0x8100, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true);      // Root signature change.
  Draw(c, 0x20, nullptr, graphics, 0x8100, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, true);  // Back, new topology.
  Dispatch(c, compute);                                                                      // Shared pipeline state.
  Draw(c, 0x20, nullptr, graphics, 0x8100, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, true);
  Draw(c, 0x20, nullptr, graphics, 0x8100, 8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, false); // Viewport changes.
  // Submission: the list is reset and holds nothing.
  list.Reset();
  c.ResetWorkTracking();
  Draw(c, 0x20, nullptr, graphics, 0x8100, 8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, false);
  // A pipeline still being created defers recording; the same pending
  // pipeline bound twice is one call, a different one is recorded.
  std::promise<Commands::Pipeline> promise;
  const auto pending = promise.get_future().share();
  int identity = 0;
  Draw(c, 0x20, &identity, nullptr, 0x8100, 8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, false, &pending);
  Draw(c, 0x20, &identity, nullptr, 0x8100, 8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, false, &pending);
  Draw(c, 0x20, nullptr, graphics, 0x8100, 8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, false);
  REQUIRE(c.deferred());
  promise.set_value(nullptr);
  c.Replay();
  return list;
}
}

TEST_CASE("redundant D3D12 state is skipped without changing any draw's state", "[native-d3d12]") {
  const auto all = Run(false);
  const auto filtered = Run(true);
  REQUIRE(all.draws.size() == 13);
  REQUIRE(filtered.draws.size() == all.draws.size());
  for (size_t i = 0; i < all.draws.size(); ++i) {
    INFO("draw " << i);
    CHECK(filtered.draws[i] == all.draws[i]);
  }
  // Repeated draws record only the draw itself plus what changed.
  CHECK(filtered.calls < all.calls * 2 / 3);
}

TEST_CASE("the state cache is empty after a list reset", "[native-d3d12]") {
  FakeList list;
  Commands c;
  c.Attach(&list);
  Draw(c, 0x20, nullptr, Fake<ID3D12PipelineState>(0x10), 0x8000, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
  const size_t first = list.calls;
  list.Reset();
  c.ResetWorkTracking();
  Draw(c, 0x20, nullptr, Fake<ID3D12PipelineState>(0x10), 0x8000, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
  CHECK(list.calls == 2 * first);  // Everything recorded again.
  CHECK(list.draws[1] == list.draws[0]);
}
