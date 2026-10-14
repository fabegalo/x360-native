// x360n: development supervisor for x360-native.
//
// This is a developer tool, not a dashboard and not a game launcher UI.
//   doctor    verify toolchain, pinned SDK revision and SDK tools
//   identify  hash a file and read XEX2 header identity
//   run       launch a build output with timeout, logs and a JSON report
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include "paths.h"
#include "preparation.h"
#include "child_process.h"
#include "report.h"
#include "sha256.h"
#include "xex_identity.h"
#include "session_profile.h"
#include "session_progress.h"
#include "session_window.h"
#include "workspace_lease.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace x360n;

namespace {

// Exit codes (documented in README.md).
constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitTimeout = 2;
constexpr int kExitUsage = 3;

fs::path SelfPath() {
#ifdef _WIN32
  std::wstring buf(32768, L'\0');
  DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
  buf.resize(n);
  return fs::path(buf);
#else
  return fs::current_path();
#endif
}

std::optional<fs::path> FindOnPath(const std::wstring& name) {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, buf, nullptr);
  if (n == 0 || n >= MAX_PATH) return std::nullopt;
  return fs::path(buf);
#else
  (void)name;
  return std::nullopt;
#endif
}

std::string ReadText(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string FirstLine(const std::string& s) {
  std::string line = s.substr(0, s.find('\n'));
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  return line;
}

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
  return s;
}

struct Captured {
  bool ok = false;  // launched and exited 0
  uint32_t exit_code = 0;
  std::string out, err, error;
};

uint32_t ProcessId() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return 0;
#endif
}

// Runs a short helper command and captures its output (doctor, git queries).
// Per-process directory: concurrent x360n invocations must not share files.
Captured Capture(const ProjectPaths& p, const fs::path& exe, std::vector<std::string> args) {
  fs::path tmp = p.local() / "tmp" / fmt::format("capture-{}", ProcessId());
  std::error_code ec;
  fs::create_directories(tmp, ec);
  ProcessSpec spec{exe, std::move(args), {}, tmp / "out.txt", tmp / "err.txt",
                   std::chrono::seconds(60)};
  auto r = RunProcess(spec);
  Captured c;
  if (!r.launched) {
    c.error = r.launch_error;
    return c;
  }
  c.exit_code = r.exit_code;
  c.ok = !r.timed_out && r.exit_code == 0;
  c.out = ReadText(spec.stdout_file);
  c.err = ReadText(spec.stderr_file);
  if (r.timed_out) c.error = "timed out";
  fs::remove_all(tmp, ec);
  return c;
}

std::string ConfigPostfix(const std::string& config) {
  // Mirrors CMAKE_DEBUG_POSTFIX / CMAKE_RELWITHDEBINFO_POSTFIX in the SDK.
  if (config == "Debug") return "d";
  if (config == "RelWithDebInfo") return "rd";
  return "";
}

fs::path RexglueExe(const ProjectPaths& p, const std::string& config) {
  return p.build() / "win-amd64" / "rexglue-sdk" / "out" / "win-amd64" / config /
         ("rexglue" + ConfigPostfix(config) + ".exe");
}

std::optional<std::string> GitHead(const ProjectPaths& p, const fs::path& repo) {
  auto git = FindOnPath(L"git.exe");
  if (!git) return std::nullopt;
  auto c = Capture(p, *git, {"-C", PathUtf8(repo), "rev-parse", "HEAD"});
  if (!c.ok) return std::nullopt;
  return Trim(c.out);
}

//---------------------------------------------------------------------------
// doctor
//---------------------------------------------------------------------------

int Doctor(const ProjectPaths& p, const std::string& config) {
  int fails = 0, warns = 0;
  auto ok = [](const std::string& m) { fmt::print("[ OK ] {}\n", m); };
  auto warn = [&](const std::string& m) { ++warns; fmt::print("[WARN] {}\n", m); };
  auto fail = [&](const std::string& m) { ++fails; fmt::print("[FAIL] {}\n", m); };

  fmt::print("x360n doctor  (root: {}, config: {})\n", PathUtf8(p.root), config);

  // Pinned SDK revision.
  auto git = FindOnPath(L"git.exe");
  if (!git) {
    fail("git not found on PATH");
  } else {
    auto head = GitHead(p, p.sdk());
    if (!head) {
      fail("cannot read external/rexglue-sdk HEAD (run: git submodule update --init --recursive)");
    } else if (*head != X360N_SDK_COMMIT_LOCKED) {
      fail(fmt::format("ReXGlue SDK at {} but deps.lock.json pins {} ({})", *head,
                       X360N_SDK_COMMIT_LOCKED, X360N_SDK_TAG_LOCKED));
    } else {
      ok(fmt::format("ReXGlue SDK at pinned {} ({})", *head, X360N_SDK_TAG_LOCKED));
    }
    auto st = Capture(p, *git, {"-C", PathUtf8(p.sdk()), "status", "--porcelain", "--ignore-submodules=none"});
    if (st.ok) {
      std::istringstream lines(st.out);
      std::string line;
      bool clean = true;
      while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty()) continue;
        clean = false;
        if (line.find("thirdparty/libmspack") != std::string::npos) {
          warn("SDK working tree: " + line +
               " (expected: symlinks materialized by scripts/materialize-symlinks.ps1)");
        } else {
          warn("SDK working tree modified: " + line);
        }
      }
      if (clean) ok("SDK working tree clean");
    }
  }

  // Toolchain.
  fs::path clang = p.local() / "toolchains" / ("llvm-" X360N_LLVM_VERSION_LOCKED) / "bin" / "clang++.exe";
  if (!fs::exists(clang)) {
    fail("pinned clang++ missing at " + PathUtf8(clang) + " (run scripts/bootstrap-llvm.ps1)");
  } else {
    auto c = Capture(p, clang, {"--version"});
    std::string want = "clang version " X360N_LLVM_VERSION_LOCKED;
    if (c.ok && FirstLine(c.out).find(want) != std::string::npos) {
      ok(FirstLine(c.out));
    } else {
      fail(fmt::format("clang++ --version did not report '{}': {}", want, FirstLine(c.out + c.error)));
    }
  }
#ifdef _WIN32
  fs::path vswhere = fs::path(L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe");
  wchar_t pf[MAX_PATH];
  DWORD pf_len = GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH);
  if (pf_len > 0 && pf_len < MAX_PATH) {
    vswhere = fs::path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
  }
  if (!fs::exists(vswhere)) {
    fail("vswhere.exe not found: install Visual Studio 2022 Build Tools with the C++ workload");
  } else {
    auto c = Capture(p, vswhere, {"-latest", "-products", "*", "-requires",
                                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                  "-property", "catalog_productDisplayVersion"});
    if (c.ok && !Trim(c.out).empty()) {
      ok("MSVC x64 tools present (Visual Studio " + Trim(c.out) + ")");
    } else {
      fail("no Visual Studio instance with MSVC x64 tools");
    }
  }
#endif

  // SDK tools built from the pinned revision.
  fs::path rexglue = RexglueExe(p, config);
  if (!fs::exists(rexglue)) {
    fail("rexglue CLI not built: " + PathUtf8(rexglue) + " (cmake --build --preset win-amd64-debug)");
  } else {
    auto c = Capture(p, rexglue, {"--version"});
    std::string v = FirstLine(c.out + c.err);
    if (!c.ok) {
      fail(fmt::format("rexglue --version failed ({}): {}", DescribeExitCode(c.exit_code), v));
    } else if (v.find("dev.unknown") != std::string::npos) {
      warn("rexglue --version reports '" + v +
           "': known issue, rex_resolve_version() reads git tags from CMAKE_SOURCE_DIR (this "
           "repo) in submodule mode; the real pin is " X360N_SDK_TAG_LOCKED);
    } else {
      ok("rexglue --version: " + v);
    }
  }
  fs::path as = p.sdk() / "tools" / "binutils" / "powerpc-none-elf-as.exe";
  if (!fs::exists(as)) {
    fail("PowerPC assembler missing: " + PathUtf8(as));
  } else {
    auto c = Capture(p, as, {"--version"});
    if (c.ok) ok("PowerPC binutils: " + FirstLine(c.out));
    else fail("powerpc-none-elf-as --version failed: " + FirstLine(c.out + c.err + c.error));
  }

#ifdef _WIN32
  // Symlink capability decides whether submodule symlinks need materializing.
  fs::path probe_dir = p.local() / "tmp" / fmt::format("symlink-probe-{}", ProcessId());
  std::error_code ec;
  fs::create_directories(probe_dir, ec);
  fs::path link = probe_dir / "link";
  fs::path target = probe_dir / "target";
  { std::ofstream(target) << "x"; }
  fs::remove(link, ec);
  if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    ok("this account can create symlinks");
    fs::remove(link, ec);
  } else {
    warn("this account cannot create symlinks (Developer Mode off, not elevated): git checks "
         "symlinks out as text files");
  }
  fs::remove_all(probe_dir, ec);
#endif

  // Local directories.
  for (auto& [name, dir] : {std::pair{"inputs", p.inputs()}, std::pair{"runs", p.runs()}}) {
    if (fs::is_directory(dir)) ok(fmt::format(".local/{} exists", name));
    else warn(fmt::format(".local/{} does not exist yet ({})", name, PathUtf8(dir)));
  }

  fmt::print("\n{} failure(s), {} warning(s)\n", fails, warns);
  return fails ? kExitFailed : kExitOk;
}

//---------------------------------------------------------------------------
// identify
//---------------------------------------------------------------------------

int Identify(const fs::path& file, bool json) {
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) {
    fmt::print(stderr, "identify: not a regular file: {}\n", PathUtf8(file));
    return kExitUsage;
  }
  auto digest = DigestFile(file);
  if (!digest) {
    fmt::print(stderr, "identify: cannot read {}\n", PathUtf8(file));
    return kExitFailed;
  }

  // Read just the header block. 64 MiB is far above any real XEX header.
  constexpr uint64_t kMaxHeader = 64ull << 20;
  std::ifstream in(file, std::ios::binary);
  std::vector<uint8_t> head(size_t(std::min<uint64_t>(digest->size, 0x18)));
  in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
  bool is_xex = head.size() >= 4 && head[0] == 'X' && head[1] == 'E' && head[2] == 'X' && head[3] == '2';
  XexParseResult parsed;
  if (is_xex && head.size() >= 0x18) {
    uint64_t hsize = (uint64_t(head[8]) << 24) | (uint64_t(head[9]) << 16) | (uint64_t(head[10]) << 8) | head[11];
    uint64_t want = std::min({hsize, digest->size, kMaxHeader});
    head.resize(size_t(std::max<uint64_t>(want, 0x18)));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
    head.resize(size_t(in.gcount()));
  }
  // Every file is classified (XEX2 / XEX1 / other XEX variant / invalid); only
  // XEX2 is read.
  parsed = ParseXexIdentity(head);
  const XexIdentity* id = parsed.identity ? &*parsed.identity : nullptr;

  if (json) {
    JsonWriter w;
    w.BeginObject();
    w.Number("schema_version", 1);
    w.String("path", PathUtf8(fs::absolute(file)));
    w.Number("size", int64_t(digest->size));
    w.String("sha256", digest->sha256);
    // "xex2" | "xex1" | "xex-variant" | "invalid"
    w.String("format", parsed.format == XexFormat::kXex2   ? "xex2"
                       : parsed.format == XexFormat::kXex1 ? "xex1"
                       : parsed.format == XexFormat::kXexOtherVariant ? "xex-variant"
                                                                      : "invalid");
    w.String("signature", parsed.signature);
    if (!id) w.String("error", parsed.error);
    if (id) {
      w.BeginObject("xex");
      w.String("module_flags", fmt::format("0x{:08X}", id->module_flags));
      std::string flags;
      for (auto& f : DescribeModuleFlags(id->module_flags)) flags += (flags.empty() ? "" : ",") + f;
      w.String("module_flag_names", flags);
      if (id->has_execution_info) {
        w.String("title_id", fmt::format("{:08X}", id->title_id));
        w.String("media_id", fmt::format("{:08X}", id->media_id));
        w.String("version", id->version.ToString());
        w.String("base_version", id->base_version.ToString());
        w.Number("disc_number", id->disc_number);
        w.Number("disc_count", id->disc_count);
        w.String("savegame_id", fmt::format("{:08X}", id->savegame_id));
        w.Bool("is_dashboard_title_id", IsDashboardTitleId(id->title_id));
      } else {
        w.Null("title_id");
      }
      if (id->original_pe_name) w.String("original_pe_name", *id->original_pe_name);
      if (id->entry_point) w.String("entry_point", fmt::format("0x{:08X}", *id->entry_point));
      if (id->image_base) w.String("image_base", fmt::format("0x{:08X}", *id->image_base));
      if (id->encryption_type) w.Number("encryption_type", *id->encryption_type);
      if (id->compression_type) w.Number("compression_type", *id->compression_type);
      if (id->image_size) w.String("image_size", fmt::format("0x{:X}", *id->image_size));
      if (id->load_address) w.String("load_address", fmt::format("0x{:08X}", *id->load_address));
      if (id->region) w.String("region", fmt::format("0x{:08X}", *id->region));
      w.BeginArray("imports");
      for (auto& lib : id->imports) {
        w.BeginObject();
        w.String("name", lib.name);
        w.String("version", lib.version.ToString());
        w.String("version_min", lib.version_min.ToString());
        w.Number("table_entries", lib.table_entries);
        w.EndObject();
      }
      w.EndArray();
      w.BeginArray("warnings");
      for (auto& msg : id->warnings) w.String({}, msg);
      w.EndArray();
      w.EndObject();
    }
    w.EndObject();
    std::cout << w.Take();
  } else {
    fmt::print("file          {}\nsize          {} bytes\nsha256        {}\nformat        {}\n",
               PathUtf8(fs::absolute(file)), digest->size, digest->sha256,
               XexFormatName(parsed.format));
    if (!id) fmt::print("error         {}\n", parsed.error);
    if (id) {
      std::string flags;
      for (auto& f : DescribeModuleFlags(id->module_flags)) flags += (flags.empty() ? "" : ", ") + f;
      fmt::print("module_flags  0x{:08X} ({})\n", id->module_flags, flags);
      if (id->has_execution_info) {
        fmt::print("title_id      {:08X}{}\n", id->title_id,
                   IsDashboardTitleId(id->title_id) ? "  (dashboard title id)" : "");
        fmt::print("media_id      {:08X}\nversion       {} (base {})\ndisc          {}/{}\n",
                   id->media_id, id->version.ToString(), id->base_version.ToString(),
                   id->disc_number, id->disc_count);
      } else {
        fmt::print("title_id      (no execution info header)\n");
      }
      if (id->original_pe_name) fmt::print("original_pe   {}\n", *id->original_pe_name);
      if (id->entry_point) fmt::print("entry_point   0x{:08X}\n", *id->entry_point);
      if (id->image_base) fmt::print("image_base    0x{:08X}\n", *id->image_base);
      if (id->encryption_type)
        fmt::print("file_format   encryption={} compression={}\n", *id->encryption_type,
                   id->compression_type.value_or(0));
      for (auto& lib : id->imports)
        fmt::print("import        {} {} (min {}), {} table entries\n", lib.name,
                   lib.version.ToString(), lib.version_min.ToString(), lib.table_entries);
      for (auto& w : id->warnings) fmt::print("warning       {}\n", w);
    }
  }
  return (is_xex && !id) ? kExitFailed : kExitOk;
}

//---------------------------------------------------------------------------
// run
//---------------------------------------------------------------------------

std::string UtcStamp() {
  auto now = std::chrono::system_clock::now();
  return fmt::format("{:%Y%m%dT%H%M%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

int Run(const ProjectPaths& p, const fs::path& exe_in, std::vector<std::string> args,
        std::string label, int timeout_s, bool rex_log, bool crash_stack,
        fs::path* handoff = nullptr, SessionWindow* window = nullptr,
        std::chrono::milliseconds* child_lifetime = nullptr) {
  fs::path exe = fs::absolute(exe_in);
  if (auto why = CheckPreparedLaunch(p, exe)) {
    fmt::print(stderr, "run: {}\n", *why);
    return kExitUsage;
  }
  if (auto why = CheckLaunchable(p, exe)) {
    fmt::print(stderr, "run: {}\n", *why);
    return kExitUsage;
  }
  if (label.empty()) {
    label = PathUtf8(exe.stem());
    for (char& c : label)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
  }
  if (!IsSafeLabel(label)) {
    fmt::print(stderr, "run: label must be 1-64 chars of [A-Za-z0-9._-]\n");
    return kExitUsage;
  }

  std::string started = UtcStamp();
  fs::path dir = p.runs() / (started + "-" + label);
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    fmt::print(stderr, "run: cannot create {}: {}\n", PathUtf8(dir), ec.message());
    return kExitUsage;
  }

  // ReXGlue apps built as WIN32 GUI programs log to a file, not to stdout:
  // --rex-log points the log_file cvar into the run directory so it is kept
  // and scanned like the captured streams.
  fs::path rex_log_file = dir / "rex.log";
  if (rex_log) args.insert(args.begin(), "--log_file=" + PathUtf8(rex_log_file));
  if (handoff) {
    *handoff = dir / "handoff.bin";
    // Refuse stale records, including a same-second reused run label.
    if (fs::exists(*handoff)) return kExitUsage;
    args.push_back("--x360n_handoff_out=" + PathUtf8(*handoff));
  }
  if (window) args.push_back("--parent_window=" + std::to_string(window->handle()));

  auto exe_digest = DigestFile(exe);
  // The child runs in its executable's directory: relative paths in its
  // arguments resolve there, not in the caller's directory.
  ProcessSpec spec{exe, args, exe.parent_path(), dir / "stdout.log", dir / "stderr.log",
                   std::chrono::seconds(timeout_s)};
  spec.capture_crash = crash_stack;
  if (window) spec.should_cancel = [window] { return window->closed(); };
  fmt::print("run: {} (working dir {}) -> {}\n", PathUtf8(exe), PathUtf8(spec.working_dir),
             PathUtf8(dir));
  auto r = RunProcess(spec);
  if (child_lifetime) *child_lifetime = r.duration;

  LogScan scan;
  if (r.launched) {
    std::vector<fs::path> scanned{spec.stdout_file, spec.stderr_file};
    if (rex_log) scanned.push_back(rex_log_file);
    for (const auto& f : scanned) scan.Merge(ScanLog(f));
  }
  auto sdk_head = GitHead(p, p.sdk());
  auto repo_head = GitHead(p, p.root);

  JsonWriter w;
  w.BeginObject();
  w.Number("schema_version", 1);
  w.String("note", "Process-level record only. It does not establish title or dashboard compatibility.");
  w.BeginObject("supervisor");
  w.String("version", X360N_VERSION);
  w.String("build_config", X360N_BUILD_CONFIG);
  w.EndObject();
  w.BeginObject("revisions");
  if (repo_head) w.String("project_commit", *repo_head); else w.Null("project_commit");
  w.String("sdk_commit_locked", X360N_SDK_COMMIT_LOCKED);
  if (sdk_head) w.String("sdk_commit", *sdk_head); else w.Null("sdk_commit");
  w.String("sdk_tag_locked", X360N_SDK_TAG_LOCKED);
  w.EndObject();
  w.BeginObject("target");
  w.String("label", label);
  w.String("exe", PathUtf8(exe));
  if (exe_digest) {
    w.String("exe_sha256", exe_digest->sha256);
    w.Number("exe_size", int64_t(exe_digest->size));
  }
  // DLLs next to the executable (the runtime and GPU plugin a ReXGlue title
  // loads first): which build of them this run actually used.
  w.BeginArray("dlls");
  {
    std::vector<fs::path> dlls;
    std::error_code dir_ec;
    for (const auto& e : fs::directory_iterator(exe.parent_path(), dir_ec)) {
      std::string ext = e.path().extension().string();
      for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
      if (e.is_regular_file() && ext == ".dll") dlls.push_back(e.path());
    }
    std::sort(dlls.begin(), dlls.end());
    for (const auto& d : dlls) {
      w.BeginObject();
      w.String("name", PathUtf8(d.filename()));
      if (auto dg = DigestFile(d)) {
        w.Number("size", int64_t(dg->size));
        w.String("sha256", dg->sha256);
      }
      w.EndObject();
    }
  }
  w.EndArray();
  std::string joined;
  for (auto& a : args) joined += (joined.empty() ? "" : " ") + a;
  w.String("args", joined);
  w.String("working_dir", PathUtf8(spec.working_dir));
  w.Number("timeout_s", timeout_s);
  w.EndObject();
  w.BeginObject("result");
  w.String("started_utc", started);
  w.Bool("launched", r.launched);
  if (!r.launched) w.String("launch_error", r.launch_error);
  w.Bool("timed_out", r.timed_out);
  w.Bool("cancelled", r.cancelled);
  w.Number("exit_code", int64_t(int32_t(r.exit_code)));
  w.String("exit_code_hex", fmt::format("0x{:08X}", r.exit_code));
  w.String("exit_description", !r.launched ? "not launched"
                                 : r.cancelled ? "session window closed by user"
                                 : r.timed_out ? "terminated by the supervisor after the timeout"
                                               : DescribeExitCode(r.exit_code));
  w.Number("duration_ms", r.duration.count());
  w.EndObject();
  w.BeginObject("logs");
  w.String("stdout", PathUtf8(spec.stdout_file));
  w.String("stderr", PathUtf8(spec.stderr_file));
  if (rex_log) w.String("rex_log", PathUtf8(rex_log_file));
  w.EndObject();
  w.BeginObject("diagnostics");
  w.Number("stub_lines", int64_t(scan.stub_lines));
  w.BeginObject("compatibility_experiments");
  for (auto& [k, v] : scan.compatibility_experiments) w.Number(k, int64_t(v));
  w.EndObject();
  w.BeginObject("stubs_by_name");
  for (auto& [k, v] : scan.stubs_by_name) w.Number(k, int64_t(v));
  w.EndObject();
  w.BeginObject("unimplemented_messages");
  for (auto& [k, v] : scan.unimplemented_messages) w.Number(k, int64_t(v));
  w.EndObject();
  w.Number("critical_lines", int64_t(scan.critical_lines));
  w.BeginObject("failed_opens");
  for (auto& [k, v] : scan.failed_opens) w.Number(k, int64_t(v));
  w.EndObject();
  w.BeginObject("unregistered_calls");
  for (auto& [k, v] : scan.unregistered_calls) w.Number(k, int64_t(v));
  w.EndObject();
  w.BeginObject("unimplemented_variables");
  for (auto& [k, v] : scan.unimplemented_variables) w.Number(k, int64_t(v));
  w.EndObject();
  w.Number("error_lines", int64_t(scan.error_lines));
  w.BeginArray("first_errors");
  for (const auto& e : scan.first_errors) w.String({}, e);
  w.EndArray();
  w.EndObject();
  if (crash_stack) {
    w.BeginObject("crash");
    w.Bool("captured", r.crash.captured);
    if (r.crash.captured) {
      w.String("exception_code", fmt::format("0x{:08X}", r.crash.code));
      w.String("exception", DescribeExitCode(r.crash.code));
      w.String("address", fmt::format("0x{:016X}", r.crash.address));
      if (!r.crash.access.empty()) w.String("access", r.crash.access);
      w.Number("thread_id", r.crash.thread_id);
      w.BeginArray("frames");
      for (const auto& f : r.crash.frames) w.String({}, f);
      w.EndArray();
    }
    w.EndObject();
  } else {
    w.Null("crash");
  }
  w.EndObject();
  std::ofstream(dir / "report.json", std::ios::binary) << w.Take();
  if (r.crash.captured) {
    std::ofstream txt(dir / "crash.txt", std::ios::binary);
    txt << fmt::format("exception 0x{:08X} ({}) at 0x{:016X}, thread {}\n", r.crash.code,
                       DescribeExitCode(r.crash.code), r.crash.address, r.crash.thread_id);
    if (!r.crash.access.empty()) txt << "access: " << r.crash.access << "\n";
    for (const auto& f : r.crash.frames) txt << f << "\n";
  }

  if (!r.launched) {
    fmt::print(stderr, "run: launch failed: {}\n", r.launch_error);
    return kExitUsage;
  }
  size_t unimpl = 0;
  for (auto& [k, v] : scan.unimplemented_messages) unimpl += v;
  // The summary line stays last: callers take the report path from its end.
  for (const auto& e : scan.first_errors) fmt::print("run:   error: {}\n", e);
  if (r.crash.captured) {
    fmt::print("run:   crash: 0x{:08X} at 0x{:016X}{}{}; stack in crash.txt\n", r.crash.code,
               r.crash.address, r.crash.access.empty() ? "" : ", ", r.crash.access);
    for (size_t i = 0; i < r.crash.frames.size() && i < 8; ++i)
      fmt::print("run:   {}\n", r.crash.frames[i]);
  } else if (crash_stack) {
    fmt::print("run:   crash: none captured (no unhandled exception)\n");
  }
  fmt::print(
      "run: {} after {} ms; {} stub line(s), {} unimplemented message(s), {} error(s), {} critical; "
      "report {}\n",
      r.cancelled ? std::string("CANCELLED, session window closed")
      : r.timed_out ? std::string("TIMEOUT, terminated by the supervisor")
                  : DescribeExitCode(r.exit_code),
      r.duration.count(), scan.stub_lines, unimpl, scan.error_lines, scan.critical_lines,
      PathUtf8((dir / "report.json")));
  if (r.timed_out) return kExitTimeout;
  return r.exit_code == 0 ? kExitOk : kExitFailed;
}

int Session(const ProjectPaths& paths, const fs::path& manifest, int timeout, bool separate_windows) {
  try {
    WorkspaceLease lease(paths);
    const auto dashboard = LoadSessionProfile(paths, manifest);
    std::vector<SessionProfile> targets;
    for (const auto& target : dashboard.launch_targets)
      targets.push_back(LoadSessionProfile(paths, target, false));
    auto active = dashboard;
    std::unique_ptr<SessionWindow> window;
    if (!separate_windows) {
      window = std::make_unique<SessionWindow>();
      fmt::print("session: persistent window {}\n", window->handle());
    }
    fs::path incoming;
    SessionProgress progress;
    for (uint64_t step = 0; ; ++step) {
      // Revalidate the profile and input bytes at every transition.
      active = LoadSessionProfile(paths, active.manifest);
      auto args = active.args;
      args.push_back("--game_data_root=" + PathUtf8(active.game_root));
      args.push_back("--user_data_root=" + PathUtf8(active.user_root));
      if (!incoming.empty()) args.push_back("--x360n_handoff_in=" + PathUtf8(incoming));
      fs::path handoff;
      std::chrono::milliseconds child_lifetime{};
      const auto run_prefix = fmt::format("session-{}-", step);
      // Content-derived project IDs may already occupy the complete safe-label
      // budget. Shorten only the diagnostic label, never the routed identity.
      const auto run_label = run_prefix + active.name.substr(0, 64 - run_prefix.size());
      const auto result = Run(paths, active.exe, args,
          run_label, timeout, true, false, &handoff, window.get(), &child_lifetime);
      if (window && window->closed()) return kExitOk;
      if (result != kExitOk) {
        // A successful compilation is not a compatibility verdict. Preserve a
        // real failed run for the imported game's next desktop inspection.
        if (result != kExitTimeout && IsWithin(active.manifest, paths.local() / "aot")) {
          fs::create_directories(paths.local() / "preparation");
          std::ofstream(paths.local() / "preparation" / (active.name + ".runtime-error"))
              << "Runtime failed; supervisor result " << result << "\nReport: " << PathUtf8(handoff.parent_path() / "report.json") << '\n';
        }
        return result;  // never relaunch after timeout/crash
      }
      LaunchRequest request;
      if (!ReadLaunchRequest(handoff, request)) {
        fmt::print(stderr, "session: missing or invalid handoff: {}\n", PathUtf8(handoff));
        return kExitFailed;
      }
      const auto can_continue = [&] {
        if (progress.CanContinue(child_lifetime)) return true;
        fmt::print(stderr, "session: repeated transitions without sustained execution; request preserved in {}\n",
                   PathUtf8(handoff));
        return false;
      };
      if (request.kind != LaunchRequest::Image) {
        // Closing a host window exits the platform, never impersonates Xbox Home.
        if (request.kind == LaunchRequest::WindowClose) return kExitOk;
        if (active.manifest == dashboard.manifest) return kExitOk;
        if (request.kind == LaunchRequest::Dashboard &&
            std::find(dashboard.dashboard_return_flags.begin(), dashboard.dashboard_return_flags.end(),
                      request.flags) == dashboard.dashboard_return_flags.end()) {
          fmt::print(stderr, "session: dashboard return flags {:08X} are not implemented; request preserved in {}\n",
                     request.flags, PathUtf8(handoff));
          return kExitFailed;
        }
        fmt::print("session: {} {}; returning to {}\n", active.name,
            request.kind == LaunchRequest::Dashboard ? "requested dashboard" :
            request.kind == LaunchRequest::TitleTerminate ? "requested title termination" :
            "main thread returned (no explicit return request)",
            dashboard.name);
        if (!can_continue()) return kExitFailed;
        active = dashboard; incoming = handoff;
        continue;
      }
      auto target = std::find_if(targets.begin(), targets.end(),
          [&](const auto& profile) { return MatchesLaunch(profile, request, dashboard.user_root); });
      if (target == targets.end()) {
        fmt::print(stderr, "session: no verified AOT profile for {} (flags {:08X})\n",
                   request.path, request.flags);
        return kExitFailed;
      }
      fmt::print("session: original request matched {} ({})\n", target->name, target->sha256);
      if (!can_continue()) return kExitFailed;
      incoming = handoff; active = *target;
    }
  } catch (const std::exception& e) {
    fmt::print(stderr, "session: {}\n", e.what());
    return kExitUsage;
  }
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"x360n - x360-native development supervisor", "x360n"};
  argv = app.ensure_utf8(argv);
  app.set_version_flag("--version", X360N_VERSION);
  app.require_subcommand(1);
  std::string root_opt;
  app.add_option("--root", root_opt, "Project root (default: search upward from the executable)");

  auto* doctor = app.add_subcommand("doctor", "Verify toolchain, pinned SDK and SDK tools");
  std::string config = X360N_BUILD_CONFIG;
  doctor->add_option("--config", config, "SDK build configuration to check")
      ->check(CLI::IsMember({"Debug", "Release", "RelWithDebInfo"}));

  auto* identify = app.add_subcommand(
      "identify",
      "Hash a file, classify it (XEX2 / XEX1 / other XEX variant / invalid) and read its XEX2 "
      "identity; exit 1 only for a malformed XEX2");
  std::string file;
  bool json = false;
  identify->add_option("file", file, "File to identify")->required();
  identify->add_flag("--json", json, "Emit JSON");

  auto* run = app.add_subcommand("run", "Launch a build output under supervision");
  auto* session = app.add_subcommand("session", "Follow original title requests using a dashboard manifest");
  std::string session_manifest;
  int session_timeout = 600;
  bool separate_windows = false;
  session->add_option("manifest", session_manifest)->required();
  session->add_flag("--separate-windows", separate_windows, "Diagnostic mode: one window per title");
  session->add_option("--timeout", session_timeout, "Timeout per title; 0 disables it")
      ->check(CLI::NonNegativeNumber);
  std::string exe, label;
  int timeout_s = 600;
  std::vector<std::string> child_args;
  run->add_option("exe", exe, "Executable (must be under .local/build or the SDK out/ dir)")->required();
  run->add_option("--label", label, "Run label used in the run directory name");
  run->add_option("--timeout", timeout_s, "Timeout in seconds (0 = none)")->check(CLI::NonNegativeNumber);
  bool crash_stack = false;
  run->add_flag("--crash-stack", crash_stack,
                "Run the child under the debug API and record the stack of an unhandled "
                "exception (crash.txt, report.json); slower");
  bool rex_log = false;
  run->add_flag("--rex-log", rex_log,
                "ReXGlue app: pass --log_file=<run dir>/rex.log to the child and scan it for stubs");
  run->add_option("args", child_args,
                  "Arguments passed to the child (after --); it runs in the executable's "
                  "directory, so pass absolute paths");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    int rc = app.exit(e);
    return rc == 0 ? kExitOk : kExitUsage;
  }

  if (identify->parsed()) return Identify(PathFromUtf8(file), json);

  auto paths = FindProjectRoot(root_opt.empty() ? SelfPath() : PathFromUtf8(root_opt));
  if (!paths) {
    fmt::print(stderr, "x360n: project root (deps.lock.json) not found; pass --root\n");
    return kExitUsage;
  }
  if (doctor->parsed()) return Doctor(*paths, config);
  if (session->parsed()) return Session(*paths, PathFromUtf8(session_manifest), session_timeout, separate_windows);
  if (run->parsed()) {
    try {
      // Session already owns this lease. Standalone prepared-title launches
      // need the same exclusion, through validation and child lifetime.
      const auto native = fs::absolute(PathFromUtf8(exe));
      std::optional<WorkspaceLease> lease;
      if (fs::exists(PreparationReceipt(native)) || IsWithin(native, paths->build() / "titles"))
        lease.emplace(*paths);
      return Run(*paths, PathFromUtf8(exe), child_args, label, timeout_s, rex_log, crash_stack);
    } catch (const std::exception& e) {
      fmt::print(stderr, "run: {}\n", e.what());
      return kExitUsage;
    }
  }
  return kExitUsage;
}
