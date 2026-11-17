// Opt-in diagnosis of the image-page contract, using synthetic memory only.
// Does not execute guest code, map a user-mode address space, or define a PTE ABI.
#include "tool_sdk.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

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
    rex::codegen::ToolApp app("user_mode_page_probe",std::filesystem::absolute(argv[1]));
    auto tool=app.Open(std::filesystem::absolute(argv[1]));if(!tool)return 3;
    auto& runtime=tool->GetRuntime();
    auto* memory=&rex::kernel::GetAddressSpace(runtime);
    auto* heap = memory->LookupHeap(base);
    const auto size = static_cast<uint32_t>(pages * page_size);
    const auto flags = rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit;
    if (!heap->AllocFixed(base, size, page_size, flags, rex::kernel::kMemoryProtectRead)) return 4;
    auto ctx = std::make_unique<PPCContext>();
    unsigned rejected = 0, unchanged = 0;
    auto caller=runtime.GetThreadRegistry().LaunchHostThread("image page probe",128*1024,[&] {
    auto query = [&](uint32_t address) {
      ctx->r3.u64 = address;
      __imp__KeGetImagePageTableEntry(*ctx, memory->GetVirtualMembase());
      return ctx->r3.u32;
    };
    std::cout << "diagnostic_only=true guest_execution=false\n"
                 "address\tread_only_entry\tread_write_entry\tpasses_attribute_gate\n";
    for (uint32_t i = 0; i < pages; ++i) {
      const auto address = base + i * page_size;
      const auto ro = query(address);
      if (!heap->Protect(address, page_size,
                         rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite)) return 5;
      const auto rw = query(address);
      rejected += !PassesAttributeGate(ro);
      unchanged += ro == rw;
      std::cout << std::hex << std::setfill('0') << std::setw(8) << address << '\t'
                << std::setw(8) << ro << '\t' << std::setw(8) << rw << '\t'
                << PassesAttributeGate(ro) << '\n';
    }
    return 0;
    });
    if(!caller)return 7;
    uint64_t timeout=uint64_t(-100'000'000ll);
    if(runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout))return 8;
    rex::kernel::CloseOwned(runtime.GetObjectTable(),*caller);
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
