#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <fstream>

TEST_CASE("runtime carries explicit graphics backend without changing plugin ABI", "[gpu-backend]") {
  rex::RuntimeConfig config;
  CHECK(config.gpu_backend == "any");
  config.gpu_backend = "vulkan";
  CHECK(config.gpu_backend == "vulkan");
  CHECK(rex::system::kGpuPluginAbiVersion == 1);
  // ReXApp is compiled into each AOT executable rather than the runtime DLL.
  // Assert the forwarding call as well as the compiled RuntimeConfig contract.
  std::ifstream in(X360N_SOURCE_ROOT "/external/rexglue-sdk/src/ui/rex_app.cpp");
  REQUIRE(in.good());
  std::string source{std::istreambuf_iterator<char>(in), {}};
  CHECK(source.find("config_.gpu_backend = REXCVAR_GET(gpu_backend)") != std::string::npos);
  CHECK(source.find("LoadGpuPlugin(config_.gpu_plugin, config_.gpu_backend)") != std::string::npos);
}
