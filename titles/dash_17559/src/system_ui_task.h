#pragma once
#include <rex/kernel/xam/task_context.h>
#include <optional>

namespace rex::kernel::xam {
uint32_t XamTaskShouldExit_entry();
uint32_t XamTaskCloseHandle_entry(uint32_t handle);
}

namespace dash17559 {
// CurrentSystemTask returns a retained handle. Balance that reference even
// when this predicate is polled once per PNG row. Original workers, which use
// XAM's own TLS/task representation, must keep their original predicate.
inline std::optional<uint32_t> RuntimeSystemTaskShouldExit() {
  const auto task = rex::kernel::xam::CurrentSystemTask();
  if (!task) return std::nullopt;
  const auto result = rex::kernel::xam::XamTaskShouldExit_entry();
  rex::kernel::xam::XamTaskCloseHandle_entry(task);
  return result;
}
}
