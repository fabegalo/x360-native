#include "gpu/xenos/xenos_graphics_driver_base.h"
#include "gpu/xenos/host/d3d12/d3d12_presenter.h"
#include <d3d12sdklayers.h>
#include <iostream>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
void Check(HRESULT result, const char* operation) {
  if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": " + std::to_string(unsigned(result)));
}
void Wait(ID3D12Device& device, ID3D12CommandQueue& queue) {
  ComPtr<ID3D12Fence> fence;
  Check(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "create fence");
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("create fence event");
  rex::ScopeGuard close([&] { CloseHandle(event); });
  Check(fence->SetEventOnCompletion(1, event), "fence event");
  Check(queue.Signal(fence.Get(), 1), "queue fence");
  if (WaitForSingleObject(event, 3000) != WAIT_OBJECT_0) throw std::runtime_error("GPU fence timeout");
}
void Fill(rex::gpu::d3d12::D3D12Provider& provider, rex::gpu::Presenter::GuestOutputRefreshContext& context) {
  auto* device = provider.GetDevice();
  auto* texture = static_cast<rex::gpu::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context).GetResourceUAVCapable();
  const auto texture_desc = texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 bytes = 0;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
  D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes; desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> upload;
  Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                       nullptr, IID_PPV_ARGS(&upload)), "create upload");
  void* mapped = nullptr;
  D3D12_RANGE no_read{};
  Check(upload->Map(0, &no_read, &mapped), "map upload");
  for (UINT y = 0; y < footprint.Footprint.Height; ++y) {
    auto* row = reinterpret_cast<UINT*>(static_cast<BYTE*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch);
    for (UINT x = 0; x < footprint.Footprint.Width; ++x) row[x] = 0xC0000000u | 256u | (512u << 10) | (768u << 20);
  }
  upload->Unmap(0, nullptr);
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "create allocator");
  Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)), "create commands");
  D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = rex::gpu::d3d12::D3D12Presenter::kGuestOutputInternalState;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  commands->ResourceBarrier(1, &barrier);
  D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = texture; destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = upload.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint;
  commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
  commands->ResourceBarrier(1, &barrier);
  Check(commands->Close(), "close commands");
  ID3D12CommandList* submitted[] = {commands.Get()};
  provider.GetDirectQueue()->ExecuteCommandLists(1, submitted);
  Wait(*device, *provider.GetDirectQueue());
  context.SetIs8bpc(false);
}
}

// White-box regression for the SDK/Native shared presenter, with real GPU work and no guest runtime.
void CheckD3D12Presenter(rex::gpu::IGraphicsDriver& graphics, rex::ScopeGuard& shutdown) {
  auto* xenos = graphics.AsXenos();
  if (!xenos) throw std::runtime_error("presenter contract requires a Xenos-family driver");
  auto& driver = *static_cast<rex::gpu::XenosGraphicsDriverBase*>(xenos);
  HWND window = CreateWindowExW(0, L"STATIC", L"x360n presenter contract", WS_OVERLAPPEDWINDOW,
                                0, 0, 160, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window) throw std::runtime_error("create test window");
  rex::ScopeGuard close([&] { shutdown.Reset(); DestroyWindow(window); });
  auto surface = graphics.SetSurface({.type = rex::NativeWindow::Type::kWin32, .window = window});
  if (!surface) throw std::runtime_error(surface.error().GetDescription());
  graphics.ResizeSurface(128, 128);
  if (!graphics.BeginPresent()) throw std::runtime_error("connect test surface");
  auto* presenter = driver.GetHostPresenter();
  auto& provider = *static_cast<rex::gpu::d3d12::D3D12Provider*>(driver.GetProvider());
  ComPtr<ID3D12InfoQueue> validation;
  Check(provider.GetDevice()->QueryInterface(IID_PPV_ARGS(&validation)), "D3D12 debug layer required");
  for (const auto* effect : {"bilinear", "cas", "fsr"}) {
    auto configured = driver.present_effect.SetText(rex::SettingLayer::kRuntime, effect);
    if (!configured) throw std::runtime_error(configured.error().GetDescription());
    validation->ClearStoredMessages();
    if (!presenter->RefreshGuestOutput(8, 8, 1, 1, [&](auto& context) { Fill(provider, context); return true; }))
      throw std::runtime_error("refresh test image");
    graphics.EndPresent();
    Wait(*provider.GetDevice(), *provider.GetDirectQueue());
    rex::gpu::RawImage image;
    if (!graphics.CaptureGuestOutput(image) || image.width != 8 || image.height != 8)
      throw std::runtime_error("capture test image");
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
      const auto offset = y * image.stride + x * 4;
      if (image.data.at(offset) != 64 || image.data.at(offset + 1) != 128 || image.data.at(offset + 2) != 191)
        throw std::runtime_error("presenter changed source pixels");
    }
    for (UINT64 i = 0; i < validation->GetNumStoredMessages(); ++i) {
      SIZE_T size = 0; Check(validation->GetMessage(i, nullptr, &size), "query validation message");
      std::vector<BYTE> bytes(size); auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
      Check(validation->GetMessage(i, message, &size), "read validation message");
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
        throw std::runtime_error(std::string("D3D12 validation: ") + message->pDescription);
    }
    std::cout << "Presenter actual GPU draw: " << effect << ", 64 source pixels unchanged, no D3D12 errors\n";
  }
}
