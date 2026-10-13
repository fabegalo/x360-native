#pragma once
#include <filesystem>
namespace x360n::content {
int Extract(const std::filesystem::path& package, const std::filesystem::path& destination);
int ExtractDisc(const std::filesystem::path& image, const std::filesystem::path& destination);
}
