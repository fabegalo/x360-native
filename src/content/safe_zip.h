#pragma once
#include <filesystem>
namespace x360n::content {
void ExtractUpdateZip(const std::filesystem::path& source, const std::filesystem::path& destination);
}
