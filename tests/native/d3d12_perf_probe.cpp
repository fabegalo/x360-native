// Headless production D3D12Render workloads. Synthetic throughput is not FPS.
#include "../../src/native/d3d12/d3d12_render.h"
#include "../../src/native/d3d12/d3d12_scanout.h"
#include "../../src/native/commands/primitive_indices.h"
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/cvar.h>
#include <rex/chrono/frame_timing.h>
#include <rex/logging.h>
#include <rex/graphics/format/ucode.h>
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace x360n::native;
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<uint32_t> ExportShader() {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      32 | (1u << 15) | (15u << 16), 0x6Cu << 16, (1u << 8) | (1u << 31) | (uint32_t(AluVectorOpcode::kMad) << 24),
      33 | (1u << 15) | (15u << 16), 0, (uint32_t(AluVectorOpcode::kMax) << 24) | (2u << 16) | (2u << 8)};
}
std::vector<uint32_t> TextureExportShader() {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(3) << 12) | (uint64_t(1) << 18) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      // eA = c0 + vertex_index * c1, preserving r0 for the fetch coordinates.
      32 | (1u << 15) | (15u << 16), 0x6Cu << 16,
      (1u << 8) | (1u << 31) | (uint32_t(AluVectorOpcode::kMad) << 24),
      // tfetch2D r1.xyzw, r0.xyz, tf0, explicit LOD 0, fetch-constant filters.
      uint32_t(FetchOpcode::kTextureFetch) | (1u << 12) | (0x24u << 26),
      0x688u | (3u << 12) | (3u << 14) | (3u << 16) | (7u << 18) | (3u << 24) | (3u << 26),
      uint32_t(FetchOpDimension::k2D) << 14,
      // eM0 = r1.
      33 | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (1u << 16) | (1u << 8) | (3u << 30)};
}
std::vector<uint32_t> RasterShader(bool vertex, unsigned constant = 0) {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(vertex ? AllocType::kVsPosition : AllocType::kPsColors) << 41) |
      (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(vertex ? 2 : 1) << 12) | (uint64_t(vertex) << 16) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16)};
  if (vertex) code.insert(code.end(), {(1u << 12) | (1u << 19),
      0x688u | (1u << 13) | (uint32_t(VertexFormat::k_32_32_32_32_FLOAT) << 16), 4});
  code.insert(code.end(), {(vertex ? 62u : 0u) | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) |
          (vertex ? (1u << 16) | (1u << 8) | (3u << 30) : (constant << 16) | (constant << 8))});
  return code;
}
uint64_t CpuNs() {
  FILETIME c, e, k, u;
  Require(GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u), "process CPU clock");
  auto ticks=[](FILETIME t) { return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
  return (ticks(k)+ticks(u))*100;
}
void CheckPipelineInitialization(rex::ui::d3d12::D3D12Provider& provider,
    rex::memory::Memory& memory, RegisterFile& draw, RegisterFile& resolve,
    const std::filesystem::path& output) {
  for (unsigned prelude=0;prelude<3;++prelude) {
    const auto directory=output/("cache-init-"+std::to_string(prelude));
    std::filesystem::path file;
    std::filesystem::file_time_type published{};
    for (unsigned run=0;run<2;++run) {
      D3D12Scanout backend(provider);
      backend.InitializeShaderStorage(directory,0);
      if (prelude==0) backend.SetSampleCounts({},false);
      else if (prelude==1) backend.ReadSampleCounts(false);
      else backend.Resolve(resolve,memory);
      auto words=ExportShader();for(auto& word:words)word=rex::byte_swap(word);
      auto* shader=backend.LoadShader(ShaderType::kVertex,words.data(),uint32_t(words.size()));
      const auto start=std::chrono::steady_clock::now();
      backend.Draw(draw,memory,shader,nullptr,PrimitiveType::kTriangleList,1,nullptr);
      const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      backend.Drain();
      if (!run) {
        for (const auto& entry:std::filesystem::recursive_directory_iterator(directory))
          if(entry.path().extension()==".pso") {Require(file.empty(),"one cache PSO expected");file=entry.path();}
        Require(!file.empty(),"non-shader initialization lost the pipeline cache directory");
        published=std::filesystem::last_write_time(file);
      } else Require(std::filesystem::last_write_time(file)==published,"valid driver cache was unnecessarily rewritten");
      std::cout<<"PASS cache_init prelude="<<prelude<<" run="<<run<<" draw_ms="<<elapsed<<"\n";
    }
  }
}
DXGI_QUERY_VIDEO_MEMORY_INFO VideoMemory(const rex::ui::d3d12::D3D12Provider& provider) {
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "memory budget factory");
  Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
  Require(SUCCEEDED(factory->EnumAdapterByLuid(provider.GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter))), "memory budget adapter");
  DXGI_QUERY_VIDEO_MEMORY_INFO result;
  Require(SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&result)), "local video memory usage");
  return result;
}
// Keep workload and guest register setup identical to the Vulkan probe.
struct Render : D3D12Render {
  using D3D12Render::D3D12Render;
  void Flush(bool wait) { D3D12Render::Flush(wait ? kFlushDrain : kFlushCapacity); }
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) { SyncPacketRead(address,length,opcode); }
  Shader* LoadShader(ShaderType type, const uint32_t* words, uint32_t count) { return shaders.Load(type,words,count); }
};
void CheckTextureViews(Render& render, RegisterFile draw, rex::memory::Memory& memory, uint32_t output) {
  const auto source = memory.SystemHeapAlloc(65536, 4096, rex::memory::kSystemHeapPhysical);
  Require(source != 0, "texture descriptor source allocation");
  auto words = TextureExportShader();
  for (auto& word : words) word = rex::byte_swap(word);
  auto* vs = render.LoadShader(ShaderType::kVertex, words.data(), uint32_t(words.size()));
  xe_gpu_texture_fetch_t fetch{};
  fetch.type = FetchConstantType::kTexture; fetch.dimension = DataDimension::k2DOrStacked;
  fetch.format = TextureFormat::k_32_32_32_32_FLOAT; fetch.pitch = 1;
  fetch.base_address = memory.GetPhysicalAddress(source) >> 12;
  fetch.size_2d.width = fetch.size_2d.height = 31;
  fetch.swizzle = 0x688;
  fetch.clamp_x = fetch.clamp_y = fetch.clamp_z = ClampMode::kClampToEdge;
  auto set_fetch = [&] { std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], &fetch, sizeof(fetch)); };
  set_fetch();
  const uint32_t physical = memory.GetPhysicalAddress(output);
  auto issue = [&](unsigned slot) {
    draw[XE_GPU_REG_SHADER_CONSTANT_000_X] = ((physical + slot * 16) >> 2) | (1u << 30);
    render.Draw(draw, memory, vs, nullptr, PrimitiveType::kTriangleList, 1, nullptr);
  };
  auto verify = [&](unsigned slot, const std::array<float, 4>& value) {
    Require(!std::memcmp(memory.TranslatePhysical(physical + slot * 16), value.data(), sizeof(value)),
        "texture descriptor sampled unexpected resource/swizzle bytes");
  };
  const std::array<float, 4> first{.25f, .5f, .75f, 1.f}, second{1.f, .75f, .5f, .25f};
  auto fill = [&](const std::array<float, 4>& value) {
    for (unsigned i = 0; i < 1024; ++i)
      std::memcpy(memory.TranslateVirtual<uint8_t*>(source) + i * 16, value.data(), sizeof(value));
  };
  fill(first);
  issue(0); render.Flush(true); verify(0, first); // Warm the real driver pipeline.
  issue(0);
  const auto before = render.performance();
  const auto start = std::chrono::steady_clock::now();
  const auto cpu = CpuNs();
  for (unsigned i = 0; i < 6000; ++i) issue(0);
  const auto after = render.performance();
  render.Flush(true);
  std::cout << "TEXTURE_BINDING_WORKLOAD draws=6000 wall_ms="
      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
      << " cpu_ms=" << double(CpuNs() - cpu) / 1e6
      << " capacity_submissions=" << after.submissions - before.submissions
      << " new_views=" << after.texture_views - before.texture_views
      << " reused_views=" << after.texture_view_reuses - before.texture_view_reuses << '\n';
  Require(after.submissions == before.submissions, "resident texture bindings forced a capacity submission");
  Require(after.texture_views == before.texture_views && after.texture_view_reuses > before.texture_view_reuses,
      "resident texture bindings recreated immutable descriptors");
  // Keep both swizzles live in the same list: updating the first descriptor
  // in place would make both writes observe the second view on the GPU.
  issue(0);
  fetch.swizzle = 3 | (2 << 3) | (1 << 6) | (0 << 9); set_fetch();
  issue(1);
  render.Flush(true); verify(0, first); verify(1, second);
  fetch.swizzle = 0x688; set_fetch();
  // Watched CPU writes, repeated arena reuse and the unchanged resource view
  // must all retain fresh data, including when descriptor indices repeat.
  for (unsigned iteration = 0; iteration < 4; ++iteration) {
    const auto& value = iteration & 1 ? first : second;
    fill(value); issue(2); render.Flush(true); verify(2, value);
  }
  fetch.type = FetchConstantType::kInvalidTexture; set_fetch();
  issue(3); render.Flush(true); verify(3, {});
  memory.SystemHeapFree(source);
  std::cout << "PASS immutable texture descriptors: 6000 reuses, live swizzles, CPU writes, null view and arena lifetime\n";
}
void CheckConstantUploads(Render& render, RegisterFile draw, rex::memory::Memory& memory, uint32_t output) {
  const uint32_t physical = memory.GetPhysicalAddress(output) + 65536;
  auto load = [&](bool alternate) {
    auto code = ExportShader();
    if (alternate) code.back() = (uint32_t(ucode::AluVectorOpcode::kMax) << 24) | (3u << 16) | (3u << 8);
    for (auto& word : code) word = rex::byte_swap(word);
    return render.LoadShader(ShaderType::kVertex, code.data(), uint32_t(code.size()));
  };
  auto* vs = load(false); auto* other_vs = load(true);
  const std::array<float, 4> a{.25f, .5f, .75f, 1}, b{1, .75f, .5f, .25f};
  auto issue = [&](unsigned slot, const std::array<float, 4>& value, unsigned bank = 0, bool alternate = false) {
    draw[XE_GPU_REG_SQ_VS_CONST] = 0x000FF000 | bank;
    xe_gpu_memexport_stream_t stream{};
    stream.base_address = (physical + slot * 16) >> 2; stream.const_0x1 = 1;
    stream.const_0x4b000000 = 0x4B000000; stream.format = ColorFormat::k_32_32_32_32_FLOAT;
    stream.num_format = SurfaceNumberFormat::kFloat; stream.const_0x4b0 = 0x4B0;
    stream.index_count = 1; stream.const_0x96 = 0x96;
    const auto base = XE_GPU_REG_SHADER_CONSTANT_000_X + bank * 4;
    std::memcpy(&draw[base], &stream, sizeof(stream));
    std::memset(&draw[base + 4], 0, 16); draw[base + 5] = std::bit_cast<uint32_t>(1.f);
    std::memcpy(&draw[base + (alternate ? 12 : 8)], value.data(), sizeof(value));
    render.Draw(draw, memory, alternate ? other_vs : vs, nullptr, PrimitiveType::kTriangleList, 1, nullptr);
  };
  auto verify = [&](unsigned slot, const std::array<float, 4>& value) {
    Require(!std::memcmp(memory.TranslatePhysical(physical + slot * 16), value.data(), sizeof(value)),
        "cached draw constants changed exported bytes");
  };
  issue(0, a);
  const auto before = render.performance();
  for (unsigned i = 0; i < 64; ++i) issue(0, a);
  const auto after = render.performance();
  for (unsigned root : {0u, 1u, 3u, 4u}) {
    Require(after.constant_uploads[root] == before.constant_uploads[root] &&
        after.constant_reuses[root] - before.constant_reuses[root] == 64,
        "identical draw constants allocated another upload");
  }
  // Even unconsumed fields are compared, never trusted to a guest dirty bit.
  for (const auto change : {std::pair{XE_GPU_REG_PA_CL_VTE_CNTL, 0u},
                            std::pair{XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031, 3u},
                            std::pair{XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 4u}}) {
    const auto prior = render.performance();
    draw[change.first] ^= change.second == 0 ? 1u << 8 : 1u;
    issue(0, a);
    Require(render.performance().constant_uploads[change.second] == prior.constant_uploads[change.second] + 1,
        "changed system/bool/fetch constants reused stale upload");
  }
  // All four different packed buffers remain referenced by queued draws.
  issue(1, a); issue(2, b); issue(3, b, 8); issue(4, a, 0, true);
  render.Flush(true);
  verify(0, a); verify(1, a); verify(2, b); verify(3, b); verify(4, a);
  for (unsigned i = 0; i < 4; ++i) {
    const auto prior = render.performance();
    issue(5, i & 1 ? a : b);
    for (unsigned root : {0u, 1u, 3u, 4u})
      Require(render.performance().constant_uploads[root] == prior.constant_uploads[root] + 1,
          "draw constant address survived recording arena rotation");
    render.Flush(true); verify(5, i & 1 ? a : b);
  }
  std::cout << "PASS draw constants: exact reuse, changed system/bool/fetch bytes, VS maps/banks, immutable exports and arenas\n";
}
void CheckResidentIndices(Render& render, RegisterFile draw, RegisterFile resolve,
    rex::memory::Memory& memory, uint32_t output, bool check_constants = false) {
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto index_storage = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(vertices && index_storage, "resident index allocations");
  auto load = [&](bool vertex, std::vector<uint32_t> words) {
    for (auto& word : words) word = rex::byte_swap(word);
    return render.LoadShader(vertex ? ShaderType::kVertex : ShaderType::kPixel, words.data(), uint32_t(words.size()));
  };
  auto* vs = load(true, RasterShader(true));
  auto* ps = load(false, RasterShader(false));
  const float triangle[]{-1, -1, .5f, 1, -1, 3, .5f, 1, 3, -1, .5f, 1};
  xe_gpu_vertex_fetch_t fetch{};
  fetch.type = FetchConstantType::kVertex; fetch.address = memory.GetPhysicalAddress(vertices) >> 2; fetch.size = 16;
  draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0] = fetch.dword_0;
  draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1] = fetch.dword_1;
  draw[XE_GPU_REG_RB_MODECONTROL] = uint32_t(EdramMode::kColorDepth);
  draw[XE_GPU_REG_RB_SURFACE_INFO] = 80;
  draw[XE_GPU_REG_RB_COLOR_MASK] = 15;
  draw[XE_GPU_REG_RB_COLOR_INFO] = 0; // RGBA8 at EDRAM base 0.
  reg::RB_BLENDCONTROL blend{};
  blend.color_srcblend = blend.alpha_srcblend = BlendFactor::kOne;
  draw[XE_GPU_REG_RB_BLENDCONTROL0] = blend.value;
  draw[XE_GPU_REG_PA_CL_VTE_CNTL] = 0x3F;
  auto fp = [&](unsigned reg, float value) { draw[reg] = std::bit_cast<uint32_t>(value); };
  fp(XE_GPU_REG_PA_CL_VPORT_XSCALE, 16); fp(XE_GPU_REG_PA_CL_VPORT_XOFFSET, 16);
  fp(XE_GPU_REG_PA_CL_VPORT_YSCALE, -16); fp(XE_GPU_REG_PA_CL_VPORT_YOFFSET, 16);
  fp(XE_GPU_REG_PA_CL_VPORT_ZSCALE, 1); fp(XE_GPU_REG_PA_CL_VPORT_ZOFFSET, 0);
  draw[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR] = 32 | (32 << 16);
  draw[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] = 0x80000000;
  draw[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = 32 | (32 << 16);
  draw[XE_GPU_REG_PA_SU_VTX_CNTL] = 1;
  const std::array<float, 4> color{.25f, .5f, .75f, 1};
  std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4], color.data(), sizeof(color));
  reg::RB_COPY_CONTROL copy{}; copy.copy_command = CopyCommand(1); copy.color_clear_enable = 1;
  resolve[XE_GPU_REG_RB_COPY_CONTROL] = copy.value;
  resolve[XE_GPU_REG_RB_COLOR_CLEAR] = resolve[XE_GPU_REG_RB_COLOR_CLEAR_LO] = 0;
  resolve[XE_GPU_REG_RB_COPY_DEST_BASE] = memory.GetPhysicalAddress(output) + 4096;
  auto capture = [&] {
    render.Resolve(resolve, memory); render.Flush(true);
    const auto* bytes = memory.TranslatePhysical<const uint8_t*>(resolve[XE_GPU_REG_RB_COPY_DEST_BASE]);
    return std::vector<uint8_t>(bytes, bytes + 4096);
  };
  capture(); // Clear EDRAM before the first comparison.
  if (check_constants) {
    std::memcpy(memory.TranslateVirtual(vertices), triangle, sizeof(triangle));
    reg::VGT_DRAW_INITIATOR initiator{}; initiator.prim_type = PrimitiveType::kTriangleList;
    draw[XE_GPU_REG_VGT_DRAW_INITIATOR] = initiator.value;
    auto issue = [&](Shader* pixel) { render.Draw(draw, memory, vs, pixel, PrimitiveType::kTriangleList, 3, nullptr); };
    issue(ps);
    const auto prior = render.performance(); issue(ps);
    Require(render.performance().constant_uploads[2] == prior.constant_uploads[2] &&
        render.performance().constant_reuses[2] == prior.constant_reuses[2] + 1,
        "identical pixel constants allocated another upload");
    const auto first = capture();
    Require(std::any_of(first.begin(), first.end(), [](uint8_t b) { return b != 0; }),
        "constant test reference triangle did not render");
    auto* other_ps = load(false, RasterShader(false, 1));
    const std::array<float, 4> other_color{1, .75f, .5f, .25f};
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 257 * 4], other_color.data(), sizeof(other_color));
    issue(other_ps); const auto second = capture();
    Require(first != second, "pixel shader constant maps did not change color");
    // Keep both pixel uploads alive across compute resolve roots, in one list.
    issue(ps); render.Resolve(resolve, memory);
    issue(other_ps); resolve[XE_GPU_REG_RB_COPY_DEST_BASE] += 4096; render.Resolve(resolve, memory);
    render.Flush(true);
    Require(!std::memcmp(memory.TranslatePhysical(resolve[XE_GPU_REG_RB_COPY_DEST_BASE] - 4096), first.data(), first.size()) &&
        !std::memcmp(memory.TranslatePhysical(resolve[XE_GPU_REG_RB_COPY_DEST_BASE]), second.data(), second.size()),
        "queued draws overwrote earlier immutable pixel constants");
    resolve[XE_GPU_REG_RB_COPY_DEST_BASE] -= 4096;
    draw[XE_GPU_REG_SQ_PS_CONST] = 0x000FF080;
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 128 * 4], other_color.data(), sizeof(other_color));
    issue(ps); Require(capture() == second, "pixel constant bank reused stale packed bytes");
    draw[XE_GPU_REG_SQ_PS_CONST] = 0x000FF100;
    std::cout << "PASS pixel constants: exact reuse, shader maps/banks and immutable pixels across compute roots\n";
  }
  const uint32_t indices_physical = memory.GetPhysicalAddress(index_storage);
  auto encode = [&](std::span<const uint32_t> indices, IndexFormat format, Endian endian, bool unaligned) {
    const unsigned bytes = format == IndexFormat::kInt16 ? 2 : 4;
    const unsigned size = (unsigned(indices.size()) * bytes + 3) & ~3u;
    std::vector<uint8_t> encoded(size);
    for (unsigned i = 0; i < indices.size(); ++i) for (unsigned b = 0; b < bytes; ++b) {
      const unsigned offset = (i * bytes + b) ^ (endian == Endian::k8in16 ? 1u : endian == Endian::k8in32 ? 3u : endian == Endian::k16in32 ? 2u : 0u);
      encoded.at(offset) = uint8_t(indices[i] >> (b * 8));
    }
    // R16 also exercises a valid address that is not aligned to four bytes.
    const unsigned offset = unaligned ? 65 : bytes == 2 ? 2 : 0;
    std::memcpy(memory.TranslateVirtual<uint8_t*>(index_storage) + offset, encoded.data(), encoded.size());
    return DrawIndices{format, endian, uint32_t(indices.size()), indices_physical + offset, size};
  };
  unsigned comparisons = 0;
  auto compare = [&](std::span<const uint32_t> indices, IndexFormat format, Endian endian,
                     PrimitiveType primitive, bool restart, int32_t offset, uint32_t minimum, uint32_t maximum) {
    std::memset(memory.TranslateVirtual(vertices), 0, 4096);
    std::memcpy(memory.TranslateVirtual<uint8_t*>(vertices) + minimum * 16, triangle, sizeof(triangle));
    draw[XE_GPU_REG_VGT_INDX_OFFSET] = uint32_t(offset);
    draw[XE_GPU_REG_VGT_MIN_VTX_INDX] = minimum; draw[XE_GPU_REG_VGT_MAX_VTX_INDX] = maximum;
    reg::VGT_DRAW_INITIATOR initiator{}; initiator.prim_type = primitive;
    draw[XE_GPU_REG_VGT_DRAW_INITIATOR] = initiator.value;
    reg::PA_SU_SC_MODE_CNTL mode{}; mode.multi_prim_ib_ena = restart;
    draw[XE_GPU_REG_PA_SU_SC_MODE_CNTL] = mode.value;
    draw[XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX] = 0xFFFFFF;
    // An unaligned copy intentionally selects the existing CPU-normalized
    // path. It is the byte-for-byte reference for the identical guest indices.
    auto cpu = encode(indices, format, endian, true);
    render.Draw(draw, memory, vs, ps, primitive, uint32_t(indices.size()), &cpu);
    const auto expected = capture();
    Require(std::any_of(expected.begin(), expected.end(), [](uint8_t b) { return b != 0; }),
        "reference indexed triangle did not render");
    auto gpu = encode(indices, format, endian, false);
    const auto before = render.performance();
    render.Draw(draw, memory, vs, ps, primitive, uint32_t(indices.size()), &gpu);
    const auto after = render.performance();
    const auto actual = capture();
    Require(actual == expected, "resident and normalized index pixels differ");
    const bool direct = !restart && !NeedsPrimitiveConversion(primitive) &&
        (format == IndexFormat::kInt32 || endian == Endian::kNone || endian == Endian::k8in16);
    Require(after.gpu_index_draws - before.gpu_index_draws == unsigned(direct),
        "resident index path/fallback selection changed");
    Require(after.normalized_indices - before.normalized_indices == (direct ? 0 : indices.size()),
        "resident indices were normalized on the CPU");
    ++comparisons;
  };
  for (const auto format : {IndexFormat::kInt16, IndexFormat::kInt32})
    for (const auto endian : {Endian::kNone, Endian::k8in16, Endian::k8in32, Endian::k16in32}) {
      const std::array<uint32_t, 3> ordinary{0, 1, 2};
      compare(ordinary, format, endian, PrimitiveType::kTriangleList, false, 0, 0, 0xFFFFFF);
      // Test R16 values above INT16_MAX after zero-extension, and all 32-bit
      // bytes before the translator masks the guest vertex index to 24 bits.
      const std::array<uint32_t, 3> high = format == IndexFormat::kInt16
          ? std::array<uint32_t, 3>{65533, 65534, 65535}
          : std::array<uint32_t, 3>{0xAB000000, 0xAB000001, 0xAB000002};
      compare(high, format, endian, PrimitiveType::kTriangleList, false,
          format == IndexFormat::kInt16 ? -65533 : 0, 0, 0xFFFFFF);
    }
  const std::array<uint32_t, 3> clamped{0, 2, 999};
  compare(clamped, IndexFormat::kInt32, Endian::k8in32, PrimitiveType::kTriangleList, false, 0, 1, 3);
  const std::array<uint32_t, 3> strip{0, 1, 2};
  compare(strip, IndexFormat::kInt16, Endian::k8in16, PrimitiveType::kTriangleStrip, false, 0, 0, 0xFFFFFF);
  compare(strip, IndexFormat::kInt32, Endian::k8in32, PrimitiveType::kTriangleFan, false, 0, 0, 0xFFFFFF);
  const std::array<uint32_t, 7> restarted{0, 1, 2, 0xFFFFFF, 0, 1, 2};
  compare(restarted, IndexFormat::kInt32, Endian::k8in32, PrimitiveType::kTriangleStrip, true, 0, 0, 0xFFFFFF);

  // A GPU producer updates the index bytes in the resident mirror. The draw
  // must consume them without waiting for their guest readback publication.
  draw[XE_GPU_REG_VGT_INDX_OFFSET] = draw[XE_GPU_REG_VGT_MIN_VTX_INDX] = 0;
  draw[XE_GPU_REG_VGT_MAX_VTX_INDX] = 0xFFFFFF; draw[XE_GPU_REG_PA_SU_SC_MODE_CNTL] = 0;
  reg::VGT_DRAW_INITIATOR initiator{}; initiator.prim_type = PrimitiveType::kTriangleList;
  draw[XE_GPU_REG_VGT_DRAW_INITIATOR] = initiator.value;
  std::memcpy(memory.TranslateVirtual(vertices), triangle, sizeof(triangle));
  std::memset(memory.TranslateVirtual(index_storage), 0xCC, 16);
  RegisterFile producer;
  producer[XE_GPU_REG_SQ_PROGRAM_CNTL] = draw[XE_GPU_REG_SQ_PROGRAM_CNTL];
  producer[XE_GPU_REG_SQ_VS_CONST] = 0x000FF000;
  producer[XE_GPU_REG_VGT_MAX_VTX_INDX] = 0xFFFFFF;
  producer[XE_GPU_REG_RB_MODECONTROL] = uint32_t(EdramMode::kNoOperation);
  xe_gpu_memexport_stream_t stream{}; stream.base_address = indices_physical >> 2; stream.const_0x1 = 1;
  stream.const_0x4b000000 = 0x4B000000; stream.format = ColorFormat::k_16_16_16_16;
  stream.num_format = SurfaceNumberFormat::kUnsignedInteger; stream.const_0x4b0 = 0x4B0;
  stream.index_count = 1; stream.const_0x96 = 0x96;
  std::memcpy(&producer[XE_GPU_REG_SHADER_CONSTANT_000_X], &stream, sizeof(stream));
  producer[XE_GPU_REG_SHADER_CONSTANT_000_X + 5] = std::bit_cast<uint32_t>(1.f);
  const std::array<float, 4> produced{0, 1, 2, 0};
  std::memcpy(&producer[XE_GPU_REG_SHADER_CONSTANT_000_X + 8], produced.data(), sizeof(produced));
  auto* exporter = load(true, ExportShader());
  render.Draw(producer, memory, exporter, nullptr, PrimitiveType::kTriangleList, 1, nullptr);
  const auto before = render.performance();
  DrawIndices produced_indices{IndexFormat::kInt16, Endian::kNone, 3, indices_physical, 8};
  render.Draw(draw, memory, vs, ps, PrimitiveType::kTriangleList, 3, &produced_indices);
  Require(render.performance().submissions == before.submissions, "GPU index consumer waited for CPU readback");
  const auto produced_pixels = capture();
  const std::array<uint16_t, 4> expected_indices{0, 1, 2, 0};
  Require(!std::memcmp(memory.TranslatePhysical(indices_physical), expected_indices.data(), 8), "GPU index producer bytes");
  const auto cpu = encode(strip, IndexFormat::kInt16, Endian::kNone, true);
  render.Draw(draw, memory, vs, ps, PrimitiveType::kTriangleList, 3, &cpu);
  Require(capture() == produced_pixels, "GPU-produced resident indices differ from CPU-normalized pixels");
  for (const auto invalid : {DrawIndices{IndexFormat::kInt16, Endian::kNone, 3, indices_physical, 5},
                            DrawIndices{IndexFormat::kInt32, Endian::kNone, 3, 0x1FFFFFF8, 12},
                            DrawIndices{IndexFormat::kInt32, Endian::kNone, 2, indices_physical, 12}}) {
    bool rejected = false;
    try { render.Draw(draw, memory, vs, ps, PrimitiveType::kTriangleList, 3, &invalid); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "resident indices bypassed extent validation");
  }
  render.Flush(true);
  memory.SystemHeapFree(index_storage); memory.SystemHeapFree(vertices);
  std::cout << "PASS resident indices: " << comparisons
      << " normalized/GPU pixel pairs, endian, offset/clamp, restart/conversion fallbacks, CPU writes, GPU producer, arenas and bounds\n";
}
void CheckResolveContracts(Render& render, RegisterFile& regs,
    rex::memory::Memory& memory, uint32_t output) {
  const uint32_t physical=memory.GetPhysicalAddress(output)+65536;
  reg::RB_COPY_CONTROL control{}; control.copy_command=CopyCommand(1); control.color_clear_enable=1;
  regs[XE_GPU_REG_RB_COPY_CONTROL]=control.value;
  auto resolve=[&](unsigned index,uint32_t clear) {
    regs[XE_GPU_REG_RB_COPY_DEST_BASE]=physical+index*4096;
    regs[XE_GPU_REG_RB_COLOR_CLEAR]=clear;
    regs[XE_GPU_REG_RB_COLOR_CLEAR_LO]=clear;
    render.Resolve(regs,memory); render.Flush(false);
  };
  constexpr uint32_t a=0x332211FF,b=0x11CC22FF;
  resolve(0,a); resolve(1,b); resolve(2,0);
  render.CompleteGuestWrites(kGuestSyncEvent,0);
  for(unsigned i=0;i<1024;++i) {
    Require(memory.TranslatePhysical<const uint32_t*>(physical+4096)[i]==a,"colored resolve A bytes");
    Require(memory.TranslatePhysical<const uint32_t*>(physical+8192)[i]==b,"colored resolve B bytes");
  }
  ScanoutTexture input{physical+4096,32,32,32,1,0,0|(1<<3)|(2<<6)|(3<<9),4096,false},scaled;
  std::vector<uint8_t> bytes;
  const bool enhanced=render.PrepareScanout(input,memory.TranslatePhysical(input.address),scaled,bytes);
  if(GetNativeQualitySettings().scale_x>1) {
    Require(enhanced,"scaled resolve cache missing completed ticket");
    for(unsigned y=0;y<scaled.height;++y) for(unsigned x=0;x<scaled.width;++x) {
      uint32_t actual; std::memcpy(&actual,bytes.data()+(size_t(y)*scaled.pitch+x)*4,4);
      Require(actual==a,"scaled ticket/scanout bytes");
    }
    *memory.TranslateVirtual<uint32_t*>(output+65536+4096)=0x89ABCDEF;
    Require(!render.PrepareScanout(input,memory.TranslatePhysical(input.address),scaled,bytes),"CPU write kept stale scaled cache");
  }
  std::cout<<"PASS colored resolves and scaled snapshots byte-for-byte\n";
}
void CheckPendingContracts(Render& render, RegisterFile& regs,
    rex::memory::Memory& memory, rex::graphics::Shader* vs, uint32_t output,
    rex::ui::d3d12::D3D12Provider& provider) {
  const uint32_t physical=memory.GetPhysicalAddress(output);
  Microsoft::WRL::ComPtr<ID3D12Fence> gate;
  Require(SUCCEEDED(provider.GetDevice()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))),"test gate fence");
  Require(SUCCEEDED(provider.GetDirectQueue()->Wait(gate.Get(),1)),"queue test gate");
  std::mutex mutex; std::condition_variable cv; bool release=false, timed_out=false;
  // A bounded watchdog also releases the gate on an exception or regression.
  std::jthread watchdog([&] {
    std::unique_lock lock(mutex);
    timed_out=!cv.wait_for(lock,std::chrono::seconds(2),[&]{return release;});
    Require(SUCCEEDED(gate->Signal(1)),"release test gate");
  });
  auto draw=[&](uint32_t address,float value) {
    regs[XE_GPU_REG_SHADER_CONSTANT_000_X]=(address>>2)|(1u<<30);
    for(unsigned c=0;c<4;++c) regs[XE_GPU_REG_SHADER_CONSTANT_000_X+8+c]=std::bit_cast<uint32_t>(value);
    render.Draw(regs,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
    render.Flush(false);
  };
  draw(physical,0.75f);
  *memory.TranslateVirtual<uint32_t*>(output)=0x12345678;
  draw(physical+16,0.875f); // Revalidate the CPU-written page with a new epoch.
  // Three recording arenas allow two queued batches and one active arena.
  // A disjoint guest read must leave both gated submissions in flight.
  render.SyncGuestRead(physical+4096,4,0xFF);
  { std::lock_guard lock(mutex); release=true; } cv.notify_all(); watchdog.join();
  Require(!timed_out,"Flush(false) or disjoint read waited for pending GPU writebacks");
  draw(physical+16,0.125f); // Later overlapping submission must win.
  // Reuse all three slots; the gate guarantees real pending resource lifetimes.
  render.SetSampleCounts({},false); render.Flush(false);
  render.SyncGuestRead(physical+16,16,0xFF);
  Require(*memory.TranslateVirtual<uint32_t*>(output)==0x12345678,"stale readback overwrote CPU epoch");
  const std::array<float,4> final{.125f,.125f,.125f,.125f};
  Require(!std::memcmp(memory.TranslatePhysical(physical+16),final.data(),16),"overlapping serial publication order");
  draw(physical+32,0.25f);
  const SampleCounts counts{1,2,3,4,5,6,7,8};
  render.SetSampleCounts(counts,false);
  Require(render.ReadSampleCounts(false)==counts,"counter readback reused live staging");
  render.CompleteGuestWrites(kGuestSyncEvent,0);

  std::cout<<"PASS gated in-flight writebacks: disjoint read, ring reuse, CPU epochs, overlap order, counters, events\n";
}
void CheckStagingGrowth(Render& render, RegisterFile& regs,
    rex::memory::Memory& memory, rex::graphics::Shader* vs) {
  constexpr uint32_t bytes=17u<<20;
  const auto allocation=memory.SystemHeapAlloc(bytes,4096,rex::memory::kSystemHeapPhysical);
  Require(allocation!=0,"large staging allocation");
  std::memset(memory.TranslateVirtual(allocation),0xCD,bytes);
  xe_gpu_memexport_stream_t stream;
  std::memcpy(&stream,&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],16);
  stream.base_address=memory.GetPhysicalAddress(allocation)>>2;
  stream.index_count=bytes/16;
  std::memcpy(&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],&stream,16);
  render.Draw(regs,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
  render.Flush(false);
  // Change the extent again before the first large staging buffer retires.
  stream.index_count=1;
  std::memcpy(&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],&stream,16);
  render.Draw(regs,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
  render.CompleteGuestWrites(kGuestSyncPacket,0);
  Require(!std::memcmp(memory.TranslateVirtual(allocation),&regs[XE_GPU_REG_SHADER_CONSTANT_000_X+8],16),"grown staging data");
  const auto* result=memory.TranslateVirtual<const uint8_t*>(allocation);
  for(uint32_t i=16;i<bytes;++i) Require(result[i]==0xCD,"grown staging padding or offsets");
  memory.SystemHeapFree(allocation);
  std::cout<<"PASS staging overflow/growth: 17 MiB checked byte-for-byte\n";
}
}
int main(int argc, char** argv) {
  try {
    Require(argc >= 3, "usage: perf OUTPUT export|resolve|sync|none [iterations=4096] [scale=1] [instrument=1] [contracts] [pipeline_threads]");
    const std::filesystem::path out=argv[1]; std::filesystem::create_directories(out);
    const std::string mode=argv[2];
    const unsigned iterations=argc>3?std::stoul(argv[3]):4096;
    const unsigned scale=argc>4?std::stoul(argv[4]):1;
    const bool measured=argc<=5 || std::stoi(argv[5])!=0;
    Require(iterations>0,"positive iteration count");
    Require(mode=="export"||mode=="resolve"||mode=="sync"||mode=="none"||mode=="batch","unknown workload");
    rex::InitLogging((out/"runtime.log").string().c_str());
    if (argc > 7) rex::cvar::SetFlagByName("native_pipeline_threads", argv[7]);
    rex::cvar::SetFlagByName("native_stats", measured?"true":"false");
    if (scale>1) {
      rex::chrono::FrameTiming::ActivateNative();
      rex::cvar::SetFlagByName("native_enhanced", "true");
      rex::cvar::SetFlagByName("native_scale_x", std::to_string(scale));
      rex::cvar::SetFlagByName("native_scale_y", std::to_string(scale));
    }
    rex::Runtime runtime(out, out/"user");
    rex::RuntimeConfig config; config.tool_mode=true; config.kernel_init=rex::kernel::InitializeKernel;
    Require(runtime.Setup(std::move(config))==0,"runtime setup");
    Require(GetNativeQualitySettings().scale_x==scale,"requested Enhanced scale is inactive");
    auto& memory=*runtime.memory();
    auto provider=rex::ui::d3d12::D3D12Provider::Create();
    Require(bool(provider),"D3D12 provider");

    const auto output=memory.SystemHeapAlloc(1<<20,4096,rex::memory::kSystemHeapPhysical);
    const auto vertices=memory.SystemHeapAlloc(4096,4096,rex::memory::kSystemHeapPhysical);
    Require(output&&vertices,"physical allocations");
    const uint32_t physical=memory.GetPhysicalAddress(output);
    std::memset(memory.TranslateVirtual(output),0xCD,1<<20);
    {
      Render render(*provider);
      if (argc > 6 && std::string(argv[6]) == "contracts") {
        render.Flush(true);
        const auto before = render.performance().submissions;
        for (unsigned i = 0; i < 8; ++i) {
          render.D3D12Render::Flush(D3D12Render::kFlushPresent);
          render.Flush(true);
        }
        Require(render.performance().submissions == before, "empty present/drain submitted GPU work");
        const SampleCounts counts{1, 2, 3, 4, 5, 6, 7, 8};
        render.SetSampleCounts(counts, false);
        render.Flush(false);
        Require(render.performance().submissions == before + 1, "real GPU commands were skipped");
        Require(render.ReadSampleCounts(false) == counts, "empty drain lost pending GPU results");
        std::cout << "PASS empty present/drain avoids submissions and preserves real work\n";
      }
      RegisterFile draw, resolve;
      reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg=2; program.ps_num_reg=1;
      draw[XE_GPU_REG_SQ_PROGRAM_CNTL]=program.value;
      draw[XE_GPU_REG_VGT_MAX_VTX_INDX]=0xFFFFFF;
      draw[XE_GPU_REG_RB_MODECONTROL]=uint32_t(EdramMode::kNoOperation);
      draw[XE_GPU_REG_SQ_VS_CONST]=0x000FF000; draw[XE_GPU_REG_SQ_PS_CONST]=0x000FF100;
      xe_gpu_memexport_stream_t stream{}; stream.base_address=physical>>2; stream.const_0x1=1;
      stream.const_0x4b000000=0x4B000000; stream.format=ColorFormat::k_32_32_32_32_FLOAT;
      stream.num_format=SurfaceNumberFormat::kFloat; stream.const_0x4b0=0x4B0;
      stream.index_count=1; stream.const_0x96=0x96;
      const std::array<float,4> data{.25f,.5f,.75f,1.f};
      std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X],&stream,16);
      draw[XE_GPU_REG_SHADER_CONSTANT_000_X+5]=std::bit_cast<uint32_t>(1.f);
      std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X+8],data.data(),16);
      auto shader=ExportShader();
      for(auto& word:shader) word=rex::byte_swap(word);
      auto* vs=render.LoadShader(ShaderType::kVertex,shader.data(),uint32_t(shader.size()));
      const float xy[]{0,0,32,0,0,32}; std::memcpy(memory.TranslateVirtual(vertices),xy,sizeof(xy));
      xe_gpu_vertex_fetch_t fetch{}; fetch.type=FetchConstantType::kVertex;
      fetch.address=memory.GetPhysicalAddress(vertices)>>2; fetch.size=6;
      resolve[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]=fetch.dword_0;
      resolve[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1]=fetch.dword_1;
      resolve[XE_GPU_REG_RB_MODECONTROL]=uint32_t(EdramMode::kCopy);
      resolve[XE_GPU_REG_RB_SURFACE_INFO]=80;
      resolve[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL]=0x80000000;
      resolve[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]=32|(32<<16);
      resolve[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR]=32|(32<<16);
      resolve[XE_GPU_REG_RB_COPY_DEST_BASE]=physical+4096;
      resolve[XE_GPU_REG_RB_COPY_DEST_PITCH]=32|(32<<16);
      resolve[XE_GPU_REG_RB_COPY_DEST_INFO]=uint32_t(TextureFormat::k_8_8_8_8)<<7;
      reg::RB_COPY_CONTROL control{}; control.copy_command=CopyCommand(1);
      resolve[XE_GPU_REG_RB_COPY_CONTROL]=control.value;
      auto workload=[&](unsigned i) {
        if(mode=="resolve") {
          resolve[XE_GPU_REG_RB_COPY_DEST_BASE]=physical+4096+(i%8)*4096;
          render.Resolve(resolve,memory);
        } else if(mode!="none") {
          // Adjacent and overlapping writes, within and across submissions.
          draw[XE_GPU_REG_SHADER_CONSTANT_000_X]=((physical+(i%8)*16)>>2)|(1u<<30);
          render.Draw(draw,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
        } else render.SetSampleCounts({},false);
        if(mode!="batch") render.Flush(false);
        if(mode=="sync") render.SyncGuestRead(physical,128,0xFF);
      };
      for(unsigned i=0;i<64;++i) workload(i);
      render.Flush(true);
      const auto before=render.performance();
      const auto cpu=CpuNs();
      const auto start=std::chrono::steady_clock::now();
      std::vector<double> frames;
      for(unsigned i=0;i<iterations;++i) {
        const auto frame=std::chrono::steady_clock::now(); workload(i);
        frames.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame).count());
      }
      render.CompleteGuestWrites(kGuestSyncPacket,0);
      render.Flush(true);
      const auto wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      const auto cpu_ns=CpuNs()-cpu;
      const auto video=VideoMemory(*provider);
      const auto counters=render.performance();
      if(mode=="export"||mode=="sync"||mode=="batch") for(unsigned i=0;i<8;++i)
        Require(!std::memcmp(memory.TranslatePhysical(physical+i*16),data.data(),16),"MEMEXPORT byte equality");
      if(mode=="resolve") for(unsigned i=0;i<8*4096;++i)
        Require(memory.TranslatePhysical<const uint8_t*>(physical+4096)[i]==0,"resolve byte equality");
      std::ofstream bytes(out/"bytes.bin",std::ios::binary);
      bytes.write(memory.TranslatePhysical<const char*>(physical),1<<20);
      std::sort(frames.begin(),frames.end());
      auto pct=[&](double q){return frames[std::min(frames.size()-1,size_t(std::ceil(q*frames.size()))-1)];};
      double low=0; const size_t n=std::max(size_t(1),(frames.size()+99)/100);
      for(size_t i=0;i<n;++i) low+=frames[frames.size()-1-i];
      std::ofstream json(out/"metrics.json");
      json<<"{\"mode\":\""<<mode<<"\",\"scale\":"<<scale<<",\"iterations\":"<<iterations
          <<",\"wall_ms\":"<<wall<<",\"cpu_ms\":"<<cpu_ns/1e6
          <<",\"p50_ms\":"<<pct(.50)<<",\"p95_ms\":"<<pct(.95)<<",\"p99_ms\":"<<pct(.99)
          <<",\"synthetic_1pct_low\":"<<1000*n/low
          <<",\"submissions\":"<<counters.submissions-before.submissions<<",\"waits\":"<<counters.waits-before.waits
          <<",\"blocked_waits\":"<<counters.waits-before.waits
          <<",\"wait_ms\":"<<(counters.wait_ns-before.wait_ns)/1e6
          <<",\"submit_ms\":null,\"gpu_ms\":"<<(counters.gpu_frequency?double(counters.gpu_ticks-before.gpu_ticks)*1000/counters.gpu_frequency:0)
          <<",\"gpu_samples\":"<<counters.gpu_samples-before.gpu_samples
          <<",\"readbacks\":null,\"readback_bytes\":null,\"buffers\":null,\"peak_buffer_bytes\":null"
          <<",\"vram_usage_bytes\":"<<video.CurrentUsage<<",\"vram_budget_bytes\":"<<video.Budget<<"}\n";
      std::cout<<"PASS "<<mode<<" iterations="<<iterations<<" wall_ms="<<wall<<" waits="<<counters.waits-before.waits<<"\n";
      if(argc>6 && std::string(argv[6])=="contracts") {
        CheckPendingContracts(render,draw,memory,vs,output,*provider);
        CheckStagingGrowth(render,draw,memory,vs);
        CheckResolveContracts(render,resolve,memory,output);
        CheckPipelineInitialization(*provider,memory,draw,resolve,out);
      }
      if(argc>6 && std::string(argv[6])=="cache-contracts")
        CheckPipelineInitialization(*provider,memory,draw,resolve,out);
      if(argc>6 && std::string(argv[6])=="texture-contracts")
        CheckTextureViews(render,draw,memory,output);
      if(argc>6 && std::string(argv[6])=="index-contracts")
        CheckResidentIndices(render,draw,resolve,memory,output);
      if(argc>6 && std::string(argv[6])=="constant-contracts") {
        CheckConstantUploads(render,draw,memory,output);
        CheckResidentIndices(render,draw,resolve,memory,output,true);
      }
    }
    if(argc>6) for(unsigned i=0;i<3;++i) {
      Render recreated(*provider); recreated.SetSampleCounts({},false); recreated.Flush(false);
    }
    memory.SystemHeapFree(output); memory.SystemHeapFree(vertices);
    runtime.Shutdown(); rex::FlushLogging();
    return 0;
  } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
