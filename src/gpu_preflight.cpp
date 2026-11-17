// Runs in a bounded child before the session window is created. Use the same
// module/driver ABI as titles, without allocating or executing a guest runtime.
#include "gpu_probe_app.h"
#include "supervisor/fs_io.h"
#ifdef _WIN32
void CheckD3D12Presenter(rex::gpu::IGraphicsDriver&, rex::ScopeGuard&);
#endif

using x360n::GPUProbeApp;
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) try {
#else
int main(int argc, char** argv) try {
#endif
  if (argc != 3 && argc != 4) return 2;
  const auto family = std::filesystem::path(argv[1]).string();
  const auto backend = std::filesystem::path(argv[2]).string();
  if ((family != "xenos" && family != "native") ||
      (backend != "d3d12" && backend != "vulkan")) return 2;
  const bool presenter_contract = argc == 4;
  if (presenter_contract && (std::filesystem::path(argv[3]).string() != "--presenter-contract" || backend != "d3d12")) return 2;
#ifndef _WIN32
  if (presenter_contract) return 2;
#endif
  const std::string selected = family == "native" ? "native-" + backend : backend;
  const std::string module = family == "native" ? "rexgpu-native" : "rexgpu-xenos-" + backend;
  const auto scratch = std::filesystem::temp_directory_path() /
      ("x360n-gpu-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!x360n::io::create_directory(scratch)) throw std::runtime_error("GPU probe scratch collision");
  rex::ScopeGuard cleanup([&] {
    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(scratch, ec);
    if (ec || resolved != scratch || x360n::io::is_redirect(scratch, ec) || ec) return;
    std::filesystem::recursive_directory_iterator entry(scratch, ec), end;
    for (unsigned count = 0; !ec && entry != end; ++count, entry.increment(ec))
      if (count >= 32 || x360n::io::is_redirect(entry->path(), ec) || ec) return;
    if (!ec) std::filesystem::remove_all(scratch, ec);
  });
  GPUProbeApp app(scratch, module, selected);
  const std::vector<std::string> options = presenter_contract
      ? std::vector<std::string>{"--gpu.d3d12.debug=true"} : std::vector<std::string>{};
  auto initialized = app.Init(rex::CLIArgList::Parse(options));
  if (!initialized) {
    std::cerr << initialized.error().GetDescription() << '\n'; return 3;
  }
  auto* driver = &app.GetProbeDriver();
  rex::ScopeGuard shutdown([&] { app.ReleaseProbeDriver(); });
  // An empty surface creates the real provider/presenter, including swapchain
  // device features, without attaching an OS window or starting guest threads.
  auto ready = static_cast<rex::gpu::IGraphicsDriver&>(*driver).SetSurface({});
  if (!ready) { std::cerr << ready.error().GetDescription() << '\n'; return 4; }
  if (driver->GetName() != selected) throw std::runtime_error("GPU module selected a different backend");
  std::cout << "GPU backend initialized: " << driver->GetName() << '\n';
#ifdef _WIN32
  if (presenter_contract) CheckD3D12Presenter(static_cast<rex::gpu::IGraphicsDriver&>(*driver), shutdown);
#endif
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 5;
}
