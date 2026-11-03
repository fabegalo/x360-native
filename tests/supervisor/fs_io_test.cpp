// x360n::io and host_path: every installation folder works, at any length, in
// Unicode, through UNC and with dot-dot or relative spellings, and links are
// refused wherever they sit. Synthetic files only; no title or dashboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <climits>
#endif

#include "child_process.h"
#include "fs_io.h"
#include "paths.h"
#include "scratch.h"
#include "sha256.h"
#include "toml_file.h"

namespace fs = std::filesystem;
namespace io = x360n::io;
namespace host_path = x360n::host_path;

namespace {

size_t Length(const fs::path& path) { return path.native().size(); }

// base/<p...>/leaf with exactly `length` native characters (components stay
// below the 255-character name limit of every supported filesystem).
fs::path Exact(const fs::path& base, size_t length, const fs::path& leaf) {
  auto directory = base;
  while (Length(directory) + 1 + Length(leaf) < length) {
    const auto room = length - Length(directory) - 1 - Length(leaf);
    if (room < 2) break;
    const auto take = room - 1 <= 200 ? room - 1 : std::min<size_t>(200, room - 3);
    directory /= std::string(take, 'p');
  }
  return directory / leaf;
}

std::string Slurp(const fs::path& file) {
  io::ifstream in(file, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Root {
  fs::path path;
  explicit Root(const std::string& name) : path(UniqueTempPath(name)) {
    std::error_code ec;
    io::remove_all(path, ec);
    io::create_directories(path);
  }
  ~Root() {
    std::error_code ec;
    io::remove_all(path, ec);
  }
};

const fs::path kDlc = "DLC_Rob_Fant_[0x2a2c7864e22311ee].loc";
const fs::path kUnicode = fs::path(u8"Jogos-Ação-ゲーム-🎮.bin");

// The whole set of operations the launcher, importer, catalog and preparation
// use, on one file at `file` (its parent does not exist yet). Moves between
// folders are left out where the host cannot make them (see the UNC case).
void ExerciseFile(const fs::path& root, const fs::path& file, bool across_folders = true) {
  INFO("path length " << Length(file));
  const std::string payload = "x360n " + std::to_string(Length(file));
  io::create_directories(file.parent_path());
  {
    io::ofstream out(file, std::ios::binary);
    out << payload;
    out.close();
    REQUIRE(out);
  }
  CHECK(io::exists(file));
  CHECK(io::is_regular_file(file));
  CHECK_FALSE(io::is_redirect(file));
  CHECK(io::file_size(file) == payload.size());
  CHECK(Slurp(file) == payload);
  CHECK_NOTHROW(io::last_write_time(file));

  // Enumeration yields the ordinary spelling, comparable with logical paths.
  std::vector<fs::path> listed;
  for (const auto& entry : io::directory_iterator(file.parent_path())) {
    CHECK_FALSE(host_path::IsExtended(entry.path()));
    CHECK(entry.is_regular_file());
    CHECK_FALSE(entry.is_redirect());
    listed.push_back(entry.path());
  }
  CHECK(listed == std::vector<fs::path>{file});
  bool found = false;
  for (auto it = io::recursive_directory_iterator(root); it != io::recursive_directory_iterator(); ++it)
    found = found || it->path() == file;
  CHECK(found);

  const auto canonical = io::canonical(file);
  CHECK_FALSE(host_path::IsExtended(canonical));
  CHECK(canonical.filename() == file.filename());
  CHECK(io::equivalent(canonical, file));
  CHECK(io::canonical(file.parent_path()) == canonical.parent_path());
  const auto weak = io::weakly_canonical(file.parent_path() / "missing" / "child");
  CHECK_FALSE(host_path::IsExtended(weak));
  CHECK(weak == canonical.parent_path() / "missing" / "child");
  CHECK(x360n::IsWithin(file, root));
  CHECK(x360n::CanonicalPath(file) == canonical);

  // SHA-256 verification streams the same bytes.
  const auto digest = x360n::DigestFile(file);
  REQUIRE(digest);
  CHECK(digest->sha256 == x360n::Sha256Hex(std::span(reinterpret_cast<const uint8_t*>(payload.data()), payload.size())));

  auto copy = file;
  copy += ".copy";
  auto moved = file;
  moved += ".moved";
  io::copy_file(file, copy);
  io::rename(copy, moved);
  CHECK_FALSE(io::exists(copy));
  CHECK(Slurp(moved) == payload);
  // Staged files are published over the previous one (settings, catalog)...
  {
    io::ofstream out(copy, std::ios::binary);
    out << "newer";
  }
  io::rename(copy, moved);
  CHECK_FALSE(io::exists(copy));
  CHECK(Slurp(moved) == "newer");
#ifdef _WIN32
  // ...or never over an existing one (imports, extraction manifests).
  io::copy_file(file, copy);
  std::error_code kept;
  io::rename(copy, moved, io::if_exists::fail, kept);
  CHECK(kept);
  CHECK(Slurp(moved) == "newer");
  CHECK(io::remove(copy));
#endif
  if (across_folders) {
    const auto other = file.parent_path() / "other" / file.filename();
    io::create_directories(other.parent_path());
    io::rename(moved, other);
    CHECK_FALSE(io::exists(moved));
    CHECK(Slurp(other) == "newer");
    io::rename(other, moved);
    CHECK(io::remove(other.parent_path()));
  }
  CHECK(io::remove(moved));
  CHECK(io::remove(file));
  CHECK_FALSE(io::exists(file));
}

bool PathSupported(size_t length) {
#ifdef __APPLE__
  // macOS rejects any path of PATH_MAX (1024) bytes or more (ENAMETOOLONG);
  // a system limit that no spelling avoids.
  return length < PATH_MAX;
#else
  (void)length;
  return true;
#endif
}

#ifdef _WIN32
// Junctions need no privilege, unlike symbolic links.
bool MakeJunction(const fs::path& link, const fs::path& target) {
  io::create_directory(link);
  const auto handle = CreateFileW(io::Host(link).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  const auto print = io::Logical(io::Host(target)).native();
  const auto substitute = L"\\??\\" + print;
  struct Header { ULONG tag; USHORT length, reserved, substitute_offset, substitute_length, print_offset, print_length; };
  std::vector<char> buffer(sizeof(Header) + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
  auto* header = reinterpret_cast<Header*>(buffer.data());
  header->tag = IO_REPARSE_TAG_MOUNT_POINT;
  header->length = USHORT(buffer.size() - 8);
  header->substitute_length = USHORT(substitute.size() * sizeof(wchar_t));
  header->print_offset = USHORT(header->substitute_length + sizeof(wchar_t));
  header->print_length = USHORT(print.size() * sizeof(wchar_t));
  auto* names = reinterpret_cast<wchar_t*>(buffer.data() + sizeof(Header));
  std::copy(substitute.c_str(), substitute.c_str() + substitute.size() + 1, names);
  std::copy(print.c_str(), print.c_str() + print.size() + 1, names + substitute.size() + 1);
  DWORD bytes = 0;
  const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()), nullptr, 0,
                                  &bytes, nullptr);
  CloseHandle(handle);
  return ok;
}
#endif

}  // namespace

TEST_CASE("host names are extended only on Windows and only for I/O", "[paths][fs-io]") {
#ifdef _WIN32
  CHECK(host_path::Extended(LR"(C:\a\b)") == fs::path(LR"(\\?\C:\a\b)"));
  CHECK(host_path::Extended(L"C:/a/./b/../c") == fs::path(LR"(\\?\C:\a\c)"));
  CHECK(host_path::Extended(LR"(\\server\share\x\..\y)") == fs::path(LR"(\\?\UNC\server\share\y)"));
  CHECK(host_path::Extended(LR"(\\?\C:\a)") == fs::path(LR"(\\?\C:\a)"));
  CHECK(host_path::Extended(LR"(\\.\pipe\x)") == fs::path(LR"(\\.\pipe\x)"));
  CHECK(host_path::Extended(L"relative\\x") == fs::path(L"\\\\?\\" + (fs::current_path() / L"relative\\x").native()));
  CHECK(host_path::Ordinary(LR"(\\?\UNC\server\share\y)") == fs::path(LR"(\\server\share\y)"));
  CHECK(host_path::Ordinary(LR"(\\?\C:\a)") == fs::path(LR"(C:\a)"));
  // A volume GUID name has no ordinary drive spelling and stays extended.
  CHECK(host_path::Ordinary(LR"(\\?\Volume{0}\x)") == fs::path(LR"(\\?\Volume{0}\x)"));
  for (const fs::path p : {LR"(C:\a\b)", LR"(\\server\share\y)"})
    CHECK(host_path::Ordinary(host_path::Extended(p)) == p);
#else
  for (const fs::path p : {"/a/b", "relative/x", "/a/../b"}) {
    CHECK(host_path::Extended(p) == p);
    CHECK(host_path::Ordinary(p) == p);
  }
#endif
  CHECK(host_path::Extended(fs::path()).empty());
}

TEST_CASE("files work at every path length", "[paths][fs-io]") {
  Root root("fsio");
  size_t serial = 0;
  for (const size_t length : {100, 259, 260, 320, 500, 1024, 1100, 2048}) {
    for (const auto& leaf : {kDlc, kUnicode, fs::path("plain.dat")}) {
      // A folder per case: a scanner may briefly hold the previous one open.
      const auto base = root.path / (std::to_string(length) + "-" + std::to_string(serial++));
      const auto file = Exact(base, length, leaf);
      if (Length(base) + 1 + Length(leaf) >= length) {
        WARN("temporary folder too deep for length " << length);
        continue;
      }
      REQUIRE(Length(file) == length);
      if (!PathSupported(length)) {
        WARN("host path limit below " << length);
        continue;
      }
      ExerciseFile(base, file);
      std::error_code ec;
      io::remove_all(base, ec);
      if (ec) WARN("cleanup of " << x360n::PathUtf8(base) << " deferred: " << ec.message());
    }
  }
}

TEST_CASE("dot-dot, relative and missing paths resolve like the host", "[paths][fs-io]") {
  Root root("fs_io_spelling");
  const auto deep = Exact(root.path / "deep", 320, "d");
  // An extended name is not normalized by Windows; the facade normalizes first.
  const auto dotted = deep / ".." / "d" / "." / "file.txt";
  io::create_directories(deep);
  { io::ofstream(dotted, std::ios::binary) << "dots"; }
  CHECK(Slurp(deep / "file.txt") == "dots");
  CHECK(io::exists(dotted));

  const auto relative = (deep / "file.txt").lexically_relative(fs::current_path());
  if (!relative.empty() && !relative.is_absolute()) {
    CHECK(io::exists(relative));
    CHECK(Slurp(relative) == "dots");
  }

  const auto missing = deep / "missing" / kDlc;
  std::error_code ec;
  CHECK_FALSE(io::exists(missing));
  CHECK_FALSE(io::exists(missing, ec));
  CHECK_FALSE(ec);
  try {
    (void)io::file_size(missing);
    FAIL("file_size of a missing file must throw");
  } catch (const fs::filesystem_error& error) {
    // Errors name the file as the user knows it.
    CHECK(error.path1() == missing.lexically_normal());
    CHECK_FALSE(host_path::IsExtended(error.path1()));
  }
  // A clean environment: a whole missing tree is created from nothing.
  const auto fresh = Exact(root.path / "fresh", 700, kUnicode);
  if (PathSupported(700)) {
    io::create_directories(fresh.parent_path());
    { io::ofstream(fresh, std::ios::binary) << "new"; }
    CHECK(Slurp(fresh) == "new");
  }
}

TEST_CASE("TOML files are read from deep folders and missing ones still fail", "[paths][fs-io]") {
  Root root("fs_io_toml");
  const auto file = Exact(root.path, 400, "preparation.toml");
  io::create_directories(file.parent_path());
  { io::ofstream(file, std::ios::binary) << "[project]\nname = \"x\"\n"; }
  CHECK(x360n::ParseTomlFile(file)["project"]["name"].value_or(std::string()) == "x");
  CHECK_THROWS_AS(x360n::ParseTomlFile(file.parent_path() / "missing.toml"), toml::parse_error);
}

TEST_CASE("links and junctions are found at any depth", "[paths][fs-io][security]") {
  Root root("fs_io_links");
  const auto target = root.path / "target";
  io::create_directories(target);
  const auto deep = Exact(root.path / "deep", 300, "d");
  io::create_directories(deep);
  CHECK(io::redirected_component(deep / "x" / kDlc).empty());
#ifdef _WIN32
  const auto junction = deep / "junction";
  REQUIRE(MakeJunction(junction, target));
  // std::filesystem::is_symlink is false for a junction; the facade is not.
  CHECK_FALSE(fs::is_symlink(fs::symlink_status(io::Host(junction))));
  CHECK(io::is_redirect(junction));
  CHECK(io::redirected_component(junction / "child" / kDlc) == junction);
  bool listed = false;
  for (const auto& entry : io::directory_iterator(deep))
    if (entry.path() == junction) listed = entry.is_redirect();
  CHECK(listed);
  CHECK(io::remove(junction));
  CHECK(io::exists(target));
#else
  const auto link = deep / "symlink";
  fs::create_directory_symlink(target, link);
  CHECK(io::is_redirect(link));
  CHECK(io::redirected_component(link / kDlc) == link);
  CHECK(io::remove(link));
  CHECK(io::exists(target));
#endif
}

#ifdef _WIN32
// Windows lets only privileged or Developer Mode accounts create symbolic
// links; without that right this case reports itself skipped, never passed.
TEST_CASE("symbolic links are found at any depth on Windows", "[paths][fs-io][security]") {
  Root root("fs_io_symlinks");
  const auto target = root.path / "target";
  io::create_directories(target);
  const auto link = Exact(root.path / "deep", 300, "symlink");
  io::create_directories(link.parent_path());
  if (!CreateSymbolicLinkW(io::Host(link).c_str(), io::Logical(io::Host(target)).c_str(),
                           SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    const auto error = GetLastError();
    if (error == ERROR_PRIVILEGE_NOT_HELD) SKIP("this account may not create symbolic links");
    FAIL("CreateSymbolicLinkW failed with " << error);
  }
  CHECK(io::is_redirect(link));
  CHECK(io::redirected_component(link / kDlc) == link);
  CHECK(io::remove(link));
  CHECK(io::exists(target));
}

namespace {

std::string Output(const fs::path& file) {
  auto text = Slurp(file);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

// An installation (a folder holding deps.lock.json) at `root`.
fs::path Installation(const fs::path& root) {
  io::create_directories(root);
  { io::ofstream(root / "deps.lock.json", std::ios::binary) << "{}"; }
  return root;
}

// The alias folder ChildWorkingDirectory used for `folder` inside `install`.
fs::path AliasOf(const fs::path& spelling, const fs::path& folder, const fs::path& install) {
  auto alias = spelling;
  for (const auto& part : io::canonical(folder).lexically_relative(io::canonical(install))) {
    (void)part;
    alias = alias.parent_path();
  }
  return alias;
}

// Removes the junction itself (never its target) when the test ends.
struct AliasCleanup {
  fs::path alias;
  ~AliasCleanup() {
    if (!alias.empty()) RemoveDirectoryW(io::Host(alias).c_str());
  }
};

}  // namespace

TEST_CASE("processes start from deep Unicode folders and keep relative paths", "[paths][fs-io][process]") {
  Root root("fs_io_process");
  // A deep installation whose product folders sit a few levels inside it.
  const auto install = Installation(Exact(root.path, 280, u8"Instalação-ゲーム-🎮"));
  const auto deep = install / ".local" / "build" / "titles" / "castle_crashers";
  io::create_directories(deep);
  { io::ofstream(deep / "relative.txt", std::ios::binary) << "inside"; }
  { io::ofstream(deep.parent_path() / "sibling.txt", std::ios::binary) << "parent"; }
  const auto exe = deep / fs::path(X360N_TEST_CHILD).filename();
  io::copy_file(X360N_TEST_CHILD, exe);
  REQUIRE(Length(deep) > MAX_PATH);

  const auto spelling = x360n::ChildWorkingDirectory(deep);
  AliasCleanup cleanup{AliasOf(spelling, deep, install)};
  CHECK(Length(spelling) <= MAX_PATH - 2);
  CHECK(io::equivalent(spelling, deep));
  // Short names play no part: the alias is a junction, whatever the volume keeps.
  CHECK(fs::symlink_status(io::Host(cleanup.alias)).type() == fs::file_type::junction);
  CHECK(io::equivalent(cleanup.alias, install));
  // The executable itself is started by its name through the same alias.
  const auto program = x360n::ChildExecutable(exe);
  CHECK(Length(program) < MAX_PATH);
  CHECK_FALSE(host_path::IsExtended(program));
  CHECK(io::equivalent(program, exe));
  CHECK(x360n::ChildExecutable(X360N_TEST_CHILD) == io::Logical(io::Host(X360N_TEST_CHILD)));

  x360n::ProcessSpec spec{exe, {"cwd"}, deep, root.path / "out.log", root.path / "err.log", std::chrono::seconds(30)};
  auto result = x360n::RunProcess(spec);
  INFO(result.launch_error);
  REQUIRE(result.launched);
  REQUIRE(result.exit_code == 0);
  const auto printed = Output(root.path / "out.log");
  REQUIRE(printed.starts_with("cwd "));
  const auto cwd = x360n::PathFromUtf8(printed.substr(4));
  CHECK(io::equivalent(cwd, deep));
  CHECK_FALSE(host_path::IsExtended(cwd));
  for (const auto& [name, expected] : {std::pair{"relative.txt", "inside"}, std::pair{"..\\sibling.txt", "parent"}}) {
    INFO(name);
    spec.args = {"read", name};
    result = x360n::RunProcess(spec);
    REQUIRE(result.launched);
    CHECK(result.exit_code == 0);
    CHECK(Output(root.path / "out.log") == expected);
  }

  // A stale alias naming another folder is replaced; that folder is untouched.
  const auto elsewhere = root.path / "elsewhere";
  io::create_directories(elsewhere);
  { io::ofstream(elsewhere / "keep.txt", std::ios::binary) << "keep"; }
  REQUIRE(RemoveDirectoryW(io::Host(cleanup.alias).c_str()));
  REQUIRE(MakeJunction(cleanup.alias, elsewhere));
  CHECK(io::equivalent(x360n::ChildWorkingDirectory(deep), deep));
  CHECK(Slurp(elsewhere / "keep.txt") == "keep");
  // Anything but a junction at the alias is refused and left in place.
  REQUIRE(RemoveDirectoryW(io::Host(cleanup.alias).c_str()));
  io::create_directory(cleanup.alias);
  CHECK_THROWS_WITH(x360n::ChildWorkingDirectory(deep), Catch::Matchers::ContainsSubstring("unexpected file"));
  CHECK(io::is_directory(cleanup.alias));
  REQUIRE(RemoveDirectoryW(io::Host(cleanup.alias).c_str()));

  // A deep folder outside any installation is refused by name, never moved.
  const auto outside = Exact(root.path / "outside", 300, "bin");
  io::create_directories(outside);
  spec.working_dir = outside;
  spec.args = {"cwd"};
  result = x360n::RunProcess(spec);
  CHECK_FALSE(result.launched);
  CHECK(result.launch_error.find("outside an x360-native installation") != std::string::npos);
}

TEST_CASE("preparation tools reach long installations through their alias", "[paths][fs-io][process]") {
  Root root("fs_io_tools");
  // Lengths below are of canonical names; a temporary folder may be spelled
  // with short (8.3) names.
  const auto base = io::canonical(root.path);
  // Too long for the ~180 characters the tools need below it, though itself
  // well inside MAX_PATH; cmd cannot even start a script this deep.
  const auto install = Installation(Exact(base, 120, u8"Ferramentas-ゲーム-🎮"));
  const auto script = install / "scripts" / "devshell.cmd";
  io::create_directories(script.parent_path());
  { io::ofstream(script, std::ios::binary) << "@type \"%~dp0..\\marker.txt\"\r\n"; }
  { io::ofstream(install / "marker.txt", std::ios::binary) << "tools"; }
  const auto spelling = x360n::ToolPath(script);
  AliasCleanup cleanup{spelling.parent_path().parent_path()};
  CHECK(fs::symlink_status(io::Host(cleanup.alias)).type() == fs::file_type::junction);
  CHECK(io::equivalent(cleanup.alias, install));
  CHECK(x360n::ToolPath(install) == cleanup.alias);
  CHECK(Length(cleanup.alias) + 180 < MAX_PATH);
  wchar_t system[MAX_PATH]{};
  REQUIRE(GetSystemDirectoryW(system, MAX_PATH));
  x360n::ProcessSpec spec{fs::path(system) / L"cmd.exe", {"/d", "/c", "call", x360n::PathUtf8(spelling)}, root.path,
                          root.path / "out.log", root.path / "err.log", std::chrono::seconds(30)};
  const auto result = x360n::RunProcess(spec);
  INFO(result.launch_error << Output(root.path / "err.log"));
  REQUIRE(result.launched);
  CHECK(result.exit_code == 0);
  CHECK(Output(root.path / "out.log") == "tools");
  // Windows PowerShell 5.1 (.NET Framework) runs the launcher's tool scripts;
  // it fails to initialize when started by an extended name.
  const auto ps1 = install / "scripts" / "prepare-tools.ps1";
  { io::ofstream(ps1, std::ios::binary) << "[Console]::Out.Write((Get-Content -Raw (Join-Path $PSScriptRoot '..\\marker.txt')))\r\n"; }
  spec.exe = fs::path(system) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
  spec.args = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", x360n::PathUtf8(x360n::ToolPath(ps1))};
  const auto powershell = x360n::RunProcess(spec);
  INFO(powershell.launch_error << Output(root.path / "err.log"));
  REQUIRE(powershell.launched);
  CHECK(powershell.exit_code == 0);
  CHECK(Output(root.path / "out.log") == "tools");

  // Short installations and paths outside any installation keep their spelling.
  const auto outside = root.path / "outside.txt";
  CHECK(x360n::ToolPath(outside) == outside);
  const auto short_install = Installation(base / "s");
  if (Length(short_install) + 180 < MAX_PATH)
    CHECK(x360n::ToolPath(short_install / "scripts") == short_install / "scripts");
  else
    WARN("temporary folder too deep for a short installation");

}

namespace {
// Removes the short folders PlaceDependencyFolders made for `install` (the
// junctions go with the installation's own folder).
struct StoreCleanup {
  fs::path install;
  ~StoreCleanup() {
    std::error_code ec;
    for (const auto* name : {L".local\\toolchains", L"external"}) {
      const auto link = install / name;
      if (fs::symlink_status(io::Host(link), ec).type() != fs::file_type::junction) continue;
      const auto store = io::canonical(link, ec);
      RemoveDirectoryW(io::Host(link).c_str());
      if (!ec) io::remove_all(store, ec);
    }
  }
};
}  // namespace

// Git and CMake resolve every junction, the alias included, so no spelling
// helps them below a deep installation; its tools and SDK live in short real
// folders instead, reached by the installation's own relative paths.
TEST_CASE("preparation keeps the tools and the SDK of deep installations in short folders", "[paths][fs-io][process]") {
  Root root("fs_io_store");
  const auto base = io::canonical(root.path);
  // Beyond MAX_PATH itself, in Unicode, on a volume with or without 8.3 names.
  const auto install = Installation(Exact(base, 300, u8"Instalação-測試-ゲーム-🎮"));
  StoreCleanup cleanup{install};
  // Nothing placed yet: refused by name before any tool starts.
  CHECK_THROWS_WITH(x360n::ToolPath(install / "scripts" / "prepare-tools.ps1"),
                    Catch::Matchers::ContainsSubstring("Prepare from the application"));
  // A folder made in place before the installation moved here.
  const auto sdk = install / "external" / "rexglue-sdk";
  io::create_directories(sdk / ".git");
  { io::ofstream(sdk / ".git" / "HEAD", std::ios::binary) << "pinned"; }
  x360n::PlaceDependencyFolders(x360n::ProjectPaths{install});
  for (const auto* name : {".local/toolchains", "external"}) {
    INFO(name);
    const auto link = install / fs::path(name);
    CHECK(fs::symlink_status(io::Host(link)).type() == fs::file_type::junction);
    const auto store = io::canonical(link);
    CHECK_FALSE(x360n::IsWithin(store, install));
    CHECK(x360n::PathUtf8(store).size() + x360n::DependencyFolderDepth(name) <= MAX_PATH);
    // Named through the installation, the files are the store's.
    CHECK(x360n::ToolPath(link) == store);
  }
  // Moved on this volume, nothing copied or left behind.
  CHECK(Slurp(sdk / ".git" / "HEAD") == "pinned");
  bool set_aside = false;
  for (const auto& entry : io::directory_iterator(install))
    set_aside = set_aside || entry.path().filename().native().find(L".previous-") != std::wstring::npos;
  CHECK_FALSE(set_aside);
  // Preparation fills them through the installation; an existing layout is kept.
  const auto cmake = install / ".local" / "toolchains" / "cmake-3.31.6" / "bin";
  io::create_directories(cmake);
  { io::ofstream(cmake / "cmake.txt", std::ios::binary) << "tool"; }
  const auto store = io::canonical(install / ".local" / "toolchains");
  x360n::PlaceDependencyFolders(x360n::ProjectPaths{install});
  CHECK(io::canonical(install / ".local" / "toolchains") == store);
  CHECK(Slurp(store / "cmake-3.31.6" / "bin" / "cmake.txt") == "tool");
  const auto tool = x360n::ToolPath(cmake / "cmake.txt");
  CHECK(Length(tool) < MAX_PATH);
  CHECK(io::equivalent(tool, cmake / "cmake.txt"));
  // Other paths of the installation go through its alias, as before.
  const auto script = x360n::ToolPath(install / "deps.lock.json");
  AliasCleanup alias{script.parent_path()};
  CHECK(io::equivalent(script, install / "deps.lock.json"));
  // A process started inside the moved SDK runs from its short real folder.
  x360n::ProcessSpec spec{X360N_TEST_CHILD, {"read", "HEAD"}, sdk / ".git", root.path / "out.log",
                          root.path / "err.log", std::chrono::seconds(30)};
  const auto result = x360n::RunProcess(spec);
  INFO(result.launch_error);
  REQUIRE(result.launched);
  CHECK(Output(root.path / "out.log") == "pinned");

  // Folders that already fit stay where they are.
  const auto short_install = Installation(base / "s");
  if (Length(short_install) + 18 + x360n::DependencyFolderDepth(".local/toolchains") < MAX_PATH) {
    x360n::PlaceDependencyFolders(x360n::ProjectPaths{short_install});
    CHECK_FALSE(io::exists(short_install / ".local" / "toolchains"));
    CHECK_FALSE(io::exists(short_install / "external"));
  }
  // A developer checkout keeps its layout and is refused by name.
  const auto checkout = Installation(Exact(base / "checkout", 200, "x360-native"));
  io::create_directories(checkout / ".git" / "modules");
  x360n::PlaceDependencyFolders(x360n::ProjectPaths{checkout});
  CHECK_FALSE(io::exists(checkout / "external"));
  CHECK_THROWS_WITH(x360n::ToolPath(checkout / "scripts"), Catch::Matchers::ContainsSubstring("developer checkout"));
}

// DependencyFolderDepth() covers CMake's own modules, which CMake names by its
// real folder and Ninja stats as build dependencies.
TEST_CASE("the dependency folder depth covers this checkout's CMake modules", "[paths]") {
  const auto pinned = x360n::PathFromUtf8(X360N_SOURCE_ROOT) / ".local" / "toolchains";
  std::error_code ec;
  // A distribution's source tree has no toolchains of its own.
  if (!io::is_directory(pinned, ec)) SKIP("no pinned CMake in this checkout");
  const auto tools = x360n::CanonicalPath(pinned);
  size_t deepest = 0;
  for (const auto& tool : io::directory_iterator(tools, ec)) {
    if (!tool.is_directory() || !tool.path().filename().native().starts_with(L"cmake-")) continue;
    for (const auto& entry : io::recursive_directory_iterator(tool.path() / "share"))
      if (entry.is_regular_file() && entry.path().native().find(L"\\Modules\\") != std::wstring::npos)
        deepest = std::max(deepest, entry.path().native().size() - tools.native().size());
  }
  if (!deepest) SKIP("no pinned CMake in this checkout");
  INFO("deepest CMake module " << deepest << " characters below " << x360n::PathUtf8(tools));
  CHECK(deepest <= x360n::DependencyFolderDepth(".local/toolchains"));
}

// DependencyFolderDepth() covers every repository metadata directory of this
// checkout, submodules of submodules included: Git opens their HEAD and
// objects/info/alternates before core.longpaths applies.
TEST_CASE("the dependency folder depth covers this checkout's git metadata", "[paths]") {
  const auto root = x360n::CanonicalPath(x360n::PathFromUtf8(X360N_SOURCE_ROOT));
  const auto git_dir = [](const fs::path& worktree) -> std::optional<fs::path> {
    const auto dot = worktree / ".git";
    if (io::is_directory(dot)) return dot;
    if (!io::is_regular_file(dot)) return std::nullopt;
    auto text = Output(dot);
    if (!text.starts_with("gitdir: ")) return std::nullopt;
    const auto target = x360n::PathFromUtf8(text.substr(8));
    return target.is_absolute() ? target : worktree / target;
  };
  std::vector<fs::path> pending;
  for (const auto& worktree : {root, root / "external" / "rexglue-sdk"})
    if (const auto dir = git_dir(worktree)) pending.push_back(*dir);
  constexpr std::wstring_view kAlternates = L"\\objects\\info\\alternates";
  size_t deepest = 0, inside = 0;
  while (!pending.empty()) {
    const auto dir = pending.back();
    pending.pop_back();
    if (io::is_regular_file(dir / "HEAD")) {
      // Below the dependency folder holding it: a checkout's .git, or external
      // for an SDK cloned on its own.
      const auto name = x360n::CanonicalPath(dir).native();
      for (const auto* folder : {L".git", L"external"}) {
        const auto base = root.native() + L"\\" + folder;
        if (!name.starts_with(base + L"\\")) continue;
        const auto below = name.size() - base.size() + kAlternates.size();
        INFO(x360n::PathUtf8(dir) << ": " << below << " characters below " << x360n::PathUtf8(fs::path(base)));
        CHECK(below <= x360n::DependencyFolderDepth(folder));
        deepest = std::max(deepest, below);
        ++inside;
      }
      if (io::is_directory(dir / "modules")) pending.push_back(dir / "modules");
      continue;
    }
    for (const auto& entry : io::directory_iterator(dir))
      if (entry.is_directory()) pending.push_back(entry.path());
  }
  if (inside < 2) SKIP("no SDK repository metadata inside this checkout");
  WARN("deepest metadata " << deepest << " characters below its dependency folder in " << x360n::PathUtf8(root));
}

// The administrative share of the temporary folder's drive. UNC installations
// are supported, so an unreachable share fails these cases.
static fs::path RequireShare(const fs::path& local) {
  const auto drive = local.root_name().native();
  REQUIRE(drive.size() == 2);
  const fs::path share = L"\\\\localhost\\" + drive.substr(0, 1) + L"$";
  std::error_code ec;
  INFO("share " << share.string() << ": " << ec.message());
  REQUIRE(fs::is_directory(share / L"", ec));
  return share / local.relative_path();
}

TEST_CASE("UNC installation folders work beyond MAX_PATH", "[paths][fs-io][unc]") {
  Root root("fs_io_unc");
  const auto unc = RequireShare(fs::absolute(root.path));
  CHECK(host_path::Extended(unc).native().starts_with(L"\\\\?\\UNC\\localhost\\"));
  ExerciseFile(unc, Exact(unc / "share", 320, kDlc));
  ExerciseFile(unc, Exact(unc / "unicode", 300, kUnicode));
  // Windows' SMB client refuses a move between folders once the share-relative
  // name passes about 500 characters (ERROR_ACCESS_DENIED); renames in place
  // name only the leaf and work at this depth.
  ExerciseFile(unc, Exact(unc / "plain", 1100, "plain.dat"), false);
  const auto toml = Exact(unc / "toml", 400, "preparation.toml");
  io::create_directories(toml.parent_path());
  { io::ofstream(toml, std::ios::binary) << "[project]\nname = \"unc\"\n"; }
  CHECK(x360n::ParseTomlFile(toml)["project"]["name"].value_or(std::string()) == "unc");
  // The share and the local spelling name the same folder.
  CHECK(io::equivalent(unc / "toml", root.path / "toml"));
}

TEST_CASE("processes start from UNC folders and deep UNC folders are refused by name", "[paths][fs-io][unc][process]") {
  Root root("fs_io_unc_process");
  const auto install = Installation(root.path / "install");
  const auto unc = RequireShare(fs::absolute(install));
  const auto exe = unc / fs::path(X360N_TEST_CHILD).filename();
  io::copy_file(X360N_TEST_CHILD, exe);
  { io::ofstream(unc / "relative.txt", std::ios::binary) << "share"; }
  x360n::ProcessSpec spec{exe, {"read", "relative.txt"}, unc, root.path / "out.log", root.path / "err.log",
                          std::chrono::seconds(30)};
  auto result = x360n::RunProcess(spec);
  INFO(result.launch_error);
  REQUIRE(result.launched);
  CHECK(result.exit_code == 0);
  CHECK(Output(root.path / "out.log") == "share");

  // A junction cannot name a network share; Windows offers no other spelling.
  const auto deep = Exact(unc / "deep", 300, "bin");
  io::create_directories(deep);
  spec.working_dir = deep;
  result = x360n::RunProcess(spec);
  CHECK_FALSE(result.launched);
  CHECK(result.launch_error.find("network share") != std::string::npos);
  // Nor can preparation tools reach a long installation there.
  const auto long_install = Installation(Exact(unc / "tools", 120, "x360-native"));
  CHECK_THROWS_WITH(x360n::ToolPath(long_install / "scripts"), Catch::Matchers::ContainsSubstring("network share"));
  // Nor keep a deep one's tools in a local folder: its junctions would be
  // resolved by the server.
  const auto deep_install = Installation(Exact(unc / "store", 220, "x360-native"));
  CHECK_THROWS_WITH(x360n::PlaceDependencyFolders(x360n::ProjectPaths{deep_install}),
                    Catch::Matchers::ContainsSubstring("network share"));
  CHECK_FALSE(io::exists(deep_install / "external"));
}
#endif
