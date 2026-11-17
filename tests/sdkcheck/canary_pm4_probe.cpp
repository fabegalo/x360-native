// patch 0190: PM4_WAIT_IB_PFD_COMPLETE must not end the command buffer
// (Xenia Canary 3e9d62ffc). Real command processor, no GPU.
#include <cstring>
#include <iostream>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include "gpu/xenos/command_processor.h"
#include "gpu/xenos/xenos_graphics_driver_base.h"
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "kernel/kernel_fixture.h"
#include <rex/kernel/heap.h>

using namespace rex::gpu;
using rex::xenos::RegisterFile;
namespace xenos=rex::xenos;
namespace {
class CpuGraphics final : public XenosGraphicsDriverBase {
 public:
  CpuGraphics() = default;
  std::string_view GetName() const override { return "pm4-cpu-check"; }
 private:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override { return {}; }
};
class CpuProcessor final : public CommandProcessor {
 public:
  CpuProcessor(XenosGraphicsDriverBase* graphics, const rex::TitleInfo& info, rex::kernel::TitleContext& physical)
      : CommandProcessor(graphics,info,physical,physical,physical) {}
  void IssueSwap(uint32_t, uint32_t, uint32_t) override {}
 private:
  bool SetupContext() override { return true; }
  void ShutdownContext() override {}
  Shader* LoadShader(xenos::ShaderType, uint32_t, const uint32_t*, uint32_t) override { return nullptr; }
  bool IssueDraw(xenos::PrimitiveType, uint32_t, IndexBuffer*, bool) override { return false; }
  bool IssueCopy() override { return false; }
};
}  // namespace

TEST_CASE("PM4 wait preserves following packets and the Canary reset state", "[sdk][gpu][pm4]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  auto& memory=*rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
  rex::kernel::TitleContext context(caller.GetRuntime());
  constexpr uint32_t physical=0x01000000;
  REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000,4096,4096,
      rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
  CpuGraphics graphics;
  CpuProcessor cp(&graphics,rex::TitleInfo{},context);
  auto& regs=*graphics.GetRegisterFile();
  // Reset snapshot from Canary 4f04bc7, register_file.cc blob
  // c697852e4775c1982c18a7e17a6d24bbc827a0e1 (including zero defaults).
  const std::pair<uint32_t, uint32_t> reset_values[] = {
      {uint32_t(xenos::Register::VGT_MAX_VTX_INDX), 0x0000FFFF},
      {uint32_t(xenos::Register::VGT_MULTI_PRIM_IB_RESET_INDX), 0x0000FFFF},
      {uint32_t(xenos::Register::PA_SC_SCREEN_SCISSOR_BR), 0x20002000},
      {uint32_t(xenos::Register::RB_STENCILREFMASK_BF), 0x00FFFF00},
      {uint32_t(xenos::Register::PA_SU_POINT_SIZE), 0x00080008},
      {uint32_t(xenos::Register::PA_SU_POINT_MINMAX), 0x04000010},
      {uint32_t(xenos::Register::PA_SU_LINE_CNTL), 0x00000008},
      {uint32_t(xenos::Register::PA_SC_LINE_CNTL), 0x00000400},
      {uint32_t(xenos::Register::VGT_HOS_REUSE_DEPTH), 0x0000000E},
      {uint32_t(xenos::Register::VGT_VERTEX_REUSE_BLOCK_CNTL), 0x0000000E},
      {uint32_t(xenos::Register::VGT_OUT_DEALLOC_CNTL), 0x00000010},
      {uint32_t(xenos::Register::PA_CL_GB_VERT_CLIP_ADJ), 0x40000000},
      {uint32_t(xenos::Register::PA_CL_GB_VERT_DISC_ADJ), 0x3F800000},
      {uint32_t(xenos::Register::PA_CL_GB_HORZ_CLIP_ADJ), 0x40000000},
      {uint32_t(xenos::Register::PA_CL_GB_HORZ_DISC_ADJ), 0x3F800000},
      {uint32_t(xenos::Register::PA_SC_AA_MASK), 0x0000FFFF},
  };
  bool reset_ok = true;
  for (uint32_t index = 0; index < RegisterFile::kRegisterCount; ++index) {
    uint32_t expected = 0;
    for (const auto& [reg, value] : reset_values) {
      if (index == reg) expected = value;
    }
    if (regs[index] != expected) {
      std::cerr << "FAIL reset register " << std::hex << index << std::dec << '\n';
      reset_ok = false;
    }
  }
  std::memset(regs.values, 0, sizeof(regs.values));
  const uint32_t words[] = {
      xenos::BuildPacketType3(xenos::PM4_WAIT_IB_PFD_COMPLETE, 1), 0,
      xenos::BuildPacketType0(uint32_t(xenos::Register::RB_SURFACE_INFO), 1), 0x12345,
  };
  auto* guest = reinterpret_cast<uint32_t*>(memory.TranslatePhysical(physical));
  for (size_t i = 0; i < std::size(words); ++i) guest[i] = rex::byte_swap(words[i]);
  cp.ExecutePacket(physical, uint32_t(std::size(words)));
  const bool ok = regs[uint32_t(xenos::Register::RB_SURFACE_INFO)] == 0x12345;
  if (!ok) std::cerr << "FAIL packet after WAIT_IB_PFD_COMPLETE was dropped\n";
  std::cout << (int(ok) + int(reset_ok)) << "/2 checks passed\n";
  CHECK(ok); CHECK(reset_ok);
  });
}
