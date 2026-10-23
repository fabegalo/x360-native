#include "command_processor.h"
#include <rex/graphics/register_file.h>
#include <rex/memory/ring_buffer.h>
#include <cstdio>
namespace rex::graphics {
// Called only by SDK sources compiled for Native with REX_GPU_VALIDATE_PACKET.
// The Xenos plugin has no extra branch, virtual call, or instrumentation.
void ValidatePacketForBackend(uint32_t header, const rex::memory::RingBuffer& payload) {
  // A wait, interrupt or memory packet may let the guest CPU write memory
  // while it executes: the next packet starts a new guest epoch as well.
  static thread_local bool after_guest_sync = false;
  if (after_guest_sync) { after_guest_sync = false; x360n::native::CompleteGuestWritesBeforePacket(x360n::native::kGuestSyncPacket, 0x80); }
  const auto remaining_dwords = payload.read_count() / 4;
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
    case PM4_INDIRECT_BUFFER: case PM4_INDIRECT_BUFFER_PFD: case PM4_REG_TO_MEM: case PM4_IM_LOAD:
    case PM4_SET_BIN_MASK: case PM4_SET_BIN_SELECT: required = 2; break;
    case PM4_SET_BIN_MASK_LO: case PM4_SET_BIN_MASK_HI:
    case PM4_SET_BIN_SELECT_LO: case PM4_SET_BIN_SELECT_HI:
    case PM4_EVENT_WRITE: case PM4_EVENT_WRITE_ZPD: case PM4_VIZ_QUERY: required = 1; break;
    case PM4_REG_RMW: case PM4_EVENT_WRITE_SHD: required = 3; break;
    // Shared handler reports maximal screen extents: a superset of any draw's
    // coverage, so tile predication may run more draws but never drops pixels.
    case PM4_EVENT_WRITE_EXT: required = 2; break;
    case PM4_WAIT_REG_MEM: required = 5; break;
    case PM4_COND_WRITE: required = 6; break;
    case PM4_MEM_WRITE: required = 2; variable = true; break;
    case PM4_SET_CONSTANT: case PM4_SET_CONSTANT2: case PM4_SET_SHADER_CONSTANTS: required = 2; variable = true; break;
    case PM4_LOAD_ALU_CONSTANT: required = 3; break;
    case PM4_INVALIDATE_STATE: case PM4_CONTEXT_UPDATE: case PM4_WAIT_FOR_IDLE: required = 1; break;
    case PM4_IM_LOAD_IMMEDIATE: required = 3; variable = true; break;
    case PM4_DRAW_INDX: required = 2; variable = true; break;
    case PM4_DRAW_INDX_2: required = 1; variable = true; break;
    // VdSwap's system command buffer (Guide, notifications). The shared handler
    // runs it as an indirect buffer, whose packets come through this guard too,
    // then restores the title state and writes the completion fence.
    case PM4_XE_SYSTEM_COMMANDS: required = 10; variable = true; break;
    default: {
      char message[96];
      std::snprintf(message, sizeof(message), "PM4 opcode 0x%02X is not supported by Native", opcode);
      x360n::native::Unsupported(message);
    }
  }
  if (count < required || (!variable && count != required))
    x360n::native::Unsupported("invalid PM4 payload size");
  // Inspect a cursor copy: validation must not consume the guest command.
  auto cursor = payload;
  auto word = [&]() { return cursor.ReadAndSwap<uint32_t>(); };
  if (opcode == PM4_CONTEXT_UPDATE && word() != 0)
    x360n::native::Unsupported("unsupported PM4 context update value");
  // Decoder stores use the unprotected physical view; reject an invalid
  // destination before that store, including variable-length MEM_WRITE.
  auto physical_write = [&](uint32_t address, uint64_t bytes) {
    if (uint64_t(address & ~3u) + bytes > 0x20000000)
      x360n::native::Unsupported("PM4 write outside guest physical memory");
  };
  if (opcode == PM4_MEM_WRITE || opcode == PM4_REG_TO_MEM || opcode == PM4_EVENT_WRITE_SHD || opcode == PM4_COND_WRITE) {
    auto write = payload;
    auto next = [&]() { return write.ReadAndSwap<uint32_t>(); };
    if (opcode == PM4_MEM_WRITE) physical_write(next(), uint64_t(count - 1) * 4);
    else if (opcode == PM4_COND_WRITE) {
      const uint32_t control = next(); next(); next(); next();
      const uint32_t destination = next();
      if (control & 0x100) physical_write(destination, 4);
    } else { next(); physical_write(next(), 4); }
  }
  if (opcode == PM4_EVENT_WRITE_EXT) {
    word();
    const auto address = word();
    // The shared writer always stores six 16-bit values swapped 8-in-16.
    if (Endian(address & 3) != Endian::k8in16)
      x360n::native::Unsupported("screen extent endianness not implemented");
    if (uint64_t(address & ~3u) + 12 > 0x20000000)
      x360n::native::Unsupported("screen extent outside guest physical memory");
  }
  if (opcode == PM4_XE_SYSTEM_COMMANDS) {
    const auto list = word() & 0x1fffffff, length = word() & 0xffffff, fence = word();
    if (uint64_t(list) + uint64_t(length) * 4 > 0x20000000)
      x360n::native::Unsupported("system command buffer outside guest physical memory");
    if (uint64_t(fence & 0x1fffffff) + 4 > 0x20000000)
      x360n::native::Unsupported("system command fence outside guest physical memory");
  }
  if (opcode == PM4_IM_LOAD || opcode == PM4_IM_LOAD_IMMEDIATE) {
    auto source = word(), size = word();
    auto type = opcode == PM4_IM_LOAD ? source & 3 : source;
    if (type > 1 || (size >> 16) || !(size & 65535))
      x360n::native::Unsupported("invalid shader type, start or length");
    if (opcode == PM4_IM_LOAD_IMMEDIATE) {
      if (count != size + 2 || uint64_t(cursor.read_offset()) + size * 4 > cursor.capacity())
        x360n::native::Unsupported("inline shader exceeds contiguous packet extent");
    } else if (uint64_t(source & ~3u) + size * 4 > 0x20000000)
      x360n::native::Unsupported("shader outside guest physical memory");
  }
  if (opcode == PM4_DRAW_INDX || opcode == PM4_DRAW_INDX_2) {
    unsigned prefix = opcode == PM4_DRAW_INDX ? 1 : 0;
    if (prefix) word(); // Visibility predicate is evaluated after decoding the entire draw.
    reg::VGT_DRAW_INITIATOR draw; draw.value = word();
    if (draw.source_select == SourceSelect::kAutoIndex) {
      if (count != prefix + 1) x360n::native::Unsupported("invalid auto-index draw size");
    } else if (draw.source_select == SourceSelect::kDMA) {
      if (count != prefix + 3) x360n::native::Unsupported("invalid DMA draw size");
      auto address = word(); reg::VGT_DMA_SIZE size; size.value = word();
      unsigned width = draw.index_size == IndexFormat::kInt16 ? 2 : 4;
      if (draw.num_indices > size.num_words || uint64_t(address & ~(width - 1)) + uint64_t(size.num_words) * width > 0x20000000)
        x360n::native::Unsupported("DMA index buffer extent invalid");
    } else x360n::native::Unsupported("immediate index source not implemented");
  }
  if (opcode == PM4_SET_CONSTANT || opcode == PM4_SET_CONSTANT2 || opcode == PM4_SET_SHADER_CONSTANTS || opcode == PM4_LOAD_ALU_CONSTANT) {
    uint32_t address = opcode == PM4_LOAD_ALU_CONSTANT ? word() & 0x3fffffff : 0;
    auto offset = word();
    uint32_t length = opcode == PM4_LOAD_ALU_CONSTANT ? word() & 0xfff : count - 1;
    uint32_t base;
    if (opcode == PM4_SET_CONSTANT2 || opcode == PM4_SET_SHADER_CONSTANTS) base = offset & 65535;
    else {
      constexpr unsigned banks[]{0x4000, 0x4800, 0x4900, 0x4908, 0x2000};
      unsigned bank = (offset >> 16) & 255;
      if (bank >= 5) x360n::native::Unsupported("invalid constant register bank");
      base = banks[bank] + (offset & 0x7ff);
    }
    if (base >= RegisterFile::kRegisterCount || length > RegisterFile::kRegisterCount - base || uint64_t(address) + length * 4 > 0x20000000)
      x360n::native::Unsupported("constant register or memory extent invalid");
  }
  // These packets read or write guest memory, or let the guest run code:
  // incomplete resolve results must be visible to them first.
  switch (opcode) {
    case PM4_WAIT_FOR_IDLE:
      // This packet has no memory destination but requires all queued GPU work,
      // including color/depth-only draws, to finish before the next command.
      x360n::native::WaitForIdleBeforePacket();
      after_guest_sync = true;
      break;
    // EVENT_WRITE_SHD/EXT store a fence or extents to guest memory after the
    // event register write; a plain EVENT_WRITE (cache flush) publishes
    // nothing the guest can observe, so it does not wait for the GPU.
    // Every guest input of earlier commands was captured when recorded and
    // guest-memory outputs are resolves and shader MEMEXPORT stores.
    case PM4_EVENT_WRITE_SHD:
      x360n::native::CompleteGuestWritesBeforePacket(x360n::native::kGuestSyncEvent, opcode);
      after_guest_sync = true;
      break;
    // IM_LOAD and LOAD_ALU_CONSTANT only read guest memory into the command
    // processor: a pending resolve matters only if it overlaps the read.
    case PM4_IM_LOAD: case PM4_LOAD_ALU_CONSTANT: {
      auto read = payload;
      auto next = [&]() { return read.ReadAndSwap<uint32_t>(); };
      uint32_t address, bytes;
      if (opcode == PM4_IM_LOAD) { address = next() & ~3u; bytes = (next() & 65535) * 4; }
      else { address = next() & 0x3fffffff; next(); bytes = (next() & 0xfff) * 4; }
      x360n::native::SyncGuestReadBeforePacket(address, bytes, opcode);
      break;
    }
    case PM4_EVENT_WRITE_EXT:
    case PM4_INTERRUPT: case PM4_REG_TO_MEM: case PM4_WAIT_REG_MEM:
    case PM4_COND_WRITE: case PM4_MEM_WRITE:
      x360n::native::CompleteGuestWritesBeforePacket(x360n::native::kGuestSyncPacket, opcode);
      after_guest_sync = true;
      break;
    default: break;
  }
}
}
