#pragma once
#include "scanout_backend.h"
#include <rex/graphics/command_processor.h>
namespace x360n::native {
class CommandProcessor final : public rex::graphics::CommandProcessor {
 public:
  CommandProcessor(rex::graphics::GraphicsSystem* graphics, rex::system::KernelState* kernel,
                   ScanoutFactory factory)
      : rex::graphics::CommandProcessor(graphics, kernel), factory_(factory) {}
  void IssueSwap(uint32_t address, uint32_t width, uint32_t height) override;
  void InitializeShaderStorage(const std::filesystem::path&, uint32_t title_id, bool blocking) override;
 protected:
  bool SetupContext() override;
  void ShutdownContext() override;
  void WriteRegister(uint32_t index, uint32_t value) override;
  // Guest-observable points: the read pointer is written back after a
  // primary buffer, and waits poll memory the guest or a resolve may write.
  void OnPrimaryBufferEnd() override { CompleteGuestWrites(kGuestSyncBufferEnd); }
  void PrepareForWait() override { CompleteGuestWrites(kGuestSyncWait); }
 public:
  // Called before PM4 packets that read or write guest memory.
  void CompleteGuestWrites(GuestSync, uint32_t opcode = 0xFF);
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode);
 protected:
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType type, uint32_t address,
                                    const uint32_t* data, uint32_t count) override;
  bool IssueDraw(rex::graphics::xenos::PrimitiveType, uint32_t, IndexBufferInfo*, bool) override;
  bool IssueCopy() override;
  bool ExecutePacketType3_EVENT_WRITE_ZPD(rex::memory::RingBuffer*, uint32_t, uint32_t) override;
 private:
  ScanoutFactory factory_;
  std::unique_ptr<ScanoutBackend> scanout_;
};
[[noreturn]] void Unsupported(const char* message);
// Single command processor thread per process; no-op before setup.
// opcode: PM4 type-3 opcode for diagnostics, 0x80 for the packet after a sync.
void CompleteGuestWritesBeforePacket(GuestSync, uint32_t opcode);
void SyncGuestReadBeforePacket(uint32_t address, uint32_t length, uint32_t opcode);
}
