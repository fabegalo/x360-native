#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace x360n {
// Whether starting a title joins the signed-in account's Games Played.
// From dashboard 2.0.6683.0, an Xbox LIVE Arcade title running as a trial
// (its package grants no license) is not recorded; earlier dashboards record
// every game, as before.
using DashboardVersion = std::array<uint32_t, 4>;
inline constexpr DashboardVersion kTrialExcludedFrom{2, 0, 6683, 0};
inline constexpr uint32_t kArcadeContentType = 0x000D0000;  // XCONTENTTYPE_ARCADE

inline std::optional<DashboardVersion> ParseDashboardVersion(std::string_view text) {
  DashboardVersion version{};
  for (size_t i = 0; i < version.size(); ++i) {
    const auto end = i + 1 < version.size() ? text.find('.') : text.size();
    if (end == std::string_view::npos || end == 0) return std::nullopt;
    const auto part = text.substr(0, end);
    auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), version[i]);
    if (ec != std::errc{} || ptr != part.data() + part.size()) return std::nullopt;
    text.remove_prefix(i + 1 < version.size() ? end + 1 : end);
  }
  return version;
}

// The package the dashboard launched, as installed in its content store:
// <title>/<content type>/<package>/<image>, with the original aggregate
// header at <title>/Headers/<content type>/<package>.header. The SDK reads a
// license mask stored after the 0x148-byte header; a header without it grants
// no license.
struct LaunchedContent {
  uint32_t content_type = 0;
  uint32_t license_mask = 0;
};
inline std::optional<LaunchedContent> ReadLaunchedContent(const std::filesystem::path& image,
                                                          uint32_t title_id) {
  auto hex = [](const std::filesystem::path& name, uint32_t& value) {
    const auto text = name.string();
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return text.size() == 8 && ec == std::errc{} && ptr == text.data() + text.size();
  };
  auto package = image.parent_path();
  uint32_t type = 0, title = 0;
  // An entrypoint may be below a subdirectory inside the package.
  while (!package.empty()) {
    if (hex(package.parent_path().filename(), type) &&
        hex(package.parent_path().parent_path().filename(), title) && title == title_id)
      break;
    const auto parent = package.parent_path();
    if (parent == package) return std::nullopt;
    package = parent;
  }
  if (package.empty()) return std::nullopt;
  const auto type_dir = package.parent_path();
  const auto title_dir = type_dir.parent_path();
  // Recognized Arcade content with missing/bad metadata must not become Full.
  LaunchedContent content{type, 0};
  const auto header = title_dir / "Headers" / type_dir.filename() /
                      (package.filename().string() + ".header");
  std::ifstream in(header, std::ios::binary);
  if (!in) return content;
  std::array<unsigned char, 0x14C> bytes{};
  in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  const auto size = in.gcount();
  if (size < 0x148) return content;
  auto be32 = [&](size_t at) {
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 |
           uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
  };
  if (be32(4) != type || be32(0x140) != title) return content;
  // Host byte order, as ContentManager writes and reads it (little endian here).
  if (size >= 0x14C)
    content.license_mask = uint32_t(bytes[0x148]) | uint32_t(bytes[0x149]) << 8 |
                           uint32_t(bytes[0x14A]) << 16 | uint32_t(bytes[0x14B]) << 24;
  return content;
}

enum class GamesPlayedDecision { Record, SkipArcadeTrial, UnknownDashboard };
inline GamesPlayedDecision DecideGamesPlayed(const std::optional<DashboardVersion>& dashboard,
                                             const std::optional<LaunchedContent>& content,
                                             uint32_t active_license_mask = 0) {
  // Without a launched package the title is not identified as Arcade content.
  if (!content || content->content_type != kArcadeContentType ||
      content->license_mask != 0 || active_license_mask != 0)
    return GamesPlayedDecision::Record;
  if (!dashboard) return GamesPlayedDecision::UnknownDashboard;
  return *dashboard < kTrialExcludedFrom ? GamesPlayedDecision::Record
                                         : GamesPlayedDecision::SkipArcadeTrial;
}
}  // namespace x360n
