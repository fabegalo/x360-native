#pragma once
#include <rex/xam/task_context.h>
#include <optional>

namespace dash17559 {
// CurrentSystemTask returns a retained handle. Balance that reference even
// when this predicate is polled once per PNG row. Original workers, which use
// XAM's own TLS/task representation, must keep their original predicate.
inline std::optional<uint32_t> RuntimeSystemTaskShouldExit() {
  const auto task = rex::xam::CurrentSystemTask();
  if (!task) return std::nullopt;
  const auto result = rex::xam::XamTaskShouldExit_entry();
  rex::xam::XamTaskCloseHandle_entry(task);
  return result;
}
}
