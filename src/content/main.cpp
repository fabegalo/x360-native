// x360n-content: reads Xbox 360 content packages (STFS: CON/LIVE/PIRS) with the
// SDK's StfsContainerDevice and extracts them safely.
//
//   info    <package>          header fields (magic, content type, execution info, names)
//   extract <package> <dest>   copy every file out; <dest> must not exist
//   dump-image <root> <xex> <out>  write the loaded (decrypted) XEX image, for diagnosis
//   imports <root> <xex>       imports as decoded by the SDK loader, with the SDK export entry
//   resources <root> <xex>     XEX resource table (what section:// locators resolve against)
//   disasm <root> <xex> <addr> <n> [before]  SDK (binutils) disassembly with .pdata marks
//   xref <root> <xex> <lo> <hi>  code that forms an address in [lo, hi]; data words holding one
//   callers <root> <xex> <target>  direct branches (b/bl/bc) to <target>
//
// The SDK's own dump routine (src/filesystem/vfs_dump.cpp, not built as a tool)
// writes base_path / entry->path() without checking names from the package. Here
// each name is validated before it touches the host filesystem, nothing is
// overwritten, and a SHA-256 manifest is written next to <dest> (not inside it).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <dis-asm.h>
#include <fmt/format.h>
#include <ppc.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xcontent.h>
#include <rex/system/xex_module.h>
#include <rex/system/xmemory.h>

#include "paths.h"
#include "sha256.h"

namespace fs = std::filesystem;
using rex::X_STATUS;  // X_STATUS_SUCCESS expands to a cast to it
using rex::filesystem::StfsContainerDevice;

namespace {

fs::path Utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string Utf16ToUtf8(const std::u16string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    uint32_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(s[++i]) - 0xDC00);
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

const char* MagicName(rex::filesystem::XContentPackageType m) {
  switch (m) {
    case rex::filesystem::XContentPackageType::kCon: return "CON";
    case rex::filesystem::XContentPackageType::kLive: return "LIVE";
    case rex::filesystem::XContentPackageType::kPirs: return "PIRS";
  }
  return "unknown";
}


int Info(const fs::path& package) {
  auto header = StfsContainerDevice::ReadPackageHeader(package);
  if (!header) {
    fmt::print(stderr, "info: not a readable STFS package: {}\n", package.string());
    return 1;
  }
  const auto& md = header->metadata;
  const auto& ei = md.execution_info;
  std::string content_id;
  for (uint8_t b : header->header.content_id) content_id += fmt::format("{:02X}", b);
  fmt::print("magic         {}\n", MagicName(header->header.magic));
  fmt::print("content_type  0x{:08X}\n", uint32_t(md.content_type.get()));
  fmt::print("content_id    {}\n", content_id);
  fmt::print("title_id      {:08X}\n", uint32_t(ei.title_id));
  fmt::print("media_id      {:08X}\n", uint32_t(ei.media_id));
  fmt::print("version       0x{:08X} (base 0x{:08X})\n", uint32_t(ei.version_value),
             uint32_t(ei.base_version_value));
  fmt::print("volume_type   {}\n", uint32_t(md.volume_type.get()));
  fmt::print("display_name  {}\n", Utf16ToUtf8(md.display_name(rex::system::XLanguage::kEnglish)));
  fmt::print("title_name    {}\n", Utf16ToUtf8(md.title_name()));
  return 0;
}

struct Extractor {
  fs::path dest;
  std::ofstream manifest;
  uint64_t files = 0, bytes = 0;
  int errors = 0;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(4 << 20);

  void Walk(rex::filesystem::Entry* entry, const fs::path& rel) {
    for (auto& child : entry->children()) {
      const std::string& name = child->name();
      if (!x360n::IsSafePackageEntryName(name)) {
        fmt::print(stderr, "extract: refusing unsafe entry name '{}' under '{}'\n", name,
                   rel.generic_string());
        ++errors;
        continue;
      }
      fs::path child_rel = rel / Utf8Path(name);
      fs::path out = dest / child_rel;
      if (child->attributes() & rex::filesystem::kFileAttributeDirectory) {
        fs::create_directories(out);
        Walk(child.get(), child_rel);
        continue;
      }
      Copy(child.get(), child_rel, out);
    }
  }

  void Copy(rex::filesystem::Entry* entry, const fs::path& rel, const fs::path& out) {
    std::error_code ec;
    if (fs::exists(out, ec)) {
      fmt::print(stderr, "extract: refusing to overwrite {}\n", out.string());
      ++errors;
      return;
    }
    rex::filesystem::File* in = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in) != X_STATUS_SUCCESS || !in) {
      fmt::print(stderr, "extract: cannot open {} in package\n", rel.generic_string());
      ++errors;
      return;
    }
    std::ofstream o(out, std::ios::binary);
    x360n::Sha256 h;
    size_t offset = 0, total = entry->size();
    bool ok = bool(o);
    while (ok && offset < total) {
      size_t want = std::min(buffer.size(), total - offset);
      size_t got = 0;
      if (in->ReadSync(std::span<uint8_t>(buffer.data(), want), offset, &got) != X_STATUS_SUCCESS ||
          got == 0) {
        ok = false;
        break;
      }
      o.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(got));
      h.Update({buffer.data(), got});
      offset += got;
    }
    in->Destroy();
    o.close();
    if (!ok || offset != total || !o) {
      fmt::print(stderr, "extract: short read for {} ({} of {} bytes)\n", rel.generic_string(),
                 offset, total);
      ++errors;
      return;
    }
    auto digest = h.Finish();
    std::string hex = x360n::ToHex(digest);
    manifest << hex << " *" << rel.generic_string() << "\n";
    fmt::print("{:>12}  {}  {}\n", total, hex, rel.generic_string());
    ++files;
    bytes += total;
  }
};

int Extract(const fs::path& package, const fs::path& dest_in) {
  fs::path dest = fs::absolute(dest_in).lexically_normal();
  std::error_code ec;
  if (fs::exists(dest, ec)) {
    fmt::print(stderr, "extract: destination already exists: {}\n", dest.string());
    return 3;
  }
  StfsContainerDevice device("", package);
  if (!device.Initialize()) {
    fmt::print(stderr, "extract: StfsContainerDevice::Initialize failed for {}\n", package.string());
    return 1;
  }
  auto* root = device.ResolvePath("/");
  if (!root) {
    fmt::print(stderr, "extract: package has no root entry\n");
    return 1;
  }
  fs::create_directories(dest);
  Extractor x;
  x.dest = dest;
  fs::path manifest_path = dest.parent_path() / (dest.filename().string() + ".sha256");
  x.manifest.open(manifest_path, std::ios::binary);
  x.Walk(root, {});
  fmt::print("extracted {} file(s), {} bytes, {} error(s); manifest {}\n", x.files, x.bytes,
             x.errors, manifest_path.string());
  return x.errors ? 1 : 0;
}

// Loads an XEX the way `rexglue codegen` does (project_recompiler.cpp: Runtime in
// tool_mode + LoadXexImage). Returns null after printing why it failed.
std::unique_ptr<rex::Runtime> LoadXex(const char* cmd, const fs::path& game_root,
                                      const std::string& xex_rel, rex::runtime::XexModule** xex) {
  auto runtime = std::make_unique<rex::Runtime>(game_root);
  rex::RuntimeConfig config{};
  config.kernel_init = rex::kernel::InitializeKernel;
  config.tool_mode = true;
  auto status = runtime->Setup(std::move(config));
  if (status != X_STATUS_SUCCESS) {
    fmt::print(stderr, "{}: Runtime::Setup failed: {:#x}\n", cmd, status);
    return nullptr;
  }
  std::string vfs = xex_rel;
  for (auto& c : vfs)
    if (c == '/') c = '\\';
  status = runtime->LoadXexImage("game:\\" + vfs);
  if (status != X_STATUS_SUCCESS) {
    fmt::print(stderr, "{}: LoadXexImage(game:\\{}) failed: {:#x}\n", cmd, vfs, status);
    return nullptr;
  }
  auto module = runtime->kernel_state()->GetExecutableModule();
  *xex = module ? module->xex_module() : nullptr;
  if (!*xex) {
    fmt::print(stderr, "{}: no executable XexModule after load\n", cmd);
    return nullptr;
  }
  return runtime;
}

// Writes the loaded image (decrypted and decompressed) for disassembly/inspection.
// Variable import slots in it are already patched by the loader. Diagnostic only.
int DumpImage(const fs::path& game_root, const std::string& xex_rel, const fs::path& out) {
  std::error_code ec;
  if (fs::exists(out, ec)) {
    fmt::print(stderr, "dump-image: output already exists: {}\n", out.string());
    return 3;
  }
  rex::runtime::XexModule* xex = nullptr;
  auto runtime = LoadXex("dump-image", game_root, xex_rel, &xex);
  if (!runtime) return 1;
  uint32_t base = xex->base_address(), size = xex->image_size();
  auto* data = runtime->memory()->TranslateVirtual<const uint8_t*>(base);
  std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(data), std::streamsize(size));
  fmt::print("image base 0x{:08X} size 0x{:X} sha256 {} -> {}\n", base, size,
             x360n::Sha256Hex({data, size}), out.string());
  return 0;
}

// Prints the imports as the SDK loader decoded them (XexModule::SetupLibraryImports
// pairs the records by ordinal; a thunk address means a function import) next to
// the SDK export table entry for the same ordinal. CSV on stdout.
int Imports(const fs::path& game_root, const std::string& xex_rel) {
  rex::runtime::XexModule* xex = nullptr;
  auto runtime = LoadXex("imports", game_root, xex_rel, &xex);
  if (!runtime) return 1;
  auto* resolver = runtime->export_resolver();
  fmt::print("library,library_version,ordinal,kind,value_address,thunk_address,sdk_name,sdk_type\n");
  for (const auto& lib : *xex->import_libraries()) {
    for (const auto& imp : lib.imports) {
      auto* e = resolver->GetExportByOrdinal(lib.name, uint16_t(imp.ordinal));
      const char* sdk_type = !e ? "absent"
                             : e->type == rex::runtime::Export::Type::kVariable ? "variable"
                                                                                : "function";
      fmt::print("{},{}.{}.{}.{},0x{:03X},{},0x{:08X},0x{:08X},{},{}\n", lib.name,
                 uint32_t(lib.version.major), uint32_t(lib.version.minor),
                 uint32_t(lib.version.build), uint32_t(lib.version.qfe), imp.ordinal,
                 imp.thunk_address ? "function" : "variable", imp.value_address,
                 imp.thunk_address, e ? e->name : "", sdk_type);
    }
  }
  return 0;
}

uint32_t ParseHex(const std::string& s) { return uint32_t(std::stoul(s, nullptr, 16)); }

uint32_t GuestWord(rex::Runtime* rt, uint32_t a) {
  auto* p = rt->memory()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// .pdata entries as IMAGE_CE_RUNTIME_FUNCTION (phase_register.cpp:43-55):
// start address -> length in bytes (FunctionLength counts instructions).
std::map<uint32_t, uint32_t> PdataEntries(rex::Runtime* rt, rex::runtime::XexModule* xex) {
  std::map<uint32_t, uint32_t> out;
  auto* sec = xex->GetPESection(".pdata");
  if (!sec) return out;
  for (uint32_t a = sec->address; a + 8 <= sec->address + sec->size; a += 8) {
    uint32_t start = GuestWord(rt, a), packed = GuestWord(rt, a + 4);
    if (start) out[start] = ((packed >> 8) & 0x3FFFFF) * 4;
  }
  return out;
}

std::string PdataContext(const std::map<uint32_t, uint32_t>& pdata, uint32_t a) {
  auto next = pdata.upper_bound(a);
  if (next != pdata.begin()) {
    auto prev = std::prev(next);
    if (a < prev->first + prev->second)
      return fmt::format("inside .pdata function 0x{:08X}-0x{:08X}", prev->first,
                         prev->first + prev->second);
    std::string s = fmt::format("not in any .pdata function; previous 0x{:08X}-0x{:08X}",
                                prev->first, prev->first + prev->second);
    if (next != pdata.end()) s += fmt::format(", next starts 0x{:08X}", next->first);
    return s;
  }
  return "before the first .pdata function";
}

// Disassembles with the SDK's binutils decoder configured as the codegen does
// (src/codegen/ppc/disasm.cpp: Xenon dialect, VMX128 included).
int Disasm(const fs::path& game_root, const std::string& xex_rel, uint32_t addr, int before,
           int count) {
  rex::runtime::XexModule* xex = nullptr;
  auto runtime = LoadXex("disasm", game_root, xex_rel, &xex);
  if (!runtime) return 1;
  auto pdata = PdataEntries(runtime.get(), xex);
  uint32_t lo = xex->base_address(), hi = lo + xex->image_size();
  disassemble_info info{};
  INIT_DISASSEMBLE_INFO(info, stdout, fprintf);
  info.arch = bfd_arch_powerpc;
  info.endian = BFD_ENDIAN_BIG;
  info.private_data = reinterpret_cast<void*>(uintptr_t(0x1 | 0x4 | 0x4000 | 0x8000000 | 0x200 |
                                                       0x1000000 | 0x10000));
  fmt::print("0x{:08X}: {}\n", addr, PdataContext(pdata, addr));
  for (int i = -before; i < count; ++i) {
    uint32_t a = addr + uint32_t(int64_t(i) * 4);
    if (a < lo || a + 4 > hi) continue;
    auto* p = runtime->memory()->TranslateVirtual<uint8_t*>(a);
    ppc_insn insn{};
    info.buffer = p;
    info.buffer_vma = a;
    info.buffer_length = 4;
    int n = decode_insn_ppc(a, &info, &insn);
    std::string text = (n == 4 && insn.opcode) ? fmt::format("{} {}", insn.opcode->name, insn.op_str)
                                               : std::string(".long");
    auto it = pdata.find(a);
    std::string note = it != pdata.end() ? fmt::format("   <.pdata start, len 0x{:X}>", it->second) : "";
    fmt::print("{} {:08X}  {:08X}  {}{}\n", a == addr ? '>' : ' ', a, GuestWord(runtime.get(), a),
               text, note);
  }
  return 0;
}

// Finds code that forms an address in [lo, hi] with lis/addis + addi/ori or a
// D-form load/store, and aligned data words holding such an address. Register
// values are tracked only along straight-line code (reset at branches, aged out
// after 64 instructions); a miss is possible, a hit is checked by disassembly.
int Xref(const fs::path& game_root, const std::string& xex_rel, uint32_t lo, uint32_t hi) {
  rex::runtime::XexModule* xex = nullptr;
  auto runtime = LoadXex("xref", game_root, xex_rel, &xex);
  if (!runtime) return 1;
  auto pdata = PdataEntries(runtime.get(), xex);
  auto in = [&](uint64_t v) { return v >= lo && v <= hi; };
  for (const auto& sec : xex->pe_sections()) {
    bool code = (sec.flags & kXEPESectionMemoryExecute) != 0;
    if (!code) {
      for (uint32_t a = sec.address; a + 4 <= sec.address + sec.size; a += 4) {
        uint32_t w = GuestWord(runtime.get(), a);
        if (in(w)) fmt::print("data  {:08X} [{}] holds 0x{:08X}\n", a, sec.name, w);
      }
      continue;
    }
    uint32_t val[32] = {};
    int age[32];
    for (auto& g : age) g = -1;
    auto forget = [&](int r) { age[r] = -1; };
    for (uint32_t a = sec.address; a + 4 <= sec.address + sec.size; a += 4) {
      uint32_t w = GuestWord(runtime.get(), a);
      uint32_t op = w >> 26, rd = (w >> 21) & 31, ra = (w >> 16) & 31;
      int32_t simm = int16_t(w & 0xFFFF);
      for (auto& g : age)
        if (g >= 0 && ++g > 64) g = -1;
      auto known = [&](int r) { return age[r] >= 0; };
      auto report = [&](const char* kind, uint32_t ea) {
        fmt::print("{:<5} {:08X}  {:08X}  ea 0x{:08X}  {}\n", kind, a, w, ea, PdataContext(pdata, a));
      };
      switch (op) {
        case 15:  // addis / lis
          if (ra == 0) { val[rd] = uint32_t(w & 0xFFFF) << 16; age[rd] = 0; }
          else if (known(ra)) { val[rd] = val[ra] + (uint32_t(w & 0xFFFF) << 16); age[rd] = 0; }
          else forget(rd);
          break;
        case 14:  // addi / li
          if (ra != 0 && known(ra)) {
            uint32_t v = val[ra] + uint32_t(simm);
            if (in(v)) report("addr", v);
            val[rd] = v; age[rd] = 0;
          } else forget(rd);
          break;
        case 24:  // ori rA,rS,uimm
          if (known(rd)) {
            uint32_t v = val[rd] | (w & 0xFFFF);
            if (in(v)) report("addr", v);
            val[ra] = v; age[ra] = 0;
          } else forget(ra);
          break;
        case 32: case 33: case 34: case 35: case 40: case 41: case 42: case 43:  // int loads
        case 48: case 49: case 50: case 51:                                      // fp loads
        case 58: {                                                               // ld/ldu/lwa
          int32_t d = op == 58 ? (simm & ~3) : simm;
          if (known(ra)) {
            uint32_t ea = val[ra] + uint32_t(d);
            if (in(ea)) report("load", ea);
            bool update = op == 33 || op == 35 || op == 41 || op == 43 || op == 49 || op == 51 ||
                          (op == 58 && (w & 3) == 1);
            if (update) { val[ra] = ea; age[ra] = 0; }
          }
          if (op < 48 || op == 58) forget(rd);
          break;
        }
        case 36: case 37: case 38: case 39: case 44: case 45:  // int stores
        case 52: case 53: case 54: case 55:                    // fp stores
        case 62: {                                             // std/stdu
          int32_t d = op == 62 ? (simm & ~3) : simm;
          if (known(ra)) {
            uint32_t ea = val[ra] + uint32_t(d);
            if (in(ea)) report("store", ea);
            bool update = op == 37 || op == 39 || op == 45 || op == 53 || op == 55 ||
                          (op == 62 && (w & 3) == 1);
            if (update) { val[ra] = ea; age[ra] = 0; }
          }
          break;
        }
        case 18:  // b / bl
          if (w & 1) { forget(0); for (int r = 3; r <= 12; ++r) forget(r); }
          else for (int r = 0; r < 32; ++r) forget(r);
          break;
        case 19:  // bclr / bcctr and friends
          for (int r = 0; r < 32; ++r) forget(r);
          break;
        case 31:  // X-form: the target is rD or rA depending on the opcode
          forget(rd);
          forget(ra);
          break;
        default:
          if (op != 10 && op != 11 && op != 16 && op != 3) forget(rd);  // cmp, bc, twi write no GPR
          break;
      }
    }
  }
  return 0;
}

// Lists every direct branch (b/bl/bc, absolute or relative) in executable
// sections whose target is <target>, e.g. the thunk address of an import.
int Callers(const fs::path& game_root, const std::string& xex_rel, uint32_t target) {
  rex::runtime::XexModule* xex = nullptr;
  auto runtime = LoadXex("callers", game_root, xex_rel, &xex);
  if (!runtime) return 1;
  auto pdata = PdataEntries(runtime.get(), xex);
  for (const auto& sec : xex->pe_sections()) {
    if (!(sec.flags & kXEPESectionMemoryExecute)) continue;
    for (uint32_t a = sec.address; a + 4 <= sec.address + sec.size; a += 4) {
      uint32_t w = GuestWord(runtime.get(), a), op = w >> 26, t = 0;
      if (op == 18) {
        int32_t li = int32_t(w & 0x03FFFFFC);
        if (li & 0x02000000) li -= 0x04000000;
        t = (w & 2) ? uint32_t(li) : a + uint32_t(li);
      } else if (op == 16) {
        int32_t bd = int16_t(w & 0xFFFC);
        t = (w & 2) ? uint32_t(bd) : a + uint32_t(bd);
      } else {
        continue;
      }
      if (t == target)
        fmt::print("{:<3} {:08X}  {:08X}  {}\n", (w & 1) ? "bl" : (op == 16 ? "bc" : "b"), a, w,
                   PdataContext(pdata, a));
    }
  }
  return 0;
}

// Lists the XEX resource table (XEX_HEADER_RESOURCE_INFO): the sections that
// XexGetModuleSection and section:// locators resolve against
// (user_module.cpp:258-277). Prints name, address, size, first 4 bytes, SHA-256.
int Resources(const fs::path& game_root, const std::string& xex_rel) {
  rex::runtime::XexModule* xex = nullptr;
  auto runtime = LoadXex("resources", game_root, xex_rel, &xex);
  if (!runtime) return 1;
  rex::xex2_opt_resource_info* info = nullptr;
  if (!xex->GetOptHeader(rex::XEX_HEADER_RESOURCE_INFO, &info) || !info) {
    fmt::print("no resource info header\n");
    return 0;
  }
  uint32_t count = (uint32_t(info->size) - 4) / sizeof(rex::xex2_resource);
  uint32_t lo = xex->base_address(), hi = lo + xex->image_size();
  fmt::print("name,address,size,magic,sha256\n");
  for (uint32_t i = 0; i < count; ++i) {
    const auto& r = info->resources[i];
    std::string name(r.name, strnlen(r.name, sizeof(r.name)));
    uint32_t addr = r.address, size = r.size;
    if (addr < lo || size > hi - addr) {
      fmt::print("{},0x{:08X},0x{:X},outside image,\n", name, addr, size);
      continue;
    }
    auto* p = runtime->memory()->TranslateVirtual<const uint8_t*>(addr);
    std::string magic;
    for (uint32_t k = 0; k < 4 && k < size; ++k)
      magic += (p[k] >= 0x20 && p[k] < 0x7F) ? char(p[k]) : '.';
    fmt::print("{},0x{:08X},0x{:X},{},{}\n", name, addr, size, magic,
               x360n::Sha256Hex({p, size}));
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 5 && std::string(argv[1]) == "dump-image") {
    rex::InitLoggingEarly();
    return DumpImage(Utf8Path(argv[2]), argv[3], Utf8Path(argv[4]));
  }
  if (argc == 4 && std::string(argv[1]) == "imports") {
    rex::InitLoggingEarly();
    return Imports(Utf8Path(argv[2]), argv[3]);
  }
  if (argc == 4 && std::string(argv[1]) == "resources") {
    rex::InitLoggingEarly();
    return Resources(Utf8Path(argv[2]), argv[3]);
  }
  if ((argc == 6 || argc == 7) && std::string(argv[1]) == "disasm") {
    rex::InitLoggingEarly();
    return Disasm(Utf8Path(argv[2]), argv[3], ParseHex(argv[4]), argc == 7 ? std::stoi(argv[6]) : 0,
                  std::stoi(argv[5]));
  }
  if (argc == 5 && std::string(argv[1]) == "callers") {
    rex::InitLoggingEarly();
    return Callers(Utf8Path(argv[2]), argv[3], ParseHex(argv[4]));
  }
  if (argc == 6 && std::string(argv[1]) == "xref") {
    rex::InitLoggingEarly();
    return Xref(Utf8Path(argv[2]), argv[3], ParseHex(argv[4]), ParseHex(argv[5]));
  }
  rex::InitLoggingEarly();
  if (argc == 3 && std::string(argv[1]) == "info") return Info(Utf8Path(argv[2]));
  if (argc == 4 && std::string(argv[1]) == "extract")
    return Extract(Utf8Path(argv[2]), Utf8Path(argv[3]));
  fmt::print(stderr,
             "usage: x360n-content info <package>\n"
             "       x360n-content extract <package> <dest>\n"
             "       x360n-content dump-image <game_root> <xex relative to game_root> <out.bin>\n"
             "       x360n-content imports <game_root> <xex relative to game_root>\n"
             "       x360n-content resources <game_root> <xex>\n"
             "       x360n-content disasm <game_root> <xex> <hex addr> <count> [words before]\n"
             "       x360n-content xref <game_root> <xex> <hex lo> <hex hi>\n"
             "       x360n-content callers <game_root> <xex> <hex target>\n");
  return 3;
}
