// A failed Runtime::Setup must leave teardown to the owner of the Runtime.
//
// At f5337cdc, a failure after the graphics system was handed over (e.g. the
// function table, src/system/runtime.cpp) called Runtime::Shutdown() inside
// Setup, destroying the graphics system and its D3D12 device while ReXApp still
// held an immediate drawer, ImGui textures and the window's presenter made from
// it. ReXApp::OnDestroy then released those and crashed inside D3D12Core
// (x360n run --crash-stack, docs/dashboard.md §0.6). patches/rexglue-sdk/0002
// keeps the partly set-up Runtime intact on failure; ~Runtime() runs Shutdown().
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <system_error>

#include <rex/image_info.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>

using rex::X_STATUS;  // X_STATUS_SUCCESS expands to a cast to it

namespace {

namespace fs = std::filesystem;

struct GraphicsProbe {
  bool alive = false;
  bool shutdown_called = false;
};

class FakeGraphics : public rex::system::IGraphicsSystem {
 public:
  explicit FakeGraphics(GraphicsProbe* probe) : probe_(probe) { probe_->alive = true; }
  ~FakeGraphics() override { probe_->alive = false; }
  rex::X_STATUS SetupPresentation(rex::ui::WindowedAppContext*) override { return X_STATUS_SUCCESS; }
  rex::X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher*, rex::system::KernelState*) override {
    return X_STATUS_SUCCESS;
  }
  bool has_presentation() const override { return false; }
  void Shutdown() override { probe_->shutdown_called = true; }

 private:
  GraphicsProbe* probe_;
};

struct GameRoot {
  fs::path dir = fs::temp_directory_path() / "x360n_setup_failure_test";
  GameRoot() { fs::create_directories(dir); }
  ~GameRoot() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

}  // namespace

TEST_CASE("a failed Runtime::Setup keeps the graphics system until the Runtime is destroyed",
          "[sdk][runtime]") {
  GameRoot root;
  GraphicsProbe probe;
  {
    auto runtime = std::make_unique<rex::Runtime>(root.dir);
    rex::RuntimeConfig config{};
    config.graphics = std::make_unique<FakeGraphics>(&probe);
    // Function table range crosses the end of the 0x90000000 XEX heap, so the
    // second phase of Setup fails after the graphics system was handed over.
    rex::PPCImageInfo image{};
    image.code_base = 0x9FFF0000;
    image.code_size = 0x1000;
    image.image_base = 0x9FFF0000;
    image.image_size = 0x8000;
    rex::X_STATUS status = runtime->Setup(image, std::move(config));
    CHECK(XFAILED(status));
    // The owner (ReXApp) still holds objects made from the graphics system: it
    // must not have been shut down or destroyed by the failed Setup.
    CHECK(probe.alive);
    CHECK_FALSE(probe.shutdown_called);
  }
  // Destroying the Runtime tears everything down, graphics included.
  CHECK(probe.shutdown_called);
  CHECK_FALSE(probe.alive);

  // The global Runtime instance was released: a new Runtime can be set up.
  auto again = std::make_unique<rex::Runtime>(root.dir);
  rex::RuntimeConfig tool{};
  tool.tool_mode = true;
  CHECK(again->Setup(std::move(tool)) == X_STATUS_SUCCESS);
}
