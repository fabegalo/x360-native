#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <rex/kernel/guest_allocator.h>
#include <rex/kernel/thread_state.h>
#include "kernel/kernel_fixture.h"
#include "kernel/title_fixture.h"
#include "kernel/title_state.h"
#include "xam/xam_state.h"

TEST_CASE("XAM workspace binding follows the executable workspace size", "[sdk][workspace-lifetime]") {
  const auto header_size = GENERATE(0u, 0x40000u, 0x80000u);
  CAPTURE(header_size);
  rex::test::TitleBoot boot(true, [] {
    REQUIRE(rex::kernel::GetTitlePaths().entry.Set("default.xex"));
    REQUIRE(rex::kernel::GetTitlePaths().imports.Set(""));
  });
  rex::test::XEXSpec spec;
  spec.load_address = rex::test::kTitleImageBase;
  spec.entry_point = rex::test::kTitleEntry;
  spec.title_id = rex::test::kTitleId;
  spec.image = rex::test::BuildTitleImage();
  spec.workspace_size = header_size;
  const auto bytes = rex::test::BuildXEX(spec);
  {
    std::ofstream output(boot.folder.path / "game/default.xex", std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
  }
  rex::kernel::TitleRuntime runtime(rex::test::MakeTitleImage());
  const auto loaded = runtime.Load();
  INFO((loaded ? std::string{} : loaded.error().GetDescription()));
  REQUIRE(loaded);
  auto& state = rex::kernel::StateOf(runtime);
  CHECK(state.GetWorkspace().GetSize() == rex::kernel::WorkspaceHeap::ResolveSize(header_size));
  // Compare before calling: the negative control exposes a dangling reference without an AV.
  auto* xam_workspace = &rex::xam::CurrentTitle().workspace;
  auto* public_workspace = &runtime.GetGuestWorkspace();
  REQUIRE(xam_workspace == public_workspace);

  rex::kernel::ThreadState thread(1, 0x8000, 0, state.GetAddressSpace(), &runtime);
  auto* previous = rex::kernel::ThreadState::Get();
  rex::kernel::ThreadState::Bind(&thread);
  rex::ScopeGuard restore([previous] { rex::kernel::ThreadState::Bind(previous); });
  rex::test::Caller caller(*thread.GetContext(), runtime.GetMembase().GetBase(), runtime);
  const auto out = caller.Alloc(4);
  REQUIRE(caller.XAM("XamAlloc", {0, 0x510, out}) == 0);
  const auto address = caller.Load32(out);
  REQUIRE(address != 0);
  CHECK(public_workspace->QueryAllocationSize(address) == 0x510);
  CHECK(caller.Load32(address) == 0);
  CHECK(caller.XAM("XamFree", {address}) == 0);
  CHECK(public_workspace->QueryAllocationSize(address) == 0);
  const auto direct = public_workspace->Allocate(32, true);
  REQUIRE(direct);
  CHECK(xam_workspace->QueryAllocationSize(*direct) == 32);
  xam_workspace->Free(*direct);
  CHECK(public_workspace->QueryAllocationSize(*direct) == 0);
}
