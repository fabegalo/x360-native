#include "catalog.h"
#include "action_trace.h"
#include "sha256.h"
#include "digest_batch.h"
#include "workspace_lease.h"
#include "host_platform.h"
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <set>

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
std::string CMakeLiteral(const fs::path& p) {
  const auto value = Path(p);
  std::string delimiter = "=";
  while (value.find("]" + delimiter + "]") != std::string::npos) delimiter += '=';
  return "[" + delimiter + "[" + value + "]" + delimiter + "]";
}
void Write(const fs::path& file, const std::string& text, const fs::path& root,
           const std::function<void()>& before_write = {}) {
  auto rel = fs::weakly_canonical(file).lexically_relative(fs::weakly_canonical(root));
  if (rel.empty() || rel.is_absolute() || *rel.begin() == "..") throw std::runtime_error("AOT project path escaped workspace");
  for (auto p = file; p != root && !p.empty(); p = p.parent_path())
    if (fs::is_symlink(p)) throw std::runtime_error("AOT project contains a redirected path");
  if (fs::exists(file) && ReadText(file) == text) return; // preserve stamps for unchanged configuration
  if (before_write) before_write();
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
  ActionTrace trace("preparation.project");
  if (!t.generic_aot || !IsSafeLabel(t.id) || t.inputs.empty()) throw std::runtime_error("Not an imported generic AOT title");
  auto validation = std::make_unique<DigestBatch>();
  if (!Inspect(paths, t).inputs_ok) throw std::runtime_error("Imported content changed; cannot prepare this identity");
  const auto identities = IdentifyContent({}, t.game_root / "default.xex");
  if (identities.size() != 1 || !identities[0].valid || identities[0].title_id != t.title_id || identities[0].version != t.version)
    throw std::runtime_error("XEX identity changed before project generation");
  // Inspect the verified inventory, including XEX images disguised as .dll.
  // Never search sibling content roots or trust the catalog's older module list.
  std::vector<ContentIdentity> modules;
  std::set<std::string> target_names{"default"}, guest_paths;
  auto inputs = t.inputs;
  std::sort(inputs.begin(), inputs.end(), [](const auto& a, const auto& b) { return a.destination < b.destination; });
  for (const auto& input : inputs) {
    const auto relative = input.destination.lexically_relative(t.game_root);
    const auto resolved = fs::weakly_canonical(input.destination).lexically_relative(fs::weakly_canonical(t.game_root));
    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
        resolved.empty() || resolved.is_absolute() || *resolved.begin() == "..")
      throw std::runtime_error("Invalid AOT module path");
    auto guest = Path(relative);
    std::transform(guest.begin(), guest.end(), guest.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; });
    if (!guest_paths.insert(guest).second) throw std::runtime_error("Duplicate case-insensitive AOT path: " + guest);
    if (guest == "default.xex") continue;
    // A disc's reserved root contains console update payloads, not title
    // dependencies. Keep every byte in the authenticated input inventory, but
    // never register firmware modules in the game's native module registry.
    if (guest.starts_with("$systemupdate/")) continue;
    std::ifstream in(input.destination, std::ios::binary);
    char magic[4]{}; in.read(magic, 4);
    if (std::string_view(magic, 3) != "XEX") continue;
    auto found = IdentifyContent({}, input.destination);
    if (found.size() != 1 || !found[0].valid || found[0].sha256 != input.sha256)
      throw std::runtime_error("Unidentified AOT dependency: " + guest);
    auto& module = found[0];
    // Only the loader's bounded data-only metadata gate proves this is a resource.
    if (module.resource_only) continue;
    if (!(module.module_flags & 8) || !module.image_base || !module.image_size)
      throw std::runtime_error("Unsupported AOT dependency identity: " + guest);
    // Match the pinned SDK's target derivation exactly. Ambiguous basenames must
    // fail rather than overwrite another module's native library or output.
    auto name = Utf8(module.file.stem());
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ' ', '_');
    if (!std::regex_match(name, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
      throw std::runtime_error("Unsupported AOT module target: " + guest);
    auto folded = name;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; });
    if (!target_names.insert(folded).second) throw std::runtime_error("Ambiguous AOT module target: " + guest);
    modules.push_back(std::move(module));
  }
  // The original inventory and XEX identities were authenticated in one protected
  // transaction. Release it before changing any generated project files.
  validation.reset();
  WorkspaceLease lease(paths);
  const auto dir = paths.local() / "aot" / t.id;
  const auto generated = t.exe.parent_path() / "generated/default";
  const auto owner_file = t.exe.parent_path() / ".x360n-aot-owner";
  const auto owner = t.id + "\n" + Utf8(t.game_root.filename()) + "\n" +
      (HostPlatform()=="windows-x64" ? "" : HostPlatform()+"\n");
  if (fs::exists(owner_file) && ReadText(owner_file) != owner)
    throw std::runtime_error("AOT build directory belongs to another content identity; existing files were preserved.");
  if (!fs::exists(owner_file) && fs::is_directory(t.exe.parent_path()) &&
      fs::directory_iterator(t.exe.parent_path()) != fs::directory_iterator())
    throw std::runtime_error("AOT build directory has no verified owner; existing files were preserved.");
  bool invalidated = false;
  auto publish = [&](const fs::path& file, const std::string& text) {
    Write(file, text, paths.local(), [&] {
      if (invalidated) return;
      const auto marker = PreparationMarker(paths, t);
      fs::create_directories(marker.parent_path());
      Write(marker, "project-generation\n", paths.local());
      invalidated = true;
    });
  };
  publish(owner_file, owner);
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
  toml::array module_tables;
  // Make original code inputs visible to the pre-codegen snapshot, including
  // the very first preparation before a codegen depfile exists.
  std::string module_cmake = "set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS " +
      CMakeLiteral(t.game_root / "default.xex") + ")\n";
  const auto analysis_file = paths.root / "titles/common/module_analysis.toml";
  const auto analysis = fs::is_regular_file(analysis_file) ? toml::parse_file(analysis_file.string()) : toml::table{};
  for (size_t i = 0; i < modules.size(); ++i) {
    const auto& module = modules[i];
    auto name = Utf8(module.file.stem());
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ' ', '_');
    const auto output = generated.parent_path() / ("module_" + std::to_string(i));
    const auto relative = Path(module.file.lexically_relative(t.game_root));
    toml::table module_table{{"file_path", Path(module.file)}, {"guest_path", relative}, {"out_directory_path", Path(output)}};
    // Optional analysis is bound to the complete original module hash, never
    // the title ID, a basename or a presumed compatible runtime version.
    if (const auto profile = analysis["modules"][module.sha256]; profile) {
      if (!profile.is_table() || !profile["functions"].is_table())
        throw std::runtime_error("Invalid hash-bound AOT module analysis: " + module.sha256);
      module_table.insert("functions", *profile["functions"].as_table());
    }
    module_tables.push_back(std::move(module_table));
    setup["files"].as_array()->push_back(toml::table{{"path", relative}, {"source_name", Utf8(module.file.filename())},
      {"sha256", module.sha256}});
    module_cmake += "set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS " + CMakeLiteral(module.file) + ")\n";
    module_cmake += "x360n_add_aot_module(" + t.id + "_" + name + " " + CMakeLiteral(output) + ")\n";
  }
  if (!modules.empty()) manifest.insert("modules", std::move(module_tables));
  if (!t.library.title_id.empty()) setup.insert("library", toml::table{{"title_id", t.library.title_id},
    {"content_type", t.library.content_type}, {"file_name", t.library.file_name},
    {"display_name", t.library.display_name}, {"source_sha256", t.library.source_sha256}});
  std::string inventory;
  for (const auto& input : t.inputs)
    inventory += input.sha256 + "\t" + Path(input.destination.lexically_relative(t.game_root)) + "\n";
  publish(dir / "assets.sha256", inventory);
  auto manifest_text = Toml(manifest);
  // The pinned codegen's version updater canonicalizes this one assignment.
  // Emit its spelling up front so the producer cannot mutate a certified input.
  Replace(manifest_text, "sdk_version = '0.10.0'", "sdk_version = \"0.10.0\"");
  publish(t.manifest, manifest_text);
  publish(dir / "preparation.toml", Toml(setup));
  // This pinned SDK template has only scalar substitutions and cmake_var calls.
  // Reject unfamiliar directives instead of silently emitting a broken project.
  auto integration = ReadText(paths.root / "external/rexglue-sdk/resources/templates/init/rexglue_cmake.inja");
  // The pinned SDK's DLL template assumes source-relative output directories.
  // Keep its registry and codegen intact; wire compact absolute outputs below.
  const auto dll_section = integration.find("# Include DLL module shared library targets");
  if (dll_section == std::string::npos) throw std::runtime_error("Unsupported pinned SDK DLL integration");
  integration.erase(dll_section);
  integration += "include(\"${CMAKE_CURRENT_SOURCE_DIR}/generated/x360n_modules.cmake\")\n";
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
  // The pinned CLI refreshes generated/rexglue.cmake unconditionally from its
  // source-relative template. Keep our absolute-output integration separate so
  // that codegen cannot undo the compact paths before the second configure.
  publish(dir / "generated/x360n_rexglue.cmake", integration);
  auto module_integration = ReadText(paths.root / "titles/common/generic/modules.cmake");
  Replace(module_integration, "@TITLE@", t.id);
  publish(dir / "generated/x360n_modules.cmake", module_integration + module_cmake);
  for (const auto& name : {"CMakeLists.txt", "main.cpp"}) {
    auto text = ReadText(paths.root / "titles/common/generic" / name);
    Replace(text, "@TITLE@", t.id);
    // CMake bracket arguments avoid interpolation of imported display names.
    Replace(text, "@ROOT@", "${CMAKE_CURRENT_SOURCE_DIR}/../../..");
    Replace(text, "@GENERATED@", Path(generated));
    Replace(text, "@HOST_DISPATCH@", modules.empty() ? "0" : "1");
    publish(dir / name, text);
  }
  using J = nlohmann::json;
#ifdef _WIN32
  const auto preset_name=HostPlatform()=="windows-x64" ? "x360n-win-amd64" : "x360n-"+HostPlatform();
  const auto llvm=paths.local()/"toolchains"/(HostPlatform()=="windows-x64" ? "llvm-22.1.8" : "llvm-22.1.8-"+HostPlatform());
  const auto python=paths.local()/"toolchains/python-3.14.8/python.exe";
#else
  const auto preset_name="x360n-"+HostPlatform();
  const auto llvm=paths.local()/"toolchains"/("llvm-22.1.8-"+HostPlatform());
  const auto python=FindHostTool("python3");
#endif
  J preset{{"version", 6}, {"configurePresets", J::array({{
    {"name", preset_name}, {"generator", "Ninja"}, {"binaryDir", Path(t.exe.parent_path())},
    {"cacheVariables", {{"CMAKE_BUILD_TYPE", "Release"},
      {"CMAKE_C_COMPILER", Path(llvm / ("bin/clang"+ExecutableSuffix()))},
      {"CMAKE_CXX_COMPILER", Path(llvm / ("bin/clang++"+ExecutableSuffix()))},
      {"CMAKE_C_FLAGS", HostPlatform().ends_with("-x64") ? "-march=x86-64-v2" : ""}, {"CMAKE_CXX_FLAGS", HostPlatform().ends_with("-x64") ? "-march=x86-64-v2" : ""},
      {"REXSDK_DIR", Path(paths.root / "external/rexglue-sdk")},
      // Vulkan is built beside D3D12; Python only runs the SDK's build-time generators.
      {"REXGLUE_USE_VULKAN", "ON"},
      {"PYTHON_EXECUTABLE", Path(python)}}}
  }})}};
  publish(dir / "CMakePresets.json", preset.dump(2) + "\n");
}
}
