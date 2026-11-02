// x360n::io and host_path: every installation folder works, at any length, in
// Unicode, through UNC and with dot-dot or relative spellings, and links are
// refused wherever they sit. Synthetic files only; no title or dashboard.
#include <catch2/catch_test_macros.hpp>

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
// use, on one file at `file` (its parent does not exist yet).
void ExerciseFile(const fs::path& root, const fs::path& file) {
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
  for (const size_t length : {100, 259, 260, 320, 500, 1024, 1100, 2048}) {
    for (const auto& leaf : {kDlc, kUnicode, fs::path("plain.dat")}) {
      const auto base = root.path / std::to_string(length);
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
      io::remove_all(base);
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

  bool linked = false;
#ifdef _WIN32
  const auto junction = deep / "junction";
  REQUIRE(MakeJunction(junction, target));
  linked = true;
  // std::filesystem::is_symlink is false for a junction; the facade is not.
  CHECK_FALSE(fs::is_symlink(fs::symlink_status(io::Host(junction))));
  CHECK(io::is_redirect(junction));
  CHECK(io::redirected_component(junction / "child" / kDlc) == junction);
  for (const auto& entry : io::directory_iterator(deep))
    if (entry.path() == junction) CHECK(entry.is_redirect());
  CHECK(io::remove(junction));
  CHECK(io::exists(target));
#endif
  const auto link = deep / "symlink";
  std::error_code ec;
  fs::create_directory_symlink(target, io::Host(link), ec);
  if (ec) {
    WARN("symbolic links unavailable here: " << ec.message());
  } else {
    linked = true;
    CHECK(io::is_redirect(link));
    CHECK(io::redirected_component(link / kDlc) == link);
    io::remove(link);
  }
  CHECK(linked);
}

#ifdef _WIN32
TEST_CASE("UNC installation folders work beyond MAX_PATH", "[paths][fs-io]") {
  Root root("fs_io_unc");
  const auto local = fs::absolute(root.path);
  const auto drive = local.root_name().native();
  const fs::path share = L"\\\\localhost\\" + drive.substr(0, 1) + L"$";
  std::error_code ec;
  if (drive.size() != 2 || !fs::exists(share / L"", ec)) {
    WARN("administrative share unavailable: " << ec.message());
    return;
  }
  const auto unc = share / local.relative_path();
  CHECK(host_path::Extended(unc).native().starts_with(L"\\\\?\\UNC\\localhost\\"));
  ExerciseFile(unc, Exact(unc / "share", 320, kDlc));
  ExerciseFile(unc, Exact(unc / "unicode", 300, kUnicode));
}

TEST_CASE("processes start from deep folders", "[paths][fs-io][process]") {
  Root root("fs_io_process");
  const auto deep = Exact(root.path, 300, "bin");
  io::create_directories(deep);
  const auto exe = deep / fs::path(X360N_TEST_CHILD).filename();
  io::copy_file(X360N_TEST_CHILD, exe);
  x360n::ProcessSpec spec{exe, {"cwd"}, root.path, root.path / "out.log", root.path / "err.log",
                          std::chrono::seconds(30)};
  auto result = x360n::RunProcess(spec);
  INFO(result.launch_error);
  REQUIRE(result.launched);
  CHECK(result.exit_code == 0);

  // A working directory beyond the host limit uses its 8.3 alias or is
  // refused by name; it never starts somewhere else.
  spec.working_dir = deep;
  result = x360n::RunProcess(spec);
  if (result.launched) {
    auto printed = Slurp(root.path / "out.log");
    REQUIRE(printed.starts_with("cwd "));
    printed.erase(0, 4);
    while (!printed.empty() && (printed.back() == '\n' || printed.back() == '\r')) printed.pop_back();
    CHECK(io::equivalent(fs::path(printed), deep));
  } else {
    CHECK(result.launch_error.find("Working directory exceeds") != std::string::npos);
  }
}
#endif
