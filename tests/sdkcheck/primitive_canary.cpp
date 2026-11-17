// CPU execution of the shared primitive processor; no GPU or gameplay claim.
#include <catch2/catch_test_macros.hpp>
#include "gpu/xenos/primitive_processor.h"
#include "gpu/xenos/xenos_settings.h"
#include <rex/xenos/register.h>
#include <rex/kernel/heap.h>
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "kernel/kernel_fixture.h"
#include <deque>
#include <iostream>
#include <numeric>
#include <stdexcept>
using namespace rex::gpu;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
namespace reg = rex::xenos::reg;
namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
struct CpuMemory : SharedMemory {
  explicit CpuMemory(rex::kernel::TitleContext& m) : SharedMemory(m, m) { InitializeCommon(); }
  bool UploadRanges(const std::vector<std::pair<uint32_t, uint32_t>>& ranges) override {
    for (auto [page, count] : ranges)
      MarkRangeValid(page << GetPageSizeLog2(), count << GetPageSizeLog2(), false);
    return true;
  }
};
struct CpuPrimitives : PrimitiveProcessor {
  struct Buffer { std::vector<uint8_t> bytes; size_t offset; };
  std::vector<uint8_t> builtin;
  std::deque<Buffer> converted;
  bool fail_allocation = false;
  CpuPrimitives(RegisterFile& r, const XenosSettings& settings, rex::kernel::TitleContext& m, SharedMemory& s,
                bool full32, bool native_fans) : PrimitiveProcessor(r, settings, m, m, s) {
    Require(InitializeCommon(full32, native_fans, true, true, true, true), "primitive setup");
  }
  bool InitializeBuiltinIndexBuffer(size_t bytes, std::function<void(void*)> fill) override {
    builtin.resize(bytes); fill(builtin.data()); return true;
  }
  void* RequestHostConvertedIndexBufferForCurrentFrame(xenos::IndexFormat format,
      uint32_t count, bool coalign, uint32_t original, size_t& handle) override {
    if (fail_allocation) return nullptr;
    handle = converted.size();
    auto& b = converted.emplace_back();
    b.bytes.resize(count * (format == xenos::IndexFormat::kInt16 ? 2 : 4) + 64);
    b.offset = coalign ? GetSIMDCoalignmentOffset(b.bytes.data(), original) : 0;
    return b.bytes.data() + b.offset;
  }
  std::vector<uint32_t> Indices(const ProcessingResult& result) const {
    if (!result.host_draw_vertex_count) return {};
    const uint8_t* data = nullptr;
    if (result.index_buffer_type == kHostConverted) {
      const auto& b = converted.at(result.host_index_buffer_handle);
      data = b.bytes.data() + b.offset;
    } else if (result.index_buffer_type == kHostBuiltinForAuto) {
      data = builtin.data() + GetBuiltinIndexBufferOffsetBytes(result.host_index_buffer_handle);
    } else throw std::runtime_error("expected converted triangle indices");
    std::vector<uint32_t> output;
    for (unsigned i = 0; i < result.host_draw_vertex_count; ++i) {
      uint32_t value;
      if (result.host_index_format == xenos::IndexFormat::kInt16) {
        uint16_t word; std::memcpy(&word, data + i * 2, 2);
        value = xenos::GPUSwap(word, result.host_shader_index_endian);
      } else {
        std::memcpy(&value, data + i * 4, 4);
        value = xenos::GPUSwap(value, result.host_shader_index_endian);
      }
      output.push_back(value & xenos::kVertexIndexMask);
    }
    return output;
  }
};
// Independent assembly of each guest primitive, including restart parity.
std::vector<uint32_t> Expected(const std::vector<uint32_t>& source, bool fan) {
  std::vector<uint32_t> output, primitive;
  auto flush = [&] {
    for (size_t i = 2; i < primitive.size(); ++i) {
      if (fan) output.insert(output.end(), {primitive[i - 1], primitive[i], primitive[0]});
      else if (i & 1) output.insert(output.end(), {primitive[i - 1], primitive[i - 2], primitive[i]});
      else output.insert(output.end(), {primitive[i - 2], primitive[i - 1], primitive[i]});
    }
    primitive.clear();
  };
  for (uint32_t index : source) {
    if (index == 0xFEEF) flush(); else primitive.push_back(index & xenos::kVertexIndexMask);
  }
  flush(); return output;
}
}
TEST_CASE("Shared tessellation converts fans and strips with restart parity", "[sdk][gpu][shader-fetch][primitive]") {
  struct TestSettings : XenosSettings {} settings;
  REQUIRE(settings.primitive_processor_cache_min_indices.Set(1000000));
  rex::test::KernelFixture fixture;
  std::exception_ptr failure;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
   try {
    auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
    rex::kernel::TitleContext context(caller.GetRuntime());
    constexpr uint32_t virt = 0xA1000000, physical = 0x01000000;
    Require(memory.LookupHeap(virt)->AllocFixed(virt, 4096, 4096,
        rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
        rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite), "index allocation");
    CpuMemory shared(context);
    RegisterFile regs;
    unsigned checks = 0;
    for (bool full32 : {false, true}) for (bool native_fans : {false, true}) {
      CpuPrimitives processor(regs, settings, context, shared, full32, native_fans);
      for (auto mode : {xenos::TessellationMode::kDiscrete, xenos::TessellationMode::kContinuous}) {
        for (bool fan : {false, true}) for (bool dma : {false, true}) {
          for (bool wide : {false, true}) for (bool swapped : {false, true}) {
            if (!dma && (wide || swapped)) continue;
            for (unsigned count : {0u, 1u, 2u, 3u, 4u, 8u, 11u}) {
              std::memset(regs.values, 0, sizeof(regs.values));
              std::vector<uint32_t> indices(count);
              std::iota(indices.begin(), indices.end(), dma ? 10u : 0u);
              const bool restart = dma && count == 11;
              if (restart) indices = {10,11,0xFEEF,20,21,22,23,0xFEEF,30,31,32};
              const auto endian = swapped ? (wide ? xenos::Endian::k8in32 : xenos::Endian::k8in16)
                                           : xenos::Endian::kNone;
              if (dma) for (unsigned i = 0; i < count; ++i) {
                if (wide) {
                  const uint32_t value = indices[i] == 0xFEEF ? indices[i] : indices[i] | 0xA5000000;
                  uint32_t raw = xenos::GPUSwap(value, endian);
                  std::memcpy(memory.TranslateVirtual(virt + i * 4), &raw, 4);
                } else {
                  uint16_t raw = xenos::GPUSwap(uint16_t(indices[i]), endian);
                  std::memcpy(memory.TranslateVirtual(virt + i * 2), &raw, 2);
                }
              }
              reg::VGT_DRAW_INITIATOR draw{};
              draw.prim_type = fan ? xenos::PrimitiveType::kTriangleFan : xenos::PrimitiveType::kTriangleStrip;
              draw.major_mode = xenos::MajorMode::kExplicit;
              draw.source_select = dma ? xenos::SourceSelect::kDMA : xenos::SourceSelect::kAutoIndex;
              draw.index_size = wide ? xenos::IndexFormat::kInt32 : xenos::IndexFormat::kInt16;
              draw.num_indices = count;
              regs[std::to_underlying(xenos::Register::VGT_DRAW_INITIATOR)] = draw.value;
              regs[std::to_underlying(xenos::Register::VGT_OUTPUT_PATH_CNTL)] = uint32_t(xenos::VGTOutputPath::kTessellationEnable);
              regs[std::to_underlying(xenos::Register::VGT_HOS_CNTL)] = uint32_t(mode);
              reg::VGT_DMA_SIZE size{}; size.num_words = count; size.swap_mode = endian;
              regs[std::to_underlying(xenos::Register::VGT_DMA_SIZE)] = size.value; regs[std::to_underlying(xenos::Register::VGT_DMA_BASE)] = physical;
              regs[std::to_underlying(xenos::Register::VGT_MULTI_PRIM_IB_RESET_INDX)] = 0xFEEF;
              reg::PA_SU_SC_MODE_CNTL sc{}; sc.multi_prim_ib_ena = restart;
              regs[std::to_underlying(xenos::Register::PA_SU_SC_MODE_CNTL)] = sc.value;
              PrimitiveProcessor::ProcessingResult result{};
              Require(processor.Process(result), "tessellated fan/strip accepted");
              Require(result.IsTessellated() && result.host_primitive_type == xenos::PrimitiveType::kTriangleList,
                      "host tessellation topology");
              Require(!result.host_primitive_reset_enabled, "restart consumed by conversion");
              Require(processor.Indices(result) == Expected(indices, fan), "converted patch order/parity");
              ++checks;
            }
          }
        }
      }
    }
    std::cout << checks << " shared tessellation conversion cases passed\n";
    CHECK(checks > 0);
   } catch (...) { failure = std::current_exception(); }
  });
  if (failure) std::rethrow_exception(failure);
}
