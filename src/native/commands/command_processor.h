#pragma once
#include "scanout_backend.h"
#include "../quality/cadence.h"
#include <gpu/xenos/command_processor.h>
namespace x360n::native {
class CommandProcessor final : public rex::gpu::CommandProcessor {
 public:
  CommandProcessor(rex::gpu::XenosGraphicsDriverBase* graphics, const rex::TitleInfo& info,
      rex::IPhysicalMemory& memory, rex::IPhysicalWriteWatch& watch, rex::IGuestInterrupts& interrupts,
      ScanoutFactory factory)
      : rex::gpu::CommandProcessor(graphics, info, memory, watch, interrupts), factory_(factory) {}

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
  // The system buffer's completion fence is next: its resolves must be visible first.
  void OnSystemCommandsExecuted() override {
    CompleteGuestWrites(kGuestSyncPacket, rex::xenos::PM4_XE_SYSTEM_COMMANDS);
  }
 public:
  // Called before PM4 packets that read or write guest memory.
  void CompleteGuestWrites(GuestSync, uint32_t opcode = 0xFF);
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode);
  void WaitForIdle();
 protected:
  rex::gpu::Shader* LoadShader(rex::xenos::ShaderType type, uint32_t address,
                                    const uint32_t* data, uint32_t count) override;
  bool IssueDraw(rex::xenos::PrimitiveType, uint32_t, IndexBuffer*, bool) override;
  bool IssueCopy() override;
  bool ShouldIssueDraw(uint32_t viz_query_condition) const override;
  bool ExecutePacketType3_EVENT_WRITE_ZPD(rex::memory::RingBuffer*, uint32_t, uint32_t) override;
  bool ExecutePacketType3_VIZ_QUERY(rex::memory::RingBuffer*, uint32_t, uint32_t) override;
 private:
  ScanoutFactory factory_;
  std::unique_ptr<ScanoutBackend> scanout_;
  OcclusionQueries occlusion_queries_;
  VisibilityQueries visibility_queries_;
  mutable bool draw_visibility_failed_ = false;
  // Generic Native Enhanced frame rate (no title timing profile).
  void UpdateGenericCadence();
  GenericCadence cadence_;
  bool generic_cadence_ = false;
  uint32_t cadence_rate_ = 0;
  uint64_t last_present_tick_ = 0;
};
[[noreturn]] void Unsupported(const char* message);
// Single command processor thread per process; no-op before setup.
// opcode: PM4 type-3 opcode for diagnostics, 0x80 for the packet after a sync.
void CompleteGuestWritesBeforePacket(GuestSync, uint32_t opcode);
void SyncGuestReadBeforePacket(uint32_t address, uint32_t length, uint32_t opcode);
void WaitForIdleBeforePacket();
}
