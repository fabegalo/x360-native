// patch 0190: PM4_WAIT_IB_PFD_COMPLETE must not end the command buffer
// (Xenia Canary 3e9d62ffc). Real command processor, no GPU.
#include <cstring>
#include <iostream>
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
  std::cout << (ok ? 1 : 0) << "/1 checks passed\n";
  return ok ? 0 : 1;
}
