// Patch 0253: a deep installation reaches its title projects through a short
// junction (x360n's alias). The codegen resolved the junction with
// std::filesystem::canonical and opened the real name, beyond MAX_PATH, so
// Castle Crashers' analysis include failed with "File could not be opened for
// reading". The config is now read by the spelling that reached it.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <rex/codegen/config.h>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <vector>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
namespace {
fs::path Long(const fs::path& path) { return fs::path(L"\\\\?\\" + path.native()); }

struct MountPointHeader {
  DWORD tag;
  USHORT length, reserved, substitute_offset, substitute_length, print_offset, print_length;
};

// A directory junction, as an unprivileged user can make one.
void Junction(const fs::path& link, const fs::path& target) {
  REQUIRE(CreateDirectoryW(link.c_str(), nullptr));
  const auto print = target.native();
  const auto substitute = L"\\??\\" + print;
  std::vector<char> buffer(sizeof(MountPointHeader) + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
  auto* header = reinterpret_cast<MountPointHeader*>(buffer.data());
  *header = {IO_REPARSE_TAG_MOUNT_POINT, USHORT(buffer.size() - 8), 0, 0,
             USHORT(substitute.size() * sizeof(wchar_t)),
             USHORT((substitute.size() + 1) * sizeof(wchar_t)), USHORT(print.size() * sizeof(wchar_t))};
  auto* names = reinterpret_cast<wchar_t*>(buffer.data() + sizeof(MountPointHeader));
  std::copy(substitute.c_str(), substitute.c_str() + substitute.size() + 1, names);
  std::copy(print.c_str(), print.c_str() + print.size() + 1, names + substitute.size() + 1);
  const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  REQUIRE(handle != INVALID_HANDLE_VALUE);
  DWORD bytes = 0;
  const bool set = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()),
                                   nullptr, 0, &bytes, nullptr);
  CloseHandle(handle);
  REQUIRE(set);
}
}  // namespace
#endif

TEST_CASE("Config includes are read through a short junction to a deep folder", "[sdk][long-path]") {
#ifdef _WIN32
  const auto base = fs::temp_directory_path() /
      ("x360n-junction-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  auto deep = base / "real";
  for (int i = 0; i < 4; ++i) deep /= std::string(70, char('a' + i));
  REQUIRE(fs::create_directories(Long(deep / "analysis")));
  std::ofstream(Long(deep / "title.toml")) << "file_path = \"default.xex\"\nincludes = [\"analysis/hints.toml\"]\n";
  std::ofstream(Long(deep / "analysis" / "hints.toml")) << "[functions]\n0x82000010 = {}\n";
  const auto alias = base / "a";
  Junction(alias, deep);
  struct Cleanup {
    fs::path base, alias;
    ~Cleanup() {
      RemoveDirectoryW(alias.c_str());
      std::error_code ec;
      fs::remove_all(Long(base), ec);
    }
  } cleanup{base, alias};

  const auto config = alias / "title.toml";
  REQUIRE(config.native().size() < MAX_PATH);
  REQUIRE(fs::canonical(config).native().size() >= MAX_PATH);
  rex::codegen::RecompilerConfig cfg;
  REQUIRE(cfg.Load(config.string()));
  CHECK(cfg.functions.contains(0x82000010));
  REQUIRE(cfg.loadedFiles.size() == 2);
  CHECK(fs::path(cfg.loadedFiles[0]) == config);
  CHECK(fs::path(cfg.loadedFiles[1]) == alias / "analysis" / "hints.toml");
  // A cycle is still found by canonical identity, whatever the spelling.
  std::ofstream(Long(deep / "cycle.toml")) << "file_path = \"default.xex\"\nincludes = [\"cycle.toml\"]\n";
  rex::codegen::RecompilerConfig cycle;
  CHECK_FALSE(cycle.Load((alias / "cycle.toml").string()));
#else
  SKIP("Windows MAX_PATH only");
#endif
}
