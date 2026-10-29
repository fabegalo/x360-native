// x360n-content apply-patch: rebuilds the loaded image of a module from an
// original base XEX2 and an original delta patch (.xexp), without changing either
// file, and checks the result against the page digests of the patched header.
#pragma once

#include <filesystem>
#include <cstdint>
#include <string_view>
#include <vector>

namespace x360n::content {

// Writes <out_dir>/<name>.header.bin (the target XEX header, as the patch produces
// it), <name>.image.bin (the patched image, decrypted and decompressed, before any
// loader import fixups) and <name>.report.txt. Refuses to overwrite. Returns 0 only
// if the base and the result both pass their page digest chains.
// The AES master keys are read from `key_file` (local, not in the repository): one per
// line, "<name> <32 hex digits>", names xex2-retail, xex2-devkit, xex1-retail,
// xex1-devkit; '#' starts a comment. A missing or malformed file is an error (exit 1).
int ApplyXexPatch(const std::filesystem::path& base, const std::filesystem::path& patch,
                  const std::filesystem::path& out_dir, const std::filesystem::path& key_file);

// $X360N_XEX_KEYS if set, else .local/keys/xex-keys.txt (relative to the working
// directory; .local/ is ignored by Git).
std::filesystem::path DefaultXexKeyFile();

// Decode an unencrypted compressed XEX with block/page digest verification.
// No runtime, keys, import fixups or PowerPC execution are involved.
std::vector<uint8_t> ReadUnencryptedXexResource(const std::filesystem::path& xex,
                                               std::string_view resource);

}  // namespace x360n::content
