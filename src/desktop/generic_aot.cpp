#include "catalog.h"
#include "sha256.h"
#include "workspace_lease.h"
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <regex>

namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Missing AOT infrastructure: " + Utf8(file));
  return {std::istreambuf_iterator<char>(in), {}};
}
void Replace(std::string& s, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
}
std::string Path(const fs::path& p) {
  const auto u = p.generic_u8string(); return {u.begin(), u.end()};
}
void Write(const fs::path& file, const std::string& text, const fs::path& root) {
  auto rel = fs::weakly_canonical(file).lexically_relative(fs::weakly_canonical(root));
  if (rel.empty() || rel.is_absolute() || *rel.begin() == "..") throw std::runtime_error("AOT project path escaped workspace");
  for (auto p = file; p != root && !p.empty(); p = p.parent_path())
    if (fs::is_symlink(p)) throw std::runtime_error("AOT project contains a redirected path");
  if (fs::exists(file) && ReadText(file) == text) return; // preserve stamps for unchanged configuration
  fs::create_directories(file.parent_path());
  auto temp = file; temp += ".tmp";
  if (fs::is_symlink(temp)) throw std::runtime_error("AOT staging file redirected");
  std::ofstream out(temp, std::ios::binary); out << text; out.close();
  if (!out) throw std::runtime_error("Cannot write AOT project");
  // The incomplete marker and absent final receipt make partial project writes
  // non-executable. A retry deterministically repairs every generated file.
  fs::copy_file(temp, file, fs::copy_options::overwrite_existing); fs::remove(temp);
}
std::string Toml(const toml::table& t) { std::ostringstream out; out << t; return out.str() + "\n"; }
}

void GenerateAotProject(const ProjectPaths& paths, const Target& t) {
  if (!t.generic_aot || !IsSafeLabel(t.id) || t.inputs.empty()) throw std::runtime_error("Not an imported generic AOT title");
  if (!Inspect(paths, t).inputs_ok) throw std::runtime_error("Imported content changed; cannot prepare this identity");
  const auto identities = IdentifyContent({}, t.game_root / "default.xex");
  if (identities.size() != 1 || !identities[0].valid || identities[0].title_id != t.title_id || identities[0].version != t.version)
    throw std::runtime_error("XEX identity changed before project generation");
  WorkspaceLease lease(paths);
  const auto dir = paths.local() / "aot" / t.id;
  const auto generated = t.exe.parent_path() / "generated/default";
  const auto owner_file = t.exe.parent_path() / ".x360n-aot-owner";
  const auto owner = t.id + "\n" + Utf8(t.game_root.filename()) + "\n";
  if (fs::exists(owner_file) && ReadText(owner_file) != owner)
    throw std::runtime_error("AOT build directory belongs to another content identity; existing files were preserved.");
  if (!fs::exists(owner_file) && fs::is_directory(t.exe.parent_path()) &&
      fs::directory_iterator(t.exe.parent_path()) != fs::directory_iterator())
    throw std::runtime_error("AOT build directory has no verified owner; existing files were preserved.");
  Write(owner_file, owner, paths.local());
  fs::create_directories(PreparationMarker(paths, t).parent_path());
  Write(PreparationMarker(paths, t), "project-generation\n", paths.local());
  const auto xex_hash = identities[0].sha256;
  toml::table manifest{
    {"project", toml::table{{"name", t.id}, {"sdk_version", "0.10.0"}, {"game_root", Path(t.game_root)}}},
    {"entrypoint", toml::table{{"file_path", Path(t.game_root / "default.xex")}, {"out_directory_path", Path(generated)}}},
    {"x360n", toml::table{{"xex_sha256", xex_hash}, {"native_exe", Path(t.exe)},
      {"user_data_root", Path(paths.local() / "user" / t.id)}, {"args", toml::array{"--gpu_plugin=xenos", "--mnk_mode=true"}}}}
  };
  toml::table setup{{"label", t.label}, {"label_en", t.label_en}, {"limitations", t.limitations},
    {"limitations_en", t.limitations_en}, {"kind", "game"}, {"session", false}, {"importable", false},
    {"inventory", "assets.sha256"},
    {"files", toml::array{toml::table{{"path", "default.xex"}, {"source_name", "default.xex"}, {"sha256", xex_hash}}}}};
  if (!t.library.title_id.empty()) setup.insert("library", toml::table{{"title_id", t.library.title_id},
    {"content_type", t.library.content_type}, {"file_name", t.library.file_name},
    {"display_name", t.library.display_name}, {"source_sha256", t.library.source_sha256}});
  std::string inventory;
  for (const auto& input : t.inputs)
    inventory += input.sha256 + "\t" + Path(input.destination.lexically_relative(t.game_root)) + "\n";
  Write(dir / "assets.sha256", inventory, paths.local());
  auto manifest_text = Toml(manifest);
  // The pinned codegen's version updater canonicalizes this one assignment.
  // Emit its spelling up front so the producer cannot mutate a certified input.
  Replace(manifest_text, "sdk_version = '0.10.0'", "sdk_version = \"0.10.0\"");
  Write(t.manifest, manifest_text, paths.local());
  Write(dir / "preparation.toml", Toml(setup), paths.local());
  // This pinned SDK template has only scalar substitutions and cmake_var calls.
  // Reject unfamiliar directives instead of silently emitting a broken project.
  auto integration = ReadText(paths.root / "external/rexglue-sdk/resources/templates/init/rexglue_cmake.inja");
  Replace(integration, "{{ cmake_var(\"CMAKE_CURRENT_SOURCE_DIR\") }}/{{ entrypoint_out_dir }}",
      "{{ cmake_var(\"X360N_GENERATED_ROOT\") }}");
  for (const auto& file : {"sources.cmake", "dll_targets.cmake"})
    Replace(integration, std::string("include({{ entrypoint_out_dir }}/") + file + ")",
        std::string("include(\"{{ cmake_var(\"X360N_GENERATED_ROOT\") }}/") + file + "\")");
  integration = std::regex_replace(integration, std::regex(R"re(\{\{ cmake_var\("([A-Za-z0-9_]+)"\) \}\})re"), "$${$1}");
  Replace(integration, "{{ names.snake_case }}", t.id);
  Replace(integration, "{{ entrypoint_out_dir }}", "generated/default");
  Replace(integration, "{{ sdk_version }}", "0.10.0");
  if (integration.find("{{") != std::string::npos || integration.find("{%") != std::string::npos)
    throw std::runtime_error("Unsupported pinned SDK project template");
  Write(dir / "generated/rexglue.cmake", integration, paths.local());
  for (const auto& name : {"CMakeLists.txt", "main.cpp"}) {
    auto text = ReadText(paths.root / "titles/common/generic" / name);
    Replace(text, "@TITLE@", t.id);
    // CMake bracket arguments avoid interpolation of imported display names.
    Replace(text, "@ROOT@", "${CMAKE_CURRENT_SOURCE_DIR}/../../..");
    Replace(text, "@GENERATED@", Path(generated));
    Write(dir / name, text, paths.local());
  }
  using J = nlohmann::json;
  J preset{{"version", 6}, {"configurePresets", J::array({{
    {"name", "x360n-win-amd64"}, {"generator", "Ninja"}, {"binaryDir", Path(t.exe.parent_path())},
    {"cacheVariables", {{"CMAKE_BUILD_TYPE", "RelWithDebInfo"},
      {"CMAKE_C_COMPILER", Path(paths.local() / "toolchains/llvm-22.1.8/bin/clang.exe")},
      {"CMAKE_CXX_COMPILER", Path(paths.local() / "toolchains/llvm-22.1.8/bin/clang++.exe")},
      {"CMAKE_C_FLAGS", "-march=x86-64-v2"}, {"CMAKE_CXX_FLAGS", "-march=x86-64-v2"},
      {"REXSDK_DIR", Path(paths.root / "external/rexglue-sdk")}}}
  }})}};
  Write(dir / "CMakePresets.json", preset.dump(2) + "\n", paths.local());
}
}
