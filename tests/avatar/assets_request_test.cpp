// New request lifecycle tests only. No SDK, proprietary data or previous
// suites.
#include <iostream>
#include <string>

#include "../../titles/common/avatar/avatar_assets_request.h"

using namespace x360n::avatar;
namespace {
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* label) {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL " << label << '\n';
  }
}
struct Trace {
  std::string calls;
  int live = 0, released = 0;
  uint32_t error = 0x81234567;
  char fail = 0;
  bool empty_result = false;
  size_t count = 2;
};
class Backend final : public AssetsBackend {
 public:
  explicit Backend(Trace& trace) : t(trace) {}
  size_t Count() const override { return t.count; }
  uint32_t Begin() override {
    ++t.live;
    return Event('B');
  }
  uint32_t Decode(size_t asset, bool& apply) override {
    apply = true;
    return Event(char('0' + asset));
  }
  uint32_t Apply(size_t asset) override { return Event(char('a' + asset)); }
  uint32_t Finish() override { return Event('F'); }
  AssetsBuffers Buffers() const override {
    return t.live && !t.empty_result ? AssetsBuffers{0x1000, 4096, 0x2000, 4096}
                                     : AssetsBuffers{};
  }
  void Reset() noexcept override {
    if (t.live) {
      --t.live;
      ++t.released;
    }
  }

 private:
  uint32_t Event(char c) {
    t.calls += c;
    return t.fail == c ? t.error : 0;
  }
  Trace& t;
};
AssetsRequest Request(Trace& trace) {
  return AssetsRequest(std::make_unique<Backend>(trace));
}
void Drain(AssetsRequest& request) {
  for (int i = 0; i < 16 && request.status() == kAssetsPending; ++i) {
    Check(!request.result().result, "no publication while pending");
    request.Step();
  }
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--modifier-counts") {
    for (size_t count : {size_t(14), size_t(22), size_t(32)}) {
      Trace trace;
      trace.count = count;
      auto request = Request(trace);
      std::string expected = "B";
      for (size_t i = 0; i < count; ++i) expected += char('0' + i);
      for (size_t i = 0; i < count; ++i) expected += char('a' + i);
      expected += 'F';
      for (size_t i = 0; i < 2 * count + 4 &&
                         request.status() == kAssetsPending; ++i)
        request.Step();
      Check(request.status() == 0 && trace.calls == expected,
            "component plus modifier counts complete every phase in order");
      Check(request.result().result == 0x1000 && trace.live == 1,
            "extended count publishes only after finalize");
      request.Release();
      Check(!trace.live && trace.released == 1,
            "extended count releases owned result once");
    }
    Trace invalid;
    invalid.count = 33;
    auto request = Request(invalid);
    Check(request.Step() == kAssetsInvalid && invalid.calls.empty(),
          "new upper bound rejects before any guest work");
    std::cout << "modifier-counts: " << checks << " checks, " << failures
              << " failures\n";
    return failures ? 1 : 0;
  }
  Trace success;
  {
    auto request = Request(success);
    Drain(request);
    Check(success.calls == "B01abF",
          "all decode precedes all apply, then finalize once");
    Check(request.status() == 0 && request.result().result == 0x1000 &&
              success.live == 1,
          "complete publishes owned buffers");
    request.Cancel();
    request.Step();
    Check(success.calls == "B01abF" && success.live == 1,
          "terminal poll and cancel retain result");
    request.Release();
    request.Release();
    request.Step();
    Check(!request.result().result && !success.live && success.released == 1,
          "release revokes borrowed addresses exactly once");
  }
  Check(success.released == 1, "destructor after release does not double free");
  for (char fail : std::string("B01abF")) {
    Trace trace;
    trace.fail = fail;
    auto request = Request(trace);
    Drain(request);
    const auto calls = trace.calls;
    request.Cancel();
    request.Step();
    Check(request.state() == AssetsRequest::State::Failed &&
              request.status() == trace.error,
          "each phase preserves exact backend failure");
    Check(!trace.live && trace.released == 1 && !request.result().result &&
              trace.calls == calls,
          "failure cleans partial allocation and stops guest work");
  }
  for (int steps = 0; steps <= 5; ++steps) {
    Trace trace;
    auto request = Request(trace);
    for (int i = 0; i < steps; ++i) request.Step();
    const auto calls = trace.calls;
    request.Cancel();
    request.Step();
    request.Cancel();
    Check(request.status() == kAssetsAbort && !request.result().result &&
              !trace.live && trace.calls == calls &&
              trace.released == (steps ? 1 : 0),
          "cancellation at every phase boundary releases unpublished work");
  }
  for (size_t count : {size_t(0), size_t(33)}) {
    Trace trace;
    trace.count = count;
    auto request = Request(trace);
    Check(request.Step() == kAssetsInvalid && trace.calls.empty(),
          "invalid count never calls backend");
  }
  AssetsRequest absent(nullptr);
  Check(absent.Step() == kAssetsInvalid, "null backend fails");
  Trace empty;
  empty.empty_result = true;
  auto invalid = Request(empty);
  Drain(invalid);
  Check(invalid.status() == kAssetsFailure && !empty.live,
        "success without output is rejected");
  Trace abandoned;
  {
    auto request = Request(abandoned);
    request.Step();
    request.Step();
  }
  Check(!abandoned.live && abandoned.released == 1,
        "pending destruction releases buffers");
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
