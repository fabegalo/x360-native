// Synthetic multi-frame workload of the real Native plugin, for frame capture
// and replay contracts on any host: per frame the CPU rewrites the vertex
// buffer, a rasterized draw writes EDRAM (never cleared: half of every later
// frame comes only from EDRAM persisted from frames 0-1), a ZPD query reads sample counters, a
// resolve copies EDRAM to guest memory, a visibility-tested draw follows while
// that result is unpublished, and XE_SWAP presents the resolve offscreen.
//   x360n_native_frame_probe OUTPUT d3d12|vulkan FRAMES [--draws N --resolves K]
//       [--no-sample-counts] [cvar=value...]
// --no-sample-counts leaves out the ZPD query and the visibility-tested draw
// (no ROV sample counting), for hosts that cannot run it.
// Prints each frame's scanout hash; no title data is involved.
#include "probe_main.h"
#include "../../src/native/capture/capture_format.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/format/ucode.h>
#include <rex/cvar.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using rex::X_STATUS;
namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
uint32_t Packet(uint32_t opcode, uint32_t count) { return 0xC0000000 | ((count - 1) << 16) | (opcode << 8); }
// VS: vfetch r0 from fetch constant 0, oPos = r0. PS: oC0 = c0.
std::vector<uint32_t> RasterShader(bool vertex) {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(vertex ? AllocType::kVsPosition : AllocType::kPsColors) << 41) |
      (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(vertex ? 2 : 1) << 12) | (uint64_t(vertex) << 16) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16)};
  if (vertex) code.insert(code.end(), {(1u << 12) | (1u << 19),
      0x688u | (1u << 13) | (uint32_t(VertexFormat::k_32_32_32_32_FLOAT) << 16), 4});
  code.insert(code.end(), {(vertex ? 62u : 0u) | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (vertex ? (1u << 16) | (1u << 8) | (3u << 30) : 0)});
  return code;
}
constexpr uint32_t kSide = 32;
// Optional game-like load per frame: extra_draws rasterized draws in
// extra_resolves groups, each group followed by a resolve into its own guest
// buffer (results the next guest sync publishes), before the ZPD/scanout part.
void Run(rex::system::IGraphicsSystem& gpu, rex::memory::Memory& memory, uint32_t frames,
         uint32_t extra_draws, uint32_t extra_resolves, bool sample_counts) {
  const auto ring = memory.SystemHeapAlloc(2u << 20, 4096, rex::memory::kSystemHeapPhysical);
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto rect = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto image = memory.SystemHeapAlloc(kSide * kSide * 4, 4096, rex::memory::kSystemHeapPhysical);
  const auto reports = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto targets = memory.SystemHeapAlloc(std::max(1u, extra_resolves) * kSide * kSide * 4, 4096, rex::memory::kSystemHeapPhysical);
  Require(ring && vertices && rect && image && reports && done && targets, "frame probe allocations");
  std::memset(memory.TranslateVirtual(done), 0, 4);
  const float xy[]{0, 0, float(kSide), 0, 0, float(kSide)};
  std::memcpy(memory.TranslateVirtual(rect), xy, sizeof(xy));
  auto* mmio = memory.LookupVirtualMappedRange(0x7FC80714);
  Require(mmio != nullptr, "probe GPU doorbell");
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 19);
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  uint32_t write = 0;
  for (uint32_t frame = 0; frame < frames; ++frame) {
    // CPU-written vertex data that changes every frame.
    // Frames 0-1 draw only the upper-left half, later frames only the
    // lower-right half (varying with the frame): the upper-left half of every
    // later scanout exists only in EDRAM persisted from earlier frames.
    const float shift = float(frame % 4) * 0.125f;
    const float triangle_early[]{-1, -1, .5f, 1, 1, -1, .5f, 1, -1, 1, .5f, 1};
    const float triangle_late[]{1, 1 - shift, .5f, 1, -1 + shift, 1, .5f, 1, 1, -1, .5f, 1};
    const float* triangle = frame < 2 ? triangle_early : triangle_late;
    std::memcpy(memory.TranslateVirtual(vertices), triangle, sizeof(triangle_early));
    std::vector<uint32_t> words;
    auto set = [&](uint32_t index, uint32_t value) { words.insert(words.end(), {index, value}); };
    auto fp = [&](uint32_t index, float value) { set(index, std::bit_cast<uint32_t>(value)); };
    auto load = [&](bool vertex) {
      const auto shader = RasterShader(vertex);
      words.insert(words.end(), {Packet(PM4_IM_LOAD_IMMEDIATE, uint32_t(shader.size() + 2)), uint32_t(!vertex), uint32_t(shader.size())});
      words.insert(words.end(), shader.begin(), shader.end());
    };
    auto vertex_fetch = [&](uint32_t address, uint32_t dwords) {
      xe_gpu_vertex_fetch_t fetch{}; fetch.type = FetchConstantType::kVertex; fetch.address = address >> 2; fetch.size = dwords;
      set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, fetch.dword_0); set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1, fetch.dword_1);
    };
    reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 1; set(XE_GPU_REG_SQ_PROGRAM_CNTL, program.value);
    set(XE_GPU_REG_VGT_MAX_VTX_INDX, 0xFFFFFF);
    set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kColorDepth));
    set(XE_GPU_REG_RB_SURFACE_INFO, 80); set(XE_GPU_REG_RB_COLOR_MASK, 15); set(XE_GPU_REG_RB_COLOR_INFO, 0);
    set(XE_GPU_REG_RB_DEPTHCONTROL, 0); set(XE_GPU_REG_RB_DEPTH_INFO, 0);
    reg::RB_BLENDCONTROL blend{};
    // Accumulate: the scanout depends on every earlier frame's EDRAM contents.
    blend.color_srcblend = blend.alpha_srcblend = BlendFactor::kOne;
    blend.color_destblend = blend.alpha_destblend = BlendFactor::kZero;
    set(XE_GPU_REG_RB_BLENDCONTROL0, blend.value);
    set(XE_GPU_REG_PA_CL_VTE_CNTL, 0x3F);
    fp(XE_GPU_REG_PA_CL_VPORT_XSCALE, kSide / 2.0f); fp(XE_GPU_REG_PA_CL_VPORT_XOFFSET, kSide / 2.0f);
    fp(XE_GPU_REG_PA_CL_VPORT_YSCALE, -float(kSide) / 2); fp(XE_GPU_REG_PA_CL_VPORT_YOFFSET, kSide / 2.0f);
    fp(XE_GPU_REG_PA_CL_VPORT_ZSCALE, 1); fp(XE_GPU_REG_PA_CL_VPORT_ZOFFSET, 0);
    set(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, 0); set(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, kSide | (kSide << 16));
    set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000); set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, kSide | (kSide << 16));
    set(XE_GPU_REG_PA_SU_VTX_CNTL, 1);
    const float color[]{float(frame % 7 + 1) / 8, float(frame % 3) / 4, frame < 2 ? 1.0f : 0.25f, 1};
    for (unsigned c = 0; c < 4; ++c) fp(XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4 + c, color[c]);
    vertex_fetch(memory.GetPhysicalAddress(vertices), 12);
    load(true); load(false);
    reg::VGT_DRAW_INITIATOR draw{}; draw.prim_type = PrimitiveType::kTriangleList;
    draw.source_select = SourceSelect::kAutoIndex; draw.num_indices = 3;
    auto resolve_to = [&](uint32_t dest) {
      vertex_fetch(memory.GetPhysicalAddress(rect), 6);
      set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kCopy));
      set(XE_GPU_REG_RB_COPY_DEST_BASE, dest);
      set(XE_GPU_REG_RB_COPY_DEST_PITCH, kSide | (kSide << 16));
      set(XE_GPU_REG_RB_COPY_DEST_INFO, uint32_t(TextureFormat::k_8_8_8_8) << 7);
      reg::RB_COPY_CONTROL copy{}; copy.copy_command = CopyCommand(1);
      set(XE_GPU_REG_RB_COPY_CONTROL, copy.value);
      reg::VGT_DRAW_INITIATOR rect_draw = draw; rect_draw.prim_type = PrimitiveType::kRectangleList;
      words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), rect_draw.value});
      set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kColorDepth));
      vertex_fetch(memory.GetPhysicalAddress(vertices), 12);
    };
    for (uint32_t group = 0; group < extra_resolves; ++group) {
      for (uint32_t i = group * extra_draws / extra_resolves; i < (group + 1) * extra_draws / extra_resolves; ++i) {
        fp(XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4, float((frame + i) % 8) / 8);
        words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
      }
      resolve_to(memory.GetPhysicalAddress(targets) + group * kSide * kSide * 4);
    }
    for (unsigned c = 0; c < 4; ++c) fp(XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4 + c, color[c]);
    // ZPD around the draw: BEGIN, END with the big-endian pending token.
    const uint32_t report = memory.GetPhysicalAddress(reports) + (frame % 32) * 64;
    auto* end = memory.TranslatePhysical<uint32_t*>(report + 32);
    std::memset(end, 0, 32); end[4] = 0xEDFEFFFF;
    if (sample_counts) {
      set(XE_GPU_REG_RB_SAMPLE_COUNT_ADDR, report);
      words.insert(words.end(), {Packet(PM4_EVENT_WRITE_ZPD, 1), ZPASS_DONE});
    }
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
    if (sample_counts) {
      set(XE_GPU_REG_RB_SAMPLE_COUNT_ADDR, report + 32);
      words.insert(words.end(), {Packet(PM4_EVENT_WRITE_ZPD, 1), ZPASS_DONE});
    }
    // Resolve EDRAM (no clear) to the guest scanout image.
    vertex_fetch(memory.GetPhysicalAddress(rect), 6);
    set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kCopy));
    set(XE_GPU_REG_RB_COPY_DEST_BASE, memory.GetPhysicalAddress(image));
    set(XE_GPU_REG_RB_COPY_DEST_PITCH, kSide | (kSide << 16));
    set(XE_GPU_REG_RB_COPY_DEST_INFO, uint32_t(TextureFormat::k_8_8_8_8) << 7);
    reg::RB_COPY_CONTROL copy{}; copy.copy_command = CopyCommand(1);
    set(XE_GPU_REG_RB_COPY_CONTROL, copy.value);
    reg::VGT_DRAW_INITIATOR rect_draw = draw; rect_draw.prim_type = PrimitiveType::kRectangleList;
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), rect_draw.value});
    // A visibility-tested draw after the resolve, while its result is still
    // unpublished: the renderer reads sample counters before and after it.
    set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kColorDepth));
    vertex_fetch(memory.GetPhysicalAddress(vertices), 12);
    if (sample_counts) {
      words.insert(words.end(), {Packet(PM4_VIZ_QUERY, 1), 5});
      reg::PA_SC_VIZ_QUERY viz{}; viz.viz_query_ena = 1; viz.viz_query_id = 5;
      set(XE_GPU_REG_PA_SC_VIZ_QUERY, viz.value);
      words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
      set(XE_GPU_REG_PA_SC_VIZ_QUERY, 0);
      words.insert(words.end(), {Packet(PM4_VIZ_QUERY, 1), 0x100u | 5});
    }
    xe_gpu_texture_fetch_t fetch{}; fetch.type = FetchConstantType::kTexture;
    fetch.dimension = DataDimension::k2DOrStacked; fetch.format = TextureFormat::k_8_8_8_8;
    fetch.base_address = memory.GetPhysicalAddress(image) >> 12; fetch.pitch = kSide / 32;
    fetch.size_2d.width = kSide - 1; fetch.size_2d.height = kSide - 1; fetch.swizzle = 0x688; fetch.tiled = true;
    for (unsigned i = 0; i < 6; ++i) set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + i, reinterpret_cast<const uint32_t*>(&fetch)[i]);
    words.insert(words.end(), {Packet(PM4_XE_SWAP, 4), kSwapSignature, memory.GetPhysicalAddress(image), kSide, kSide,
        Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), frame + 1});
    if ((write + words.size()) * 4 >= (2u << 20)) {
      // Restart the ring after the previous frame completed (it is idle).
      gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 19);
      write = 0;
    }
    Require(words.size() * 4 < (2u << 20), "frame probe ring capacity");
    for (size_t i = 0; i < words.size(); ++i)
      rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring + uint32_t((write + i) * 4)), words[i]);
    write += uint32_t(words.size());
    mmio->write(nullptr, mmio->callback_context, 0x7FC80714, write);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (*marker != frame + 1 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    Require(*marker == frame + 1, "frame probe completion timeout");
    const auto hash = x360n::native::capture::HashBytes(memory.TranslateVirtual(image), kSide * kSide * 4);
    std::cout << "frame " << frame << " scanout " << std::hex << hash[0] << hash[1] << std::dec << '\n';
  }
}
}
X360N_PROBE_MAIN {
  try {
    // Test helper: shorten a file (damaged capture control).
    if (argc == 4 && std::filesystem::path(argv[1]).string() == "--truncate") {
      std::filesystem::resize_file(argv[2], std::stoull(std::filesystem::path(argv[3]).string()));
      return 0;
    }
    Require(argc >= 4, "usage: x360n_native_frame_probe OUTPUT d3d12|vulkan FRAMES [--draws N --resolves K] [--no-sample-counts] [cvar=value...]");
    const ProbeArg api = argv[2];
    Require(api == X360N_PROBE_ARG("d3d12") || api == X360N_PROBE_ARG("vulkan"), "invalid graphics API");
    const uint32_t frames = uint32_t(std::stoul(std::filesystem::path(argv[3]).string()));
    Require(frames >= 1 && frames <= 2000, "frames must be in [1, 2000]");
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    rex::InitLogging((output / "runtime.log").string().c_str());
    rex::Runtime runtime(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    Require(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS, "runtime setup");
    auto gpu = rex::system::LoadGpuPlugin("native", api == X360N_PROBE_ARG("d3d12") ? "d3d12" : "vulkan");
    Require(bool(gpu), "Native plugin load");
    uint32_t extra_draws = 0, extra_resolves = 0;
    bool sample_counts = true;
    int first_cvar = 4;
    while (first_cvar < argc) {
      const auto flag = std::filesystem::path(argv[first_cvar]).string();
      if (flag == "--no-sample-counts") { sample_counts = false; ++first_cvar; continue; }
      if ((flag != "--draws" && flag != "--resolves") || first_cvar + 1 >= argc) break;
      (flag == "--draws" ? extra_draws : extra_resolves) = uint32_t(std::stoul(std::filesystem::path(argv[first_cvar + 1]).string()));
      first_cvar += 2;
    }
    Require(extra_draws <= 4096 && extra_resolves <= 64 && (extra_draws == 0) == (extra_resolves == 0), "invalid --draws/--resolves");
    for (int i = first_cvar; i < argc; ++i) {
      const auto assignment = std::filesystem::path(argv[i]).string();
      const auto equals = assignment.find('=');
      Require(equals != std::string::npos && rex::cvar::SetFlagByName(assignment.substr(0, equals), assignment.substr(equals + 1)),
              "invalid cvar assignment");
    }
    Require(gpu->SetupPresentation(nullptr) == X_STATUS_SUCCESS, "presentation setup");
    Require(gpu->SetupGuestGpu(runtime.function_dispatcher(), runtime.kernel_state()) == X_STATUS_SUCCESS, "guest GPU setup");
    struct Shutdown { rex::system::IGraphicsSystem* gpu; ~Shutdown() { if (gpu) gpu->Shutdown(); } } shutdown{gpu.get()};
    Run(*gpu, *runtime.memory(), frames, extra_draws, extra_resolves, sample_counts);
    gpu->Shutdown(); shutdown.gpu = nullptr; gpu.reset(); runtime.Shutdown(); rex::FlushLogging();
    std::cout << "PASS frames=" << frames << '\n';
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
