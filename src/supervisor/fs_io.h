// Filesystem access for logical paths. Same names and signatures as
// std::filesystem; each call reaches the host through host_path::Extended, so
// any installation folder works on Windows (long, Unicode, UNC) with no system
// setting, and nothing changes on other hosts (as Rust's std and RPCS3 do).
// Results and filesystem_error paths come back in the ordinary spelling;
// iterator entries keep the spelling of the directory being enumerated, as in
// std::filesystem. Header-only, like host_path.h.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "host_path.h"

#ifdef _WIN32
struct _SECURITY_ATTRIBUTES;
#endif

namespace x360n::io {
namespace fs = std::filesystem;

inline fs::path Host(const fs::path& path) { return host_path::Extended(path); }
inline fs::path Logical(const fs::path& path) { return host_path::Ordinary(path); }

namespace detail {
[[noreturn]] inline void Rethrow(const char* operation, const fs::filesystem_error& error) {
  if (error.path2().empty())
    throw fs::filesystem_error(operation, Logical(error.path1()), error.code());
  throw fs::filesystem_error(operation, Logical(error.path1()), Logical(error.path2()), error.code());
}
}  // namespace detail

#define X360N_IO_ONE_PATH(name)                                                    \
  template <class... Args>                                                         \
  decltype(auto) name(const fs::path& path, Args&&... args) {                      \
    try {                                                                          \
      return fs::name(Host(path), std::forward<Args>(args)...);                    \
    } catch (const fs::filesystem_error& error) { detail::Rethrow(#name, error); } \
  }
#define X360N_IO_TWO_PATHS(name)                                                   \
  template <class... Args>                                                         \
  decltype(auto) name(const fs::path& from, const fs::path& to, Args&&... args) {  \
    try {                                                                          \
      return fs::name(Host(from), Host(to), std::forward<Args>(args)...);          \
    } catch (const fs::filesystem_error& error) { detail::Rethrow(#name, error); } \
  }
X360N_IO_ONE_PATH(exists)
X360N_IO_ONE_PATH(is_directory)
X360N_IO_ONE_PATH(is_regular_file)
X360N_IO_ONE_PATH(is_symlink)
X360N_IO_ONE_PATH(is_empty)
X360N_IO_ONE_PATH(status)
X360N_IO_ONE_PATH(symlink_status)
X360N_IO_ONE_PATH(file_size)
X360N_IO_ONE_PATH(hard_link_count)
X360N_IO_ONE_PATH(last_write_time)
X360N_IO_ONE_PATH(create_directory)
X360N_IO_ONE_PATH(create_directories)
X360N_IO_ONE_PATH(remove)
X360N_IO_ONE_PATH(remove_all)
X360N_IO_ONE_PATH(resize_file)
X360N_IO_ONE_PATH(space)
X360N_IO_ONE_PATH(permissions)
X360N_IO_TWO_PATHS(rename)
X360N_IO_TWO_PATHS(copy)
X360N_IO_TWO_PATHS(copy_file)
X360N_IO_TWO_PATHS(equivalent)
X360N_IO_TWO_PATHS(create_hard_link)
#undef X360N_IO_ONE_PATH
#undef X360N_IO_TWO_PATHS
// Queries on an already obtained status touch no path.
inline bool exists(fs::file_status status) noexcept { return fs::exists(status); }
inline bool is_directory(fs::file_status status) noexcept { return fs::is_directory(status); }
inline bool is_regular_file(fs::file_status status) noexcept { return fs::is_regular_file(status); }
inline bool is_symlink(fs::file_status status) noexcept { return fs::is_symlink(status); }

// A name that redirects to another place: a symbolic link or, on Windows, a
// junction (mount point). fs::is_symlink is false for junctions, so security
// checks use this. Other reparse points (cloud placeholders, deduplicated
// files) hold their own data and are ordinary files or directories.
inline bool is_redirect(fs::file_status status) noexcept {
#ifdef _WIN32
  return status.type() == fs::file_type::symlink || status.type() == fs::file_type::junction;
#else
  return fs::is_symlink(status);
#endif
}
inline bool is_redirect(const fs::path& path, std::error_code& ec) noexcept {
  return is_redirect(fs::symlink_status(Host(path), ec));
}
inline bool is_redirect(const fs::path& path) {
  try { return is_redirect(fs::symlink_status(Host(path))); } catch (const fs::filesystem_error& e) { detail::Rethrow("symlink_status", e); }
}

// The first component of path's absolute, normalized ordinary spelling that
// redirects, from the filesystem root down; empty when there is none. Walks
// ordinary names (an extended name's ancestors end at the invalid "\\?\C:")
// and inspects each one through the host, so depth beyond MAX_PATH is checked
// too. The volume root (a UNC server and share) is not a link; a missing
// component is not one either, and an unreadable one is an error.
inline fs::path redirected_component(const fs::path& path) {
  const auto absolute = fs::absolute(Logical(Host(path))).lexically_normal();
  const bool unc = absolute.root_name().native().size() > 2;
  fs::path prefix = absolute.root_path();
  bool share = unc;
  for (const auto& part : absolute.relative_path()) {
    prefix /= part;
    if (std::exchange(share, false)) continue;
    std::error_code ec;
    const auto status = fs::symlink_status(Host(prefix), ec);
    if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory)
      throw fs::filesystem_error("symlink_status", prefix, ec);
    if (!ec && is_redirect(status)) return prefix;
  }
  return {};
}

// A link's target is stored as written (it may be relative); only the new
// link's own name is a host name.
inline void create_symlink(const fs::path& target, const fs::path& link) {
  try { fs::create_symlink(target, Host(link)); } catch (const fs::filesystem_error& e) { detail::Rethrow("create_symlink", e); }
}
inline void create_directory_symlink(const fs::path& target, const fs::path& link) {
  try { fs::create_directory_symlink(target, Host(link)); } catch (const fs::filesystem_error& e) { detail::Rethrow("create_directory_symlink", e); }
}

#ifdef _WIN32
namespace detail {
// The final name as opened: resolved through every link like the normalized
// name, without its per-component normalization, which some redirectors
// refuse. Windows' own SMB loopback share answers ERROR_BAD_NET_RESP for a
// folder that holds a non-BMP (emoji) name; FAT and older servers answer
// ERROR_INVALID_FUNCTION or ERROR_NOT_SUPPORTED (Python's realpath has the
// same fallback). Declared here so this header does not bring in <windows.h>.
extern "C" {
__declspec(dllimport) void* __stdcall CreateFileW(const wchar_t*, unsigned long, unsigned long,
                                                  ::_SECURITY_ATTRIBUTES*, unsigned long, unsigned long, void*);
__declspec(dllimport) unsigned long __stdcall GetFinalPathNameByHandleW(void*, wchar_t*, unsigned long, unsigned long);
__declspec(dllimport) int __stdcall CloseHandle(void*);
__declspec(dllimport) unsigned long __stdcall GetLastError();
}
inline bool NormalizationRefused(const std::error_code& ec) {
  return ec.category() == std::system_category() && (ec.value() == 1 || ec.value() == 50 || ec.value() == 58);
}
inline fs::path OpenedFinalName(const fs::path& host, std::error_code& ec) {
  constexpr unsigned long kShareAll = 7, kOpenExisting = 3, kBackupSemantics = 0x02000000, kNameOpened = 0x8;
  void* const file = CreateFileW(host.c_str(), 0, kShareAll, nullptr, kOpenExisting, kBackupSemantics, nullptr);
  if (file == reinterpret_cast<void*>(-1)) {
    ec.assign(int(GetLastError()), std::system_category());
    return {};
  }
  std::wstring name(512, L'\0');
  auto count = GetFinalPathNameByHandleW(file, name.data(), static_cast<unsigned long>(name.size()), kNameOpened);
  if (count >= name.size()) {
    name.resize(size_t(count) + 1);
    count = GetFinalPathNameByHandleW(file, name.data(), static_cast<unsigned long>(name.size()), kNameOpened);
  }
  const auto error = GetLastError();
  CloseHandle(file);
  if (!count || count >= name.size()) {
    ec.assign(int(error), std::system_category());
    return {};
  }
  name.resize(count);
  ec.clear();
  return name;
}
}  // namespace detail
#endif

// fs::canonical through the host name; the result is ordinary.
inline fs::path canonical(const fs::path& path, std::error_code& ec) {
  const auto host = Host(path);
  auto result = fs::canonical(host, ec);
#ifdef _WIN32
  if (ec && detail::NormalizationRefused(ec)) result = detail::OpenedFinalName(host, ec);
#endif
  return ec ? fs::path() : Logical(result);
}
inline fs::path canonical(const fs::path& path) {
  std::error_code ec;
  auto result = x360n::io::canonical(path, ec);
  if (ec) throw fs::filesystem_error("canonical", Logical(Host(path)), ec);
  return result;
}

// weakly_canonical without walking an extended name: the longest existing
// prefix of the normalized ordinary path is resolved through the host and the
// rest is appended, as std::filesystem::weakly_canonical specifies.
inline fs::path weakly_canonical(const fs::path& path, std::error_code& ec) {
  ec.clear();
#ifdef _WIN32
  const auto normal = Logical(Host(path));
  fs::path existing = normal, rest;
  while (true) {
    std::error_code probe;
    if (fs::exists(Host(existing), probe)) break;
    if (probe && probe != std::errc::no_such_file_or_directory) { ec = probe; return {}; }
    const auto parent = existing.parent_path();
    if (parent.empty() || parent == existing) return normal;  // nothing exists yet
    rest = rest.empty() ? existing.filename() : existing.filename() / rest;
    existing = parent;
  }
  auto resolved = x360n::io::canonical(existing, ec);
  if (ec) return {};
  return rest.empty() ? resolved : (resolved / rest).lexically_normal();
#else
  return fs::weakly_canonical(path, ec);
#endif
}
inline fs::path weakly_canonical(const fs::path& path) {
  std::error_code ec;
  auto result = x360n::io::weakly_canonical(path, ec);
  if (ec) throw fs::filesystem_error("weakly_canonical", path, ec);
  return result;
}

// Directory entry with its logical path. Attributes come from the host entry,
// cached by the enumeration (no second lookup through the logical name).
class directory_entry {
 public:
  directory_entry() = default;
  directory_entry(const fs::directory_entry& host, fs::path path) : host_(host), path_(std::move(path)) {}
  const fs::path& path() const noexcept { return path_; }
  operator const fs::path&() const noexcept { return path_; }
  const fs::directory_entry& host() const noexcept { return host_; }
  template <class... A> bool exists(A&... a) const { return host_.exists(a...); }
  template <class... A> bool is_directory(A&... a) const { return host_.is_directory(a...); }
  template <class... A> bool is_regular_file(A&... a) const { return host_.is_regular_file(a...); }
  template <class... A> bool is_symlink(A&... a) const { return host_.is_symlink(a...); }
  bool is_redirect() const { return x360n::io::is_redirect(host_.symlink_status()); }
  bool is_redirect(std::error_code& ec) const noexcept { return x360n::io::is_redirect(host_.symlink_status(ec)); }
  template <class... A> bool is_other(A&... a) const { return host_.is_other(a...); }
  template <class... A> auto file_size(A&... a) const { return host_.file_size(a...); }
  template <class... A> auto last_write_time(A&... a) const { return host_.last_write_time(a...); }
  template <class... A> auto status(A&... a) const { return host_.status(a...); }
  template <class... A> auto symlink_status(A&... a) const { return host_.symlink_status(a...); }
  template <class... A> void refresh(A&... a) { host_.refresh(a...); }

 private:
  fs::directory_entry host_;
  fs::path path_;
};

template <class Base>
class basic_directory_iterator {
 public:
  using iterator_category = std::input_iterator_tag;
  using value_type = directory_entry;
  using difference_type = std::ptrdiff_t;
  using pointer = const directory_entry*;
  using reference = const directory_entry&;

  basic_directory_iterator() = default;
  // Entries keep the spelling of `path`, as std::filesystem builds them, so
  // they compare with paths made from the same base; only the enumeration
  // itself goes through the host name.
  template <class... Args>
  explicit basic_directory_iterator(const fs::path& path, Args&&... args) : base_(path) {
    const auto host = Host(path);
    host_size_ = host.native().size();
    try { it_ = Base(host, std::forward<Args>(args)...); } catch (const fs::filesystem_error& e) { detail::Rethrow("directory_iterator", e); }
    Load();
  }
  reference operator*() const noexcept { return entry_; }
  pointer operator->() const noexcept { return &entry_; }
  basic_directory_iterator& operator++() {
    try { ++it_; } catch (const fs::filesystem_error& e) { detail::Rethrow("directory_iterator::operator++", e); }
    Load();
    return *this;
  }
  basic_directory_iterator& increment(std::error_code& ec) { it_.increment(ec); Load(); return *this; }
  bool operator==(const basic_directory_iterator& other) const noexcept { return it_ == other.it_; }
  int depth() const requires std::is_same_v<Base, fs::recursive_directory_iterator> { return it_.depth(); }
  bool recursion_pending() const requires std::is_same_v<Base, fs::recursive_directory_iterator> { return it_.recursion_pending(); }
  void disable_recursion_pending() requires std::is_same_v<Base, fs::recursive_directory_iterator> { it_.disable_recursion_pending(); }
  void pop() requires std::is_same_v<Base, fs::recursive_directory_iterator> {
    try { it_.pop(); } catch (const fs::filesystem_error& e) { detail::Rethrow("recursive_directory_iterator::pop", e); }
    Load();
  }
  void pop(std::error_code& ec) requires std::is_same_v<Base, fs::recursive_directory_iterator> { it_.pop(ec); Load(); }
  fs::directory_options options() const requires std::is_same_v<Base, fs::recursive_directory_iterator> { return it_.options(); }

 private:
  // The host enumeration names each entry host-base + separator + rest.
  void Load() {
    if (it_ == Base()) {
      entry_ = directory_entry();
      return;
    }
    const auto& name = it_->path().native();
    auto rest = name.substr(std::min(host_size_, name.size()));
    while (!rest.empty() && (rest.front() == fs::path::preferred_separator || rest.front() == '/')) rest.erase(0, 1);
    entry_ = directory_entry(*it_, base_ / fs::path(rest));
  }
  fs::path base_;
  size_t host_size_ = 0;
  Base it_;
  directory_entry entry_;
};
template <class Base> basic_directory_iterator<Base> begin(basic_directory_iterator<Base> it) noexcept { return it; }
template <class Base> basic_directory_iterator<Base> end(const basic_directory_iterator<Base>&) noexcept { return {}; }
using directory_iterator = basic_directory_iterator<fs::directory_iterator>;
using recursive_directory_iterator = basic_directory_iterator<fs::recursive_directory_iterator>;

// File streams that open the host name.
template <class Stream, std::ios::openmode Default>
class basic_file : public Stream {
 public:
  basic_file() = default;
  explicit basic_file(const fs::path& path, std::ios::openmode mode = Default) : Stream(Host(path), mode) {}
  void open(const fs::path& path, std::ios::openmode mode = Default) { Stream::open(Host(path), mode); }
};
using ifstream = basic_file<std::ifstream, std::ios::in>;
using ofstream = basic_file<std::ofstream, std::ios::out>;
using fstream = basic_file<std::fstream, std::ios::in | std::ios::out>;

}  // namespace x360n::io
