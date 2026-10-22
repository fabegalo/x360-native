#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/xenos.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/graphics_system.h>
#include <rex/cvar.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/ui/presenter.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <iostream>
#include <fstream>
#include <cmath>
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
double ProcessCpuMs() {
  FILETIME created, exited, kernel, user;
  Require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user), "process CPU time");
  auto ticks = [](const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
  return double(ticks(kernel) + ticks(user)) / 10000.0;
}
double ThreadCpuMs() {
  FILETIME created, exited, kernel, user;
  Require(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user), "thread CPU time");
  auto ticks = [](const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
  return double(ticks(kernel) + ticks(user)) / 10000.0;
}
// CPU of every thread but this one, which only polls the completion marker.
double WorkerCpuMs() { return ProcessCpuMs() - ThreadCpuMs(); }
// Holds the cvar registry lock from another thread (ListModifiedFlags calls this
// flag's getter while locked) so a draw path that looks a cvar up by name blocks.
struct RegistryGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool armed = false, held = false, released = false;
  std::thread holder;
  std::optional<rex::cvar::FlagRegistrar> flag;
  void Register() {
    rex::cvar::FlagEntry entry;
    entry.name = "x360n_probe_registry_gate"; entry.type = rex::cvar::FlagType::String;
    entry.category = "Probe"; entry.description = "Diagnostic registry lock holder";
    entry.setter = [](std::string_view) { return false; };
    entry.getter = [this] {
      std::unique_lock lock(mutex);
      if (armed) {
        held = true; changed.notify_all();
        changed.wait_for(lock, std::chrono::seconds(6), [&] { return released; });
      }
      return std::string();
    };
    entry.command_callback = [](std::string_view) {};
    flag.emplace(std::move(entry));
  }
  void Hold() {
    { std::lock_guard lock(mutex); armed = true; }
    holder = std::thread([] { rex::cvar::ListModifiedFlags(); });
    std::unique_lock lock(mutex);
    Require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return held; }), "registry gate held");
  }
  void Release() {
    { std::lock_guard lock(mutex); released = true; armed = false; }
    changed.notify_all();
    if (holder.joinable()) holder.join();
  }
};
// Timestamps on the direct queue both D3D12 backends submit to. A mark runs
// once every earlier submission on the queue has, so the span between two
// marks covers the backend's GPU work in between, plus any time the queue
// sat empty waiting for it.
struct QueueTimestamps {
  template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  ID3D12CommandQueue* queue = nullptr;
  Ptr<ID3D12QueryHeap> heap; Ptr<ID3D12Resource> readback; Ptr<ID3D12Fence> fence;
  Ptr<ID3D12CommandAllocator> allocators[2]; Ptr<ID3D12GraphicsCommandList> lists[2];
  uint64_t frequency = 0;
  explicit QueueTimestamps(rex::ui::d3d12::D3D12Provider& provider) : queue(provider.GetDirectQueue()) {
    auto check = [](HRESULT hr) { Require(SUCCEEDED(hr), "queue timestamp setup"); };
    auto* device = provider.GetDevice();
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
    check(device->CreateQueryHeap(&qd, IID_PPV_ARGS(&heap)));
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 16; rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    for (unsigned i = 0; i < 2; ++i) {
      check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
      check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(), nullptr, IID_PPV_ARGS(&lists[i])));
    }
    check(queue->GetTimestampFrequency(&frequency));
  }
  void Mark(unsigned i) {
    auto& list = lists[i];
    list->EndQuery(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i);
    if (i == 1) list->ResolveQueryData(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
    Require(SUCCEEDED(list->Close()), "queue timestamp list");
    ID3D12CommandList* lists_to_run[]{list.Get()};
    queue->ExecuteCommandLists(1, lists_to_run);
    Require(SUCCEEDED(queue->Signal(fence.Get(), i + 1)), "queue timestamp signal");
  }
  double SpanMs() {
    if (fence->GetCompletedValue() < 2) {
      HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      Require(event && SUCCEEDED(fence->SetEventOnCompletion(2, event)), "queue timestamp wait");
      const bool done = WaitForSingleObject(event, 10000) == WAIT_OBJECT_0;
      CloseHandle(event);
      Require(done, "queue timestamp timeout");
    }
    void* mapped; D3D12_RANGE range{0, 16};
    Require(SUCCEEDED(readback->Map(0, &range, &mapped)), "queue timestamp map");
    uint64_t ticks[2]; std::memcpy(ticks, mapped, sizeof(ticks));
    D3D12_RANGE none{}; readback->Unmap(0, &none);
    return double(ticks[1] - ticks[0]) * 1000.0 / double(frequency);
  }
};
enum class ReplayMode { kSingle, kStress, kTiming, kRegistryLocked, kInterleaved };
void ReplayDraw(rex::system::IGraphicsSystem& gpu, rex::memory::Memory& memory,
                const std::filesystem::path& input, const std::filesystem::path& output,
                ReplayMode replay, RegistryGate* gate = nullptr, QueueTimestamps* queue_timestamps = nullptr) {
  const unsigned repetitions = replay == ReplayMode::kStress ? 4096 : 1;
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
  // Ring segments kicked one at a time, each retired by its own MEM_WRITE marker.
  struct Kick { size_t end; uint32_t marker; unsigned draws; };
  std::vector<Kick> kicks;
  auto end_kick = [&](unsigned draws) {
    const uint32_t value = uint32_t(kicks.size() + 1);
    words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), value});
    kicks.push_back({words.size(), value, draws});
  };
  auto add_draws = [&](unsigned count) {
    for (unsigned n = 0; n < count; ++n) words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
  };
  constexpr unsigned kTimingSegments = 4, kTimingDraws = 2048, kLockedDraws = 4096, kInterleavedDraws = 32;
  // Resolves the draw's color target to an independently allocated framebuffer.
  const float xy[]{0, 0, float(width), 0, 0, float(height)};
  std::memcpy(memory.TranslateVirtual(vertices), xy, sizeof(xy));
  auto resolve = [&](uint32_t dest, uint32_t copy_control) {
    xe_gpu_vertex_fetch_t vf{}; vf.type = FetchConstantType::kVertex;
    vf.address = memory.GetPhysicalAddress(vertices) >> 2; vf.size = 6; vf.endian = Endian::kNone;
    set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, vf.dword_0);
    set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1, vf.dword_1);
    reg::RB_MODECONTROL mode{}; mode.edram_mode = EdramMode::kCopy; set(XE_GPU_REG_RB_MODECONTROL, mode.value);
    set(XE_GPU_REG_PA_SC_WINDOW_OFFSET, 0); set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000);
    set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, width | (height << 16));
    set(XE_GPU_REG_RB_COPY_DEST_BASE, memory.GetPhysicalAddress(dest));
    set(XE_GPU_REG_RB_COPY_DEST_PITCH, width | (height << 16));
    set(XE_GPU_REG_RB_COPY_DEST_INFO, uint32_t(TextureFormat::k_2_10_10_10) << 7);
    set(XE_GPU_REG_RB_COPY_CONTROL, copy_control);
    reg::VGT_DRAW_INITIATOR rect = draw; rect.prim_type = PrimitiveType::kRectangleList; rect.num_indices = 3;
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), rect.value});
  };
  if (replay == ReplayMode::kTiming) {
    add_draws(1); end_kick(1);  // Shader translation and pipeline creation stay outside the timed segments.
    for (unsigned s = 0; s < kTimingSegments; ++s) { add_draws(kTimingDraws); end_kick(kTimingDraws); }
  } else if (replay == ReplayMode::kRegistryLocked) {
    add_draws(1); end_kick(1);
    add_draws(kLockedDraws); end_kick(kLockedDraws);
  } else if (replay == ReplayMode::kInterleaved) {
    // ROV draws around a resolve that clears the color target and around a
    // render target base moved over half of the same EDRAM range.
    const auto aux = memory.SystemHeapAlloc(width * 736 * 4, 4096, rex::memory::kSystemHeapPhysical);
    Require(aux != 0, "interleaved resolve allocation");
    add_draws(1); end_kick(1);
    add_draws(kInterleavedDraws);
    set(XE_GPU_REG_RB_COLOR_CLEAR, 0);
    reg::RB_COPY_CONTROL copy{}; copy.copy_command = CopyCommand(1); copy.color_clear_enable = 1;
    resolve(aux, copy.value);
    for (unsigned index : {XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1,
        XE_GPU_REG_RB_MODECONTROL, XE_GPU_REG_PA_SC_WINDOW_OFFSET, XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL,
        XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR}) set(index, regs[index]);
    add_draws(kInterleavedDraws);
    reg::RB_COLOR_INFO moved; moved.value = regs[XE_GPU_REG_RB_COLOR_INFO];
    moved.color_base = (moved.color_base + 360) % 2048;
    set(XE_GPU_REG_RB_COLOR_INFO, moved.value);
    add_draws(kInterleavedDraws);
    set(XE_GPU_REG_RB_COLOR_INFO, regs[XE_GPU_REG_RB_COLOR_INFO]);
    add_draws(kInterleavedDraws);
  } else {
    add_draws(repetitions);
  }
  resolve(image, 0x100000);
  xe_gpu_texture_fetch_t fetch{}; fetch.type = FetchConstantType::kTexture;
  fetch.dimension = DataDimension::k2DOrStacked; fetch.format = TextureFormat::k_2_10_10_10;
  fetch.base_address = memory.GetPhysicalAddress(image) >> 12; fetch.pitch = width / 32;
  fetch.size_2d.width = width - 1; fetch.size_2d.height = height - 1; fetch.swizzle = 0x688; fetch.tiled = true;
  const auto* raw_fetch = reinterpret_cast<const uint32_t*>(&fetch);
  for (unsigned i = 0; i < 6; ++i) set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+i, raw_fetch[i]);
  set(XE_GPU_REG_DC_LUT_RW_MODE, 1); set(XE_GPU_REG_DC_LUT_WRITE_EN_MASK, 7); set(XE_GPU_REG_DC_LUT_RW_INDEX, 0);
  for (unsigned i = 0; i < 128; ++i) for (unsigned c = 0; c < 3; ++c) set(XE_GPU_REG_DC_LUT_PWL_DATA, (i*512) | (512u<<16));
  words.insert(words.end(), {Packet(PM4_XE_SWAP, 4), kSwapSignature, memory.GetPhysicalAddress(image), width, height});
  end_kick(0);
  Require(words.size()*4 <= 262144, "replay ring capacity");
  for (size_t i = 0; i < words.size(); ++i)
    rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring+uint32_t(i*4)), words[i]);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 15);
  auto* mmio = memory.LookupVirtualMappedRange(0x7fc80714);
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  using clock = std::chrono::steady_clock;
  auto wait = [&](uint32_t value, clock::duration limit, bool spin) {
    const auto deadline = clock::now() + limit;
    while (*marker != value && clock::now() < deadline)
      spin ? std::this_thread::yield() : std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return *marker == value;
  };
  clock::time_point timed_start{};
  double timed_worker_cpu = 0;
  for (size_t k = 0; k < kicks.size(); ++k) {
    const Kick& kick = kicks[k];
    const bool locked = replay == ReplayMode::kRegistryLocked && k == 1;
    if (locked) gate->Hold();
    const double cpu_before = WorkerCpuMs();
    const auto start = clock::now();
    if (k == 1) {
      timed_start = start; timed_worker_cpu = cpu_before;
      if (queue_timestamps) queue_timestamps->Mark(0);
    }
    mmio->write(nullptr, mmio->callback_context, 0x7fc80714, uint32_t(kick.end));
    bool retired = wait(kick.marker, locked ? std::chrono::seconds(3) : std::chrono::seconds(15),
                        replay == ReplayMode::kTiming);
    const double wall_ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    const double cpu_ms = WorkerCpuMs() - cpu_before;
    if (locked) {
      gate->Release();
      std::cout << "registry_locked draws=" << kick.draws << " retired_while_locked=" << retired
                << " wall_ms=" << wall_ms << '\n';
      Require(retired, "non-memexport draws waited for the cvar registry lock");
    }
    if (replay == ReplayMode::kTiming && k > 0 && kick.draws)
      std::cout << "segment=" << k << " draws=" << kick.draws << " wall_ms=" << wall_ms
                << " ns_per_draw=" << wall_ms * 1e6 / kick.draws << " worker_cpu_ms=" << cpu_ms << '\n';
    Require(retired, "isolated draw completion");
  }
  // The last kick ends after XE_SWAP, by which both backends submitted the
  // timed draws, the resolve and the swap; the capture's own work comes later.
  if (queue_timestamps) queue_timestamps->Mark(1);
  rex::ui::RawImage capture; Require(gpu.presenter()->CaptureGuestOutput(capture), "isolated draw capture");
  if (replay == ReplayMode::kTiming) {
    // The capture needs the resolve, hence every timed draw, done on the GPU:
    // unlike the segment marker, this interval includes work still in flight.
    std::cout << "timed_to_capture_ms=" << std::chrono::duration<double, std::milli>(clock::now() - timed_start).count()
              << " timed_to_capture_worker_cpu_ms=" << WorkerCpuMs() - timed_worker_cpu << '\n';
    if (queue_timestamps) std::cout << "queue_gpu_span_ms=" << queue_timestamps->SpanMs() << '\n';
    // Cost of one registry query by name, the lookup a readback setting check makes.
    constexpr unsigned kQueries = 1000000;
    unsigned modified = 0;
    const auto start = clock::now();
    for (unsigned i = 0; i < kQueries; ++i) modified += rex::cvar::HasNonDefaultValue("vulkan_readback_memexport");
    std::cout << "registry_query_ns=" << std::chrono::duration<double, std::nano>(clock::now() - start).count() / kQueries
              << " modified=" << modified << '\n';
  }
  WritePNG(output / "draw.png", capture);
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 3 && argc != 4 && argc != 5) { std::cerr << "usage: x360n_native_probe native|xenos OUTPUT_DIRECTORY [--reject|--abi|--replay INPUT_DIRECTORY|--draw-replay|--draw-stress|--draw-timing|--draw-registry-locked|--draw-interleaved INPUT_DIRECTORY]\n"; return 2; }
  try {
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM setup");
    const std::filesystem::path output(argv[2]);
    auto plugin = std::filesystem::path(argv[1]).string();
    std::string backend = "d3d12";
    if (const auto colon = plugin.find(':'); colon != std::string::npos) {
      backend = plugin.substr(colon + 1); plugin.resize(colon);
    }
    std::filesystem::create_directories(output);
    rex::InitLogging((output / "runtime.log").string().c_str());
    rex::Runtime rt(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    Require(rt.Setup(std::move(config)) == X_STATUS_SUCCESS, "runtime setup");
    auto gpu = rex::system::LoadGpuPlugin(plugin, backend);
    Require(bool(gpu), "load plugin ABI");
    const auto implementation = static_cast<rex::graphics::GraphicsSystem*>(gpu.get())->name();
    if (backend == "vulkan") Require(implementation == "Vulkan", "requested actual Vulkan graphics system");
    if (backend == "d3d12" && plugin == "xenos") Require(implementation == "Direct3D 12", "requested actual D3D12 graphics system");
    std::cout << "API selected: " << backend << "; implementation: " << implementation << '\n';
    const std::wstring_view replay_flag = argc == 5 ? std::wstring_view(argv[3]) : std::wstring_view();
    const bool stress = replay_flag == L"--draw-stress";
    const ReplayMode replay_mode = stress ? ReplayMode::kStress
        : replay_flag == L"--draw-timing" ? ReplayMode::kTiming
        : replay_flag == L"--draw-registry-locked" ? ReplayMode::kRegistryLocked
        : replay_flag == L"--draw-interleaved" ? ReplayMode::kInterleaved : ReplayMode::kSingle;
    const bool draw_replay = replay_flag == L"--draw-replay" || replay_mode != ReplayMode::kSingle;
    RegistryGate registry_gate;
    if (replay_mode == ReplayMode::kRegistryLocked) registry_gate.Register();
    if (plugin == "native" && draw_replay) rex::cvar::SetFlagByName("native_stats", "true");
    if (stress) rex::cvar::SetFlagByName("d3d12_debug", "true");
    if (draw_replay) {
      std::filesystem::create_directories(output / "shaders");
      rex::cvar::SetFlagByName("dump_shaders", (output / "shaders").string());
      rex::cvar::SetFlagByName("d3d12_dxbc_disasm", "true");
    }
    if (draw_replay && plugin == "xenos") {
      // ROV matches Native; X360N_PROBE_XENOS_RT_PATH=rtv measures host render targets.
      char rt_path[8]{};
      const DWORD rt_length = GetEnvironmentVariableA("X360N_PROBE_XENOS_RT_PATH", rt_path, sizeof(rt_path));
      rex::cvar::SetFlagByName("render_target_path_d3d12", rt_length && rt_length < sizeof(rt_path) ? rt_path : "rov");
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
      std::optional<QueueTimestamps> queue_timestamps;
      if (replay_mode == ReplayMode::kTiming && backend == "d3d12")
        queue_timestamps.emplace(*static_cast<rex::ui::d3d12::D3D12Provider*>(
            static_cast<rex::graphics::GraphicsSystem*>(gpu.get())->provider()));
      ReplayDraw(*gpu, *memory, argv[4], output, replay_mode, &registry_gate,
                 queue_timestamps ? &*queue_timestamps : nullptr);
      queue_timestamps.reset();  // Its D3D12 objects go before the provider's device.
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
    const bool overlay_test = argc == 4 && std::wstring_view(argv[3]) == L"--overlay";
    const auto overlay_image = overlay_test ? memory->SystemHeapAlloc(64 * 64 * 4, 4096, rex::memory::kSystemHeapPhysical) : 0;
    if (overlay_test) Require(overlay_image != 0, "overlay allocation");
    if (argc == 4 && !overlay_test) {
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
      if (overlay_test) width = (frame & 1) ? 192 : 256; // Resize every frame.
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
      if (overlay_test) {
        const uint32_t colors[]{0x00FFFFFF, 0xFFFF0000, 0xFF0000FF, 0x8000FF00};
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
          const auto offset = texture_util::GetTiledOffset2D(x, y, 64, 2);
          std::memcpy(memory->TranslateVirtual<uint8_t*>(overlay_image) + offset, &colors[x / 16], 4);
        }
        auto set = [&](unsigned index, uint32_t value) { words.insert(words.end(), {index, value}); };
        set(XE_GPU_REG_D1OVL_ENABLE, frame % 4 != 3);
        set(XE_GPU_REG_D1OVL_CONTROL1, 0);
        set(XE_GPU_REG_D1OVL_SURFACE_ADDRESS, memory->GetPhysicalAddress(overlay_image));
        set(XE_GPU_REG_D1OVL_PITCH, 64);
        // Probe uses display coordinates from VdQueryVideoMode (1280x720).
        set(XE_GPU_REG_D1OVL_START, (32 << 16) | 16);
        set(XE_GPU_REG_D1OVL_END, (96 << 16) | 80);
        set(XE_GPU_REG_D1OVL_SURFACE_OFFSET_X, 32);
        set(XE_GPU_REG_D1OVL_SURFACE_OFFSET_Y, 16);
      }
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
          if (overlay_test && frame % 4 != 3) {
            const double sx = (x + 0.5) * 1280.0 / width, sy = (y + 0.5) * 720.0 / height;
            if (sx >= 32 && sx < 96 && sy >= 16 && sy < 80) {
              const unsigned stripe = unsigned(sx - 32) / 16;
              const unsigned colors[4][3]{{255,255,255},{255,0,0},{0,0,255},{0,255,0}};
              const double alpha = stripe == 0 ? 0 : stripe == 3 ? 128.0/255.0 : 1;
              const unsigned limit = pwl ? 1023 : 255;
              expected[c] = unsigned(std::lround(expected[c] * (1-alpha) + colors[stripe][c] * (double(limit)/255.0) * alpha));
            }
          }
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
    if (overlay_image) memory->SystemHeapFree(overlay_image);
    rt.Shutdown();
    rex::FlushLogging();
    std::cout << "PASS plugin=" << plugin << " frames=" << (replay ? 1 : 48) << " checked_channels=" << tested
              << " elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()
              << " shutdown=clean (diagnostic, not an FPS benchmark)\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
