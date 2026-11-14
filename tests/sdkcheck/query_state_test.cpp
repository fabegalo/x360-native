// Synthetic backend exercising the real PM4/query state machine, without a GPU.
#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/system/xmemory.h>
#include <iostream>
#include <stdexcept>

using namespace rex::graphics;
namespace {
class Graphics final : public GraphicsSystem {
 public:
  explicit Graphics(rex::memory::Memory& memory) { memory_ = &memory; }
  std::string name() const override { return "query-state-cpu-check"; }
 private:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override { return {}; }
};
class Processor final : public CommandProcessor {
 public:
  explicit Processor(Graphics* graphics) : CommandProcessor(graphics, nullptr) {}
  void IssueSwap(uint32_t, uint32_t, uint32_t) override {}
  void Reset(const char* mode) {
    SetZPDMode(mode);
    ResetVIZState(); ResetZPDState();
    pending.clear(); raw = {}; opens = 0;
  }
  void Report(uint32_t address) {
    register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR] = address;
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
  unsigned opens = 0, waits = 0;
 private:
  struct Pending { ReportHandle handle; VIZQueryHandle viz; XenosZPDReport counts; uint32_t scale; };
  std::vector<Pending> pending;
  XenosZPDReport raw;
  void Packet(xenos::Type3Opcode opcode, uint32_t value) {
    uint32_t words[] = {rex::byte_swap(value), 0};
    rex::memory::RingBuffer reader(reinterpret_cast<uint8_t*>(words), sizeof(words));
    reader.set_write_offset(sizeof(uint32_t));
    const bool ok = opcode == xenos::PM4_EVENT_WRITE_ZPD
        ? ExecutePacketType3_EVENT_WRITE_ZPD(&reader, xenos::MakePacketType3(opcode, 1), 1)
        : ExecutePacketType3_VIZ_QUERY(&reader, xenos::MakePacketType3(opcode, 1), 1);
    if (!ok || reader.read_count()) throw std::runtime_error("query PM4 packet failed");
  }
  bool IsQueryPoolReady() const override { return true; }
  QueryOpenResult OpenQuery(bool) override { ++opens; raw = {}; return QueryOpenResult::kOpened; }
  bool CloseQuery(ReportHandle handle, const VIZQueryHandle& viz, uint64_t& submission) override {
    pending.push_back({handle, viz, raw, GetZPDScaleArea()}); submission = 1; return true;
  }
  bool AwaitQueryResolve(ReportHandle, uint64_t) override { ++waits; Resolve(); return true; }
  void AwaitVIZQueryResolve(uint64_t) override { ++waits; Resolve(); }
  bool SetupContext() override { return true; }
  void ShutdownContext() override {}
  Shader* LoadShader(xenos::ShaderType, uint32_t, const uint32_t*, uint32_t) override { return nullptr; }
  bool IssueDraw(xenos::PrimitiveType, uint32_t, IndexBufferInfo*, bool) override { return true; }
  bool IssueCopy() override { return false; }
};
}
int main() {
  try {
    rex::memory::Memory memory;
    if (!memory.Initialize()) return 2;
    if (!memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000, 4096, 4096,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 3;
    Graphics graphics(memory);
    Processor cp(&graphics);
    unsigned checks = 0;
    auto check = [&](bool value, const char* reason) {
      ++checks;
      if (!value) throw std::runtime_error(reason);
    };
    constexpr uint32_t first = 0x01000000, second = first + 32, third = second + 32;
    auto guest = [&](uint32_t address) { return memory.TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(address); };
    auto passed = [&](uint32_t address) { return uint32_t(guest(address)->ZPass_A) + uint32_t(guest(address)->ZPass_B); };
    const uint32_t sentinel = rex::byte_swap(0xFFFFFEEDu);
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
    for (const char* mode : {"fast", "fast-alt"}) {
      cp.Reset(mode);
      cp.Report(first); cp.Retire(); cp.Draw(0); cp.Report(second); cp.Resolve();
      check(passed(second) == 0, "completed zero query did not replace speculation");
      cp.Draw(0); cp.Report(second);
      check(passed(second) == (std::string_view(mode) == "fast" ? 1u : 0u), "fast/fast-alt zero-cache behavior");
      cp.Resolve();
    }
    cp.Reset("strict");
    reg::PA_SC_VIZ_QUERY viz{}; viz.viz_query_ena = 1; viz.viz_query_id = 7;
    (*graphics.register_file())[XE_GPU_REG_PA_SC_VIZ_QUERY] = viz.value;
    cp.Survey(7, false); cp.Draw(0); cp.Survey(7, true);
    check(!cp.Consumer(7), "zero VIZ survey failed to cull its consumer");
    cp.Survey(7, false); cp.Draw(1); cp.Survey(7, true);
    check(cp.Consumer(7), "reused VIZ ID retained an old invisible result");
    cp.Survey(7, false); cp.Survey(7, true);
    check(!cp.Consumer(7), "empty VIZ survey fabricated visibility");
    std::cout << checks << '/' << checks << " synthetic query-state checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
