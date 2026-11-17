#include <catch2/catch_test_macros.hpp>
#include <rex/driver_module.h>
#include <string>
#include <vector>

TEST_CASE("Explicit graphics backends resolve only their requested Xenos modules", "[gpu-backend]") {
  using rex::DriverType;
  using rex::GraphicsFamily;
  for (const std::string backend : {"d3d12", "vulkan"}) {
    const std::vector<std::string> expected{"rexgpu-xenos-" + backend};
    CHECK(rex::GetDriverModules(DriverType::kGraphics, backend, GraphicsFamily::kXenos) == expected);
  }
  CHECK(rex::GetDriverModules(DriverType::kGraphics, "none", GraphicsFamily::kXenos).empty());
  // Preserve an unknown request so selection reports it instead of choosing another API.
  const std::vector<std::string> missing{"rexgpu-xenos-unavailable"};
  CHECK(rex::GetDriverModules(DriverType::kGraphics, "unavailable", GraphicsFamily::kXenos) == missing);
  const std::vector<std::string> ordered{"rexgpu-xenos-vulkan", "rexgpu-xenos-d3d12"};
  CHECK(rex::GetDriverModules(DriverType::kGraphics, "vulkan,d3d12,vulkan", GraphicsFamily::kXenos) == ordered);
}
