// x360n_native_replay: deterministic replay and profiling of Native frame
// captures (native_capture), without the title.
//
//   x360n_native_replay CAPTURE OUTPUT_DIR [--backend d3d12|vulkan] [--repeat N]
//       [--stats] [--no-present] [--skip-edram | --tamper-memory N | --drop-output] [cvar=value...]
//
// Writes OUTPUT_DIR/frames.csv (one row per replayed frame and pass, same
// columns as native_frame_profile) and OUTPUT_DIR/replay.json. Frames are
// presented to an offscreen presenter unless --no-present. Exit 0 when every
// frame matched the capture (scanout, GPU-written guest memory, sample
// counters, presenter output) and every pass produced identical results;
// 3 on a mismatch, 2 for an invalid capture file, 1 on other errors.
// --skip-edram, --tamper-memory and --drop-output are negative controls that
// must yield 3. --no-present skips the visual check: replay.json then reports
// "visual_checked":false and the run is not a full equivalence proof.
#include "../capture/frame_replay.h"
#include "../commands/scanout_backend.h"
#include "../../../tests/native/probe_main.h"
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/presenter.h>
#include <rex/graphics/register_file.h>
#include <optional>
#if X360N_NATIVE_VULKAN
#include "../vulkan/vulkan_backend.h"
#include <rex/ui/vulkan/provider.h>
#endif
#if X360N_NATIVE_D3D12
#include <rex/ui/d3d12/d3d12_provider.h>
#endif
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
using namespace x360n::native;
using rex::X_STATUS;
namespace {
std::string Narrow(const ProbeChar* text) { return std::filesystem::path(text).string(); }
}
X360N_PROBE_MAIN {
  try {
    if (argc < 3) {
      std::cerr << "usage: x360n_native_replay CAPTURE OUTPUT_DIR [--backend d3d12|vulkan] [--repeat N] [--stats] [--no-present]\n"
                   "       [--skip-edram | --tamper-memory N | --drop-output] [cvar=value...]\n";
      return 1;
    }
    const std::filesystem::path capture_path(argv[1]), output(argv[2]);
    std::string backend_name;
    uint32_t repeat = 1;
    bool stats = false, skip_edram = false, drop_output = false, present = true;
    int64_t tamper_memory = -1;
    std::vector<std::pair<std::string, std::string>> cvars;
    for (int i = 3; i < argc; ++i) {
      const auto arg = Narrow(argv[i]);
      if (arg == "--backend" && i + 1 < argc) backend_name = Narrow(argv[++i]);
      else if (arg == "--repeat" && i + 1 < argc) repeat = uint32_t(std::stoul(Narrow(argv[++i])));
      else if (arg == "--stats") stats = true;
      else if (arg == "--no-present") present = false;     // CompleteFrame only: no presenter output check.
      else if (arg == "--skip-edram") skip_edram = true;  // Negative control of the EDRAM snapshot.
      else if (arg == "--drop-output") drop_output = true;  // Negative control: presenter yields no image.
      else if (arg == "--tamper-memory" && i + 1 < argc) tamper_memory = std::stoll(Narrow(argv[++i]));  // Negative control.
      else if (const auto equals = arg.find('='); equals != std::string::npos) cvars.emplace_back(arg.substr(0, equals), arg.substr(equals + 1));
      else throw std::invalid_argument("unknown argument: " + arg);
    }
    std::optional<capture::Reader> loaded;
    try {
      loaded.emplace(capture_path);
      capture::ValidateFile(*loaded, rex::graphics::RegisterFile::kRegisterCount);
    } catch (const std::runtime_error& error) {
      std::cerr << "invalid capture: " << error.what() << '\n';
      return 2;
    }
    capture::Reader& reader = *loaded;
    if (backend_name.empty()) backend_name = reader.header().backend;
    std::filesystem::create_directories(output);
    rex::InitLogging((output / "replay.log").string().c_str());
    rex::Runtime runtime(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    if (runtime.Setup(std::move(config)) != X_STATUS_SUCCESS) throw std::runtime_error("runtime setup");
    if (stats) {
      // Counters with fence waits and GPU timestamps (renderer diagnostics).
      rex::cvar::SetFlagByName("native_stats", "true");
      rex::cvar::SetFlagByName("native_vulkan_perf", "true");
    }
    for (const auto& [name, value] : cvars)
      if (!rex::cvar::SetFlagByName(name, value)) throw std::invalid_argument("unknown cvar or value: " + name);
    std::unique_ptr<rex::ui::GraphicsProvider> provider;
    std::unique_ptr<ScanoutBackend> backend;
#if X360N_NATIVE_VULKAN
    if (backend_name == "vulkan") {
      provider = rex::ui::vulkan::VulkanProvider::Create(true, present);
      if (!provider) throw std::runtime_error("Vulkan provider unavailable");
      backend = CreateVulkanScanout(*provider);
    }
#endif
#if X360N_NATIVE_D3D12
    if (backend_name == "d3d12") {
      provider = rex::ui::d3d12::D3D12Provider::Create();
      if (!provider) throw std::runtime_error("D3D12 provider unavailable");
      backend = CreateD3D12Scanout(*provider);
    }
#endif
    if (!backend) throw std::runtime_error("backend '" + backend_name + "' is not built on this host");
    // Offscreen presenter: gamma LUT, overlay plane and post effects run as in
    // the title, and their output is compared with the capture's.
    std::unique_ptr<rex::ui::Presenter> presenter;
    if (present) {
      presenter = provider->CreatePresenter();
      if (!presenter) throw std::runtime_error("offscreen presenter unavailable");
    }
    std::ofstream csv(output / "frames.csv");
    csv << "pass," << FrameStatsCsvHeader() << '\n';
    ReplayOptions options;
    options.repeat = repeat;
    options.skip_edram = skip_edram;
    options.drop_output = drop_output;
    if (drop_output && !present) throw std::invalid_argument("--drop-output needs the presenter");
    if (!present) std::cerr << "warning: --no-present: the composed image is not checked\n";
    options.tamper_memory_record = tamper_memory;
    options.presenter = presenter.get();
    std::vector<double> warm_frame_ms;
    // Means over warm passes (all passes when there is only one): the
    // renderer's cost per frame by phase, for bottleneck attribution.
    struct Means { double n = 0, frame, backend, draw, resolve, sync, sample, present, shader, draws, resolves, syncs,
                   submissions, fence_waits, fence_ms, gpu_ms; bool gpu = true, measured = true; } warm{}, all{};
    auto add = [](Means& m, const FrameStats& f) {
      ++m.n; m.frame += f.frame_ms; m.backend += f.backend_ms; m.draw += f.draw_ms; m.resolve += f.resolve_ms;
      m.sync += f.sync_ms; m.sample += f.sample_ms; m.present += f.present_ms; m.shader += f.shader_ms;
      m.draws += double(f.draws); m.resolves += double(f.resolves); m.syncs += double(f.guest_syncs);
      m.submissions += double(f.counters.submissions); m.fence_waits += double(f.counters.fence_waits);
      m.fence_ms += double(f.counters.fence_wait_ns) / 1e6; m.gpu_ms += double(f.counters.gpu_busy_ns) / 1e6;
      m.gpu = m.gpu && f.counters.gpu_timed; m.measured = m.measured && f.counters.measured;
    };
    options.on_frame = [&](uint32_t pass, const FrameStats& frame) {
      csv << pass << ',' << FrameStatsCsvRow(frame) << '\n';
      if (pass) { warm_frame_ms.push_back(frame.frame_ms); add(warm, frame); }
      add(all, frame);
    };
    const auto start = std::chrono::steady_clock::now();
    const auto result = ReplayCapture(reader, *backend, *runtime.memory(), options);
    const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    backend->Drain();
    backend.reset();
    presenter.reset();
    provider.reset();
    std::sort(warm_frame_ms.begin(), warm_frame_ms.end());
    // Nearest-rank percentile of the warm frames.
    auto percentile = [&](double q) {
      if (warm_frame_ms.empty()) return 0.0;
      const size_t rank = size_t(std::ceil(q * double(warm_frame_ms.size())));
      return warm_frame_ms[std::clamp<size_t>(rank, 1, warm_frame_ms.size()) - 1];
    };
    const Means& m = warm.n ? warm : all;
    auto mean = [&](double total) { return m.n ? total / m.n : 0.0; };
    // Dominant phase of the replayed frame: GPU-bound when the GPU was busy
    // for most of it; otherwise where the renderer's thread spent its time.
    std::string bottleneck = "unmeasured";
    if (m.n) {
      const std::pair<double, const char*> phases[]{{mean(m.draw), "draw_recording"}, {mean(m.resolve), "resolves"},
          {mean(m.sync) + mean(m.sample), "guest_syncs_and_counters"}, {mean(m.present), "present"}, {mean(m.shader), "shader_loading"}};
      bottleneck = std::max_element(std::begin(phases), std::end(phases))->second;
      if (m.gpu && mean(m.gpu_ms) >= 0.8 * mean(m.frame)) bottleneck = "gpu";
    }
    auto optional = [](bool valid, double value) { return valid ? fmt::format("{:.3f}", value) : std::string("null"); };
    const auto summary = fmt::format(
        "{{\"frames\":{},\"frame_ms\":{:.3f},\"renderer_ms\":{:.3f},\"draw_ms\":{:.3f},\"resolve_ms\":{:.3f},"
        "\"sync_ms\":{:.3f},\"sample_ms\":{:.3f},\"present_ms\":{:.3f},\"shader_ms\":{:.3f},\"draws\":{:.1f},"
        "\"resolves\":{:.1f},\"guest_syncs\":{:.1f},\"submissions\":{:.2f},\"fence_waits\":{},\"fence_wait_ms\":{},"
        "\"gpu_busy_ms\":{},\"gpu_idle_ms\":{},\"bottleneck\":\"{}\"}}",
        uint64_t(m.n), mean(m.frame), mean(m.backend), mean(m.draw), mean(m.resolve), mean(m.sync), mean(m.sample),
        mean(m.present), mean(m.shader), mean(m.draws), mean(m.resolves), mean(m.syncs), mean(m.submissions),
        optional(m.measured, mean(m.fence_waits)), optional(m.measured, mean(m.fence_ms)), optional(m.gpu, mean(m.gpu_ms)),
        optional(m.gpu, mean(m.frame) - mean(m.gpu_ms)), bottleneck);
    std::string mismatches;
    for (const auto& text : result.mismatches) mismatches += fmt::format("{}\"{}\"", mismatches.empty() ? "" : ",", text);
    const auto json = fmt::format(
        "{{\"capture\":\"{}\",\"backend\":\"{}\",\"passes\":{},\"frames\":{},\"visual_checked\":{},\"total_ms\":{:.3f},"
        "\"warm_frame_ms_p50\":{:.3f},\"warm_frame_ms_p95\":{:.3f},\"warm_frame_ms_p99\":{:.3f},\"equivalent\":{},\"repeatable\":{},"
        "\"frontbuffer_checks\":{},\"frontbuffer_mismatches\":{},\"written_checks\":{},\"written_mismatches\":{},"
        "\"sample_checks\":{},\"sample_mismatches\":{},\"output_checks\":{},\"output_mismatches\":{},"
        "\"memory_records\":{},\"memory_pages_written\":{},\"per_frame\":{},\"mismatches\":[{}]}}\n",
        capture_path.filename().string(), backend_name, result.passes, result.frames, result.visual_checked, total_ms, percentile(0.5),
        percentile(0.95), percentile(0.99), result.equivalent(), result.repeatable(), result.frontbuffer_checks, result.frontbuffer_mismatches,
        result.written_checks, result.written_mismatches, result.sample_checks, result.sample_mismatches,
        result.output_checks, result.output_mismatches, result.memory_records, result.memory_pages_written, summary, mismatches);
    std::ofstream(output / "replay.json") << json;
    std::cout << json;
    runtime.Shutdown();
    rex::FlushLogging();
    return result.equivalent() && result.repeatable() ? 0 : 3;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
