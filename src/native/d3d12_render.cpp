#include "d3d12_render.h"
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/logging.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <bit>
#include <cmath>
namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace {
void Check(HRESULT hr, const char* what) {
  if (FAILED(hr)) throw std::runtime_error(std::string("Native render: ") + what + " HRESULT=" + std::to_string(uint32_t(hr)));
}
void Require(bool v, const char* what) { if (!v) throw std::runtime_error(std::string("Native: ") + what); }
}
D3D12Render::Ptr<ID3D12Resource> D3D12Render::Buffer(uint64_t bytes, D3D12_HEAP_TYPE type,
    D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
  D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
  D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
  d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
  Ptr<ID3D12Resource> r;
  Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)), "buffer");
  return r;
}
D3D12Render::Ptr<ID3D12Resource> D3D12Render::Upload(const void* data, size_t bytes) {
  auto r = Buffer((bytes + 255) & ~size_t(255), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
  void* p; D3D12_RANGE empty{}; Check(r->Map(0, &empty, &p), "map upload");
  std::memcpy(p, data, bytes); r->Unmap(0, nullptr); transient_.push_back(r); return r;
}
D3D12_GPU_VIRTUAL_ADDRESS D3D12Render::Constants(const void* p, size_t n) { return Upload(p, n)->GetGPUVirtualAddress(); }
void D3D12Render::Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES after) {
  if (before == after) return;
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list_->ResourceBarrier(1, &b); before = after;
}
void D3D12Render::Barrier() {
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = edram_.Get();
  list_->ResourceBarrier(1, &b);
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Render::Cpu(uint32_t i, bool s) {
  auto h = (s ? samplers_ : views_)->GetCPUDescriptorHandleForHeapStart();
  h.ptr += i * device_->GetDescriptorHandleIncrementSize(s ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE D3D12Render::Gpu(uint32_t i, bool s) {
  auto h = (s ? samplers_ : views_)->GetGPUDescriptorHandleForHeapStart();
  h.ptr += i * device_->GetDescriptorHandleIncrementSize(s ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
uint32_t D3D12Render::Views(uint32_t n) { Require(view_count_ + n <= 65536, "view heap exhausted"); auto i = view_count_; view_count_ += n; return i; }
uint32_t D3D12Render::Samplers(uint32_t n) { Require(sampler_count_ + n <= 2048, "sampler heap exhausted"); auto i = sampler_count_; sampler_count_ += n; return i; }
D3D12Render::D3D12Render(rex::ui::d3d12::D3D12Provider& provider) : provider_(provider), device_(provider.GetDevice()) {
  device_->QueryInterface(IID_PPV_ARGS(&debug_));
  D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
  Check(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)), "feature query");
  Require(options.ROVsSupported, "D3D12 rasterizer ordered views required");
  Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "allocator");
  Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "command list");
  Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence");
  event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(event_ != nullptr, "fence event");
  memory_ = Buffer(0x20000000, D3D12_HEAP_TYPE_DEFAULT, memory_state_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  edram_ = Buffer(kEdramSizeBytes, D3D12_HEAP_TYPE_DEFAULT, edram_state_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&views_)), "views");
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = 2048;
  Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&samplers_)), "samplers");
  // Fixed binding ABI of the shared translator. Submission and resources are Native-owned.
  D3D12_ROOT_PARAMETER p[10]{};
  const unsigned cb[] = {0, 1, 1, 2, 3};
  for (unsigned i = 0; i < 5; ++i) { p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[i].Descriptor = {cb[i], 0}; }
  p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_DESCRIPTOR_RANGE ranges[7] = {
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 255, 1, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 127, 0, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 255, 1, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 127, 0, 0, 0}};
  p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[5].DescriptorTable = {2, ranges};
  for (unsigned i = 6; i < 10; ++i) {
    p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[i].DescriptorTable = {1, &ranges[i - 4]};
    p[i].ShaderVisibility = i < 8 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_VERTEX;
  }
  D3D12_ROOT_SIGNATURE_DESC rd{10, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Ptr<ID3DBlob> blob, errors;
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "draw root serialization");
  Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)), "draw root");
}
D3D12Render::~D3D12Render() { if (event_) CloseHandle(event_); }
void D3D12Render::Flush() {
  Check(list_->Close(), "close"); ID3D12CommandList* lists[]{list_.Get()};
  provider_.GetDirectQueue()->ExecuteCommandLists(1, lists);
  Check(provider_.GetDirectQueue()->Signal(fence_.Get(), ++serial_), "signal");
  Check(fence_->SetEventOnCompletion(serial_, event_), "fence completion");
  Require(WaitForSingleObject(event_, 5000) == WAIT_OBJECT_0, "render GPU timeout");
  Check(device_->GetDeviceRemovedReason(), "device status");
  if (debug_) {
    const auto messages = debug_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (uint64_t i = 0; i < messages; ++i) {
      SIZE_T bytes = 0; debug_->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> storage(bytes);
      auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
      Check(debug_->GetMessage(i, message, &bytes), "debug message");
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
        throw std::runtime_error(std::string("Native D3D12 validation: ") + message->pDescription);
    }
    debug_->ClearStoredMessages();
  }
  transient_.clear(); view_count_ = sampler_count_ = 0;
  Check(allocator_->Reset(), "reset allocator"); Check(list_->Reset(allocator_.Get(), nullptr), "reset list");
}
void D3D12Render::UploadGuest(rex::memory::Memory& m, uint32_t address, uint32_t bytes) {
  Require(uint64_t(address) + bytes <= 0x20000000, "guest upload bounds");
  if (!bytes) return;
  auto data = Upload(m.TranslatePhysical(address), bytes);
  Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  list_->CopyBufferRegion(memory_.Get(), address, data.Get(), 0, bytes);
}
}
