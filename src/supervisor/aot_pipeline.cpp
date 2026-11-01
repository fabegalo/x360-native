#include "aot_pipeline.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <toml++/toml.hpp>

#include "child_process.h"
#include "host_platform.h"
#include "preparation.h"
#include "sha256.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace x360n {
namespace fs = std::filesystem;
namespace {
using J = nlohmann::json;
using Files = std::map<std::string, std::string>;
std::string Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot read preparation input: " +
                             PathUtf8(file));
  return {std::istreambuf_iterator<char>(in), {}};
}
J Json(const fs::path& file) { return J::parse(Read(file)); }
std::string Path(const fs::path& file) {
  return PathUtf8(fs::absolute(file).lexically_normal());
}
std::string Hash(const std::string& text) {
  return PlatformSha256Hex(
      {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}
std::string Digest(const fs::path& file) {
  const auto value = DigestFile(file);
  if (!value)
    throw std::runtime_error("Missing/unreadable preparation input: " +
                             Path(file));
  return value->sha256;
}
void Record(Files& files, const fs::path& path) {
  const auto absolute = Path(CanonicalPath(path));
  if (!files.contains(absolute)) files[absolute] = Digest(path);
}
bool Verify(const Files& files) {
  for (const auto& [path, hash] : files) {
    const auto actual = DigestFile(PathFromUtf8(path));
    if (!actual || actual->sha256 != hash) return false;
  }
  return true;
}
bool VerifyProducts(const J& state) {
  if (!state.contains("products") || !state.at("products").is_array() ||
      state.at("products").empty())
    return false;
  for (const auto& product : state.at("products")) {
    const auto actual = DigestFile(PathFromUtf8(product.at("path")));
    if (!actual || actual->sha256 != product.at("sha256").get<std::string>())
      return false;
  }
  return true;
}
void Tree(Files& files, const fs::path& root) {
  if (!fs::is_directory(root)) return;
  for (auto it = fs::recursive_directory_iterator(root);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto name = it->path().filename();
    if (it->is_directory() && (name == ".git" || name == "out" ||
                               name == ".local" || name == "generated")) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_symlink()) {
      // SDK uses file symlinks in vendored sources; directory links could
      // silently import another SDK/version and are never traversed.
      if (it->is_directory())
        throw std::runtime_error("Source directory link refused: " +
                                 Path(it->path()));
    }
    if (it->is_regular_file() && name != ".git") Record(files, it->path());
  }
}
void Publish(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  const auto temporary =
      file.parent_path() /
      (PathUtf8(file.filename()) + "." + std::to_string(CurrentProcessId()) +
       "-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".new");
  if (fs::exists(temporary))
    throw std::runtime_error("Unfinished publication preserved: " +
                             Path(temporary));
  {
    std::ofstream out(temporary, std::ios::binary);
    out.write(text.data(), std::streamsize(text.size()));
    out.flush();
    if (!out) throw std::runtime_error("Cannot write preparation checkpoint");
  }
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), file.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error(
        "Cannot atomically publish preparation checkpoint");
#else
  int fd = open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0 || fsync(fd) != 0) {
    if (fd >= 0) close(fd);
    throw std::runtime_error("Cannot flush preparation checkpoint");
  }
  close(fd);
  fs::rename(temporary, file);
  fd = open(file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0 || fsync(fd) != 0) {
    if (fd >= 0) close(fd);
    throw std::runtime_error("Cannot flush preparation directory");
  }
  close(fd);
#endif
}
J Entries(const Files& files) {
  J result = J::array();
  for (const auto& [path, hash] : files)
    result.push_back({{"path", path}, {"sha256", hash}});
  return result;
}
Files Entries(const J& entries) {
  Files result;
  for (const auto& entry : entries) {
    auto path = entry.at("path").get<std::string>(),
         hash = entry.at("sha256").get<std::string>();
    if (!PathFromUtf8(path).is_absolute() || hash.size() != 64 ||
        !result.emplace(path, hash).second)
      throw std::runtime_error("Invalid checkpoint entry");
  }
  return result;
}
std::map<std::string, std::string> Cache(const fs::path& build) {
  std::map<std::string, std::string> result;
  std::istringstream input(Read(build / "CMakeCache.txt"));
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':'), equals = line.find('=');
    if (colon != std::string::npos && equals > colon &&
        equals != std::string::npos)
      result[line.substr(0, colon)] = line.substr(equals + 1);
  }
  return result;
}
bool NativeProduct(const fs::path& file) {
  const auto ext = PathUtf8(file.extension());
  return ext.empty() || ext == ".o" || ext == ".obj" || ext == ".a" ||
         ext == ".lib" || ext == ".res" || ext == ".pch" || ext == ".exe" ||
         IsNativeModule(file);
}
struct Graph {
  std::map<std::string, fs::path> nodes;
  std::set<std::string> rules;
  std::map<std::string, std::vector<std::string>> parents;
};
Graph ParseGraph(const fs::path& build, const std::string& text) {
  const std::regex node(
      R"n(^"([^"]+)" \[label="((?:\\.|[^"\\])*)"(, shape=ellipse)?\]$)n");
  const std::regex edge(R"n(^"([^"]+)" -> "([^"]+)"(.*)$)n");
  Graph result;
  std::istringstream lines(text);
  std::string line;
  std::smatch match;
  while (std::getline(lines, line)) {
    if (std::regex_match(line, match, node)) {
      if (match[3].matched)
        result.rules.insert(match[1]);
      else {
        auto name = J::parse("\"" + match[2].str() + "\"").get<std::string>();
        result.nodes[match[1]] =
            fs::absolute(build / PathFromUtf8(name)).lexically_normal();
      }
    } else if (std::regex_match(line, match, edge) &&
               match[3].str().find("style=dotted") == std::string::npos)
      result.parents[match[2]].push_back(match[1]);
  }
  if (result.nodes.empty())
    throw std::runtime_error("Ninja returned an empty dependency graph");
  return result;
}
class Driver {
 public:
  AotOptions options;
  AotResult result;
  fs::path project, manifest, build, marker, tool, sdk_export, xex_keys,
      symbol_tool;
  J locked, identity;
  Files static_inputs, sdk_inputs;
  unsigned serial = 0;
  std::string run =
      std::to_string(CurrentProcessId()) + "-" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());
  explicit Driver(AotOptions value) : options(std::move(value)) {}
  void Cancel() const {
    if (options.cancelled && options.cancelled())
      throw std::runtime_error("Preparation cancelled");
  }
  void Stage(const std::string& name) {
    Cancel();
    if (options.progress)
      options.progress("X360N_STAGE " + options.title + " " + name);
  }
  std::string Command(const fs::path& exe, std::vector<std::string> args,
                      const fs::path& directory, const std::string& phase) {
    Cancel();
    fs::create_directories(build);
    const auto log = build / ("x360n-build-" + phase + "." + run + "-" +
                              std::to_string(serial++) + ".log");
    ProcessSpec spec{exe,
                     std::move(args),
                     directory,
                     log,
                     PathFromUtf8(Path(log) + ".err"),
                     options.timeout};
    spec.no_console = true;
    spec.should_cancel = options.cancelled;
    spec.environment = {
        {"X360N_OWNED_BUILD", Path(directory)},
        {"CMAKE_BUILD_PARALLEL_LEVEL",
         std::to_string(std::max(
             1u, std::min(16u, std::thread::hardware_concurrency())))}};
    if (!xex_keys.empty())
      spec.environment.emplace_back("X360N_XEX_KEYS", Path(xex_keys));
    const auto executed = RunProcess(spec);
    ++result.commands;
    if (!executed.launched || executed.timed_out || executed.cancelled ||
        executed.exit_code || !executed.launch_error.empty())
      throw std::runtime_error("Preparation " + phase + " failed: " +
                               (executed.cancelled   ? std::string("cancelled")
                                : executed.timed_out ? std::string("timeout")
                                : executed.launch_error.empty()
                                    ? DescribeExitCode(executed.exit_code)
                                    : executed.launch_error) +
                               "; log: " + Path(log));
    auto output = Read(log);
    std::erase(output, '\r');
    return output;
  }
  std::string Ninja(const fs::path& dir, std::vector<std::string> args) {
    std::vector<std::string> command{"-C", Path(dir), "-t"};
    command.insert(command.end(), args.begin(), args.end());
    return Command(options.ninja, command, dir, "ninja-query");
  }
  Graph GraphFor(const fs::path& dir, const std::vector<std::string>& targets) {
    std::vector<std::string> args{"graph"};
    args.insert(args.end(), targets.begin(), targets.end());
    return ParseGraph(dir, Ninja(dir, args));
  }
  void Configure(const fs::path& source, const fs::path& output, bool bootstrap,
                 bool sdk = false) {
    Stage(sdk ? "sdk-configure" : bootstrap ? "1-configure" : "3-reconfigure");
    std::vector<std::string> args{"-S",         Path(source), "-B",
                                  Path(output), "-G",         "Ninja"};
    for (const auto& [key, value] : options.variables)
      args.push_back("-D" + key + "=" + value);
    // Re-evaluate the platform default instead of retaining the historical
    // implicit OFF in an existing title cache. Explicit presets/--define win.
    if (!sdk && !options.variables.contains("X360N_NATIVE_RENDERER"))
      args.push_back("-UX360N_NATIVE_RENDERER");
    args.insert(args.end(), {"-DCMAKE_BUILD_TYPE=Release",
                             "-DCMAKE_C_COMPILER=" + Path(options.cc),
                             "-DCMAKE_CXX_COMPILER=" + Path(options.cxx),
                             "-DCMAKE_MAKE_PROGRAM=" + Path(options.ninja),
                             "-DREXSDK_DIR=" + Path(options.sdk)});
    // Authenticate a stable guard snapshot instead of a mutable application
    // path. Relinking the launcher/version must not invalidate a shared SDK.
    const auto guard =
        output / ".preparation/supervisor" / ("x360n" + ExecutableSuffix());
    fs::create_directories(guard.parent_path());
    if (!fs::is_regular_file(guard) ||
        Digest(guard) != Digest(CurrentExecutablePath()))
      fs::copy_file(CurrentExecutablePath(), guard,
                    fs::copy_options::overwrite_existing);
    args.push_back("-DX360N_PROCESS_SUPERVISOR=" + Path(guard));
    if (sdk)
      args.push_back("-DX360N_ROOT=" + Path(options.paths.root));
    else {
      args.push_back(std::string("-DX360N_CODEGEN_BOOTSTRAP=") +
                     (bootstrap ? "ON" : "OFF"));
      if (!sdk_export.empty())
        args.push_back("-DX360N_SHARED_SDK_EXPORT=" + Path(sdk_export));
      else
        args.push_back("-UX360N_SHARED_SDK_EXPORT");
    }
    fs::create_directories(output);
    Command(options.cmake, args, output, "configure");
    if (Cache(output)["CMAKE_BUILD_TYPE"] != "Release")
      throw std::runtime_error("Preparation requires Release");
#ifdef _WIN32
    const auto directories = output / "CMakeFiles/TargetDirectories.txt";
    if (fs::is_regular_file(directories)) {
      std::istringstream lines(Read(directories));
      std::string line;
      while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto list = PathFromUtf8(line) / "exports.def.objs";
        if (!fs::is_regular_file(list)) continue;
        std::istringstream objects(Read(list));
        std::string entry, text;
        bool changed = false;
        while (std::getline(objects, entry)) {
          if (!entry.empty() && entry.back() == '\r') entry.pop_back();
          auto absolute = fs::absolute(output / PathFromUtf8(entry));
          if (!entry.empty() && !entry.starts_with("\\\\?\\") &&
              absolute.native().size() >= 260) {
            auto path = PathUtf8(absolute.make_preferred());
            entry = path.starts_with("\\\\") ? "\\\\?\\UNC\\" + path.substr(2)
                                             : "\\\\?\\" + path;
            changed = true;
          }
          text += entry + '\n';
        }
        if (changed) Publish(list, text);
      }
    }
#endif
  }
  J Commands(const fs::path& dir) {
    auto commands = J::parse(Ninja(dir, {"compdb", "-x"}));
    for (const auto& command : commands) {
      const auto text = command.at("command").get<std::string>();
      const auto output = command.value("output", std::string{});
      if (output.ends_with(".o") || output.ends_with(".obj")) {
        if (text.find("NDEBUG") == std::string::npos ||
            std::regex_search(text,
                              std::regex(R"((^|\s)(-O0|/Od|/MDd|/MTd)(\s|$))")))
          throw std::runtime_error("Non-Release compiler command refused");
      }
      // Response files are not approved by an opaque command string.
      if (std::regex_search(text, std::regex(R"((^|\s)@)")))
        throw std::runtime_error("Untracked compiler response file refused");
    }
    return commands;
  }
  Files Closure(const fs::path& dir, const std::vector<std::string>& targets) {
    Files inputs;
    auto graph = GraphFor(dir, targets);
    for (const auto& [id, file] : graph.nodes)
      if (fs::is_regular_file(file) && !NativeProduct(file))
        Record(inputs, file);
    if (targets == std::vector<std::string>{"build.ninja"}) {
      Record(inputs, dir / "CMakeCache.txt");
      return inputs;
    }
    // Include every actual compiler dependency, deduplicating names before IO.
    std::istringstream deps(Ninja(dir, {"deps"}));
    std::string line;
    std::set<std::string> unique;
    while (std::getline(deps, line))
      if (line.starts_with("    ")) unique.insert(line.substr(4));
    for (const auto& file : unique)
      if (fs::is_regular_file(dir / PathFromUtf8(file)))
        Record(inputs, dir / PathFromUtf8(file));
    auto configure = GraphFor(dir, {"build.ninja"});
    for (const auto& [id, file] : configure.nodes)
      if (fs::is_regular_file(file)) Record(inputs, file);
    Record(inputs, dir / "CMakeCache.txt");
    return inputs;
  }
  J Products(const fs::path& dir) {
    auto graph = GraphFor(dir, {});
    auto commands = Commands(dir);
    std::map<std::string, std::string> command_by_path, rule_command;
    for (const auto& command : commands)
      if (command.contains("output"))
        command_by_path[Path(dir / PathFromUtf8(command["output"]))] =
            command.at("command");
    for (const auto& [id, file] : graph.nodes)
      if (command_by_path.contains(Path(file)))
        for (const auto& parent : graph.parents[id])
          if (graph.rules.contains(parent))
            rule_command[parent] = command_by_path[Path(file)];
    for (const auto& [id, file] : graph.nodes)
      for (const auto& parent : graph.parents[id])
        if (rule_command.contains(parent))
          command_by_path[Path(file)] = rule_command[parent];
    std::map<std::string, std::set<fs::path>> headers;
    std::istringstream dependencies(Ninja(dir, {"deps"}));
    std::string line, selected;
    while (std::getline(dependencies, line)) {
      const auto end = line.find(": #deps ");
      if (end != std::string::npos)
        selected = Path(dir / PathFromUtf8(line.substr(0, end)));
      else if (!selected.empty() && line.starts_with("    "))
        headers[selected].insert(
            fs::absolute(dir / PathFromUtf8(line.substr(4)))
                .lexically_normal());
    }
    J products = J::array();
    Files snapshot;
    for (const auto& [id, file] : graph.nodes) {
      if (!NativeProduct(file) || !command_by_path.contains(Path(file)) ||
          !fs::is_regular_file(file))
        continue;
      if (!IsWithin(file, dir))
        throw std::runtime_error("Native product escaped its build owner: " +
                                 Path(file));
      const auto command = command_by_path[Path(file)];
      if (command.find(" codegen ") != std::string::npos) continue;
      const bool recorded_headers = headers.contains(Path(file));
      std::set<fs::path> inputs = headers[Path(file)];
      for (const auto& parent : graph.parents[id]) {
        auto parents = graph.rules.contains(parent)
                           ? graph.parents[parent]
                           : std::vector<std::string>{parent};
        for (const auto& input : parents)
          if (graph.nodes.contains(input) &&
              fs::is_regular_file(graph.nodes[input]))
            inputs.insert(graph.nodes[input]);
      }
      if ((file.extension() == ".o" || file.extension() == ".obj") &&
          !recorded_headers)
        throw std::runtime_error("Compiler dependency record missing");
      Files entries;
      for (const auto& input : inputs) {
        if (!fs::is_regular_file(input)) {
          entries[Path(input)] = "missing";
          continue;
        }
        Record(snapshot, input);
        const auto key = Path(CanonicalPath(input));
        entries[key] = snapshot.at(key);
      }
      products.push_back({{"path", Path(file)},
                          {"signature", Hash(J{{"command", command},
                                               {"inputs", entries},
                                               {"toolchain", identity}}
                                                 .dump())},
                          {"sha256", Digest(file)}});
    }
    return products;
  }
  void Invalidate(const J& previous) {
    auto current = Products(build);
    std::map<std::string, J> old;
    for (const auto& product : previous.at("products"))
      old[product.at("path")] = product;
    for (const auto& product : current) {
      const auto path = product.at("path").get<std::string>();
      if (!old.contains(path) || old[path] != product) {
        const auto file = PathFromUtf8(path);
        if (!NativeProduct(file) || !IsWithin(file, build) ||
            fs::is_symlink(file))
          throw std::runtime_error("Unsafe incremental product");
        fs::remove(file);
      }
    }
  }
  void Build(const fs::path& output, std::vector<std::string> targets) {
    std::vector<std::string> args{
        "--build", Path(output), "--parallel",
        std::to_string(
            std::max(1u, std::min(16u, std::thread::hardware_concurrency())))};
    if (!targets.empty()) {
      args.push_back("--target");
      args.insert(args.end(), targets.begin(), targets.end());
    }
    Command(options.cmake, args, output, "build");
  }
  void ModuleSymbols(const fs::path& file,
                     const std::vector<std::string>& required) {
    if (required.empty()) return;
#ifdef _WIN32
    const auto output = Command(symbol_tool, {"--coff-exports", Path(file)},
                                build, "module-exports");
    std::set<std::string> symbols;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
      const auto name = line.find("Name: ");
      if (name != std::string::npos) symbols.insert(line.substr(name + 6));
    }
#else
    std::vector<std::string> args{"--defined-only", "--extern-only",
                                  "--format=posix"};
#ifndef __APPLE__
    args.push_back("--dynamic");
#endif
    args.push_back(Path(file));
    const auto output = Command(symbol_tool, args, build, "module-exports");
    std::set<std::string> symbols;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
      const auto end = line.find(' ');
      auto name = line.substr(0, end);
#ifdef __APPLE__
      if (name.starts_with('_')) name.erase(0, 1);
#endif
      const auto version = name.find('@');
      if (version != std::string::npos) name.resize(version);
      symbols.insert(name);
    }
#endif
    for (const auto& name : required)
      if (!symbols.contains(name))
        throw std::runtime_error(
            "Native module ABI export missing: " + Path(file) + " : " + name);
  }
  void Init();
  void SharedSdk();
  AotResult Run();
};
bool VerifyInventory(const J& globs) {
  try {
    for (const auto& glob : globs) {
      const auto pattern = glob.at("pattern").get<std::string>();
      std::string expression =
          glob.at("recursive").get<bool>() ? "([^/]*/)*" : "";
      for (const char c : pattern) {
        if (c == '*')
          expression += "[^/]*";
        else if (c == '?')
          expression += "[^/]";
        else {
          if (std::string(".\\+(){}[]^$|").find(c) != std::string::npos)
            expression += '\\';
          expression += c;
        }
      }
      const std::regex matcher(expression, std::regex::icase);
      const auto directory = PathFromUtf8(glob.at("directory"));
      std::vector<std::string> actual;
      const auto append = [&](const auto& file) {
        if (!glob.at("directories").get<bool>() && !file.is_regular_file())
          return;
        const auto relative =
            file.path().lexically_relative(directory).generic_u8string();
        if (std::regex_match(std::string(relative.begin(), relative.end()),
                             matcher))
          actual.push_back(Path(file.path()));
      };
      if (glob.at("recursive").get<bool>() ||
          pattern.find('/') != std::string::npos)
        for (const auto& file : fs::recursive_directory_iterator(directory))
          append(file);
      else
        for (const auto& file : fs::directory_iterator(directory)) append(file);
      std::sort(actual.begin(), actual.end());
      if (J(actual) != glob.at("files")) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}
J CMakeGlobs(const fs::path& build) {
  const auto file = build / "CMakeFiles/VerifyGlobs.cmake";
  J result = J::array();
  if (!fs::is_regular_file(file)) return result;
  const std::regex begin(
      R"g(^file\(GLOB(_RECURSE)? NEW_GLOB LIST_DIRECTORIES (true|false) "([^"]+)"\)$)g");
  const std::regex entry(R"g(^  "([^"]+)"$)g");
  std::istringstream lines(Read(file));
  std::string line;
  std::smatch match;
  J glob;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (std::regex_match(line, match, begin)) {
      const auto full = match[3].str();
      const auto wildcard = full.find_first_of("*?[");
      const auto slash = full.rfind(
          '/', wildcard == std::string::npos ? full.size() - 1 : wildcard);
      if (slash == std::string::npos || full.find('[') != std::string::npos)
        throw std::runtime_error("Unsupported source glob");
      glob = {{"directory", Path(PathFromUtf8(full.substr(0, slash)))},
              {"pattern", full.substr(slash + 1)},
              {"recursive", match[1].matched},
              {"directories", match[2] == "true"},
              {"files", J::array()}};
    } else if (!glob.is_null() && std::regex_match(line, match, entry))
      glob["files"].push_back(Path(PathFromUtf8(match[1])));
    else if (!glob.is_null() && line == "  )") {
      std::sort(glob["files"].begin(), glob["files"].end());
      result.push_back(glob);
      glob = nullptr;
    }
  }
  return result;
}
void Driver::Init() {
  if (!IsSafeLabel(options.title) ||
      options.title.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
    throw std::runtime_error("Invalid title identity");
  options.paths.root = fs::canonical(options.paths.root);
  const auto keys_override = NativeEnvironmentValue("X360N_XEX_KEYS");
  const auto keys = keys_override.empty()
                        ? options.paths.local() / "keys/xex-keys.txt"
                        : fs::absolute(PathFromUtf8(keys_override));
  if (fs::is_regular_file(keys))
    xex_keys = fs::canonical(keys);
  else if (!keys_override.empty())
    throw std::runtime_error("Configured local XEX key file is missing");
  project = options.paths.root / "titles" / options.title;
  if (!fs::is_regular_file(project / "CMakeLists.txt"))
    project = options.paths.local() / "aot" / options.title;
  manifest = project / (options.title + "_manifest.toml");
  if (!fs::is_regular_file(manifest) ||
      !fs::is_regular_file(project / "CMakeLists.txt"))
    throw std::runtime_error("Title project missing: " + Path(project));
  const auto profile = toml::parse_file(Path(manifest));
  if (profile["project"]["name"].value_or(std::string{}) != options.title)
    throw std::runtime_error("Manifest title identity mismatch");
  auto configured = profile["x360n"]["native_exe"].value<std::string>();
  const auto executable =
      configured ? ResolveNativeExecutable(options.paths, options.title,
                                           project / PathFromUtf8(*configured))
                 : NativeTitleExecutable(options.paths, options.title);
  build = options.build.empty()         ? executable.parent_path()
          : options.build.is_absolute() ? options.build
                                        : options.paths.root / options.build;
  fs::create_directories(build);
  build = fs::canonical(build);
  if (!IsWithin(build, options.paths.build()) ||
      IsWithin(build, options.paths.inputs()))
    throw std::runtime_error("Build directory escaped the environment");
  result.executable = build / executable.filename();
  result.receipt = PreparationReceipt(result.executable);
  marker =
      options.paths.local() / "preparation" / (options.title + ".incomplete");
  if (NativeEnvironmentValue("X360N_SHARED_SDK") == "0")
    options.shared_sdk = false;
  if (options.sdk.empty()) options.sdk = options.paths.sdk();
  options.sdk = fs::canonical(options.sdk);
  if (options.cmake.empty()) options.cmake = FindHostTool("cmake");
  if (options.ninja.empty()) options.ninja = FindHostTool("ninja");
  if (options.cc.empty()) options.cc = FindHostTool("clang");
  if (options.cxx.empty()) options.cxx = FindHostTool("clang++");
#ifdef _WIN32
  symbol_tool = options.cxx.parent_path() / "llvm-readobj.exe";
#else
  symbol_tool = options.cxx.parent_path() / "llvm-nm";
#endif
  const auto variables_override = options.variables;
  if (fs::is_regular_file(project / "CMakePresets.json")) {
    const auto presets = Json(project / "CMakePresets.json");
    const J* selected = nullptr;
    for (const auto& preset : presets.at("configurePresets")) {
      if (preset.at("name") == "x360n-" + HostPlatform()) {
        selected = &preset;
        break;
      }
#ifdef _WIN32
      if (HostPlatform() == "windows-x64" &&
          preset.at("name") == "x360n-win-amd64")
        selected = &preset;
#endif
    }
    if (selected) {
      if (selected->contains("inherits") ||
          selected->at("generator") != "Ninja")
        throw std::runtime_error(
            "Preparation requires an explicit Ninja preset");
      for (const auto& [key, value] : selected->at("cacheVariables").items()) {
        auto text = value.is_string() ? value.get<std::string>() : value.dump();
        for (const auto& [token, replacement] :
             std::map<std::string, std::string>{
                 {"${sourceDir}", Path(project)},
                 {"${sourceParentDir}", Path(project.parent_path())}})
          for (size_t pos = 0;
               (pos = text.find(token, pos)) != std::string::npos;
               pos += replacement.size())
            text.replace(pos, token.size(), replacement);
        if (text.find("${") != std::string::npos)
          throw std::runtime_error("Unsupported preset macro: " + key);
        options.variables[key] = text;
      }
    }
  }
  for (const auto& [key, value] : variables_override)
    options.variables[key] = value;
  options.variables.erase("CMAKE_C_COMPILER");
  options.variables.erase("CMAKE_CXX_COMPILER");
  options.variables.erase("REXSDK_DIR");
#ifndef _WIN32
  options.variables["REXGLUE_USE_D3D12"] = "OFF";
  options.variables["REXGLUE_USE_VULKAN"] = "ON";
#endif
  if (!options.variables.contains("CMAKE_C_FLAGS") &&
      HostPlatform().ends_with("-x64"))
    options.variables["CMAKE_C_FLAGS"] = "-march=x86-64-v2";
  if (!options.variables.contains("CMAKE_CXX_FLAGS") &&
      HostPlatform().ends_with("-x64"))
    options.variables["CMAKE_CXX_FLAGS"] = "-march=x86-64-v2";
  for (const auto& [key, value] : options.variables)
    if (key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuv"
                              "wxyz0123456789_") != std::string::npos ||
        value.find('\0') != std::string::npos)
      throw std::runtime_error("Invalid CMake variable");
  locked = Json(options.paths.root / "deps.lock.json");
  for (const auto& patch : locked.at("rexglue_sdk").at("patches"))
    if (Digest(options.paths.root / PathFromUtf8(patch.at("file"))) !=
        patch.at("sha256").get<std::string>())
      throw std::runtime_error("SDK patch differs from deps.lock.json");
  const auto head =
      Command(FindHostTool("git"),
              {"-C", Path(options.sdk), "rev-parse", "HEAD"}, build, "sdk-pin");
  if (!head.starts_with(
          locked.at("rexglue_sdk").at("commit").get<std::string>()))
    throw std::runtime_error("SDK commit differs from deps.lock.json");
  Command(
      options.cmake,
      {"-DSDK_DIR=" + Path(options.sdk),
       "-DLOCK_FILE=" + Path(options.paths.root / "deps.lock.json"), "-P",
       Path(options.paths.root / "cmake/sdk/x360n_verify_sdk_patches.cmake")},
      build, "sdk-patches");
  const auto compiler = Command(options.cxx, {"--version"}, build, "compiler");
  if (compiler.find(locked.at("llvm").at("version").get<std::string>()) ==
      std::string::npos)
    throw std::runtime_error("Compiler version differs from deps.lock.json");
  const auto target =
      Command(options.cxx, {"-dumpmachine"}, build, "compiler-target");
  if ((HostPlatform().ends_with("-arm64") &&
       target.find("aarch64") == std::string::npos &&
       target.find("arm64") == std::string::npos) ||
      (HostPlatform().ends_with("-x64") &&
       target.find("x86_64") == std::string::npos))
    throw std::runtime_error(
        "Cross-architecture toolchain refused for native preparation");
  if ((HostPlatform().starts_with("windows") &&
       target.find("windows") == std::string::npos) ||
      (HostPlatform().starts_with("linux") &&
       target.find("linux") == std::string::npos) ||
      (HostPlatform().starts_with("macos") &&
       target.find("darwin") == std::string::npos))
    throw std::runtime_error(
        "Cross-OS toolchain refused for native preparation");
  J environment = J::object();
  for (const auto* name : {"PATH",
                           "INCLUDE",
                           "LIB",
                           "LIBPATH",
                           "CL",
                           "_CL_",
                           "LINK",
                           "_LINK_",
                           "CPATH",
                           "C_INCLUDE_PATH",
                           "CPLUS_INCLUDE_PATH",
                           "LIBRARY_PATH",
                           "COMPILER_PATH",
                           "CFLAGS",
                           "CXXFLAGS",
                           "LDFLAGS",
                           "SDKROOT",
                           "MACOSX_DEPLOYMENT_TARGET",
                           "SOURCE_DATE_EPOCH",
                           "PKG_CONFIG_PATH",
                           "PKG_CONFIG_LIBDIR",
                           "PKG_CONFIG_SYSROOT_DIR"}) {
    const auto value = NativeEnvironmentValue(name);
    environment[name] = value;
    if (std::regex_search(value, std::regex(R"((^|\s)@)")))
      throw std::runtime_error("Opaque environment response flags refused");
  }
  identity = {{"platform", HostPlatform()},      {"abi", target},
              {"sdk_source", Path(options.sdk)}, {"compiler", compiler},
              {"cc", Path(options.cc)},          {"cxx", Path(options.cxx)},
              {"cmake", Path(options.cmake)},    {"ninja", Path(options.ninja)},
              {"variables", options.variables},  {"environment", environment}};
  for (const auto& file :
       {options.cc, options.cxx, options.cmake, options.ninja,
        options.paths.root / "deps.lock.json"})
    Record(sdk_inputs, file);
  Record(sdk_inputs, symbol_tool);
  for (const auto& directory : {options.sdk, options.paths.root / "cmake",
                                options.paths.root / "patches/rexglue-sdk"})
    Tree(sdk_inputs, directory);
  // Compiler builtins and native implicit search trees are part of the cache
  // identity, including new files that could shadow an existing dependency.
  const auto builtin = options.cxx.parent_path().parent_path() / "lib/clang";
  Tree(sdk_inputs, builtin);
  for (const auto* name : {"INCLUDE", "LIB", "CPATH", "C_INCLUDE_PATH",
                           "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"})
    if (const auto value = NativeEnvironmentValue(name); !value.empty()) {
      std::string paths(value);
      size_t begin = 0;
      const char separator = ExecutableSuffix().empty() ? ':' : ';';
      for (;;) {
        const auto end = paths.find(separator, begin);
        const auto path = paths.substr(begin, end - begin);
        if (!path.empty() && fs::path(path).is_absolute())
          Tree(sdk_inputs, PathFromUtf8(path));
        if (end == std::string::npos) break;
        begin = end + 1;
      }
    }
  for (const auto* name : {"aot_pipeline.cpp", "aot_pipeline.h",
                           "host_platform.cpp", "child_process.cpp"}) {
    const auto file = options.paths.root / "src/supervisor" / name;
    if (fs::is_regular_file(file)) Record(sdk_inputs, file);
  }
  static_inputs = sdk_inputs;
  Record(static_inputs, CurrentExecutablePath());
  Tree(static_inputs, project);
  Tree(static_inputs, options.paths.root / "titles/common");
  if (!xex_keys.empty()) Record(static_inputs, xex_keys);
  const auto originals = [&](const toml::table& table) {
    if (auto file = table["file_path"].value<std::string>())
      Record(static_inputs, project / PathFromUtf8(*file));
  };
  if (const auto* entry = profile["entrypoint"].as_table()) originals(*entry);
  if (const auto* modules = profile["modules"].as_array())
    for (const auto& module : *modules)
      if (module.is_table()) originals(*module.as_table());
  if (fs::is_regular_file(project / "preparation.toml")) {
    const auto registration =
        toml::parse_file(Path(project / "preparation.toml"));
    const auto game =
        project /
        PathFromUtf8(profile["project"]["game_root"].value_or(std::string{}));
    if (const auto* files = registration["files"].as_array())
      for (const auto& item : *files) {
        if (!item.is_table())
          throw std::runtime_error("Invalid original file registration");
        const auto& record = *item.as_table();
        const auto file =
            game / PathFromUtf8(record["path"].value_or(std::string{}));
        const bool optional = record["optional"].value_or(false);
        if (!fs::is_regular_file(file) && optional) continue;
        if (Digest(file) != record["sha256"].value_or(std::string{}))
          throw std::runtime_error("Original resource hash/version mismatch: " +
                                   Path(file));
        Record(static_inputs, file);
      }
  }
}
void Driver::SharedSdk() {
  const auto key =
      Hash(J{{"toolchain", identity}, {"inputs", sdk_inputs}}.dump());
  const auto cache =
      options.paths.local() / "cache/shared-sdk" / HostPlatform();
  FileLease publisher(cache / "publisher.lock", true);
  const auto pointer = cache / (key + ".json");
  auto reuse = [&]() {
    if (!fs::is_regular_file(pointer)) return false;
    try {
      const auto binding = Json(pointer);
      const auto file = PathFromUtf8(binding.at("receipt"));
      if (Digest(file) != binding.at("sha256").get<std::string>()) return false;
      const auto receipt = Json(file);
      if (receipt.at("identity") != key ||
          receipt.at("platform") != HostPlatform() ||
          !Verify(Entries(receipt.at("inputs"))) ||
          !Verify(Entries(receipt.at("artifacts"))) ||
          !VerifyInventory(receipt.at("globs")))
        return false;
      sdk_export = PathFromUtf8(receipt.at("export"));
      tool = PathFromUtf8(receipt.at("tool"));
      for (const auto& [path, hash] : Entries(receipt.at("inputs")))
        static_inputs[path] = hash;
      for (const auto& [path, hash] : Entries(receipt.at("artifacts")))
        static_inputs[path] = hash;
      Record(static_inputs, file);
      Stage("reused-shared-sdk");
      return true;
    } catch (...) {
      return false;
    }
  };
  if (reuse()) return;
  const auto generation = options.paths.build() / "shared-sdk" /
                          HostPlatform() / (key.substr(0, 16) + "-" + run);
  fs::create_directories(generation);
  FileLease owner(generation / ".x360n-build.lock", true);
  Configure(options.paths.root / "cmake/sdk/shared", generation, true, true);
  // Authenticate translation units before compiling the generic generation.
  std::set<std::string> owned;
  std::istringstream tracked(
      Command(FindHostTool("git"),
              {"-c", "core.quotepath=false", "-C", Path(options.sdk),
               "ls-files", "--recurse-submodules"},
              generation, "sdk-owned-sources"));
  std::string tracked_file;
  while (std::getline(tracked, tracked_file))
    if (!tracked_file.empty())
      owned.insert(Path(options.sdk / PathFromUtf8(tracked_file)));
  for (const auto& patch : locked.at("rexglue_sdk").at("patches"))
    for (const auto& [file, hash] : patch.at("files").items())
      owned.insert(Path(options.sdk / PathFromUtf8(file)));
  for (const auto& command : Commands(generation)) {
    const auto extension =
        PathFromUtf8(command.value("file", std::string{})).extension();
    if (extension != ".cpp" && extension != ".cc" && extension != ".c" &&
        extension != ".cxx" && extension != ".m" && extension != ".mm" &&
        extension != ".rc")
      continue;
    const auto source =
        fs::absolute(generation / PathFromUtf8(command.at("file")))
            .lexically_normal();
    const auto relative = source.lexically_relative(generation);
    const bool generated = !relative.empty() && !relative.is_absolute() &&
                           *relative.begin() != "..";
    if ((!IsWithin(source, options.sdk) && !generated) ||
        (IsWithin(source, options.sdk) && !owned.contains(Path(source))) ||
        fs::is_symlink(source))
      throw std::runtime_error(
          "Unidentified/title-specific translation unit cannot enter shared "
          "SDK: " +
          Path(source));
  }
  Stage("sdk-build");
  Build(generation, {"rexglue", "rexgpu-xenos"});
  Commands(generation);
  const auto graph = GraphFor(generation, {"rexglue", "rexgpu-xenos"});
  Files artifacts;
  for (const auto& [id, path] : graph.nodes)
    if (fs::is_regular_file(path) && IsWithin(path, generation) &&
        NativeProduct(path)) {
      Record(artifacts, path);
      if (path.filename() == "rexglue" + ExecutableSuffix()) tool = path;
    }
  if (tool.empty())
    throw std::runtime_error(
        "Shared SDK codegen producer missing from owned generation");
  ValidateNativeImage(tool);
  for (const auto& [path, hash] : artifacts)
    if (IsNativeModule(PathFromUtf8(path))) {
      const auto file = PathFromUtf8(path);
      ValidateNativeImage(file);
      if (PathUtf8(file.filename()).find("rexgpu-xenos") != std::string::npos)
        ModuleSymbols(file, {"rex_gpu_create", "rex_gpu_abi_version"});
    }
  for (const auto& entry : fs::recursive_directory_iterator(generation))
    if (entry.is_regular_file() && entry.path().extension() == ".cmake")
      Record(artifacts, entry.path());
  for (const auto& name :
       {"x360n-ninja" + ExecutableSuffix(), std::string("x360n-ninja.json")})
    if (fs::is_regular_file(generation / name))
      Record(artifacts, generation / name);
  sdk_export = generation / "sdk.cmake";
  if (!fs::is_regular_file(sdk_export))
    throw std::runtime_error("Shared SDK export missing");
  auto inputs = Closure(generation, {"rexglue", "rexgpu-xenos"});
  for (const auto& file :
       {options.cxx, options.cc, options.cmake, options.ninja,
        options.paths.root / "deps.lock.json"})
    Record(inputs, file);
  if (!Verify(sdk_inputs))
    throw std::runtime_error("SDK inputs changed during build");
  const auto globs = CMakeGlobs(generation);
  const auto file = generation / "sdk.preparation.json";
  Publish(file, J{{"schema", 1},
                  {"platform", HostPlatform()},
                  {"identity", key},
                  {"inputs", Entries(inputs)},
                  {"artifacts", Entries(artifacts)},
                  {"globs", globs},
                  {"tool", Path(tool)},
                  {"export", Path(sdk_export)}}
                    .dump(2));
  Publish(pointer,
          J{{"receipt", Path(file)}, {"sha256", Digest(file)}}.dump(2));
  static_inputs.insert(inputs.begin(), inputs.end());
  static_inputs.insert(artifacts.begin(), artifacts.end());
  Record(static_inputs, file);
}
AotResult Driver::Run() {
  Init();
  FileLease environment(options.paths.local() / "environment.lock", false);
  FileLease title(
      options.paths.local() / "preparation" / (options.title + ".lock"), true);
  FileLease directory(build / ".x360n-build.lock", true);
  for (const auto* path :
       {"payload-update/pending", "dependency-update.pending"})
    if (fs::exists(options.paths.local() / path))
      throw std::runtime_error(
          "Interrupted application update; no preparation allowed");
  const auto owner_file = build / ".x360n-aot-owner";
  const auto owner =
      options.title + "\n" + Path(project) + "\n" + HostPlatform() + "\n";
  if (fs::exists(owner_file) && Read(owner_file) != owner) {
    const auto previous_owner = Read(owner_file);
    const auto game =
        toml::parse_file(Path(manifest))["project"]["game_root"].value_or(
            std::string{});
    const auto imported_owner =
        options.title + "\n" + PathUtf8(PathFromUtf8(game).filename()) + "\n";
    if (previous_owner !=
        imported_owner +
            (HostPlatform() == "windows-x64" ? "" : HostPlatform() + "\n"))
      throw std::runtime_error(
          "AOT build belongs to another title/platform; select its isolated "
          "native build directory");
  }
  if (!fs::exists(owner_file)) Publish(owner_file, owner);
  Record(static_inputs, owner_file);
  const auto root_inputs = static_inputs;
  const auto state_file = build / ".preparation/native-state.json";
  J previous;
  try {
    previous = Json(state_file);
  } catch (...) {
  }
  const auto preflight_commands = result.commands;
  const bool interrupted = fs::exists(marker);
  try {
    if (!interrupted && fs::is_regular_file(result.receipt)) {
      const auto receipt = Json(result.receipt);
      const bool products_valid = VerifyProducts(previous);
      if (receipt.at("native_identity") == identity &&
          receipt.at("root_inputs") == static_inputs && products_valid &&
          Verify(Entries(receipt.at("inputs"))) &&
          Verify(Entries(receipt.at("artifacts"))) &&
          VerifyInventory(receipt.at("globs"))) {
        result.reused = true;
        Stage("reused-preparation");
        return result;
      }
    }
  } catch (...) {
  }
  Publish(marker, "Preparing " + options.title + " (" + HostPlatform() + ")\n");
  try {
    if (options.shared_sdk) SharedSdk();
    const bool incremental =
        !interrupted && !previous.is_null() &&
        previous.value("identity", J{}) == identity &&
        fs::is_regular_file(build / "CMakeCache.txt") &&
        PathFromUtf8(Cache(build)["CMAKE_HOME_DIRECTORY"]).lexically_normal() ==
            project.lexically_normal();
    bool configuration =
        incremental && previous.value("configure_inputs", Files{}) ==
                           Closure(build, {"build.ninja"});
    if (configuration &&
        PathFromUtf8(Cache(build)["X360N_SHARED_SDK_EXPORT"])
                .lexically_normal() != sdk_export.lexically_normal())
      configuration = false;
    if (!configuration) Configure(project, build, true);
    if (!incremental) {
      Stage("safe-rebuild");
      Ninja(build, {"clean"});
    } else
      Invalidate(previous);
    if (tool.empty()) {
      Stage("2-tool");
      Build(build, {"rexglue"});
      for (const auto& [id, path] : GraphFor(build, {"rexglue"}).nodes)
        if (path.filename() == "rexglue" + ExecutableSuffix() &&
            fs::is_regular_file(path))
          tool = path;
      if (tool.empty()) throw std::runtime_error("Codegen producer missing");
    }
    const auto producer_dir = build / ".preparation/codegen";
    fs::create_directories(producer_dir);
    const auto original_tool = tool;
    for (const auto& file : fs::directory_iterator(original_tool.parent_path()))
      if (file.is_regular_file() &&
          (file.path() == original_tool || IsNativeModule(file.path()))) {
        const auto copy = producer_dir / file.path().filename();
        if (!fs::is_regular_file(copy) || Digest(copy) != Digest(file.path()))
          fs::copy_file(file.path(), copy,
                        fs::copy_options::overwrite_existing);
      }
    tool = producer_dir / original_tool.filename();
    Files producer;
    Record(producer, tool);
    for (const auto& file : fs::directory_iterator(tool.parent_path()))
      if (file.is_regular_file() && IsNativeModule(file.path()))
        Record(producer, file.path());
    std::vector<fs::path> manifests{manifest};
    const auto extra = build / "x360n-codegen-manifests.txt";
    if (fs::is_regular_file(extra)) {
      std::istringstream lines(Read(extra));
      std::string line;
      while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto file = PathFromUtf8(line);
        if (!fs::is_regular_file(file) || !IsWithin(file, project) ||
            !line.ends_with("_manifest.toml"))
          throw std::runtime_error("Additional manifest escaped title project");
        manifests.push_back(file);
      }
    }
    Files codegen_inputs, generated;
    if (!xex_keys.empty()) Record(codegen_inputs, xex_keys);
    for (auto it = fs::recursive_directory_iterator(project);
         it != fs::recursive_directory_iterator(); ++it) {
      if (it->is_directory() && it->path().filename() == "generated") {
        it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file() && it->path().extension() == ".toml")
        Record(codegen_inputs, it->path());
    }
    for (const auto& file : manifests) {
      Record(codegen_inputs, file);
      const auto table = toml::parse_file(Path(file));
      for (const auto* node : {"entrypoint"})
        if (auto path = table[node]["file_path"].value<std::string>())
          Record(codegen_inputs, file.parent_path() / PathFromUtf8(*path));
      if (const auto* modules = table["modules"].as_array())
        for (const auto& module : *modules)
          if (module.is_table())
            if (auto path =
                    (*module.as_table())["file_path"].value<std::string>())
              Record(codegen_inputs, file.parent_path() / PathFromUtf8(*path));
      Tree(generated, file.parent_path() / "generated");
    }
    Tree(generated, build / "generated");
    const bool codegen_reuse =
        incremental &&
        previous.value("codegen_inputs", Files{}) == codegen_inputs &&
        previous.value("producer", Files{}) == producer &&
        previous.contains("generated") &&
        Verify(previous.at("generated").get<Files>()) &&
        previous.at("generated") == generated;
    if (!codegen_reuse) {
      Stage("2-codegen");
      for (const auto& file : manifests)
        Command(tool, {"codegen", Path(file), "--ignore-stamp"}, project,
                "codegen");
    } else
      Stage("reused-codegen");
    generated.clear();
    for (const auto& file : manifests)
      Tree(generated, file.parent_path() / "generated");
    Tree(generated, build / "generated");
    if (!configuration || previous.value("configure_inputs", Files{}) !=
                              Closure(build, {"build.ninja"}))
      Configure(project, build, false);
    if (incremental) Invalidate(previous);
    Stage("4-build");
    Build(build, {options.title});
    Commands(build);
    if (!fs::is_regular_file(result.executable))
      throw std::runtime_error("Expected native executable missing: " +
                               Path(result.executable));
    ValidateNativeImage(result.executable);
    Files artifacts;
    Record(artifacts, result.executable);
    for (const auto& file : fs::directory_iterator(build))
      if (file.is_regular_file() &&
          (IsNativeModule(file.path()) ||
           file.path().filename() ==
               "x360n_gpu_preflight" + ExecutableSuffix()))
        Record(artifacts, file.path());
    if (fs::is_regular_file(project / "preparation.toml")) {
      const auto registration =
          toml::parse_file(Path(project / "preparation.toml"));
      if (const auto* files = registration["runtime_files"].as_array())
        for (const auto& item : *files) {
          const auto name = item.value<std::string>();
          if (!name || PathFromUtf8(*name).has_parent_path())
            throw std::runtime_error("Invalid required runtime module");
          const auto file = build / NativeRuntimeFilename(PathFromUtf8(*name));
          Record(artifacts, file);
          const auto stem = PathUtf8(PathFromUtf8(*name).stem());
          if (stem == "rexgpu-xenos" || stem == "rexgpu-native")
            ModuleSymbols(file, {"rex_gpu_create", "rex_gpu_abi_version"});
          else if (stem.ends_with("_session_ui"))
            ModuleSymbols(file,
                          {"X360nSystemUiInstall", "X360nSystemUiRelease"});
          else if (stem.starts_with(options.title + "_"))
            ModuleSymbols(file,
                          {"ReXModule_Register", "ReXModule_GetImageInfo"});
        }
    }
    for (const auto& [path, hash] : artifacts)
      ValidateNativeImage(PathFromUtf8(path));
    Files inputs = Closure(build, {options.title});
    inputs.insert(producer.begin(), producer.end());
    inputs.insert(generated.begin(), generated.end());
    inputs.insert(codegen_inputs.begin(), codegen_inputs.end());
    for (const auto& name :
         {"x360n-ninja" + ExecutableSuffix(), std::string("x360n-ninja.json")})
      if (fs::is_regular_file(build / name)) Record(inputs, build / name);
    // Keep launch receipts tied to the actual build closure. The complete SDK
    // source/tool inventory selects a preparation generation, not every launch.
    for (const auto& [path, hash] : static_inputs) {
      const auto file = PathFromUtf8(path);
      if (IsWithin(file, project) || IsWithin(file, options.paths.inputs()) ||
          file == owner_file || file == options.paths.root / "deps.lock.json" ||
          PathUtf8(file.filename()) == "sdk.preparation.json" ||
          IsNativeModule(file))
        inputs[path] = hash;
    }
    const auto configure_inputs = Closure(build, {"build.ninja"});
    if (!Verify(static_inputs) || !Verify(producer) || !Verify(generated))
      throw std::runtime_error(
          "Inputs changed during preparation; no receipt published");
    const auto products = Products(build);
    Publish(state_file, J{{"schema", 2},
                          {"identity", identity},
                          {"configure_inputs", configure_inputs},
                          {"producer", producer},
                          {"codegen_inputs", codegen_inputs},
                          {"generated", generated},
                          {"products", products}}
                            .dump(2));
    Record(inputs, state_file);
    Record(inputs, manifest);
    Record(inputs, options.cxx);
    auto globs = CMakeGlobs(build);
    if (!sdk_export.empty()) {
      const auto shared =
          Json(sdk_export.parent_path() / "sdk.preparation.json");
      for (const auto& glob : shared.at("globs")) globs.push_back(glob);
      for (const auto& [path, hash] : Entries(shared.at("inputs")))
        inputs[path] = hash;
      for (const auto& [path, hash] : Entries(shared.at("artifacts")))
        inputs[path] = hash;
    }
    const J receipt{{"schema", 1},
                    {"title", options.title},
                    {"manifest", Path(manifest)},
                    {"exe", Path(result.executable)},
                    {"sdk_commit", locked.at("rexglue_sdk").at("commit")},
                    {"native_identity", identity},
                    {"root_inputs", root_inputs},
                    {"static_inputs", static_inputs},
                    {"inputs", Entries(inputs)},
                    {"artifacts", Entries(artifacts)},
                    {"globs", globs},
                    {"producer", Entries(producer)},
                    {"roles",
                     {{"compiler", Path(options.cxx)},
                      {"codegen", Path(tool)},
                      {"build_config", Path(build / "CMakeCache.txt")}}}};
    if (!Verify(inputs) || !Verify(artifacts) || !VerifyInventory(globs))
      throw std::runtime_error("Preparation changed during publication");
    Publish(result.receipt, receipt.dump(2));
    if (!options.keep_incomplete) fs::remove(marker);
    fs::remove(options.paths.local() / "preparation" /
               (options.title + ".runtime-error"));
    Stage("prepared");
    (void)preflight_commands;
    return result;
  } catch (const std::exception& error) {
    Publish(marker, "Error: " + std::string(error.what()) + "\n");
    throw;
  }
}
}  // namespace
AotResult PrepareAot(AotOptions options) {
  return Driver(std::move(options)).Run();
}
int GuardNinja(const fs::path& configuration,
               const std::vector<std::string>& arguments,
               std::function<bool()> cancelled) {
  const auto config = Json(configuration);
  const auto root = PathFromUtf8(config.at("root")),
             build = PathFromUtf8(config.at("build"));
  const auto ninja = PathFromUtf8(config.at("ninja"));
  const auto title = config.at("title").get<std::string>();
  if (!IsWithin(build, root / ".local/build") ||
      (!title.empty() && !IsSafeLabel(title)))
    throw std::runtime_error("Unsafe Ninja build owner");
  const auto inherited = NativeEnvironmentValue("X360N_OWNED_BUILD");
  const bool owned = !inherited.empty() &&
                     fs::absolute(PathFromUtf8(inherited)).lexically_normal() ==
                         fs::absolute(build).lexically_normal();
  std::vector<std::unique_ptr<FileLease>> leases;
  if (!owned) {
    leases.push_back(
        std::make_unique<FileLease>(root / ".local/environment.lock", false));
    if (!title.empty())
      leases.push_back(std::make_unique<FileLease>(
          root / ".local/preparation" / (title + ".lock"), true));
    leases.push_back(
        std::make_unique<FileLease>(build / ".x360n-build.lock", true));
  }
  bool query =
      std::find(arguments.begin(), arguments.end(), "--version") !=
          arguments.end() ||
      std::find(arguments.begin(), arguments.end(), "-n") != arguments.end();
  const auto tool = std::find(arguments.begin(), arguments.end(), "-t");
  if (tool != arguments.end() && tool + 1 != arguments.end() &&
      *(tool + 1) != "clean")
    query = true;
  const auto working_directory = fs::current_path();
  auto effective_directory = working_directory;
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (arguments[i] == "-C") {
      if (++i == arguments.size())
        throw std::runtime_error("Missing Ninja working directory");
      effective_directory =
          fs::absolute(working_directory / PathFromUtf8(arguments[i]));
    } else if (arguments[i].starts_with("-C") && arguments[i].size() > 2) {
      effective_directory = fs::absolute(working_directory /
                                         PathFromUtf8(arguments[i].substr(2)));
    }
  }
  if (std::find(arguments.begin(), arguments.end(), "--version") ==
          arguments.end() &&
      !IsWithin(effective_directory, build))
    throw std::runtime_error("Ninja working directory escaped its build owner");
  const auto marker = root / ".local/preparation" / (title + ".incomplete");
  const auto receipt_file =
      PreparationReceipt(build / (title + ExecutableSuffix()));
  const bool invalidate = !owned && !query && !title.empty();
  const bool restore_noop = invalidate && !fs::exists(marker);
  std::string previous_receipt;
  if (restore_noop && fs::is_regular_file(receipt_file))
    previous_receipt = Read(receipt_file);
  if (invalidate)
    Publish(marker, "Direct native build; prepare to verify its products\n");
  const auto log =
      build / (".ninja-guard-" + std::to_string(CurrentProcessId()));
  ProcessSpec spec{ninja,
                   arguments,
                   working_directory,
                   PathFromUtf8(Path(log) + ".out"),
                   PathFromUtf8(Path(log) + ".err"),
                   std::chrono::minutes(30)};
  spec.no_console = true;
  spec.environment = {{"X360N_OWNED_BUILD", Path(build)}};
  spec.inherit_process_group = owned;
  spec.should_cancel = std::move(cancelled);
  uint64_t out_offset = 0, err_offset = 0;
  const auto relay = [&](const fs::path& path, FILE* stream, uint64_t& offset) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    in.seekg(std::streamoff(offset));
    char buffer[8192];
    while (in.read(buffer, sizeof(buffer)) || in.gcount()) {
      const auto count = in.gcount();
      std::fwrite(buffer, 1, size_t(count), stream);
      offset += uint64_t(count);
    }
    std::fflush(stream);
  };
  spec.on_tick = [&] {
    relay(spec.stdout_file, stdout, out_offset);
    relay(spec.stderr_file, stderr, err_offset);
  };
  const auto result = RunProcess(spec);
  spec.on_tick();
  std::error_code ignored;
  fs::remove(spec.stdout_file, ignored);
  fs::remove(spec.stderr_file, ignored);
  if (!result.launched || result.timed_out || result.cancelled ||
      !result.launch_error.empty())
    throw std::runtime_error("Ninja supervision failed: " +
                             result.launch_error);
  if (restore_noop && result.exit_code == 0 && !previous_receipt.empty()) {
    try {
      const auto receipt = J::parse(previous_receipt);
      if (Read(receipt_file) == previous_receipt &&
          receipt.at("title") == title &&
          receipt.at("native_identity").at("platform") == HostPlatform() &&
          !receipt.at("inputs").empty() && !receipt.at("artifacts").empty() &&
          Verify(Entries(receipt.at("inputs"))) &&
          Verify(Entries(receipt.at("artifacts"))) &&
          VerifyInventory(receipt.at("globs")) &&
          VerifyProducts(Json(build / ".preparation/native-state.json")))
        fs::remove(marker);
    } catch (...) { /* Preserve the marker unless the complete prior receipt
                       still authenticates. */
    }
  }
  return int(result.exit_code);
}
}  // namespace x360n
