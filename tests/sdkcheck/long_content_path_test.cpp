#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/kernel/init.h>
#include <rex/memory/utils.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
using rex::X_STATUS;

namespace {
namespace fs = std::filesystem;
// Creates directories and files below MAX_PATH-unsafe depths without relying on
// the code under test; the tests then pass only the ordinary spelling.
fs::path Long(const fs::path& path) {
#ifdef _WIN32
  const auto name = fs::absolute(path).lexically_normal().make_preferred().native();
  return name.starts_with(L"\\\\?\\") ? fs::path(name) : fs::path(L"\\\\?\\" + name);
#else
  return path;
#endif
}
struct DeepRoot {
  fs::path path = fs::temp_directory_path() /
      ("x360n-long-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  DeepRoot() { fs::create_directories(Long(path)); }
  ~DeepRoot() { std::error_code ec; fs::remove_all(Long(path), ec); }
};
}  // namespace

TEST_CASE("Host paths use the extended form only where the host needs it", "[sdk][long-path]") {
#ifdef _WIN32
  using rex::filesystem::HostPath;
  using rex::filesystem::OrdinaryPath;
  CHECK(HostPath(LR"(C:\a\.\b\..\c)") == fs::path(LR"(\\?\C:\a\c)"));
  CHECK(HostPath(L"C:/a/b") == fs::path(LR"(\\?\C:\a\b)"));
  CHECK(HostPath(LR"(\\server\share\x)") == fs::path(LR"(\\?\UNC\server\share\x)"));
  CHECK(HostPath(LR"(\\?\C:\a)") == fs::path(LR"(\\?\C:\a)"));
  CHECK(HostPath(HostPath(LR"(C:\a)")) == HostPath(LR"(C:\a)"));
  CHECK(HostPath(fs::path()).empty());
  CHECK(HostPath(L"rel").is_absolute());
  CHECK(OrdinaryPath(LR"(\\?\C:\a\c)") == fs::path(LR"(C:\a\c)"));
  CHECK(OrdinaryPath(LR"(\\?\UNC\server\share\x)") == fs::path(LR"(\\server\share\x)"));
  CHECK(OrdinaryPath(LR"(\\?\Volume{0}\x)") == fs::path(LR"(\\?\Volume{0}\x)"));
#else
  CHECK(rex::filesystem::HostPath("rel/x") == fs::path("rel/x"));
  CHECK(rex::filesystem::OrdinaryPath("/a/b") == fs::path("/a/b"));
#endif
}

TEST_CASE("Content beyond MAX_PATH keeps its attributes and device entries", "[sdk][long-path]") {
  // The packaged Windows install placed Skate 3's installed files at 270+
  // characters; GetContentAttributes then failed and My Games dropped it.
  DeepRoot root;
  const auto package = root.path / "0000000000000000" / "12345678" / "00004000" / "package";
  auto deep = package;
  while (deep.native().size() < 300) deep /= std::string(30, 'd');
  fs::create_directories(Long(deep));
  std::ofstream(Long(package / "default.xex"), std::ios::binary).write("xex", 3);
  std::ofstream(Long(deep / "asset.bin"), std::ios::binary).write("long-asset", 10);
  REQUIRE((deep / "asset.bin").native().size() > 300);

  rex::Runtime rt(root.path, root.path);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.title_id = 0x12345678;
  data.content_type = rex::system::XContentType(0x4000);
  data.set_file_name("package");
  uint8_t attributes[40]{};
  REQUIRE(rt.kernel_state()->content_manager()->GetContentAttributes(data, attributes) == 0);
  CHECK(rex::memory::load_and_swap<uint32_t>(attributes) == 0x10);
  CHECK(rex::memory::load_and_swap<uint64_t>(attributes + 28) == 13);

  // A device mounted on the ordinary spelling resolves the same deep file.
  rex::filesystem::HostPathDevice device("\\Deep", package, true);
  REQUIRE(device.Initialize());
  std::string guest;
  for (const auto& part : deep.lexically_relative(package)) guest += part.string() + "\\";
  auto* entry = device.ResolvePath(guest + "asset.bin");
  REQUIRE(entry);
  CHECK(entry->size() == 10);
}
