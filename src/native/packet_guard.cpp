#include "command_processor.h"
#include <rex/graphics/register_file.h>
#include <cstdio>
namespace rex::graphics {
// Called only by SDK sources compiled for Native with REX_GPU_VALIDATE_PACKET.
// The Xenos plugin has no extra branch, virtual call, or instrumentation.
void ValidatePacketForBackend(uint32_t header, size_t remaining_dwords) {
  if (!header) return;
  const uint32_t type = header >> 30;
  const uint32_t count = type == 2 ? 0 : type == 1 ? 2 : ((header >> 16) & 0x3fff) + 1;
  if (count > remaining_dwords) x360n::native::Unsupported("truncated PM4 packet");
  if (type == 0) {
    const uint32_t base = header & 0x7fff;
    if (base >= RegisterFile::kRegisterCount ||
        (!(header & 0x8000) && count > RegisterFile::kRegisterCount - base))
      x360n::native::Unsupported("PM4 register range outside supported GPU state");
    return;
  }
  if (type != 3) return;
  using namespace xenos;
  const auto opcode = (header >> 8) & 0x7f;
  unsigned required = 0;
  bool variable = false;
  switch (opcode) {
    case PM4_ME_INIT: case PM4_NOP: return;
    case PM4_INTERRUPT: required = 1; break;
    case PM4_XE_SWAP: required = 4; variable = true; break;
    case PM4_INDIRECT_BUFFER: case PM4_REG_TO_MEM: case PM4_IM_LOAD: required = 2; break;
    case PM4_REG_RMW: case PM4_EVENT_WRITE_SHD: required = 3; break;
    case PM4_WAIT_REG_MEM: required = 5; break;
    case PM4_COND_WRITE: required = 6; break;
    case PM4_MEM_WRITE: required = 2; variable = true; break;
    case PM4_SET_CONSTANT: case PM4_SET_CONSTANT2: case PM4_SET_SHADER_CONSTANTS: required = 2; variable = true; break;
    case PM4_LOAD_ALU_CONSTANT: required = 3; break;
    case PM4_INVALIDATE_STATE: required = 1; break;
    case PM4_IM_LOAD_IMMEDIATE: x360n::native::Unsupported("inline guest shaders are not implemented");
    case PM4_DRAW_INDX: case PM4_DRAW_INDX_2: x360n::native::Unsupported("guest draw/EDRAM operation is not implemented");
    default: {
      char message[96];
      std::snprintf(message, sizeof(message), "PM4 opcode 0x%02X is not supported by Native", opcode);
      x360n::native::Unsupported(message);
    }
  }
  if (count < required || (!variable && count != required))
    x360n::native::Unsupported("invalid PM4 payload size");
}
}
