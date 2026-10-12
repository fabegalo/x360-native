#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/app_manager.h>
#include <rex/system/xnotifylistener.h>

extern "C" REX_FUNC(__imp__XamShowSigninUI);
extern "C" REX_FUNC(__imp__XamShowGamerCardUI);

namespace {
class SystemUiProbe final : public rex::system::xam::App {
 public:
  explicit SystemUiProbe(rex::system::KernelState* state) : App(state, 0xFE) {}
  rex::X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer,
                                   uint32_t length) override {
    ++calls;
    last_message = message;
    words.clear();
    for (uint32_t i = 0; i < length / 4; ++i)
      words.push_back(rex::memory::load_and_swap<uint32_t>(memory_->TranslateVirtual(buffer + i * 4)));
    return result;
  }
  unsigned calls = 0;
  uint32_t last_message = 0;
  std::vector<uint32_t> words;
  rex::X_HRESULT result = static_cast<rex::X_HRESULT>(0x80004005u);
};
}

TEST_CASE("Profile UI requests reach the system application without invented signin notifications",
          "[sdk][profile-ui]") {
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true; // Install only our app probe, not the real FE application.
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto app = std::make_unique<SystemUiProbe>(rt.kernel_state());
  auto* probe = app.get();
  rt.kernel_state()->app_manager()->RegisterApp(std::move(app));
  auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
      new rex::system::XNotifyListener(rt.kernel_state()));
  listener->Initialize(1, 10);
  uint32_t id = 0, param = 0;
  while (listener->DequeueNotification(&id, &param)) {}
  PPCContext ctx{};
  ctx.r3.u64 = 1; ctx.r4.u64 = 2;
  __imp__XamShowSigninUI(ctx, rt.memory()->virtual_membase());
  CHECK(ctx.r3.u32 == 1627); // Original wrapper maps failed UI dispatch to ERROR_FUNCTION_FAILED.
  CHECK(probe->calls == 1);
  CHECK(probe->last_message == 0x21001);
  CHECK(probe->words == std::vector<uint32_t>{0, 0xFF, 1, 2, 0});
  CHECK_FALSE(listener->DequeueNotification(&id, &param));
  ctx.r3.u64 = 0;
  __imp__XamShowGamerCardUI(ctx, rt.memory()->virtual_membase());
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->calls == 2);
  CHECK(probe->last_message == 0x21008);
  CHECK(probe->words == std::vector<uint32_t>(544 / 4, 0));
  CHECK_FALSE(listener->DequeueNotification(&id, &param));
}
