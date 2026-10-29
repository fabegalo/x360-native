#pragma once
#include <filesystem>
namespace x360n::content {
// Bounds-only preflight before the SDK mounts an untrusted update PIRS.
void ValidateUpdateContainer(const std::filesystem::path& package);
}
