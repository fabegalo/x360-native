#include "launch_preflight.h"
#include "digest_batch.h"
#include "action_trace.h"
#include <algorithm>

namespace x360n {
void BindLibraryLaunchTargets(SessionProfile& profile,
    const std::vector<desktop::Target>& catalog) {
  namespace fs = std::filesystem;
  const auto dashboard = std::find_if(catalog.begin(), catalog.end(), [&](const auto& target) {
    return target.session && target.launch_library &&
        fs::weakly_canonical(target.manifest) == fs::weakly_canonical(profile.manifest);
  });
  if (dashboard == catalog.end()) return; // Explicit non-library session profiles.
  std::erase_if(profile.launch_targets, [&](const auto& manifest) {
    return std::none_of(catalog.begin(), catalog.end(), [&](const auto& game) {
      return desktop::InGameLibrary(game) && game.aot && game.import_error.empty() &&
          fs::weakly_canonical(game.manifest) == fs::weakly_canonical(manifest);
    });
  });
}
void PrimeLaunchVerification(const ProjectPaths& paths,
    const std::vector<desktop::Target>& catalog, const std::string& selected) {
  ActionTrace trace("launch.read-ahead");
  namespace fs = std::filesystem;
  const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& target) { return target.id == selected; });
  if (found == catalog.end()) return; // The mandatory gate reports this error.
  std::vector<fs::path> files;
  auto inputs = [&](const desktop::Target& target) {
    for (const auto& input : target.inputs)
      files.push_back(target.preparation_source.empty() ? input.destination :
          target.preparation_source / input.destination.lexically_relative(target.game_root));
  };
  auto installed = [&](const desktop::Target& game) {
    return desktop::InGameLibrary(game) && game.aot && !game.library.title_id.empty() && !game.inputs.empty() &&
        fs::is_regular_file(game.exe) && !fs::exists(desktop::PreparationMarker(paths, game));
  };
  auto copies = [&](const desktop::Target& dashboard, const desktop::Target& game) {
    if (dashboard.user_root.empty()) return;
    const auto folder = dashboard.user_root / "0000000000000000" / game.library.title_id /
        game.library.content_type / game.library.file_name;
    for (const auto& input : game.inputs) files.push_back(folder / input.destination.lexically_relative(game.game_root));
  };
  auto prime = [&](const desktop::Target& target) {
    inputs(target);
    if (target.session) {
      if (target.launch_library)
        for (const auto& game : catalog) if (installed(game)) { inputs(game); copies(target, game); }
    } else if (installed(target)) {
      for (const auto& dashboard : catalog)
        if (dashboard.session && dashboard.launch_library && !dashboard.inputs.empty() && fs::is_regular_file(dashboard.inputs.front().destination))
          copies(dashboard, target);
    }
  };
  prime(*found);
  for (const auto& id : found->dependencies)
    for (const auto& target : catalog) if (target.id == id) prime(target);
  PrimeVerificationDigests(files);
}
}
