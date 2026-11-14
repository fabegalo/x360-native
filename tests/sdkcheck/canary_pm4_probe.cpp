// patch 0190: PM4_WAIT_IB_PFD_COMPLETE must not end the command buffer
// (Xenia Canary 3e9d62ffc). Real command processor, no GPU.
#include <cstring>
#include <iostream>
#include <utility>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/system/xmemory.h>

using namespace rex::graphics;
namespace {
class CpuGraphics final : public GraphicsSystem {
 public:
  explicit CpuGraphics(rex::memory::Memory& memory) { memory_ = &memory; }
  std::string name() const override { return "pm4-cpu-check"; }
 private:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override { return {}; }
};
class CpuProcessor final : public CommandProcessor {
 public:
  explicit CpuProcessor(GraphicsSystem* graphics) : CommandProcessor(graphics, nullptr) {}
  void IssueSwap(uint32_t, uint32_t, uint32_t) override {}
 private:
  bool SetupContext() override { return true; }
  void ShutdownContext() override {}
  Shader* LoadShader(xenos::ShaderType, uint32_t, const uint32_t*, uint32_t) override { return nullptr; }
  bool IssueDraw(xenos::PrimitiveType, uint32_t, IndexBufferInfo*, bool) override { return false; }
  bool IssueCopy() override { return false; }
};
}  // namespace

int main() {
  rex::memory::Memory memory;
  if (!memory.Initialize()) return 2;
  constexpr uint32_t physical = 0x01000000;
  if (!memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000, 4096, 4096,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 3;
  CpuGraphics graphics(memory);
  CpuProcessor cp(&graphics);
  auto& regs = *graphics.register_file();
  // Reset snapshot from Canary 4f04bc7, register_file.cc blob
  // c697852e4775c1982c18a7e17a6d24bbc827a0e1 (including zero defaults).
  const std::pair<uint32_t, uint32_t> reset_values[] = {
      {XE_GPU_REG_VGT_MAX_VTX_INDX, 0x0000FFFF},
      {XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX, 0x0000FFFF},
      {XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, 0x20002000},
      {XE_GPU_REG_RB_STENCILREFMASK_BF, 0x00FFFF00},
      {XE_GPU_REG_PA_SU_POINT_SIZE, 0x00080008},
      {XE_GPU_REG_PA_SU_POINT_MINMAX, 0x04000010},
      {XE_GPU_REG_PA_SU_LINE_CNTL, 0x00000008},
      {XE_GPU_REG_PA_SC_LINE_CNTL, 0x00000400},
      {XE_GPU_REG_VGT_HOS_REUSE_DEPTH, 0x0000000E},
      {XE_GPU_REG_VGT_VERTEX_REUSE_BLOCK_CNTL, 0x0000000E},
      {XE_GPU_REG_VGT_OUT_DEALLOC_CNTL, 0x00000010},
      {XE_GPU_REG_PA_CL_GB_VERT_CLIP_ADJ, 0x40000000},
      {XE_GPU_REG_PA_CL_GB_VERT_DISC_ADJ, 0x3F800000},
      {XE_GPU_REG_PA_CL_GB_HORZ_CLIP_ADJ, 0x40000000},
      {XE_GPU_REG_PA_CL_GB_HORZ_DISC_ADJ, 0x3F800000},
      {XE_GPU_REG_PA_SC_AA_MASK, 0x0000FFFF},
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
      xenos::MakePacketType3(xenos::PM4_WAIT_IB_PFD_COMPLETE, 1), 0,
      xenos::MakePacketType0(XE_GPU_REG_RB_SURFACE_INFO, 1), 0x12345,
  };
  auto* guest = reinterpret_cast<uint32_t*>(memory.TranslatePhysical(physical));
  for (size_t i = 0; i < std::size(words); ++i) guest[i] = rex::byte_swap(words[i]);
  cp.ExecutePacket(physical, uint32_t(std::size(words)));
  const bool ok = regs[XE_GPU_REG_RB_SURFACE_INFO] == 0x12345;
  if (!ok) std::cerr << "FAIL packet after WAIT_IB_PFD_COMPLETE was dropped\n";
  std::cout << (int(ok) + int(reset_ok)) << "/2 checks passed\n";
  return ok && reset_ok ? 0 : 1;
}
