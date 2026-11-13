#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <system_error>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <cerrno>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;

namespace x360n {
uint32_t CurrentProcessId() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return uint32_t(getpid());
#endif
}

fs::path CurrentExecutablePath() {
#ifdef _WIN32
  std::wstring buffer(1024, L'\0');
  for (;;) {
    const auto count = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!count) throw std::system_error(int(GetLastError()), std::system_category(), "GetModuleFileNameW");
    if (count < buffer.size()) { buffer.resize(count); return fs::path(buffer); }
    if (buffer.size() >= 32768) throw std::runtime_error("Executable path exceeds Windows path limit");
    buffer.resize(std::min<size_t>(buffer.size() * 2, 32768));
  }
#elif defined(__APPLE__)
  uint32_t count = 0;
  _NSGetExecutablePath(nullptr, &count);
  std::vector<char> buffer(count);
  if (buffer.empty() || _NSGetExecutablePath(buffer.data(), &count) != 0)
    throw std::runtime_error("Could not resolve native executable path");
  return io::canonical(fs::path(buffer.data()));
#else
  std::string buffer(1024, '\0');
  for (;;) {
    const auto count = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (count < 0) throw std::system_error(errno, std::generic_category(), "readlink /proc/self/exe");
    if (size_t(count) < buffer.size()) { buffer.resize(size_t(count)); return fs::path(buffer); }
    if (buffer.size() >= 1024 * 1024) throw std::runtime_error("Executable path exceeds diagnostic limit");
    buffer.resize(buffer.size() * 2);
  }
#endif
}

std::string PathUtf8(const fs::path& path) {
  auto value = path.u8string(); return {value.begin(), value.end()};
}
fs::path PathFromUtf8(const std::string& value) {
  return fs::path(std::u8string(value.begin(), value.end()));
}
fs::path LongPath(const fs::path& path) { return host_path::Extended(path); }
fs::path OrdinaryPath(const fs::path& path) { return host_path::Ordinary(path); }
fs::path CanonicalPath(const fs::path& path) { return io::canonical(path); }
namespace {
// FNV-1a over the UTF-8 installation path, independent of std::hash's
// implementation/CRT and stable across processes and updates.
std::string ShortProjectId(const fs::path& path) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : PathUtf8(fs::absolute(path).lexically_normal())) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string id(16, '0');
  for (int i = 15; i >= 0; --i) {
    id[size_t(i)] = digits[hash & 15];
    hash >>= 4;
  }
  return id;
}
}  // namespace

fs::path SharedSdkBuildRoot(const ProjectPaths& paths) {
#ifdef _WIN32
  // Use the current user's persistent local application data, not TEMP (which
  // may be deleted automatically) and not the install folder (which can be
  // arbitrarily deep). Avoid choosing a different drive via filesystem root.
  std::wstring local;
  if (const wchar_t* value = _wgetenv(L"LOCALAPPDATA"); value && *value)
    local = value;
  else
    local = fs::temp_directory_path().wstring();
  const auto root = fs::path(local) / "x360n" / "s" / ShortProjectId(paths.root);
  // SDL3's nested try_compile adds 113+ characters to the CMake binary dir.
  // Fail before making a broken cache if the OS profile itself is too deep.
  if (root.wstring().size() > 80)
    throw std::runtime_error("Windows user-local application-data path too deep "
                             "for CMake/Ninja shared SDK; select a shorter build "
                             "cache location");
  return root;
#else
  return paths.build() / "shared-sdk";
#endif
}

fs::path NativeBuildRoot(const ProjectPaths& paths) {
#ifdef _WIN32
  return SharedSdkBuildRoot(paths).parent_path().parent_path() / "b";
#else
  return paths.build();
#endif
}

fs::path NativeBuildDirectory(const ProjectPaths& paths, const fs::path& published) {
#ifdef _WIN32
  // <local>\x360n\b\<id>. A generic title's deepest object lies ~190
  // characters below it; fail before making a tree Ninja cannot build.
  const auto tree = NativeBuildRoot(paths) / ShortProjectId(io::weakly_canonical(published));
  if (tree.native().size() > 69)
    throw std::runtime_error("Windows user-local application-data path too deep "
                             "for CMake/Ninja title builds; select a shorter build "
                             "cache location");
  return tree;
#else
  (void)paths;
  return published;
#endif
}

fs::path SharedSdkGenerationPath(const ProjectPaths& paths,
                                 const std::string& generation_id) {
  if (generation_id.empty() || generation_id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
          std::string::npos)
    throw std::invalid_argument("Invalid shared SDK generation identity");
#ifdef _WIN32
  const auto path = SharedSdkBuildRoot(paths) / generation_id;
  if (path.wstring().size() > 104)
    throw std::runtime_error("Windows shared SDK generation path exceeds "
                             "the safe CMake/Ninja depth");
  return path;
#else
  return SharedSdkBuildRoot(paths) / generation_id;
#endif
}

fs::path UserDataRoot(const ProjectPaths& paths, const fs::path& default_root) {
  const auto setting = paths.local() / "user-data-root.txt";
  if (!io::exists(setting)) return default_root;
  io::ifstream in(setting, std::ios::binary); std::string value;
  if (!std::getline(in, value) || value.empty()) throw std::runtime_error("User data setting unreadable; select the storage folder again.");
  if (value.back() == '\r') value.pop_back();
  auto root = PathFromUtf8(value);
  if (!root.is_absolute()) throw std::runtime_error("User data path must be absolute.");
  for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()}) {
    auto rel = io::weakly_canonical(root).lexically_relative(io::weakly_canonical(protected_root));
    if (!rel.empty() && !rel.is_absolute() && *rel.begin() != "..") throw std::runtime_error("User data cannot share content, source or build directories.");
  }
  return root / default_root.filename();
}
void SetUserDataRoot(const ProjectPaths& paths, const fs::path& directory) {
  if (!directory.is_absolute() || !io::is_directory(directory)) throw std::runtime_error("Select an existing absolute storage folder.");
  for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()}) {
    auto rel = io::weakly_canonical(directory).lexically_relative(io::weakly_canonical(protected_root));
    if (!rel.empty() && !rel.is_absolute() && *rel.begin() != "..") throw std::runtime_error("Select storage outside content, source and build folders.");
  }
  io::create_directories(paths.local());
  const auto setting = paths.local() / "user-data-root.txt";
  auto staging = setting; staging += ".tmp";
  if (io::exists(staging)) throw std::runtime_error("Unfinished storage setting found. Existing data was preserved.");
  { io::ofstream out(staging, std::ios::binary); out << PathUtf8(directory) << '\n'; out.flush(); if (!out) throw std::runtime_error("Cannot save storage selection."); }
  std::error_code committed;
  io::rename(staging, setting, committed);
  if (committed) throw std::runtime_error("Cannot commit storage selection.");
}
namespace {

fs::path::string_type Folded(const fs::path& p) {
  auto s = p.lexically_normal().native();
#ifdef _WIN32
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  std::replace(s.begin(), s.end(), L'/', L'\\');
#endif
  while (s.size() > 1 && s.back() == fs::path::preferred_separator) s.pop_back();
  return s;
}

}  // namespace

std::optional<ProjectPaths> FindProjectRoot(const fs::path& start) {
  std::error_code ec;
  fs::path dir = io::weakly_canonical(start, ec);
  if (ec) return std::nullopt;
  if (!io::is_directory(dir, ec)) dir = dir.parent_path();
  while (true) {
    if (io::exists(dir / "deps.lock.json", ec)) return ProjectPaths{dir};
    fs::path parent = dir.parent_path();
    if (parent == dir) return std::nullopt;
    dir = parent;
  }
}

bool IsWithin(const fs::path& path, const fs::path& base) {
  std::error_code ec;
  const auto p = io::canonical(path, ec);
  if (ec) return false;
  const auto b = io::canonical(base, ec);
  if (ec) return false;
  // io::canonical answers ordinary spellings, comparable across both forms.
  const auto ps = Folded(p), bs = Folded(b);
  if (ps.size() < bs.size() || ps.compare(0, bs.size(), bs) != 0) return false;
  // On POSIX, a backslash is a filename character, not a directory boundary.
  // A filesystem root already ends in its separator ("/" on POSIX).
  return ps.size() == bs.size() || bs.back() == fs::path::preferred_separator ||
         ps[bs.size()] == fs::path::preferred_separator;
}

#ifdef _WIN32
namespace {
// Mount-point reparse data; its declaration lives in the kernel-mode ntifs.h.
struct MountPointHeader {
  ULONG tag;
  USHORT length, reserved, substitute_offset, substitute_length, print_offset, print_length;
};

// The ordinary target of the junction `link`, or nothing for anything else.
std::optional<std::wstring> JunctionTarget(const fs::path& link) {
  const auto handle = CreateFileW(io::Host(link).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                  nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
  std::vector<char> buffer(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  DWORD bytes = 0;
  const bool read = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(), DWORD(buffer.size()),
                                    &bytes, nullptr);
  CloseHandle(handle);
  if (!read || bytes < sizeof(MountPointHeader)) return std::nullopt;
  const auto* header = reinterpret_cast<const MountPointHeader*>(buffer.data());
  if (header->tag != IO_REPARSE_TAG_MOUNT_POINT ||
      sizeof(MountPointHeader) + size_t(header->substitute_offset) + header->substitute_length > bytes)
    return std::nullopt;
  std::wstring name(reinterpret_cast<const wchar_t*>(buffer.data() + sizeof(MountPointHeader) + header->substitute_offset),
                    header->substitute_length / sizeof(wchar_t));
  if (!name.starts_with(L"\\??\\")) return std::nullopt;
  return name.substr(4);
}

// Creates `link` as a new junction to the local directory `target`; false if
// `link` already exists (another process won the race).
bool CreateJunction(const fs::path& link, const fs::path& target) {
  const auto host = io::Host(link);
  if (!CreateDirectoryW(host.c_str(), nullptr)) {
    if (GetLastError() == ERROR_ALREADY_EXISTS) return false;
    throw std::system_error(int(GetLastError()), std::system_category(), "CreateDirectoryW " + PathUtf8(link));
  }
  const auto print = target.native();
  const auto substitute = L"\\??\\" + print;
  std::vector<char> buffer(sizeof(MountPointHeader) + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
  auto* header = reinterpret_cast<MountPointHeader*>(buffer.data());
  header->tag = IO_REPARSE_TAG_MOUNT_POINT;
  header->length = USHORT(buffer.size() - 8);
  header->substitute_length = USHORT(substitute.size() * sizeof(wchar_t));
  header->print_offset = USHORT(header->substitute_length + sizeof(wchar_t));
  header->print_length = USHORT(print.size() * sizeof(wchar_t));
  auto* names = reinterpret_cast<wchar_t*>(buffer.data() + sizeof(MountPointHeader));
  std::copy(substitute.c_str(), substitute.c_str() + substitute.size() + 1, names);
  std::copy(print.c_str(), print.c_str() + print.size() + 1, names + substitute.size() + 1);
  const auto handle = CreateFileW(host.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  DWORD bytes = 0, error = 0;
  if (handle == INVALID_HANDLE_VALUE) error = GetLastError();
  else {
    if (!DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()), nullptr, 0, &bytes,
                         nullptr))
      error = GetLastError();
    CloseHandle(handle);
  }
  if (error) {
    RemoveDirectoryW(host.c_str());
    throw std::system_error(int(error), std::system_category(), "FSCTL_SET_REPARSE_POINT " + PathUtf8(link));
  }
  return true;
}

// <local>\x360n\r\<id>: a verified junction to the local installation `root`
// (canonical). Throws the reason it cannot be made.
fs::path InstallationAlias(const ProjectPaths& project, const fs::path& root) {
  // A junction can only name a local volume.
  if (root.native().starts_with(L"\\\\")) throw std::runtime_error("the installation is on a network share");
  const auto alias = NativeBuildRoot(project).parent_path() / "r" / ShortProjectId(root);
  for (int attempt = 0;; ++attempt) {
    if (attempt == 3) throw std::runtime_error("its installation alias could not be created");
    std::error_code ec;
    const auto status = fs::symlink_status(io::Host(alias), ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
      throw fs::filesystem_error("symlink_status", alias, ec);
    if (!ec && status.type() != fs::file_type::not_found) {
      if (status.type() != fs::file_type::junction)
        throw std::runtime_error("an unexpected file occupies its installation alias " + PathUtf8(alias));
      if (const auto current = JunctionTarget(alias); current && Folded(*current) == Folded(root)) break;
      // A stale alias (another target): remove the link itself, never its target.
      if (!RemoveDirectoryW(io::Host(alias).c_str()))
        throw std::system_error(int(GetLastError()), std::system_category(), "RemoveDirectoryW " + PathUtf8(alias));
      continue;
    }
    io::create_directories(alias.parent_path());
    if (CreateJunction(alias, root)) break;
  }
  if (Folded(io::canonical(alias)) != Folded(root)) throw std::runtime_error("its installation alias resolves elsewhere");
  return alias;
}

// The deepest name the preparation tools open below an installation root:
// 177 characters, in the SDK's third-party test data.
constexpr size_t kToolDepth = 180;

// Folders below an installation that Git, CMake and Ninja name by their real
// path: they resolve junctions, the installation alias included. Git opens
// repository metadata before it applies core.longpaths, and Ninja refuses to
// stat a name over MAX_PATH bytes of UTF-8 unless Windows opts in to long
// paths. `depth` is the deepest name each opens below its folder:
// - .local\toolchains, CMake's own modules:
//   \cmake-3.31.6\share\cmake-3.31\Modules\CMakeExtraGeneratorDetermineCompilerMacrosAndIncludeDirs.cmake
// - external, the SDK as prepare-tools clones it:
//   \rexglue-sdk\.git\modules\thirdparty\vulkan-memory-allocator\objects\info\alternates
// - .git, a developer checkout's metadata, which stays where its developer put it:
//   \modules\external\rexglue-sdk\modules\thirdparty\vulkan-memory-allocator\objects\info\alternates
struct DependencyFolder {
  const wchar_t* name;
  size_t depth;
  const wchar_t* store;  // <local>\x360n\<store>\<id> when it must move
};
constexpr DependencyFolder kDependencyFolders[] = {
    {L".local\\toolchains", 101, L"t"}, {L"external", 84, L"e"},
    // FidelityFX's shader include handler also probes relative paths below its source.
    {L".local\\deps", 180, L"d"}, {L".git", 96, nullptr}};

// Ninja counts UTF-8 bytes; Win32 opens fewer than MAX_PATH UTF-16 units.
bool Fits(const fs::path& folder, size_t depth) {
  return PathUtf8(folder).size() + depth <= MAX_PATH && folder.native().size() + depth < MAX_PATH;
}

bool OnNetwork(const fs::path& root) {
  return root.native().starts_with(L"\\\\") ||
         GetDriveTypeW((root.root_name().native() + L"\\").c_str()) == DRIVE_REMOTE;
}

// Throws for a dependency folder of `root` (canonical) whose real name leaves
// its tools too little room.
void RequireDependencyFolders(const fs::path& root) {
  std::error_code ec;
  const bool checkout = io::exists(root / ".git", ec);
  for (const auto& folder : kDependencyFolders) {
    const auto link = root / folder.name;
    const bool present = folder.store ? io::exists(link, ec) : io::is_directory(link, ec);
    if (!present && !folder.store) continue;
    const auto real = present ? io::canonical(link) : link;
    if (Fits(real, folder.depth)) continue;
    throw std::runtime_error(
        "The folder " + PathUtf8(real) + " takes " + std::to_string(PathUtf8(real).size()) +
        " bytes of UTF-8, and Git, CMake and Ninja need " + std::to_string(folder.depth) +
        " more below it within the Windows limit of " + std::to_string(MAX_PATH) + ". " +
        (checkout ? "Move this developer checkout to a shorter folder."
                  : "Prepare from the application, which moves it to a short folder of this user."));
  }
}
}  // namespace
#endif

size_t DependencyFolderDepth(const fs::path& name) {
#ifdef _WIN32
  for (const auto& folder : kDependencyFolders)
    if (name.lexically_normal() == fs::path(folder.name)) return folder.depth;
#else
  (void)name;
#endif
  return 0;
}

void PlaceDependencyFolders(const ProjectPaths& paths) {
#ifdef _WIN32
  const auto root = io::canonical(paths.root);
  // A developer checkout's submodules name their metadata relative to it.
  if (io::exists(root / ".git")) return;
  for (const auto& folder : kDependencyFolders) {
    if (!folder.store) continue;
    const auto link = root / folder.name;
    std::error_code ec;
    const auto status = fs::symlink_status(io::Host(link), ec);
    if (ec && ec != std::errc::no_such_file_or_directory) throw fs::filesystem_error("symlink_status", link, ec);
    const bool present = !ec && status.type() != fs::file_type::not_found;
    if (Fits(present ? io::canonical(link) : link, folder.depth)) continue;
    const auto refuse = [&](const std::string& reason) {
      return std::runtime_error("The folder " + PathUtf8(link) + " is too deep for Git, CMake and Ninja, and " +
                                reason + ". Move the installation to a shorter folder.");
    };
    if (OnNetwork(root)) throw refuse("the installation is on a network share, which cannot name a local folder");
    const auto store = NativeBuildRoot(paths).parent_path() / folder.store / ShortProjectId(root);
    if (!Fits(store, folder.depth)) throw refuse("so is this user's application data folder " + PathUtf8(store));
    io::create_directories(store.parent_path());
    if (present) {
      if (status.type() == fs::file_type::junction) {
        // To an earlier store: the link itself, never its target.
        if (!RemoveDirectoryW(io::Host(link).c_str()))
          throw std::system_error(int(GetLastError()), std::system_category(), "RemoveDirectoryW " + PathUtf8(link));
      } else if (status.type() != fs::file_type::directory) {
        throw refuse("an unexpected file occupies it");
      } else if (io::exists(store) ||
                 !MoveFileExW(io::Host(link).c_str(), io::Host(store).c_str(), MOVEFILE_WRITE_THROUGH)) {
        // Made in place before (the installation moved here). Another volume or
        // an existing store: set it aside; preparation refills the new folder.
        auto previous = link;
        previous += L".previous-" + std::to_wstring(GetTickCount64());
        io::rename(link, previous);
      }
    }
    io::create_directories(store);
    io::create_directories(link.parent_path());
    try {
      CreateJunction(link, store);
    } catch (const std::system_error& error) {
      throw refuse(std::string("its file system cannot hold a junction (") + error.what() + ")");
    }
    if (Folded(io::canonical(link)) != Folded(io::canonical(store))) throw refuse("it resolves elsewhere");
  }
#else
  (void)paths;
#endif
}

fs::path ToolPath(const fs::path& path) {
#ifdef _WIN32
  const auto name = io::Logical(io::Host(path));
  // By its spelling: a path through a dependency folder's junction resolves
  // outside the installation, yet belongs to it.
  std::optional<ProjectPaths> project;
  std::error_code ec;
  for (auto dir = name.lexically_normal(); !project; dir = dir.parent_path()) {
    if (io::exists(dir / "deps.lock.json", ec)) project = ProjectPaths{dir};
    else if (dir.parent_path() == dir) return name;
  }
  const auto root = io::canonical(project->root);
  // Git and CMake resolve the alias, so it cannot help here.
  RequireDependencyFolders(root);
  if (root.native().size() + kToolDepth < MAX_PATH) return name;
  const auto real = io::weakly_canonical(name);
  const auto relative = real.lexically_relative(root);
  if (relative.empty()) return name;
  // Through a dependency folder's junction: its short real name.
  if (*relative.begin() == "..") return real;
  fs::path alias;
  try {
    alias = InstallationAlias(*project, root);
  } catch (const std::runtime_error& error) {
    throw std::runtime_error("The installation folder leaves the preparation tools less than the " +
                             std::to_string(kToolDepth) + " characters they need under the Windows limit of " +
                             std::to_string(MAX_PATH) + ", and " + error.what() + ": " + PathUtf8(root));
  }
  return relative == "." ? alias : alias / relative;
#else
  return path;
#endif
}

fs::path ChildExecutable(const fs::path& exe) {
#ifdef _WIN32
  const auto name = io::Logical(io::Host(exe));
  if (name.native().size() < MAX_PATH) return name;
  if (const auto project = FindProjectRoot(name.parent_path())) {
    try {
      const auto root = io::canonical(project->root);
      const auto relative = io::weakly_canonical(name).lexically_relative(root);
      if (!relative.empty() && *relative.begin() != "..") {
        const auto spelling = InstallationAlias(*project, root) / relative;
        if (spelling.native().size() < MAX_PATH) return spelling;
      }
    } catch (const std::runtime_error&) {
      // No alias (a network share, an occupied alias): native programs still
      // start from the extended name.
    }
  }
  return io::Host(exe);
#else
  return exe;
#endif
}

fs::path ChildWorkingDirectory(const fs::path& directory) {
#ifdef _WIN32
  const auto folder = io::Logical(io::Host(directory));
  if (folder.native().size() <= MAX_PATH - 2) return folder;
  const auto refuse = [&](const std::string& reason) {
    return std::runtime_error("Working directory exceeds the Windows limit of " + std::to_string(MAX_PATH - 2) +
                              " characters and " + reason + ": " + PathUtf8(folder));
  };
  // Inside a dependency folder kept in a short folder (PlaceDependencyFolders).
  std::error_code ec;
  if (const auto real = io::canonical(folder, ec); !ec && real.native().size() <= MAX_PATH - 2) return real;
  const auto project = FindProjectRoot(folder);
  if (!project) throw refuse("is outside an x360-native installation");
  const auto root = io::canonical(project->root), target = io::canonical(folder);
  if (!IsWithin(target, root)) throw refuse("is outside its installation");
  fs::path alias;
  try {
    alias = InstallationAlias(*project, root);
  } catch (const std::runtime_error& error) {
    throw refuse(error.what());
  }
  const auto spelling = alias / target.lexically_relative(root);
  if (spelling.native().size() > MAX_PATH - 2) throw refuse("is too deep inside its installation");
  return spelling;
#else
  return directory;
#endif
}

std::optional<std::string> CheckLaunchable(const ProjectPaths& p, const fs::path& exe) {
  std::error_code ec;
  if (!io::is_regular_file(exe, ec)) return "not an existing file: " + PathUtf8(exe);
#ifdef _WIN32
  std::wstring ext = exe.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  if (ext != L".exe") return "only .exe build outputs can be launched: " + PathUtf8(exe);
#else
  if (access(exe.c_str(), X_OK) != 0) return "build output is not executable: " + PathUtf8(exe);
#endif
  if (io::exists(p.inputs(), ec) && IsWithin(exe, p.inputs()))
    return "refusing to launch a file from .local/inputs (untrusted content): " + PathUtf8(exe);
  bool in_build = io::exists(p.build(), ec) && IsWithin(exe, p.build());
  bool in_sdk_out = io::exists(p.sdk_out(), ec) && IsWithin(exe, p.sdk_out());
  if (!in_build && !in_sdk_out)
    return "executable is not a build output under .local/build or external/rexglue-sdk/out: " +
           PathUtf8(exe);
  return std::nullopt;
}

bool IsSafePackageEntryName(const std::string& name) {
  if (name.empty() || name == "." || name == ".." || name.size() > 255) return false;
  for (unsigned char c : name) {
    if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|')
      return false;
  }
  if (name.back() == ' ' || name.back() == '.') return false;
  std::string stem = name.substr(0, name.find('.'));
  for (auto& ch : stem) ch = char(std::toupper(static_cast<unsigned char>(ch)));
  static const char* kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                    "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                    "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const char* r : kReserved)
    if (stem == r) return false;
  return true;
}

bool IsSafeLabel(const std::string& label) {
  if (label.empty() || label.size() > 64) return false;
  return std::all_of(label.begin(), label.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
  }) && label.find("..") == std::string::npos;
}

}  // namespace x360n
