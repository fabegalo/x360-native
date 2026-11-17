// Synthetic backend exercising the real PM4/query state machine, without a GPU.
#include <catch2/catch_test_macros.hpp>
#include "gpu/xenos/command_processor.h"
#include "gpu/xenos/xenos_graphics_driver_base.h"
#include "gpu/xenos/pipeline/shader/shader.h"
#include <rex/kernel/heap.h>
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "kernel/kernel_fixture.h"
#include <iostream>
#include <algorithm>
#include <bit>
#include <functional>
#include <span>
#include <stdexcept>
#include <chrono>
#include <thread>

using namespace rex::gpu;
namespace xenos = rex::xenos;
namespace reg = rex::xenos::reg;
namespace {
class Graphics final : public XenosGraphicsDriverBase {
 public:
  Graphics() = default;
  std::string_view GetName() const override { return "query-state-cpu-check"; }
 private:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override { return {}; }
};
class Processor final : public CommandProcessor {
 public:
  Processor(Graphics* graphics, const rex::TitleInfo& info, rex::kernel::TitleContext& context)
      : CommandProcessor(graphics, info, context, context, context) {}
  void IssueSwap(uint32_t, uint32_t, uint32_t) override {}
  void Reset(const char* mode) {
    SetZPDMode(GetGraphicsDriver(), mode);
    ResetVIZState(); ResetZPDState();
    pending.clear(); raw = {}; opens = 0;
    resources_ready = true; close_succeeds = true; wait_completes = true;
  }
  void Report(uint32_t address) {
    register_file_->values[std::to_underlying(xenos::Register::RB_SAMPLE_COUNT_ADDR)] = address;
    Packet(xenos::PM4_EVENT_WRITE_ZPD, xenos::ZPASS_DONE);
  }
  void Survey(uint32_t id, bool end) { Packet(xenos::PM4_VIZ_QUERY, id | (end ? 0x100 : 0)); }
  void Draw(uint64_t passed, uint32_t scale = 1, uint64_t failed = 0, uint64_t stencil = 0) {
    UpdateQuerySegment(scale, false, false);
    if (!active_segment_.segment_active) throw std::runtime_error("query segment not open");
    raw += XenosZPDReport{failed, passed, stencil};
    OnVIZSurveyDraw(true);
  }
  void Split() { CloseQuerySegment(); }
  void Resolve() {
    for (const auto& p : pending) {
      if (p.handle != kInvalidReportHandle) OnZPDQueryResolved(p.handle, p.counts, p.scale);
      if (p.viz.generation != kInvalidVIZGeneration)
        OnVIZQueryResolved(p.viz.id, p.viz.generation, p.counts.z_pass != 0);
    }
    pending.clear(); PumpPendingRetire();
  }
  void Retire() { PumpPendingRetire(); }
  bool Consumer(uint32_t id) { return PrepareVIZDraw(0x100 | id); }
  void Wait() { PrepareForWait(); }
  void UnmeasuredSurvey() { OnVIZSurveyDraw(false); }
  bool TryDraw() { UpdateQuerySegment(1, false, false); return active_segment_.segment_active; }
  bool Failed() const { return query_backend_failed_; }
  bool resources_ready = true, close_succeeds = true, wait_completes = true;
  unsigned opens = 0, waits = 0;
  std::function<void()> system_completion;
  bool SystemCommands(std::span<const uint32_t> payload) {
    std::vector<uint32_t> words(payload.size() + 1);
    std::transform(payload.begin(), payload.end(), words.begin(), [](uint32_t value) { return rex::byte_swap(value); });
    rex::memory::RingBuffer reader(reinterpret_cast<uint8_t*>(words.data()), words.size() * sizeof(uint32_t));
    reader.SetWriteOffset(payload.size() * sizeof(uint32_t));
    const bool result = ExecutePacketType3_XE_SYSTEM_COMMANDS(&reader,
        xenos::BuildPacketType3(xenos::PM4_XE_SYSTEM_COMMANDS, static_cast<uint32_t>(payload.size())),
        static_cast<uint32_t>(payload.size()));
    CHECK(reader.GetReadCount() == 0);
    return result;
  }
  void SeedTitle(Shader& vertex, Shader& pixel) {
    active_vertex_shader_ = &vertex;
    active_pixel_shader_ = &pixel;
    bin_select_ = 0x123456789ABCDEF0;
    bin_mask_ = 0xFEDCBA9876543210;
  }
  bool IsTitleRestored(const Shader& vertex, const Shader& pixel) const {
    return active_vertex_shader_ == &vertex && active_pixel_shader_ == &pixel &&
        bin_select_ == 0x123456789ABCDEF0 && bin_mask_ == 0xFEDCBA9876543210;
  }
 private:
  void OnSystemCommandsExecuted() override { if (system_completion) system_completion(); }
  struct Pending { ReportHandle handle; VIZQueryHandle viz; XenosZPDReport counts; uint32_t scale; };
  std::vector<Pending> pending;
  XenosZPDReport raw;
  void Packet(xenos::Type3Opcode opcode, uint32_t value) {
    uint32_t words[] = {rex::byte_swap(value), 0};
    rex::memory::RingBuffer reader(reinterpret_cast<uint8_t*>(words), sizeof(words));
    reader.SetWriteOffset(sizeof(uint32_t));
    const bool ok = opcode == xenos::PM4_EVENT_WRITE_ZPD
        ? ExecutePacketType3_EVENT_WRITE_ZPD(&reader, xenos::BuildPacketType3(opcode, 1), 1)
        : ExecutePacketType3_VIZ_QUERY(&reader, xenos::BuildPacketType3(opcode, 1), 1);
    if (!ok || reader.GetReadCount()) throw std::runtime_error("query PM4 packet failed");
  }
  bool IsQueryPoolReady() const override { return resources_ready; }
  QueryOpenResult OpenQuery(bool) override { ++opens; raw = {}; return QueryOpenResult::kOpened; }
  bool CloseQuery(ReportHandle handle, const VIZQueryHandle& viz, uint64_t& submission) override {
    if (!close_succeeds) return false;
    pending.push_back({handle, viz, raw, GetZPDScaleArea()}); submission = 1; return true;
  }
  bool AwaitQueryResolve(ReportHandle, uint64_t) override { ++waits; if (!wait_completes) return false; Resolve(); return true; }
  void AwaitVIZQueryResolve(uint64_t) override { ++waits; Resolve(); }
  bool SetupContext() override { return true; }
  void ShutdownContext() override {}
  Shader* LoadShader(xenos::ShaderType, uint32_t, const uint32_t*, uint32_t) override { return nullptr; }
  bool IssueDraw(xenos::PrimitiveType, uint32_t, IndexBuffer*, bool) override { return true; }
  bool IssueCopy() override { return false; }
};
}

TEST_CASE("System command buffers restore title state before publishing completion",
          "[sdk][gpu][shader-fetch][system-command-buffer]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
    constexpr uint32_t list = 0xA1000000, fence = 0x01000100;
    REQUIRE(memory.LookupHeap(list)->AllocFixed(list, 4096, 4096,
        rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
        rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
    rex::kernel::TitleContext context(caller.GetRuntime());
    const rex::TitleInfo info{};
    Graphics graphics;
    Processor cp(&graphics, info, context);
    uint32_t code[] = {0, 0, 0};
    Shader vertex(xenos::ShaderType::kVertex, 1, code, 3, std::endian::native);
    Shader pixel(xenos::ShaderType::kPixel, 2, code, 3, std::endian::native);
    cp.SeedTitle(vertex, pixel);
    auto& regs = *graphics.GetRegisterFile();
    regs[xenos::Register::PA_SU_SC_MODE_CNTL] = 0x123;
    regs[xenos::Register::SHADER_CONSTANT_000_X] = 0x11223344;
    const std::vector<uint32_t> before(std::begin(regs.values), std::end(regs.values));
    const uint32_t commands[] = {
      xenos::BuildPacketType0(std::to_underlying(xenos::Register::PA_SU_SC_MODE_CNTL), 1), 0x456,
      xenos::BuildPacketType0(std::to_underlying(xenos::Register::SHADER_CONSTANT_000_X), 1), 0x55667788,
      xenos::BuildPacketType3(xenos::PM4_SET_BIN_MASK_LO, 1), 7,
      xenos::BuildPacketType3(xenos::PM4_SET_BIN_SELECT_LO, 1), 9};
    auto* guest_commands = memory.TranslateVirtual<rex::be_u32*>(list);
    for (size_t i = 0; i < std::size(commands); ++i) guest_commands[i] = commands[i];
    REQUIRE(context.StoreRelease32(fence, 0));
    unsigned completed = 0;
    cp.system_completion = [&] {
      ++completed;
      CHECK(context.LoadAcquire32(fence).value() == 0);
      CHECK(cp.IsTitleRestored(vertex, pixel));
      CHECK(std::equal(before.begin(), before.end(), std::begin(regs.values)));
    };
    const uint32_t payload[] = {list, static_cast<uint32_t>(std::size(commands)), fence, 42, 1, 2, 3, 4, 5, 6};
    REQUIRE(cp.SystemCommands(payload));
    CHECK(context.LoadAcquire32(fence).value() == 42);
    CHECK(completed == 1);
    CHECK(cp.IsTitleRestored(vertex, pixel));
    CHECK(std::equal(before.begin(), before.end(), std::begin(regs.values)));
    const uint32_t malformed[] = {list, 0};
    CHECK_FALSE(cp.SystemCommands(malformed));
    CHECK(completed == 1);
  });
}
TEST_CASE("Real query state preserves reports, visibility and explicit failure", "[sdk][gpu][shader-fetch][query-state]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
    rex::kernel::TitleContext context(caller.GetRuntime());
    const rex::TitleInfo info{};
    REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000,4096,4096,
        rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
        rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
    Graphics graphics;
    Processor cp(&graphics, info, context);
    unsigned checks = 0;
    auto check = [&](bool value, const char* reason) {
      ++checks;
      INFO(reason);
      REQUIRE(value);
    };
    constexpr uint32_t first = 0x01000000, second = first + 32, third = second + 32;
    auto guest = [&](uint32_t address) { return memory.TranslatePhysical<xenos::DepthSampleCounts*>(address); };
    auto passed = [&](uint32_t address) { return uint32_t(guest(address)->ZPass_A) + uint32_t(guest(address)->ZPass_B); };
    const uint32_t sentinel = rex::byte_swap(0xFFFFFEEDu);
    check(GetZPDMode(graphics) == ZPDMode::kStrict, "real strict queries must be the default");
    cp.Reset("strict");
    cp.Report(first); cp.Retire();
    check(passed(first) == 0 && cp.opens == 0, "initial report must be zero without allocating a query");
    guest(second)->ZPass_A = sentinel;
    cp.Draw(8, 4, 4, 0); cp.Report(second);
    check(uint32_t(guest(second)->ZPass_A) == sentinel, "strict published before completion");
    cp.Resolve();
    check(passed(second) == 2 && uint32_t(guest(second)->ZFail_A) == 1, "full counter normalization");
    cp.Draw(3); cp.Split(); cp.Draw(2, 4); cp.Report(third); cp.Resolve();
    check(passed(third) == 6 && cp.opens == 3, "submission split lost counts or normalized with the wrong scale");
    cp.Draw(5); guest(first)->ZPass_A = sentinel; cp.Report(first); cp.Wait();
    check(passed(first) == 11 && cp.waits == 1, "strict wait did not resolve a real queued report");
    cp.Reset("strict");
    reg::PA_SC_VIZ_QUERY viz{}; viz.viz_query_ena = 1; viz.viz_query_id = 7;
    (*graphics.GetRegisterFile())[std::to_underlying(xenos::Register::PA_SC_VIZ_QUERY)] = viz.value;
    cp.Survey(7, false); cp.Draw(0); cp.Survey(7, true);
    check(!cp.Consumer(7), "zero VIZ survey failed to cull its consumer");
    cp.Survey(7, false); cp.Draw(1); cp.Survey(7, true);
    check(cp.Consumer(7), "reused VIZ ID retained an old invisible result");
    cp.Survey(7, false); cp.Survey(7, true);
    check(!cp.Consumer(7), "empty VIZ survey fabricated visibility");
    (*graphics.GetRegisterFile())[std::to_underlying(xenos::Register::PA_SC_VIZ_QUERY)] = 0;
    cp.Reset("strict"); cp.Report(first); cp.Retire();
    cp.Draw(7); guest(second)->ZPass_A = sentinel; cp.Report(second);
    cp.wait_completes = false; cp.Wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); cp.Wait();
    check(uint32_t(guest(second)->ZPass_A) == sentinel, "strict timeout fabricated completion");
    cp.wait_completes = true; cp.Wait();
    check(passed(second) == 7, "late strict completion lost the real samples");
    for (bool close_failure : {false, true}) {
      cp.Reset("strict"); cp.Report(first); cp.Retire();
      guest(second)->ZPass_A = sentinel;
      if (close_failure) { cp.Draw(9); cp.close_succeeds = false; }
      else { cp.resources_ready = false; check(!cp.TryDraw(), "unavailable pool opened a query"); }
      bool rejected = false;
      try { cp.Report(second); } catch (const std::runtime_error&) { rejected = true; }
      check(rejected && cp.Failed() && uint32_t(guest(second)->ZPass_A) == sentinel,
            "query failure silently published a fake report");
    }
    cp.Reset("strict");
    (*graphics.GetRegisterFile())[std::to_underlying(xenos::Register::PA_SC_VIZ_QUERY)] = viz.value;
    cp.Survey(7, false); cp.UnmeasuredSurvey();
    check(cp.Failed(), "unmeasured VIZ survey fabricated visibility");
    std::cout << checks << '/' << checks << " synthetic query-state checks passed\n";
    for (const uint32_t invalid : {0x01000001u, 0x20000000u, 0xFFFFFFFCu}) {
      cp.Reset("strict");
      CHECK_THROWS_AS(cp.Report(invalid), std::runtime_error);
      CHECK(cp.Failed());
    }
  });
}

TEST_CASE("Fast query modes correct speculation with completed GPU counts",
          "[sdk][gpu][shader-fetch][query-state]") {
  for (const char* mode : {"fast", "fast-alt"}) {
    // Restart settings must be selected before the app freezes startup values.
    Graphics graphics;
    SetZPDMode(graphics, mode);
    REQUIRE(GetZPDMode(graphics) ==
            (std::string_view(mode) == "fast" ? ZPDMode::kFast : ZPDMode::kFastAlt));
    rex::test::KernelFixture fixture;
    fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
      auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
      rex::kernel::TitleContext context(caller.GetRuntime());
      const rex::TitleInfo info{};
      REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000, 4096, 4096,
          rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
          rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
      Processor cp(&graphics, info, context);
      cp.Reset(mode);
      constexpr uint32_t first = 0x01000000, second = first + 32;
      auto passed = [&] {
        const auto* report = memory.TranslatePhysical<xenos::DepthSampleCounts*>(second);
        return uint32_t(report->ZPass_A) + uint32_t(report->ZPass_B);
      };
      cp.Report(first); cp.Retire(); cp.Draw(0); cp.Report(second); cp.Resolve();
      CHECK(passed() == 0);
      cp.Draw(0); cp.Report(second);
      CHECK(passed() == (std::string_view(mode) == "fast" ? 1u : 0u));
      std::this_thread::sleep_for(std::chrono::milliseconds(260));
      cp.Retire(); cp.Resolve();
      CHECK(passed() == 0);
    });
  }
}
