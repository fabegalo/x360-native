// Synthetic session participant. Uses the original binary handoff protocol.
#include "session_profile.h"
#include "console_settings.h"
#include <CLI/CLI.hpp>
#include <chrono>
#include <thread>

using namespace x360n;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  CLI::App app;
  argv = app.ensure_utf8(argv);
  auto arg = [&](const std::string& prefix) {
    for (int i = 1; i < argc; ++i)
      if (std::string(argv[i]).starts_with(prefix)) return std::string(argv[i]).substr(prefix.size());
    return std::string{};
  };
  const auto mode = arg("--mode=");
  if (arg("--gpu_plugin=") != "xenos") return 76;
  const bool dashboard = arg("--role=") == "dashboard";
  const auto root = PathFromUtf8(arg("--game_data_root="));
  if (PathFromUtf8(arg("--x360n_profile_root=")) != root.parent_path() / "user/dashboard") return 75;
  const auto output = PathFromUtf8(arg("--x360n_handoff_out="));
  const auto input = arg("--x360n_handoff_in=");
  unsigned step = 0;
  if (!input.empty()) {
    LaunchRequest previous;
    if (!ReadLaunchRequest(PathFromUtf8(input), previous) || !previous.data_present ||
        previous.data.size() != 4 || previous.data[1] != 0 || previous.data[2] != 0xFE ||
        previous.data[3] != 0xFF) return 70;
    step = previous.data[0] + 1;
    if (dashboard ? (previous.kind != LaunchRequest::Dashboard || previous.flags != 0x100 ||
                     !previous.path.empty()) :
                    (previous.kind != LaunchRequest::Image || previous.flags != 0 ||
                     previous.path != PathUtf8(root / "game.xex"))) return 71;
  } else if (!dashboard) return 72;
  if ((step % 2 == 0) != dashboard) return 73;
  std::ofstream(root / "observed.txt", std::ios::app) << step << ' ' << dashboard << '\n';
  if (mode == "locale") {
    std::ofstream(root / "console-observed.txt", std::ios::app | std::ios::binary)
        << arg("--user_language=") << ' ' << arg("--user_country=") << '\n';
    if (!step) SaveConsoleSettings(ProjectPaths{root.parent_path().parent_path()}, {1, 103});
  }
  if (mode == "version")
    std::ofstream(root / "version-observed.txt", std::ios::app | std::ios::binary)
        << arg("--x360n_dashboard_version=") << '\n';
  // Real interaction is represented by a dwell every 16 launches. The other
  // transitions deliberately form fast bursts to test watchdog recovery.
  if (mode == "healthy" && step % 16 == 15)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  if (!dashboard && mode == "failure") return 9;
  if (!dashboard && mode == "timeout") std::this_thread::sleep_for(std::chrono::seconds(5));
  if (!dashboard && mode == "missing") return 0;
  LaunchRequest request;
  request.data_present = true;
  request.data = {uint8_t(step), 0, 0xFE, 0xFF};
  request.kind = dashboard ? LaunchRequest::Image : LaunchRequest::Dashboard;
  request.flags = dashboard ? 0 : 0x100;
  if (dashboard) request.path = PathUtf8(root / "game.xex");
  if (step == 80 || ((mode == "locale" || mode == "version") && step == 2)) { request.kind = LaunchRequest::WindowClose; request.path.clear(); }
  if (!dashboard && mode == "invalid-flags") request.flags = 0x200;
  if (!dashboard && mode == "invalid-target") {
    request.kind = LaunchRequest::Image; request.flags = 0;
    request.path = PathUtf8(root / "unregistered.xex");
  }
  if (!dashboard && mode == "tamper") std::ofstream(root / "game.xex") << "modified";
  return WriteLaunchRequest(output, request) ? 0 : 74;
}
