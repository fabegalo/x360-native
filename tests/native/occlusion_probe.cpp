// PM4 -> Native -> translated ROV/FSI shader -> GPU counter -> guest report.
// Synthetic contract test only; this does not establish gameplay acceptance.
#include "probe_main.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/format/ucode.h>
#include <rex/cvar.h>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using rex::X_STATUS;
namespace {
#if defined(X360N_QUERY_XENOS)
constexpr bool kXenos = true;
#else
constexpr bool kXenos = false;
#endif
void Require(bool result, const char* what) { if (!result) throw std::runtime_error(what); }
uint32_t Packet(uint32_t opcode, uint32_t count) { return 0xC0000000 | ((count - 1) << 16) | (opcode << 8); }
std::vector<uint32_t> Shader(bool vertex, bool depth = false, bool kill = false) {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(vertex ? AllocType::kVsPosition : AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const unsigned n = vertex ? 2 : kill ? 2 : 1;
  const uint64_t exec = 1 | (uint64_t(n) << 12) | (uint64_t(vertex) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16)};
  if (vertex) code.insert(code.end(), {(1u << 12) | (1u << 19),
      0x688u | (1u << 13) | (uint32_t(VertexFormat::k_32_32_32_32_FLOAT) << 16), 4});
  if (kill) code.insert(code.end(), {0, 0, uint32_t(AluVectorOpcode::kKillEq) << 24});
  const uint32_t dest = vertex ? 62 : depth ? 61 : 0;
  code.insert(code.end(), {dest | (1u << 15) | ((depth ? 1u : 15u) << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (vertex ? (1u << 16) | (1u << 8) | (3u << 30) : 0)});
  return code;
}
void Run(rex::system::IGraphicsSystem& gpu, rex::memory::Memory& memory, bool extended_edram) {
  constexpr uint32_t side = 16, pixels = side * side;
  const auto ring = memory.SystemHeapAlloc(65536, 4096, rex::memory::kSystemHeapPhysical);
  const auto reports = memory.SystemHeapAlloc(16384, 4096, rex::memory::kSystemHeapPhysical);
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(ring && reports && vertices && done, "probe guest allocations");
  std::memset(memory.TranslateVirtual(reports), 0, 16384);
  const float triangle[]{-1, -1, 0.5f, 1, -1, 3, 0.5f, 1, 3, -1, 0.5f, 1};
  std::memcpy(memory.TranslateVirtual(vertices), triangle, sizeof(triangle));
  std::vector<uint32_t> words;
  auto set = [&](uint32_t index, uint32_t value) { words.insert(words.end(), {index, value}); };
  auto fp = [&](uint32_t index, float value) { set(index, std::bit_cast<uint32_t>(value)); };
  auto load = [&](bool vertex, bool depth = false, bool kill = false) {
    const auto shader = Shader(vertex, depth, kill);
    words.insert(words.end(), {Packet(PM4_IM_LOAD_IMMEDIATE, uint32_t(shader.size() + 2)), uint32_t(!vertex), uint32_t(shader.size())});
    words.insert(words.end(), shader.begin(), shader.end());
  };
  set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kColorDepth));
  set(XE_GPU_REG_RB_COLOR_MASK, 0);
  set(XE_GPU_REG_RB_DEPTH_INFO, extended_edram ? 4096 : 0);
  reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 1; set(XE_GPU_REG_SQ_PROGRAM_CNTL, program.value);
  set(XE_GPU_REG_VGT_MAX_VTX_INDX, 0xFFFFFF);
  set(XE_GPU_REG_PA_CL_VTE_CNTL, 0x3F);
  fp(XE_GPU_REG_PA_CL_VPORT_XSCALE, side / 2.0f); fp(XE_GPU_REG_PA_CL_VPORT_XOFFSET, side / 2.0f);
  fp(XE_GPU_REG_PA_CL_VPORT_YSCALE, -float(side) / 2.0f); fp(XE_GPU_REG_PA_CL_VPORT_YOFFSET, side / 2.0f);
  fp(XE_GPU_REG_PA_CL_VPORT_ZSCALE, 1.0f); fp(XE_GPU_REG_PA_CL_VPORT_ZOFFSET, 0.0f);
  set(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, 0);
  set(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, side | (side << 16));
  set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000);
  set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, side | (side << 16));
  set(XE_GPU_REG_PA_SU_VTX_CNTL, 1); // D3D half-integer centers.
  for (unsigned c = 0; c < 4; ++c) fp(XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4 + c, 0.5f);
  xe_gpu_vertex_fetch_t fetch{}; fetch.type = FetchConstantType::kVertex;
  fetch.address = memory.GetPhysicalAddress(vertices) >> 2; fetch.size = 12;
  set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, fetch.dword_0);
  set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1, fetch.dword_1);
  load(true); load(false);
  reg::VGT_DRAW_INITIATOR draw{}; draw.prim_type = PrimitiveType::kTriangleList;
  draw.source_select = SourceSelect::kAutoIndex; draw.num_indices = 3;
  uint32_t draw_condition = 0;
  auto issue_draw = [&] { words.insert(words.end(), {Packet(PM4_DRAW_INDX, 2), draw_condition, draw.value}); };
  struct Case { const char* name; std::array<uint32_t, 8> expected; };
  std::vector<Case> cases;
  auto query = [&](const char* name, uint32_t pass, uint32_t zfail, uint32_t stencilfail, unsigned draws = 1) {
    const uint32_t address = memory.GetPhysicalAddress(reports) + uint32_t(cases.size()) * 64;
    // END sentinel is big endian, while the eight GPU counters are little endian.
    auto* end = memory.TranslatePhysical<uint32_t*>(address + 32); end[4] = 0xEDFEFFFF;
    set(XE_GPU_REG_RB_SAMPLE_COUNT_ADDR, address);
    words.insert(words.end(), {Packet(PM4_EVENT_WRITE_ZPD, 1), ZPASS_DONE});
    for (unsigned i = 0; i < draws; ++i) issue_draw();
    set(XE_GPU_REG_RB_SAMPLE_COUNT_ADDR, address + 32);
    words.insert(words.end(), {Packet(PM4_EVENT_WRITE_ZPD, 1), ZPASS_DONE});
    cases.push_back({name, {pass + zfail + stencilfail, 0, zfail, 0, pass, 0, stencilfail, 0}});
  };
  for (unsigned samples = 0; samples < 3; ++samples) {
    reg::RB_SURFACE_INFO surface{}; surface.surface_pitch = 80; surface.msaa_samples = MsaaSamples(samples);
    set(XE_GPU_REG_RB_SURFACE_INFO, surface.value);
    set(XE_GPU_REG_RB_DEPTHCONTROL, 0);
    issue_draw(); // OFF variant must not contribute to the next query.
    query(samples == 0 ? "1x visible" : samples == 1 ? "2x visible" : "4x visible", pixels << samples, 0, 0);
    reg::RB_DEPTHCONTROL depth{}; depth.z_enable = 1; depth.zfunc = CompareFunction::kNever;
    set(XE_GPU_REG_RB_DEPTHCONTROL, depth.value);
    query("early depth rejects", 0, pixels << samples, 0);
    depth.stencil_enable = 1; depth.stencilfunc = CompareFunction::kNever;
    set(XE_GPU_REG_RB_DEPTHCONTROL, depth.value);
    query("stencil takes precedence", 0, 0, pixels << samples);
    load(false, true);
    depth = {}; depth.z_enable = 1; depth.zfunc = CompareFunction::kNever;
    set(XE_GPU_REG_RB_DEPTHCONTROL, depth.value);
    query("shader depth rejects", 0, pixels << samples, 0);
    depth.zfunc = CompareFunction::kAlways;
    set(XE_GPU_REG_RB_DEPTHCONTROL, depth.value);
    query("shader depth passes", pixels << samples, 0, 0);
    set(XE_GPU_REG_RB_DEPTHCONTROL, 0);
    set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kDepthOnly));
    query("depth-only passes", pixels << samples, 0, 0);
    set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kColorDepth));
    load(false);
    reg::RB_COLORCONTROL color{}; color.alpha_test_enable = 1; color.alpha_func = CompareFunction::kNever;
    set(XE_GPU_REG_RB_COLORCONTROL, color.value);
    query("alpha rejects", 0, 0, 0);
    set(XE_GPU_REG_RB_COLORCONTROL, 0);
    load(false, false, true);
    query("shader kills", 0, 0, 0);
    load(false);
    query("two draws accumulate", (pixels << samples) * 2, 0, 0, 2);
  }
  for (unsigned id : {0u, 31u, 32u, 63u}) {
    auto survey = [&](bool visible) {
      if constexpr (!kXenos) return;
      draw_condition = 0;
      words.insert(words.end(), {Packet(PM4_VIZ_QUERY, 1), id});
      reg::PA_SC_VIZ_QUERY viz{}; viz.viz_query_ena = 1; viz.kill_pix_post_hi_z = 1; viz.viz_query_id = id;
      set(XE_GPU_REG_PA_SC_VIZ_QUERY, viz.value);
      reg::RB_HIZCONTROL hiz{}; hiz.hiz_enable = 1;
      set(XE_GPU_REG_RB_HIZCONTROL, hiz.value);
      reg::RB_DEPTHCONTROL depth{}; depth.z_enable = 1;
      depth.zfunc = visible ? CompareFunction::kAlways : CompareFunction::kNever;
      set(XE_GPU_REG_RB_DEPTHCONTROL, depth.value);
      issue_draw();
      words.insert(words.end(), {Packet(PM4_VIZ_QUERY, 1), id | 0x100});
      set(XE_GPU_REG_PA_SC_VIZ_QUERY, 0);
      set(XE_GPU_REG_RB_DEPTHCONTROL, 0);
    };
    survey(false);
    draw_condition = 0x100 | id;
    set(XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0, 0);
    set(XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1, 0);
    query("predicate false", 0, 0, 0);
    survey(true);
    draw_condition = 0x100 | id;
    set(id < 32 ? XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0 : XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1, 1u << (id & 31));
    query("predicate true", pixels * 4, 0, 0);
  }
  draw_condition = 0;
  const size_t consecutive_cases = cases.size();
  auto edge = [&](size_t index, bool end) {
    const auto address = memory.GetPhysicalAddress(reports) + uint32_t(index * 64) + (end ? 32 : 0);
    if (end) memory.TranslatePhysical<uint32_t*>(address)[4] = 0xEDFEFFFF;
    set(XE_GPU_REG_RB_SAMPLE_COUNT_ADDR, address);
    words.insert(words.end(), {Packet(PM4_EVENT_WRITE_ZPD, 1), ZPASS_DONE});
  };
  const auto outer = cases.size(), inner = outer + 1;
  edge(outer, false); issue_draw(); edge(inner, false); issue_draw();
  edge(outer, true); issue_draw(); edge(inner, true);
  cases.push_back({"overlapping outer", {pixels * 8, 0, 0, 0, pixels * 8, 0, 0, 0}});
  cases.push_back({"overlapping inner", {pixels * 8, 0, 0, 0, pixels * 8, 0, 0, 0}});
  words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), 1});
  Require(words.size() * 4 < 65536, "probe command capacity");
  for (size_t i = 0; i < words.size(); ++i)
    rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring + uint32_t(i * 4)), words[i]);
  struct ReportWatch { uint32_t address; std::atomic<unsigned> invalidations{0}; } report_watch{memory.GetPhysicalAddress(reports)};
  auto* watch = memory.RegisterPhysicalMemoryInvalidationCallback(
      [](void* context, uint32_t address, uint32_t length, bool) {
        auto& state = *static_cast<ReportWatch*>(context);
        if (address < state.address + 32 && state.address < uint64_t(address) + length) ++state.invalidations;
        return std::pair<uint32_t, uint32_t>{0, UINT32_MAX};
      }, &report_watch);
  struct Unwatch { rex::memory::Memory& memory; void* handle; ~Unwatch() { memory.UnregisterPhysicalMemoryInvalidationCallback(handle); } } unwatch{memory, watch};
  memory.EnablePhysicalMemoryAccessCallbacks(report_watch.address, 32, true, false);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 13);
  auto* mmio = memory.LookupVirtualMappedRange(0x7FC80714);
  Require(mmio != nullptr, "probe GPU doorbell");
  mmio->write(nullptr, mmio->callback_context, 0x7FC80714, uint32_t(words.size()));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  while (*marker != 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  Require(*marker == 1, "probe GPU completion timeout");
  if constexpr (kXenos) {
    // Strict reports may retire after the CPU command marker. Wait for all
    // sentinels rather than treating command consumption as GPU completion.
    for (size_t i = 0; i < cases.size(); ++i) {
      auto* end = memory.TranslateVirtual<volatile uint32_t*>(reports + uint32_t(i * 64) + 32);
      while (end[4] == 0xEDFEFFFF && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      Require(end[4] != 0xEDFEFFFF, "strict ZPD report completion timeout");
    }
  }
  Require(report_watch.invalidations.load() != 0, "ZPD physical publication did not invalidate guest aliases");
  bool passed = true;
  for (size_t i = 0; i < cases.size(); ++i) {
    auto* begin = memory.TranslateVirtual<const uint32_t*>(reports + uint32_t(i * 64));
    auto* end = begin + 8;
    std::cout << cases[i].name << ':';
    for (unsigned c = 0; c < 8; ++c) {
      const uint32_t delta = end[c] - begin[c];
      std::cout << ' ' << delta;
      if constexpr (kXenos) {
        if (!(c & 1)) passed &= delta + (end[c + 1] - begin[c + 1]) == cases[i].expected[c];
      } else passed &= delta == cases[i].expected[c];
      // Draws outside a query must neither instrument shaders nor advance the
      // counters. Counter snapshots remain cumulative between query intervals.
      // Canary snapshots free-running counters, including draws between queries.
      if (!kXenos && i && i < consecutive_cases) passed &= begin[c] == (begin - 8)[c];
    }
    std::cout << '\n';
  }
  Require(passed, "GPU sample counters differ from expected sample coverage");
}
}
X360N_PROBE_MAIN {
  try {
    Require(argc >= 2 && argc <= 4, "usage: x360n_native_occlusion_probe OUTPUT [d3d12|vulkan] [striped|scalar]");
    const ProbeArg requested = argc >= 3 ? argv[2] : X360N_PROBE_ARG("d3d12");
    Require(requested == X360N_PROBE_ARG("d3d12") || requested == X360N_PROBE_ARG("vulkan"), "invalid probe graphics API");
    const char* backend = requested == X360N_PROBE_ARG("vulkan") ? "vulkan" : "d3d12";
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    rex::InitLogging((output / "runtime.log").string().c_str());
    rex::Runtime rt(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    Require(rt.Setup(std::move(config)) == X_STATUS_SUCCESS, "runtime setup");
    if (requested == X360N_PROBE_ARG("d3d12")) rex::cvar::SetFlagByName("d3d12_debug", "true");
    auto gpu = rex::system::LoadGpuPlugin(kXenos ? "xenos" : "native", backend); Require(bool(gpu), "GPU plugin load");
    if constexpr (kXenos) {
      if (requested == X360N_PROBE_ARG("d3d12"))
        Require(rex::cvar::SetFlagByName("d3d12_debug", "true"), "D3D12 validation flag");
      else
        Require(rex::cvar::SetFlagByName("vulkan_validation_enabled", "true"), "Vulkan validation flag");
      Require(rex::cvar::SetFlagByName("occlusion_query", "strict"), "strict query flag");
      Require(rex::cvar::SetFlagByName("occlusion_query_full_counters", "true"), "full query counter flag");
      Require(rex::cvar::SetFlagByName("async_shader_compilation", "false"), "synchronous probe pipelines");
      Require(rex::cvar::SetFlagByName(requested == X360N_PROBE_ARG("d3d12") ? "render_target_path_d3d12" : "render_target_path_vulkan",
          requested == X360N_PROBE_ARG("d3d12") ? "rov" : "fsi"), "interlock query path flag");
    }
    bool extended_edram = false;
    if(argc==4 && ProbeArg(argv[3]) == X360N_PROBE_ARG("extended")) {
      extended_edram = true;
      Require(rex::cvar::SetFlagByName("edram_tile_count", "extended"), "extended EDRAM flag");
    } else if(argc==4) {
      const auto variant=ProbeArg(argv[3]);
      Require(requested==X360N_PROBE_ARG("d3d12") &&
          (variant==X360N_PROBE_ARG("striped") || variant==X360N_PROBE_ARG("scalar")),"invalid counter variant");
      Require(rex::cvar::SetFlagByName("native_d3d12_striped_sample_counts",variant==X360N_PROBE_ARG("striped")?"true":"false"),"counter variant flag not registered");
    }
    Require(gpu->SetupPresentation(nullptr) == X_STATUS_SUCCESS, "presentation setup");
    Require(gpu->SetupGuestGpu(rt.function_dispatcher(), rt.kernel_state()) == X_STATUS_SUCCESS, "guest GPU setup");
    struct Shutdown { rex::system::IGraphicsSystem* gpu; ~Shutdown() { if (gpu) gpu->Shutdown(); } } shutdown{gpu.get()};
    Run(*gpu, *rt.memory(), extended_edram);
    gpu->Shutdown(); shutdown.gpu = nullptr; gpu.reset(); rt.Shutdown(); rex::FlushLogging();
    std::cout << "PASS " << (kXenos ? "Xenos/" : "Native/") << backend
              << " ZPD real samples, early/late depth, stencil, alpha, kill, 1x/2x/4x\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
