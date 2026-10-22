#include "guest_fault_context.h"
#include <rex/system/thread_state.h>
#include <array>
#include <algorithm>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace x360n {
bool GuestFaultContext::Start(rex::memory::Memory& memory, const std::filesystem::path& path) {
  if (memory_ || path.empty()) return false;
#if defined(_WIN32)
  // Never truncate an earlier diagnostic. No handles, buffers or directories
  // are created from the exception callback itself.
  auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  file_ = file;
  memory_ = &memory;
  writing_.clear();
  records_ = 0;
  rex::arch::ExceptionHandler::Install(Observe, this);
  return true;
#else
  return false;
#endif
}

void GuestFaultContext::Stop() {
  if (!memory_) return;
  rex::arch::ExceptionHandler::Uninstall(Observe, this);
#if defined(_WIN32)
  CloseHandle(file_);
#endif
  file_ = nullptr;
  memory_ = nullptr;
}

bool GuestFaultContext::Observe(rex::arch::Exception* exception, void* data) {
#if defined(_WIN32)
  auto& self = *static_cast<GuestFaultContext*>(data);
  if (exception->code() != rex::arch::Exception::Code::kAccessViolation) return false;
  auto* state = rex::runtime::ThreadState::Get();
  if (!state || state->memory() != self.memory_) return false;
  const auto address = exception->fault_address();
  const auto begin = reinterpret_cast<uintptr_t>(self.memory_->virtual_membase());
  const auto end = reinterpret_cast<uintptr_t>(self.memory_->physical_membase());
  if (address < begin || address >= end || self.writing_.test_and_set()) return false;
  if (self.records_ == 16) { self.writing_.clear(); return false; }
  ++self.records_;

  const auto& ctx = *state->context();
  const std::array<uint64_t, 32> gpr{
      ctx.r0.u64, ctx.r1.u64, ctx.r2.u64, ctx.r3.u64,
      ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, ctx.r7.u64,
      ctx.r8.u64, ctx.r9.u64, ctx.r10.u64, ctx.r11.u64,
      ctx.r12.u64, ctx.r13.u64, ctx.r14.u64, ctx.r15.u64,
      ctx.r16.u64, ctx.r17.u64, ctx.r18.u64, ctx.r19.u64,
      ctx.r20.u64, ctx.r21.u64, ctx.r22.u64, ctx.r23.u64,
      ctx.r24.u64, ctx.r25.u64, ctx.r26.u64, ctx.r27.u64,
      ctx.r28.u64, ctx.r29.u64, ctx.r30.u64, ctx.r31.u64};
  const uint32_t cr = (ctx.cr0.raw() << 28) | (ctx.cr1.raw() << 24) |
      (ctx.cr2.raw() << 20) | (ctx.cr3.raw() << 16) | (ctx.cr4.raw() << 12) |
      (ctx.cr5.raw() << 8) | (ctx.cr6.raw() << 4) | ctx.cr7.raw();
  const char* operation = exception->access_violation_operation() ==
      rex::arch::Exception::AccessViolationOperation::kRead ? "read" :
      exception->access_violation_operation() ==
      rex::arch::Exception::AccessViolationOperation::kWrite ? "write" : "unknown";
  // Fixed stack buffer; no logger, guest dereference, dispatcher or heap lock
  // in the fault path. LR and last_indirect_target are evidence, not guest PC.
  char output[2048];
  int used = std::snprintf(output, sizeof(output),
      "runtime-declined guest access; observer does not handle exception\n"
      "host_pc=%016llX host_thread=%08lX guest_thread=%08X\n"
      "operation=%s guest_address=%08llX host_address=%016llX\n"
      "lr=%016llX ctr=%016llX last_indirect_target=%08X cr=%08X fpscr=%08X\n"
      "PPCContext snapshot: AOT host locals may hold newer register values; LR is not PC\n",
      static_cast<unsigned long long>(exception->pc()), GetCurrentThreadId(), state->thread_id(),
      operation, static_cast<unsigned long long>(address - begin),
      static_cast<unsigned long long>(address), static_cast<unsigned long long>(ctx.lr),
      static_cast<unsigned long long>(ctx.ctr.u64), ctx.last_indirect_target, cr, ctx.fpscr.csr);
  for (size_t i = 0; i < gpr.size() && used > 0 && size_t(used) < sizeof(output); ++i) {
    const int added = std::snprintf(output + used, sizeof(output) - size_t(used),
        "r%02zu=%016llX%s", i, static_cast<unsigned long long>(gpr[i]),
        i % 4 == 3 ? "\n" : " ");
    if (added < 0) break;
    used += added;
  }
  if (used > 0) {
    DWORD written = 0;
    WriteFile(self.file_, output, DWORD(std::min(size_t(used), sizeof(output) - 1)), &written, nullptr);
  }
  self.writing_.clear();
#endif
  return false;
}
}  // namespace x360n
