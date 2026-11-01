// x360n_native_replay: deterministic replay and profiling of Native frame
// captures (native_capture), without the title.
//
//   x360n_native_replay CAPTURE OUTPUT_DIR [--backend d3d12|vulkan] [--repeat N]
//       [--stats] [--no-present] [--skip-edram | --tamper-memory N] [cvar=value...]
//
// Writes OUTPUT_DIR/frames.csv (one row per replayed frame and pass, same
// columns as native_frame_profile) and OUTPUT_DIR/replay.json. Frames are
// presented to an offscreen presenter unless --no-present. Exit 0 when every
// frame matched the capture (scanout, GPU-written guest memory, sample
// counters, presenter output) and every pass produced identical results;
// 3 on a mismatch, 2 for an invalid capture file, 1 on other errors.
// --skip-edram and --tamper-memory are negative controls that must yield 3.
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
                   "       [--skip-edram | --tamper-memory N] [cvar=value...]\n";
      return 1;
    }
    const std::filesystem::path capture_path(argv[1]), output(argv[2]);
    std::string backend_name;
    uint32_t repeat = 1;
    bool stats = false, skip_edram = false, present = true;
    int64_t tamper_memory = -1;
    std::vector<std::pair<std::string, std::string>> cvars;
    for (int i = 3; i < argc; ++i) {
      const auto arg = Narrow(argv[i]);
      if (arg == "--backend" && i + 1 < argc) backend_name = Narrow(argv[++i]);
      else if (arg == "--repeat" && i + 1 < argc) repeat = uint32_t(std::stoul(Narrow(argv[++i])));
      else if (arg == "--stats") stats = true;
      else if (arg == "--no-present") present = false;     // CompleteFrame only: no presenter output check.
      else if (arg == "--skip-edram") skip_edram = true;  // Negative control of the EDRAM snapshot.
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
    options.tamper_memory_record = tamper_memory;
    options.presenter = presenter.get();
    std::vector<double> warm_frame_ms;
    options.on_frame = [&](uint32_t pass, const FrameStats& frame) {
      csv << pass << ',' << FrameStatsCsvRow(frame) << '\n';
      if (pass) warm_frame_ms.push_back(frame.frame_ms);
    };
    const auto start = std::chrono::steady_clock::now();
    const auto result = ReplayCapture(reader, *backend, *runtime.memory(), options);
    const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    backend->Drain();
    backend.reset();
    presenter.reset();
    provider.reset();
    std::sort(warm_frame_ms.begin(), warm_frame_ms.end());
    auto percentile = [&](double q) {
      return warm_frame_ms.empty() ? 0.0 : warm_frame_ms[std::min(warm_frame_ms.size() - 1, size_t(q * warm_frame_ms.size()))];
    };
    std::string mismatches;
    for (const auto& text : result.mismatches) mismatches += fmt::format("{}\"{}\"", mismatches.empty() ? "" : ",", text);
    const auto json = fmt::format(
        "{{\"capture\":\"{}\",\"backend\":\"{}\",\"passes\":{},\"frames\":{},\"total_ms\":{:.3f},"
        "\"warm_frame_ms_p50\":{:.3f},\"warm_frame_ms_p95\":{:.3f},\"equivalent\":{},\"repeatable\":{},"
        "\"frontbuffer_checks\":{},\"frontbuffer_mismatches\":{},\"written_checks\":{},\"written_mismatches\":{},"
        "\"sample_checks\":{},\"sample_mismatches\":{},\"output_checks\":{},\"output_mismatches\":{},"
        "\"memory_records\":{},\"memory_pages_written\":{},\"mismatches\":[{}]}}\n",
        capture_path.filename().string(), backend_name, result.passes, result.frames, total_ms, percentile(0.5),
        percentile(0.95), result.equivalent(), result.repeatable(), result.frontbuffer_checks, result.frontbuffer_mismatches,
        result.written_checks, result.written_mismatches, result.sample_checks, result.sample_mismatches,
        result.output_checks, result.output_mismatches, result.memory_records, result.memory_pages_written, mismatches);
    std::ofstream(output / "replay.json") << json;
    std::cout << json;
    runtime.Shutdown();
    rex::FlushLogging();
    return result.equivalent() && result.repeatable() ? 0 : 3;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
