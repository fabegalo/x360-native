#pragma once
#include "preparation.h"
#include "report.h"
#include "sha256.h"
#include <fstream>
#include <set>

// Synthetic receipts only: never signs a real title's existing artifacts.
inline void WriteTestReceipt(const x360n::ProjectPaths& paths, const std::string& title,
    const std::filesystem::path& manifest, const std::filesystem::path& exe,
    std::vector<std::filesystem::path> inputs) {
  namespace fs = std::filesystem;
  using namespace x360n;
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
  for (const auto& file : fs::directory_iterator(exe.parent_path())) if (file.path().extension() == ".dll")
    out.BeginObject().String("path", PathUtf8(file.path())).String("sha256", DigestFile(file.path())->sha256).EndObject();
  out.EndArray().EndObject();
  std::ofstream(PreparationReceipt(exe), std::ios::binary) << out.Take();
}
