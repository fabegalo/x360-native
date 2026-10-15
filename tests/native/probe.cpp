#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/xenos.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/flags.h>
#include <rex/cvar.h>
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
void ReplayDraw(rex::system::IGraphicsSystem& gpu, rex::memory::Memory& memory,
                const std::filesystem::path& input, const std::filesystem::path& output) {
  auto read = [&](const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    Require(bool(in), "snapshot file");
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
  };
  auto regs_bytes = read(input / "registers.bin");
  uint32_t regs[RegisterFile::kRegisterCount];
  Require(regs_bytes.size() == sizeof(regs), "snapshot registers");
  std::memcpy(regs, regs_bytes.data(), regs_bytes.size());
  unsigned primitive = 0, count = 0, indexed = 0;
  std::ifstream draw_info(input / "draw.txt");
  Require(bool(draw_info >> primitive >> count >> indexed), "snapshot draw metadata");
  Require(!indexed, "isolated probe currently requires non-indexed draw");
  std::vector<std::pair<uint32_t, std::vector<char>>> ranges;
  std::vector<bool> pages(0x20000000 / 65536);
  for (const auto& file : std::filesystem::directory_iterator(input)) {
    const auto name = file.path().filename().string();
    if (!name.starts_with("memory-")) continue;
    uint32_t address = uint32_t(std::stoul(name.substr(7)));
    auto bytes = read(file.path());
    Require(uint64_t(address) + bytes.size() <= 0x20000000, "snapshot memory bounds");
    for (size_t p = address >> 16; p < (address + bytes.size() + 65535) >> 16; ++p) pages[p] = true;
    ranges.emplace_back(address, std::move(bytes));
  }
  for (unsigned p = 0; p < pages.size();) {
    if (!pages[p]) { ++p; continue; }
    unsigned end = p + 1; while (end < pages.size() && pages[end]) ++end;
    const uint32_t address = 0xA0000000u + (p << 16);
    Require(memory.LookupHeap(address)->AllocFixed(address, (end - p) << 16, 65536,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite), "snapshot allocation");
    p = end;
  }
  for (const auto& [address, bytes] : ranges)
    std::memcpy(memory.TranslatePhysical(address), bytes.data(), bytes.size());
  constexpr unsigned width = 1280, height = 720;
  const auto image = memory.SystemHeapAlloc(width * 736 * 4, 4096, rex::memory::kSystemHeapPhysical);
  const auto ring = memory.SystemHeapAlloc(262144, 4096, rex::memory::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(image && ring && done && vertices, "replay allocations");
  std::vector<uint32_t> words;
  auto set = [&](unsigned index, uint32_t value) { words.insert(words.end(), {index, value}); };
  // Rendering state and constants only; avoid MMIO side effects from other registers.
  for (unsigned index = 0x2000; index < 0x5000; ++index) set(index, regs[index]);
  for (unsigned stage = 0; stage < 2; ++stage) {
    const auto bytes = read(input / (stage ? "ps.bin" : "vs.bin"));
    Require(bytes.size() % 4 == 0, "snapshot shader alignment");
    words.insert(words.end(), {Packet(PM4_IM_LOAD_IMMEDIATE, unsigned(bytes.size()/4 + 2)), stage, uint32_t(bytes.size()/4)});
    for (size_t i = 0; i < bytes.size(); i += 4) { uint32_t w; std::memcpy(&w, bytes.data()+i, 4); words.push_back(w); }
  }
  reg::VGT_DRAW_INITIATOR draw{};
  draw.prim_type = PrimitiveType(primitive); draw.source_select = SourceSelect::kAutoIndex; draw.num_indices = count;
  words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
  // Resolve the draw's color target to an independently allocated framebuffer.
  const float xy[]{0, 0, float(width), 0, 0, float(height)};
  std::memcpy(memory.TranslateVirtual(vertices), xy, sizeof(xy));
  xe_gpu_vertex_fetch_t vf{}; vf.type = FetchConstantType::kVertex;
  vf.address = memory.GetPhysicalAddress(vertices) >> 2; vf.size = 6; vf.endian = Endian::kNone;
  set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, vf.dword_0);
  set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1, vf.dword_1);
  reg::RB_MODECONTROL mode{}; mode.edram_mode = EdramMode::kCopy; set(XE_GPU_REG_RB_MODECONTROL, mode.value);
  set(XE_GPU_REG_PA_SC_WINDOW_OFFSET, 0); set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000);
  set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, width | (height << 16));
  set(XE_GPU_REG_RB_COPY_DEST_BASE, memory.GetPhysicalAddress(image));
  set(XE_GPU_REG_RB_COPY_DEST_PITCH, width | (height << 16));
  set(XE_GPU_REG_RB_COPY_DEST_INFO, uint32_t(TextureFormat::k_2_10_10_10) << 7);
  set(XE_GPU_REG_RB_COPY_CONTROL, 0x100000);
  draw.prim_type = PrimitiveType::kRectangleList; draw.num_indices = 3;
  words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
  xe_gpu_texture_fetch_t fetch{}; fetch.type = FetchConstantType::kTexture;
  fetch.dimension = DataDimension::k2DOrStacked; fetch.format = TextureFormat::k_2_10_10_10;
  fetch.base_address = memory.GetPhysicalAddress(image) >> 12; fetch.pitch = width / 32;
  fetch.size_2d.width = width - 1; fetch.size_2d.height = height - 1; fetch.swizzle = 0x688; fetch.tiled = true;
  const auto* raw_fetch = reinterpret_cast<const uint32_t*>(&fetch);
  for (unsigned i = 0; i < 6; ++i) set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+i, raw_fetch[i]);
  set(XE_GPU_REG_DC_LUT_RW_MODE, 1); set(XE_GPU_REG_DC_LUT_WRITE_EN_MASK, 7); set(XE_GPU_REG_DC_LUT_RW_INDEX, 0);
  for (unsigned i = 0; i < 128; ++i) for (unsigned c = 0; c < 3; ++c) set(XE_GPU_REG_DC_LUT_PWL_DATA, (i*512) | (512u<<16));
  words.insert(words.end(), {Packet(PM4_XE_SWAP, 4), kSwapSignature, memory.GetPhysicalAddress(image), width, height,
      Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), 1});
  Require(words.size()*4 <= 262144, "replay ring capacity");
  for (size_t i = 0; i < words.size(); ++i)
    rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring+uint32_t(i*4)), words[i]);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 15);
  auto* mmio = memory.LookupVirtualMappedRange(0x7fc80714);
  mmio->write(nullptr, mmio->callback_context, 0x7fc80714, uint32_t(words.size()));
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(15);
  while (*marker != 1 && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  Require(*marker == 1, "isolated draw completion");
  rex::ui::RawImage capture; Require(gpu.presenter()->CaptureGuestOutput(capture), "isolated draw capture");
  WritePNG(output / "draw.png", capture);
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 3 && argc != 4 && argc != 5) { std::cerr << "usage: x360n_native_probe native|xenos OUTPUT_DIRECTORY [--reject|--abi|--replay INPUT_DIRECTORY|--draw-replay INPUT_DIRECTORY]\n"; return 2; }
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
    const bool draw_replay = argc == 5 && std::wstring_view(argv[3]) == L"--draw-replay";
    if (draw_replay) {
      std::filesystem::create_directories(output / "shaders");
      rex::cvar::SetFlagByName("dump_shaders", (output / "shaders").string());
      rex::cvar::SetFlagByName("d3d12_dxbc_disasm", "true");
    }
    if (draw_replay && plugin == "xenos") {
      rex::cvar::SetFlagByName("render_target_path_d3d12", "rov");
      rex::cvar::SetFlagByName("d3d12_bindless", "true");
      rex::cvar::SetFlagByName("async_shader_compilation", "false");
      rex::cvar::SetFlagByName("d3d12_pipeline_creation_threads", "0");
    }
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
    if (draw_replay) {
      if (plugin == "native") gpu->InitializeShaderStorage(output / "pipeline-cache", 0, true);
      ReplayDraw(*gpu, *memory, argv[4], output);
      gpu->Shutdown(); shutdown.gpu = nullptr; gpu.reset(); rt.Shutdown(); rex::FlushLogging();
      std::cout << "PASS isolated draw capture\n"; return 0;
    }
    const bool replay = argc == 5 && std::wstring_view(argv[3]) == L"--replay";
    unsigned width = 256, height = 128, pitch = 320, replay_tiled = 0, replay_endian = 0, replay_swizzle = 0, replay_rgb10 = 0;
    std::vector<uint8_t> replay_pixels;
    uint32_t replay_lut[384]{};
    if (replay) {
      const std::filesystem::path input(argv[4]);
      std::ifstream metadata(input / "scanout.txt");
      metadata >> width >> height >> pitch >> replay_tiled >> replay_endian >> replay_swizzle >> replay_rgb10;
      Require(bool(metadata) && replay_rgb10 && width && height && pitch >= width, "replay metadata");
      std::ifstream pixels(input / "pixels.bin", std::ios::binary);
      replay_pixels.assign(std::istreambuf_iterator<char>(pixels), {});
      std::ifstream lut(input / "lut.bin", std::ios::binary);
      lut.read(reinterpret_cast<char*>(replay_lut), sizeof(replay_lut));
      Require(bool(lut), "replay gamma ramp");
    }
    const unsigned image_bytes = pitch * ((height + 31) & ~31u) * 4;
    const auto image = memory->SystemHeapAlloc(image_bytes, 4096, rex::memory::kSystemHeapPhysical);
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
      std::vector<uint32_t> words;
      const std::wstring_view mode(argv[3]);
      if (mode == L"--reject") words = {Packet(0x7e, 1), 0};
      else if (mode == L"--reject-inline") words = {Packet(PM4_IM_LOAD_IMMEDIATE, 3), 0, 9, 0};
      else if (mode == L"--reject-draw") {
        reg::VGT_DRAW_INITIATOR draw{}; draw.prim_type = PrimitiveType::kTriangleList;
        draw.source_select = SourceSelect::kImmediate; draw.num_indices = 3;
        words = {Packet(PM4_DRAW_INDX_2, 1), draw.value};
      } else throw std::runtime_error("unknown probe mode");
      words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), physical_done, 1});
      for (unsigned i = 0; i < words.size(); ++i)
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(ring + i * 4), words[i]);
      gpu->InitializeRingBuffer(physical_ring, 10);
      mmio->write(nullptr, mmio->callback_context, 0x7fc80714, unsigned(words.size()));
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
    for (unsigned frame = replay ? 24 : 0; frame < (replay ? 25u : 48u); ++frame) {
      const bool pwl = frame >= 24;
      xe_gpu_texture_fetch_t fetch{};
      fetch.type = FetchConstantType::kTexture; fetch.dimension = DataDimension::k2DOrStacked;
      fetch.format = TextureFormat::k_8_8_8_8; fetch.pitch = pitch / 32;
      if (pwl) fetch.format = frame & 2 ? TextureFormat::k_2_10_10_10 : TextureFormat::k_2_10_10_10_AS_16_16_16_16;
      fetch.base_address = physical_image >> 12;
      fetch.size_2d.width = width - 1; fetch.size_2d.height = height - 1;
      fetch.swizzle = frame & 1 ? 0x60a : 0x688; // BGRA / RGBA
      fetch.tiled = (frame / 4) & 1;
      fetch.endianness = Endian(frame & 3);
      auto* data = memory->TranslateVirtual<uint8_t*>(image);
      std::memset(data, 0, image_bytes);
      for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        uint32_t color = x | ((y * 2) << 8) | (((x ^ y ^ frame) & 255) << 16) | 0xff000000;
        if (pwl) color = (x * 4 + (y & 3)) | ((y * 8 + (x & 7)) << 10) |
            (((x ^ y ^ frame) * 4 + (y & 3)) << 20) | 0xc0000000;
        color = GpuSwap(color, fetch.endianness);
        const auto offset = fetch.tiled ? texture_util::GetTiledOffset2D(x, y, pitch, 2) : (y * pitch + x) * 4;
        std::memcpy(data + offset, &color, 4);
      }
      if (replay) {
        Require(replay_pixels.size() == image_bytes, "replay extent");
        std::memcpy(data, replay_pixels.data(), image_bytes);
        fetch.tiled = replay_tiled; fetch.endianness = Endian(replay_endian); fetch.swizzle = replay_swizzle;
      }
      for (unsigned c = 0; c < 3; ++c) for (unsigned i = 0; i < 256; ++i)
        gamma[c * 256 + i] = uint16_t((i * 65535u / 255u) >> (frame >= 16 ? c : 0));
      // The same ABI system-frame path used by original dashboard transitions.
      if (!pwl) Require(gpu->PresentSystemFrame(reinterpret_cast<const uint32_t*>(&fetch), gamma), "system frame");
      // Then a real guest ring: TYPE0 fetch, XE_SWAP from VdSwap, MEM_WRITE completion.
      std::vector<uint32_t> words{(5u << 16) | XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0};
      const auto* raw = reinterpret_cast<const uint32_t*>(&fetch);
      words.insert(words.end(), raw, raw + 6);
      if (pwl) {
        // Exercise actual guest PWL programming, with independent RGB slopes.
        words.insert(words.end(), {XE_GPU_REG_DC_LUT_RW_MODE, 1,
            XE_GPU_REG_DC_LUT_WRITE_EN_MASK, 7, XE_GPU_REG_DC_LUT_RW_INDEX, 0});
        for (unsigned i = 0; i < 128; ++i) for (unsigned c = 0; c < 3; ++c) {
          const unsigned shift = frame >= 40 ? c : 0;
          const unsigned base = ((i * 512) >> shift) & ~63u;
          const unsigned delta = 512 >> shift;
          words.insert(words.end(), {XE_GPU_REG_DC_LUT_PWL_DATA, replay ? replay_lut[i * 3 + c] : base | (delta << 16)});
        }
      }
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
        if (pwl) {
          expected[0] = x * 4 + (y & 3);
          expected[1] = y * 8 + (x & 7);
          expected[2] = (x ^ y ^ frame) * 4 + (y & 3);
        }
        if (frame & 1) std::swap(expected[0], expected[2]);
        if (replay) {
          uint32_t packed;
          const auto offset = fetch.tiled ? texture_util::GetTiledOffset2D(x, y, pitch, 2) : (y * pitch + x) * 4;
          std::memcpy(&packed, replay_pixels.data() + offset, 4);
          packed = GpuSwap(packed, fetch.endianness);
          const unsigned channels[]{packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023, (packed >> 30) * 341, 0, 1023};
          for (unsigned c = 0; c < 3; ++c) expected[c] = channels[(fetch.swizzle >> (c * 3)) & 7];
        }
        for (unsigned c = 0; c < 3; ++c) {
          // Hardware LUT is 10-bit, exposed to the guest as 16-bit entries.
          const unsigned value = pwl ? 0 : gamma[c * 256 + expected[c]] >> 6;
          int reference = int((value * 255 + 511) / 1023);
          if (pwl) {
            const unsigned shift = frame >= 40 ? c : 0;
            const unsigned base = (((expected[c] >> 3) * 512) >> shift) & ~63u;
            const double linear = base + double(512 >> shift) * (expected[c] & 7) / 8;
            reference = int(std::min(1.0, linear / 65472.0) * 255 + 0.5);
            if (replay) {
              const auto entry = replay_lut[(expected[c] >> 3) * 3 + c];
              const double value = (entry & 65535) + double(entry >> 16) * (expected[c] & 7) / 8;
              reference = int(std::clamp(value / 65472.0, 0.0, 1.0) * 255 + 0.5);
            }
          }
          const int actual = capture.data[y * capture.stride + x * 4 + c];
          max_error = std::max(max_error, unsigned(std::abs(reference - actual)));
          ++tested;
        }
      }
      if (replay || frame == 0 || frame == 7 || frame == 23 || frame == 31 || frame == 47) {
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
    std::cout << "PASS plugin=" << plugin << " frames=" << (replay ? 1 : 48) << " checked_channels=" << tested
              << " elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()
              << " shutdown=clean (diagnostic, not an FPS benchmark)\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
