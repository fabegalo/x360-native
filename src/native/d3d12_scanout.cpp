#include "d3d12_scanout.h"
#include "scanout_source.h"
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>
#include <string>
namespace x360n::native {
std::unique_ptr<ScanoutBackend> CreateD3D12Scanout(rex::ui::GraphicsProvider& provider) {
  return std::make_unique<D3D12Scanout>(static_cast<rex::ui::d3d12::D3D12Provider&>(provider));
}
namespace {
void Check(HRESULT hr, const char* operation) {
  if (FAILED(hr)) throw std::runtime_error(std::string("Native D3D12: ") + operation +
                                         " HRESULT=" + std::to_string(uint32_t(hr)));
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  return b;
}
}
D3D12Scanout::D3D12Scanout(rex::ui::d3d12::D3D12Provider& provider) : provider_(provider) {
  auto* device = provider_.GetDevice();
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  D3D12_ROOT_PARAMETER parameters[4]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor = {0, 0};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[1].Descriptor = {1, 0};
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable = {1, &range};
  parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[3].Constants = {0, 0, 8};
  D3D12_ROOT_SIGNATURE_DESC desc{4, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Ptr<ID3DBlob> blob, errors;
  Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root signature");
  Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)), "create root signature");
  blob.Reset();
  const auto hr = D3DCompile(kScanoutSource, sizeof(kScanoutSource) - 1, "native-scanout", nullptr,
      nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blob, &errors);
  if (FAILED(hr) && errors) throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
  Check(hr, "compile scanout");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = root_.Get();
  pso.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
  Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline_)), "scanout pipeline");
  D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors_)), "descriptor heap");
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence");
  for (auto& slot : slots_) {
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator)), "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr, IID_PPV_ARGS(&slot.list)), "list");
    Check(slot.list->Close(), "close initial list");
  }
  event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event_) throw std::runtime_error("Native: cannot create fence event");
}
D3D12Scanout::~D3D12Scanout() {
  // Owner drains before destruction. Never throw from teardown.
  if (event_) CloseHandle(event_);
}
void D3D12Scanout::Wait(uint64_t serial) {
  if (!serial) return;
  const auto completed = fence_->GetCompletedValue();
  if (completed == UINT64_MAX) throw std::runtime_error("Native: D3D12 device removed");
  if (completed >= serial) return;
  Check(fence_->SetEventOnCompletion(serial, event_), "fence notification");
  if (WaitForSingleObject(event_, 5000) != WAIT_OBJECT_0)
    throw std::runtime_error("Native: GPU fence exceeded 5 seconds");
  Check(provider_.GetDevice()->GetDeviceRemovedReason(), "device status");
}
void D3D12Scanout::Drain() { if (render_) render_->Flush(); Wait(serial_); }
rex::graphics::Shader* D3D12Scanout::LoadShader(rex::graphics::xenos::ShaderType type, const uint32_t* data, uint32_t count) {
  if (!render_) render_ = std::make_unique<D3D12Render>(provider_);
  return render_->shaders.Load(type, data, count);
}
void D3D12Scanout::Draw(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory, rex::graphics::Shader* vs, rex::graphics::Shader* ps,
    rex::graphics::xenos::PrimitiveType primitive, uint32_t count, const DrawIndices* indices) {
  if (!render_) render_ = std::make_unique<D3D12Render>(provider_);
  render_->Draw(regs, memory, vs, ps, primitive, count, indices);
}
void D3D12Scanout::Resolve(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory) {
  if (!render_) render_ = std::make_unique<D3D12Render>(provider_);
  render_->Resolve(regs, memory);
}
void D3D12Scanout::Present(rex::ui::Presenter& presenter, const ScanoutTexture& t,
                         const void* pixels, const uint32_t* lut) {
  if (render_) render_->Flush();
  const unsigned index = unsigned(serial_ % slots_.size());
  auto& slot = slots_[index];
  Wait(slot.serial);
  auto* device = provider_.GetDevice();
  const uint64_t lut_offset = (uint64_t(t.bytes) + 255) & ~255ull;
  const uint64_t required = lut_offset + 1024;
  if (slot.capacity < required) {
    slot.upload.Reset();
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = required;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&slot.upload)), "upload buffer");
    slot.capacity = required;
  }
  void* mapped = nullptr; D3D12_RANGE read_range{};
  Check(slot.upload->Map(0, &read_range, &mapped), "map upload");
  std::memcpy(mapped, pixels, t.bytes);
  std::memcpy(static_cast<uint8_t*>(mapped) + lut_offset, lut, 1024);
  slot.upload->Unmap(0, nullptr);
  if (!presenter.RefreshGuestOutput(t.width, t.height, t.width, t.height,
      [&](rex::ui::Presenter::GuestOutputRefreshContext& context) {
    using Presenter = rex::ui::d3d12::D3D12Presenter;
    auto* output = static_cast<Presenter::D3D12GuestOutputRefreshContext&>(context).resource_uav_capable();
    Check(slot.allocator->Reset(), "reset allocator");
    Check(slot.list->Reset(slot.allocator.Get(), pipeline_.Get()), "reset list");
    auto cpu = descriptors_->GetCPUDescriptorHandleForHeapStart();
    auto gpu = descriptors_->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += index * provider_.GetViewDescriptorSize();
    gpu.ptr += index * provider_.GetViewDescriptorSize();
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
    view.Format = Presenter::kGuestOutputFormat; view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(output, nullptr, &view, cpu);
    auto before = Transition(output, Presenter::kGuestOutputInternalState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    slot.list->ResourceBarrier(1, &before);
    ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
    slot.list->SetDescriptorHeaps(1, heaps);
    slot.list->SetComputeRootSignature(root_.Get());
    slot.list->SetComputeRootShaderResourceView(0, slot.upload->GetGPUVirtualAddress());
    slot.list->SetComputeRootShaderResourceView(1, slot.upload->GetGPUVirtualAddress() + lut_offset);
    slot.list->SetComputeRootDescriptorTable(2, gpu);
    const uint32_t constants[] = {t.width, t.height, t.pitch, t.tiled, t.endian, t.swizzle, 0, 0};
    slot.list->SetComputeRoot32BitConstants(3, 8, constants, 0);
    slot.list->Dispatch((t.width + 7) / 8, (t.height + 7) / 8, 1);
    auto after = Transition(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, Presenter::kGuestOutputInternalState);
    slot.list->ResourceBarrier(1, &after);
    Check(slot.list->Close(), "close scanout list");
    ID3D12CommandList* lists[] = {slot.list.Get()};
    provider_.GetDirectQueue()->ExecuteCommandLists(1, lists);
    Check(provider_.GetDirectQueue()->Signal(fence_.Get(), ++serial_), "signal scanout fence");
    slot.serial = serial_;
    return true;
  })) throw std::runtime_error("Native: presenter rejected guest frame");
}
}
