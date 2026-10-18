// Explicit diagnostic of a real generated DLL's narrow registration ABI.
// Loads native exports and records mappings; never executes guest functions or
// publishes a title preparation receipt. It does not exchange runtime objects
// or CRT-owned containers with the DLL.
#include "sha256.h"
#include "xex_identity.h"
#include "paths.h"
#include <rex/image_info.h>
#include <rex/system/function_dispatcher.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <sstream>
#include <windows.h>

namespace fs = std::filesystem;
struct Collector final : rex::runtime::IModuleRegistrar {
  std::map<uint32_t, PPCFunc*> functions;
  bool valid = true;
  bool SetFunction(uint32_t address, PPCFunc* fn) override {
    const bool accepted = address && !(address & 3) && fn && functions.emplace(address, fn).second;
    valid = valid && accepted;
    return accepted;
  }
};
int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  if (argc < 4) { std::cerr << "usage: module_probe original.xex native.dll expected_entry_hex|auto [required_callback_hex ...]\n"; return 2; }
  try {
    const auto original = fs::absolute(x360n::PathFromUtf8(argv[1]));
    const auto native = fs::absolute(x360n::PathFromUtf8(argv[2]));
    const auto input_hash = x360n::DigestFile(original), native_hash = x360n::DigestFile(native);
    if (!input_hash || !native_hash) return 3;
    std::ifstream in(original, std::ios::binary);
    std::vector<uint8_t> header(24); in.read(reinterpret_cast<char*>(header.data()), header.size());
    if (in.gcount() != 24) return 4;
    const uint32_t size = uint32_t(header[8]) << 24 | uint32_t(header[9]) << 16 | uint32_t(header[10]) << 8 | header[11];
    if (size < 24 || size > 16 * 1024 * 1024 || size > input_hash->size) return 4;
    header.resize(size); in.clear(); in.seekg(0); in.read(reinterpret_cast<char*>(header.data()), size);
    if (in.gcount() != size) return 4;
    const auto parsed = x360n::ParseXexIdentity(header);
    if (!parsed.identity) return 4;
    const uint32_t original_entry = parsed.identity->entry_point.value_or(0);
    const uint32_t entry = std::string(argv[3]) == "auto" ? original_entry :
        static_cast<uint32_t>(std::stoul(argv[3], nullptr, 16));
    if (original_entry != entry) return 4;
    const auto library = LoadLibraryExW(native.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) { std::cerr << "LoadLibrary error=" << GetLastError() << '\n'; return 5; }
    struct Unload { HMODULE library; ~Unload() { FreeLibrary(library); } } unload{library};
    const auto image_fn = reinterpret_cast<const rex::PPCImageInfo* (*)()>(GetProcAddress(library, "ReXModule_GetImageInfo"));
    const auto register_fn = reinterpret_cast<rex::runtime::FunctionDispatcher::RegisterFn>(GetProcAddress(library, "ReXModule_Register"));
    const auto host_fn = reinterpret_cast<bool (*)()>(GetProcAddress(library, "ReXModule_UsesHostFunctionTable"));
    if (!image_fn || !register_fn) return 6;
    const auto* image = image_fn();
    if (!image || image->image_base != parsed.identity->image_base || image->image_size != parsed.identity->image_size) return 7;
    Collector collector;
    register_fn(&collector);
    if (!collector.valid) return 8;
    std::vector<uint32_t> required;
    // Resource-only XEX modules legitimately have no entry point. They still
    // require matching original/native image layout and a valid registration.
    if (entry) required.push_back(entry);
    for (int i = 4; i < argc; ++i)
      required.push_back(static_cast<uint32_t>(std::stoul(argv[i], nullptr, 16)));
    for (auto address : required) {
      const auto mapping = collector.functions.find(address);
      if (mapping == collector.functions.end()) {
        std::cerr << "required mapping missing: " << std::hex << address << '\n'; return 8;
      }
      HMODULE owner = nullptr;
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(mapping->second), &owner) || owner != library) return 9;
    }
    std::ostringstream addresses;
    for (const auto& [address, function] : collector.functions)
      addresses << std::hex << std::setfill('0') << std::setw(8) << address << '\n';
    const auto address_bytes = addresses.str();
    const auto address_sha = x360n::Sha256Hex(std::span(
        reinterpret_cast<const uint8_t*>(address_bytes.data()), address_bytes.size()));
    std::cout << "diagnostic_only=true guest_execution=false\noriginal_sha256=" << input_hash->sha256
        << "\nnative_sha256=" << native_hash->sha256 << "\nmappings=" << collector.functions.size()
        << "\nmapping_addresses_sha256=" << address_sha
        << "\nimage_base=" << std::hex << image->image_base << " image_size=" << image->image_size
        << " entry=" << entry << " entry_metadata_present=" << parsed.identity->entry_point.has_value()
        << " entry_owned_by_native_module=" << (entry ? "true" : "not_applicable")
        << " required_mappings=" << std::dec << required.size()
        << " required_mappings_owned_by_native_module=true\nhost_function_table="
        << (host_fn && host_fn() ? "true" : "false") << '\n';
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 10; }
}
