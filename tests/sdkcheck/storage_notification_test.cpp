#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xnotifylistener.h>
using rex::X_STATUS;

TEST_CASE("Initial storage availability reaches the system notification listener",
          "[sdk][storage-notification]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto make_listener = [&](uint64_t mask) {
    auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
        new rex::system::XNotifyListener(rt.kernel_state()));
    listener->Initialize(mask, 10);
    return listener;
  };
  // A different notification area must not consume the initial system state.
  auto live = make_listener(2);
  uint32_t id = 0, param = 0;
  CHECK_FALSE(live->DequeueNotification(&id, &param));
  auto system = make_listener(0x23);
  unsigned storage_count = 0;
  while (system->DequeueNotification(&id, &param)) {
    if (id == 0xB) {
      ++storage_count;
      CHECK(param == 0);  // Re-enumerate available devices, not a game count.
    }
  }
  CHECK(storage_count == 1);
  CHECK_FALSE(system->DequeueNotification(&id, &param));
  CHECK_FALSE(live->DequeueNotification(&id, &param));
}
