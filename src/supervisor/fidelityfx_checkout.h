#pragma once
#include "paths.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace x360n::detail {
// Command retains the preparation driver's cancellation, timeout and logs.
template <typename Command>
void PrepareFidelityFxCheckout(const std::filesystem::path& source,
    const std::string& url, const std::string& commit, Command command) {
  const std::string path = PathUtf8(std::filesystem::absolute(source).lexically_normal());
  if (!io::exists(source / ".git")) {
    io::create_directories(source);
    command({"-C", path, "init", "-q"}, "fidelityfx-init");
  }
  // init may have succeeded before fetch/checkout failed or was cancelled.
  // --all includes detached HEAD and succeeds with no output in an empty repo.
  if (command({"-C", path, "rev-list", "--all", "--max-count=1"}, "fidelityfx-commits").empty()) {
    command({"-C", path, "fetch", "--depth", "1", url, commit}, "fidelityfx-fetch");
    command({"-C", path, "checkout", "-q", "--detach", "FETCH_HEAD"}, "fidelityfx-checkout");
  }
  if (!command({"-C", path, "rev-parse", "HEAD"}, "fidelityfx-pin").starts_with(commit))
    throw std::runtime_error("FidelityFX SDK commit differs from deps.lock.json: " + path);
  if (!command({"-C", path, "status", "--porcelain", "--untracked-files=no"}, "fidelityfx-clean").empty())
    throw std::runtime_error("FidelityFX SDK checkout has local changes: " + path);
}
}
