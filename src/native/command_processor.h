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
 protected:
  bool SetupContext() override;
  void ShutdownContext() override;
  void WriteRegister(uint32_t index, uint32_t value) override;
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
}
