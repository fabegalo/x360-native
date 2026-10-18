// Opt-in diagnosis of the image-page contract, using synthetic memory only.
// Does not execute guest code, map a user-mode address space, or define a PTE ABI.
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

using rex::X_STATUS;
extern "C" REX_FUNC(__imp__KeGetImagePageTableEntry);

// Observed consumer requirements, not a replacement kernel implementation.
// RuntimeHost's image mapper rejects zero, missing bits 0+1, and bit 2 without
// bit 1 when bit 0 is set. The remaining paths still have other prerequisites.
static bool PassesAttributeGate(uint32_t entry) {
  if (!entry) return false;
  if (entry & 1) return !(entry & 4) || (entry & 2);
  return (entry & 2) != 0;
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: user_mode_page_probe <scratch-directory> <hex-base> <pages>\n";
    return 2;
  }
  try {
    const auto parsed_base = std::stoull(argv[2], nullptr, 16);
    const auto pages = std::stoul(argv[3]);
    constexpr uint32_t page_size = 0x10000;
    if (!pages || pages > 256 || (parsed_base & (page_size - 1)) ||
        parsed_base < 0x80000000 || parsed_base >= 0x90000000 ||
        parsed_base + pages * page_size > 0x90000000)
      return 2;
    const auto base = static_cast<uint32_t>(parsed_base);
    std::filesystem::create_directories(argv[1]);
    rex::Runtime runtime(std::filesystem::absolute(argv[1]));
    rex::RuntimeConfig config{};
    config.tool_mode = true;
    config.kernel_init = rex::kernel::InitializeKernel;
    if (runtime.Setup(std::move(config)) != X_STATUS_SUCCESS) return 3;
    auto* heap = runtime.memory()->LookupHeap(base);
    const auto size = static_cast<uint32_t>(pages * page_size);
    const auto flags = rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit;
    if (!heap->AllocFixed(base, size, page_size, flags, rex::memory::kMemoryProtectRead)) return 4;
    auto ctx = std::make_unique<PPCContext>();
    auto query = [&](uint32_t address) {
      ctx->r3.u64 = address;
      __imp__KeGetImagePageTableEntry(*ctx, runtime.memory()->virtual_membase());
      return ctx->r3.u32;
    };
    unsigned rejected = 0, unchanged = 0;
    std::cout << "diagnostic_only=true guest_execution=false\n"
                 "address\tread_only_entry\tread_write_entry\tpasses_attribute_gate\n";
    for (uint32_t i = 0; i < pages; ++i) {
      const auto address = base + i * page_size;
      const auto ro = query(address);
      if (!heap->Protect(address, page_size,
                         rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 5;
      const auto rw = query(address);
      rejected += !PassesAttributeGate(ro);
      unchanged += ro == rw;
      std::cout << std::hex << std::setfill('0') << std::setw(8) << address << '\t'
                << std::setw(8) << ro << '\t' << std::setw(8) << rw << '\t'
                << PassesAttributeGate(ro) << '\n';
    }
    if (!heap->Release(base)) return 6;
    std::cout << std::dec << "allocated_pages=" << pages << " rejected_attribute_gates=" << rejected
              << " unchanged_after_protect=" << unchanged << '\n';
    // 78 is an explicitly diagnosed unsupported contract, not a passing test.
    return rejected ? 78 : 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
