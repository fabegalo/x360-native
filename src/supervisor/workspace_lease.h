#pragma once
#include "paths.h"
#include <cstdint>
namespace x360n {
// One preparation or original-title session per environment. The OS releases
// the lease after a crash; the file is not a stale "busy" marker.
class WorkspaceLease {
 public:
  explicit WorkspaceLease(const ProjectPaths& paths);
  ~WorkspaceLease();
  WorkspaceLease(const WorkspaceLease&) = delete;
  WorkspaceLease& operator=(const WorkspaceLease&) = delete;
 private:
  intptr_t handle_ = -1;
};
}
