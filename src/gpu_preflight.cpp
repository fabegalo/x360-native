// Runs in a bounded child process before the session window is created. Driver
// initialization and plugin ABI checks cannot crash or hang the launcher.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <iostream>
#include <chrono>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) try {
#else
int main(int argc, char** argv) try {
#endif
  if (argc < 3) return 2;
  const auto backend = std::filesystem::path(argv[2]).string();
  if (backend != "d3d12" && backend != "vulkan") return 2;
  rex::InitLogging();
  // The presenter uses runtime services (including the host clock). No guest
  // image is loaded and tool mode does not execute dashboard/game code.
  const auto scratch = std::filesystem::temp_directory_path() /
      ("x360n-gpu-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(scratch);
  rex::Runtime runtime(scratch, scratch / "user");
  rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(std::move(config)) != 0) {
    std::cerr << "Cannot initialize GPU probe runtime services\n"; return 7;
  }
  auto gpu = rex::system::LoadGpuPlugin(std::filesystem::path(argv[1]).string(), backend);
  if (!gpu) {
    std::cerr << "GPU plugin absent, incompatible ABI, or requested backend not compiled. See GPU log.\n";
    return 3;
  }
  // SetupPresentation creates the provider with presentation features, including
  // VK_KHR_swapchain, but no HWND. This exercises the actual SDK device selector.
  if (gpu->SetupPresentation(nullptr) != 0) {
    gpu->Shutdown();
    std::cerr << "Cannot create " << backend << " provider/presenter: loader, driver or required GPU features unavailable. See GPU log.\n";
    return 4;
  }
  const auto implementation = static_cast<rex::graphics::GraphicsSystem*>(gpu.get())->name();
  if (backend == "vulkan" && implementation != "Vulkan") {
    gpu->Shutdown(); std::cerr << "Plugin did not create VulkanGraphicsSystem\n"; return 6;
  }
  std::cout << "GPU backend initialized: " << implementation << '\n';
  gpu->Shutdown();
  gpu.reset(); runtime.Shutdown();
  rex::FlushLogging();
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 5;
}
