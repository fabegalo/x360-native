#pragma once
#include "preparation.h"
#include "report.h"
#include "sha256.h"
#include "host_platform.h"
#include <fstream>
#include <set>

// Fixture executables are text. Windows accepts them; Linux/macOS validate the
// native image header of a prepared title, so the fixture prefixes one (ELF or
// Mach-O for this host). Distinct fixture contents stay distinct.
inline void MakeFixtureNativeImage(const std::filesystem::path& exe) {
#ifndef _WIN32
  namespace fs = std::filesystem;
  const auto executable = [&] {
    fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add);
  };
  try { x360n::ValidateNativeImage(exe); executable(); return; } catch (const std::exception&) {}
  std::ifstream in(exe, std::ios::binary);
  const std::string body{std::istreambuf_iterator<char>(in), {}};
  in.close();
  std::string header(64, '\0');
  const bool arm64 = x360n::HostPlatform().ends_with("-arm64");
  auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + size_t(i)] = char(v >> (8 * i)); };
#ifdef __APPLE__
  put32(0, 0xFEEDFACF); put32(4, arm64 ? 0x0100000C : 0x01000007);
#else
  header[0] = 0x7F; header[1] = 'E'; header[2] = 'L'; header[3] = 'F'; header[4] = 2; header[5] = 1;
  header[18] = char(arm64 ? 183 : 62);
#endif
  std::ofstream(exe, std::ios::binary | std::ios::trunc) << header << body;
  executable();
#else
  (void)exe;
#endif
}
// Synthetic receipts only: never signs a real title's existing artifacts.
inline void WriteTestReceipt(const x360n::ProjectPaths& paths, const std::string& title,
    const std::filesystem::path& manifest, const std::filesystem::path& exe,
    std::vector<std::filesystem::path> inputs) {
  namespace fs = std::filesystem;
  using namespace x360n;
  MakeFixtureNativeImage(exe);
  const auto lock = paths.root / "deps.lock.json";
  if (!fs::exists(lock)) std::ofstream(lock) << "fixture lock";
  fs::create_directories(paths.sdk() / ".git");
  std::ofstream(paths.sdk() / ".git/HEAD") << std::string(40, 'a') << '\n';
  inputs.push_back(lock); inputs.push_back(manifest);
  auto registration = manifest.parent_path() / "preparation.toml";
  if (fs::exists(registration)) inputs.push_back(registration);
  JsonWriter out;
  out.BeginObject().Number("schema", 1).String("title", title)
      .String("manifest", PathUtf8(fs::absolute(manifest))).String("exe", PathUtf8(fs::absolute(exe)))
      .String("sdk_commit", std::string(40, 'a'));
  out.BeginObject("roles").String("compiler", PathUtf8(lock)).String("codegen", PathUtf8(lock))
      .String("build_config", PathUtf8(lock)).EndObject();
  out.BeginArray("globs").EndArray();
  out.BeginArray("inputs");
  for (const auto& path : std::set<fs::path>(inputs.begin(), inputs.end()))
    out.BeginObject().String("path", PathUtf8(fs::absolute(path))).String("sha256", DigestFile(path)->sha256).EndObject();
  out.EndArray().BeginArray("artifacts");
  out.BeginObject().String("path", PathUtf8(fs::absolute(exe))).String("sha256", DigestFile(exe)->sha256).EndObject();
  for (const auto& file : fs::directory_iterator(exe.parent_path())) if (file.path().extension() == ".dll" || IsNativeModule(file.path()) || file.path().filename()=="x360n_gpu_preflight"+ExecutableSuffix())
    out.BeginObject().String("path", PathUtf8(file.path())).String("sha256", DigestFile(file.path())->sha256).EndObject();
  out.EndArray().EndObject();
  std::ofstream(PreparationReceipt(exe), std::ios::binary) << out.Take();
}
