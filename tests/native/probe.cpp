#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/xenos.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/ui/presenter.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <wincodec.h>
#include <wrl/client.h>
using namespace rex::graphics;
using rex::X_STATUS;
using namespace rex::graphics::xenos;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t Packet(uint32_t op, uint32_t count) { return 0xc0000000 | ((count - 1) << 16) | (op << 8); }
void WritePNG(const std::filesystem::path& path, const rex::ui::RawImage& image) {
  using Microsoft::WRL::ComPtr;
  auto check = [](HRESULT hr) { Require(SUCCEEDED(hr), "WIC PNG encoding"); };
  ComPtr<IWICImagingFactory> factory;
  check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
  ComPtr<IWICStream> stream; check(factory->CreateStream(&stream));
  check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
  ComPtr<IWICBitmapEncoder> encoder; check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
  check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
  ComPtr<IWICBitmapFrameEncode> frame; check(encoder->CreateNewFrame(&frame, nullptr));
  check(frame->Initialize(nullptr)); check(frame->SetSize(image.width, image.height));
  auto format = GUID_WICPixelFormat24bppBGR; check(frame->SetPixelFormat(&format));
  Require(format == GUID_WICPixelFormat24bppBGR, "WIC pixel format");
  std::vector<uint8_t> bgr(image.width * image.height * 3);
  for (unsigned y = 0; y < image.height; ++y) for (unsigned x = 0; x < image.width; ++x)
    for (unsigned c = 0; c < 3; ++c)
      bgr[(y * image.width + x) * 3 + c] = image.data[y * image.stride + x * 4 + 2 - c];
  check(frame->WritePixels(image.height, image.width * 3, UINT(bgr.size()), bgr.data()));
  check(frame->Commit()); check(encoder->Commit());
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 3 && argc != 4) { std::cerr << "usage: x360n_native_probe native|xenos OUTPUT_DIRECTORY [--reject|--abi]\n"; return 2; }
  try {
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM setup");
    const std::filesystem::path output(argv[2]);
    const auto plugin = std::filesystem::path(argv[1]).string();
    std::filesystem::create_directories(output);
    rex::InitLogging((output / "runtime.log").string().c_str());
    rex::Runtime rt(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    Require(rt.Setup(std::move(config)) == X_STATUS_SUCCESS, "runtime setup");
    auto gpu = rex::system::LoadGpuPlugin(plugin, "d3d12");
    Require(bool(gpu), "load plugin ABI");
    if (argc == 4 && std::wstring_view(argv[3]) == L"--abi") {
      using namespace rex::system;
      Require(plugin == "native", "ABI rejection test requires Native");
      HMODULE module = nullptr;
      for (const auto* name : {L"rexgpu-native.dll", L"rexgpu-natived.dll", L"rexgpu-nativerd.dll"})
        if (auto loaded = GetModuleHandleW(name)) module = loaded;
      Require(module != nullptr, "Native module loaded");
      const auto create = reinterpret_cast<GpuCreateFn>(GetProcAddress(module, kGpuCreateSymbol));
      const auto version = reinterpret_cast<GpuAbiVersionFn>(GetProcAddress(module, kGpuAbiVersionSymbol));
      Require(create && version && version() == kGpuPluginAbiVersion, "versioned exports");
      GpuCreateInfo info{sizeof(GpuCreateInfo), "d3d12"};
      Require(create(kGpuPluginAbiVersion + 1, &info) == nullptr, "reject wrong ABI");
      Require(create(kGpuPluginAbiVersion, nullptr) == nullptr, "reject null create info");
      info.struct_size = sizeof(GpuCreateInfo) - 1;
      Require(create(kGpuPluginAbiVersion, &info) == nullptr, "reject short create info");
      Require(!LoadGpuPlugin("native", "vulkan"), "reject Vulkan without fallback");
      for (const auto* name : {L"rexgpu-xenos.dll", L"rexgpu-xenosd.dll", L"rexgpu-xenosrd.dll"})
        Require(GetModuleHandleW(name) == nullptr, "Native must not load Xenos plugin");
      gpu.reset(); rt.Shutdown(); rex::FlushLogging();
      std::cout << "PASS ABI1, invalid ABI/info rejected, Vulkan rejected, no Xenos loaded\n";
      return 0;
    }
    Require(gpu->SetupPresentation(nullptr) == X_STATUS_SUCCESS, "presentation setup");
    Require(gpu->SetupGuestGpu(rt.function_dispatcher(), rt.kernel_state()) == X_STATUS_SUCCESS, "guest GPU setup");
    struct ShutdownGuard {
      rex::system::IGraphicsSystem* gpu;
      ~ShutdownGuard() { if (gpu) gpu->Shutdown(); }
    } shutdown{gpu.get()};
    auto* memory = rt.memory();
    constexpr unsigned width = 256, height = 128, pitch = 320;
    const auto image = memory->SystemHeapAlloc(pitch * height * 4, 4096, rex::memory::kSystemHeapPhysical);
    const auto ring = memory->SystemHeapAlloc(8192, 4096, rex::memory::kSystemHeapPhysical);
    const auto done = memory->SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
    Require(image && ring && done, "guest allocations");
    const auto physical_image = memory->GetPhysicalAddress(image);
    const auto physical_ring = memory->GetPhysicalAddress(ring);
    const auto physical_done = memory->GetPhysicalAddress(done);
    Require(physical_image != UINT32_MAX && physical_ring != UINT32_MAX && physical_done != UINT32_MAX, "physical allocation translation");
    auto* mmio = memory->LookupVirtualMappedRange(0x7fc80714);
    Require(mmio != nullptr, "GPU MMIO registration");
    if (argc == 4) {
      Require(std::wstring_view(argv[3]) == L"--reject", "unknown probe mode");
      const uint32_t words[] = {Packet(0x7e, 1), 0, Packet(PM4_MEM_WRITE, 2), physical_done, 1};
      for (unsigned i = 0; i < 5; ++i)
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(ring + i * 4), words[i]);
      gpu->InitializeRingBuffer(physical_ring, 10);
      mmio->write(nullptr, mmio->callback_context, 0x7fc80714, 5);
      auto* marker = memory->TranslateVirtual<volatile uint32_t*>(done);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (*marker != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      Require(*marker == 1, "rejection probe timed out without explicit failure");
      std::cerr << "UNSUPPORTED PACKET WAS RETIRED\n";
      return 0;
    }
    uint16_t gamma[768];
    uint64_t tested = 0;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned frame = 0; frame < 24; ++frame) {
      xe_gpu_texture_fetch_t fetch{};
      fetch.type = FetchConstantType::kTexture; fetch.dimension = DataDimension::k2DOrStacked;
      fetch.format = TextureFormat::k_8_8_8_8; fetch.pitch = pitch / 32;
      fetch.base_address = physical_image >> 12;
      fetch.size_2d.width = width - 1; fetch.size_2d.height = height - 1;
      fetch.swizzle = frame & 1 ? 0x60a : 0x688; // BGRA / RGBA
      fetch.tiled = (frame / 4) & 1;
      fetch.endianness = Endian(frame & 3);
      auto* data = memory->TranslateVirtual<uint8_t*>(image);
      std::memset(data, 0, pitch * height * 4);
      for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        uint32_t color = x | ((y * 2) << 8) | (((x ^ y ^ frame) & 255) << 16) | 0xff000000;
        color = GpuSwap(color, fetch.endianness);
        const auto offset = fetch.tiled ? texture_util::GetTiledOffset2D(x, y, pitch, 2) : (y * pitch + x) * 4;
        std::memcpy(data + offset, &color, 4);
      }
      for (unsigned c = 0; c < 3; ++c) for (unsigned i = 0; i < 256; ++i)
        gamma[c * 256 + i] = uint16_t((i * 65535u / 255u) >> (frame >= 16 ? c : 0));
      // The same ABI system-frame path used by original dashboard transitions.
      Require(gpu->PresentSystemFrame(reinterpret_cast<const uint32_t*>(&fetch), gamma), "system frame");
      // Then a real guest ring: TYPE0 fetch, XE_SWAP from VdSwap, MEM_WRITE completion.
      std::vector<uint32_t> words{(5u << 16) | XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0};
      const auto* raw = reinterpret_cast<const uint32_t*>(&fetch);
      words.insert(words.end(), raw, raw + 6);
      words.insert(words.end(), {Packet(PM4_WAIT_REG_MEM, 5), 7, 0, 0, 0, 0});
      words.insert(words.end(), {Packet(PM4_XE_SWAP, 4), kSwapSignature, physical_image,
          width, height, Packet(PM4_MEM_WRITE, 2), physical_done, frame + 1});
      for (unsigned i = 0; i < words.size(); ++i)
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(ring + i * 4), words[i]);
      gpu->InitializeRingBuffer(physical_ring, 10);
      mmio->write(nullptr, mmio->callback_context, 0x7fc80714, uint32_t(words.size()));
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      auto* marker = memory->TranslateVirtual<volatile uint32_t*>(done);
      while (*marker != frame + 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      Require(*marker == frame + 1, "PM4 completion deadline");
      rex::ui::RawImage capture;
      Require(gpu->presenter()->CaptureGuestOutput(capture), "GPU capture");
      Require(capture.width == width && capture.height == height, "capture size");
      unsigned max_error = 0;
      for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        unsigned expected[] = {x, y * 2, (x ^ y ^ frame) & 255};
        if (frame & 1) std::swap(expected[0], expected[2]);
        for (unsigned c = 0; c < 3; ++c) {
          // Hardware LUT is 10-bit, exposed to the guest as 16-bit entries.
          const unsigned value = gamma[c * 256 + expected[c]] >> 6;
          const int reference = int((value * 255 + 511) / 1023);
          const int actual = capture.data[y * capture.stride + x * 4 + c];
          max_error = std::max(max_error, unsigned(std::abs(reference - actual)));
          ++tested;
        }
      }
      if (frame == 0 || frame == 7 || frame == 23) {
        WritePNG(output / ("frame-" + std::to_string(frame) + ".png"), capture);
      }
      std::cout << "frame=" << frame << " tiled=" << fetch.tiled << " endian=" << uint32_t(fetch.endianness)
                << " max_channel_error=" << max_error << '\n';
      Require(max_error <= 1, "GPU pixel comparison exceeds one 8-bit level");
    }
    gpu->Shutdown(); shutdown.gpu = nullptr; gpu.reset();
    memory->SystemHeapFree(image); memory->SystemHeapFree(ring); memory->SystemHeapFree(done);
    rt.Shutdown();
    rex::FlushLogging();
    std::cout << "PASS plugin=" << plugin << " frames=48 checked_channels=" << tested
              << " elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()
              << " shutdown=clean (diagnostic, not an FPS benchmark)\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
