#include "d3d12_render.h"
#include "../shaders/rectangle_shader.h"
#include "../shaders/point_shader.h"
#include "../textures/texture_load.h"
#include "../commands/primitive_indices.h"
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/logging.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <cmath>
#include <bit>
#include <d3dcompiler.h>
#include <rex/graphics/flags.h>
#include <fstream>
#include <stdexcept>
REXCVAR_DEFINE_STRING(native_draw_dump, "", "GPU/Native", "Diagnostic draw snapshot directory");
REXCVAR_DEFINE_STRING(native_draw_dump_ps, "", "GPU/Native", "Diagnostic pixel shader hash to snapshot");
REXCVAR_DEFINE_STRING(native_pso_dump, "", "GPU/Native", "Diagnostic directory for the latest graphics PSO inputs");
namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using T = DxbcShaderTranslator;
namespace {
void Require(bool v, const char* what) { if (!v) throw std::runtime_error(std::string("Native draw: ") + what); }
uint64_t CpuNs(bool process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (process) GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  else GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
  auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
  return (ticks(kernel) + ticks(user)) * 100;
}
}
const std::vector<uint8_t>& D3D12Render::RectangleShader(unsigned interpolators, unsigned clip_planes) {
  auto& binary = rectangles_[interpolators | (clip_planes << 8)];
  if (!binary.empty()) return binary;
  binary = CompileRectangleShader(interpolators, clip_planes);
  return binary;
}

void D3D12Render::Draw(const RegisterFile& r, rex::memory::Memory& memory, Shader* vertex, Shader* pixel,
    PrimitiveType primitive, uint32_t count, const DrawIndices* indices) {
  Timer timer(stats_enabled_, draw_ns_);
  Require(vertex, "missing vertex shader");
  auto& vs = *static_cast<DxbcShader*>(vertex);
  auto* ps = static_cast<DxbcShader*>(pixel);
  static bool draw_dumped = false;
  static const auto dump_dir = REXCVAR_GET(native_draw_dump);
  static const uint64_t dump_hash = REXCVAR_GET(native_draw_dump_ps).empty() ? 0 :
      std::stoull(REXCVAR_GET(native_draw_dump_ps), nullptr, 16);
  if (!dump_dir.empty() && !draw_dumped && ps && ps->ucode_data_hash() == dump_hash) {
    Flush(kFlushDrain);
    const auto dir = std::filesystem::path(dump_dir);
    std::filesystem::create_directories(dir);
    auto save = [&](const std::string& name, const void* p, size_t n) {
      std::ofstream(dir / name, std::ios::binary).write(static_cast<const char*>(p), n);
    };
    save("registers.bin", r.values, sizeof(r.values));
    save("vs.bin", vs.ucode_dwords(), vs.ucode_dword_count() * 4);
    save("ps.bin", ps->ucode_dwords(), ps->ucode_dword_count() * 4);
    std::ofstream(dir / "draw.txt") << uint32_t(primitive) << ' ' << count << ' ' << bool(indices);
    if (indices) save("indices.bin", indices, sizeof(*indices));
    auto range = [&](uint32_t address, uint32_t bytes) {
      if (address && bytes && uint64_t(address) + bytes <= 0x20000000)
        save("memory-" + std::to_string(address) + ".bin", memory.TranslatePhysical(address), bytes);
    };
    if (indices) range(indices->guest_base, indices->length);
    for (const auto& b : vs.vertex_bindings()) {
      auto f = r.GetVertexFetch(b.fetch_constant);
      if (f.type == FetchConstantType::kVertex) range(f.address << 2, f.size << 2);
    }
    for (unsigned i = 0; i < 32; ++i) {
      auto f = r.GetTextureFetch(i);
      if (f.type != FetchConstantType::kTexture) continue;
      uint32_t w, h, d, base, mip, lo, hi;
      texture_util::GetSubresourcesFromFetchConstant(f, &w, &h, &d, &base, &mip, &lo, &hi);
      auto layout = texture_util::GetGuestTextureLayout(f.dimension, f.pitch, w + 1, h + 1, d + 1, f.tiled, f.format, f.packed_mips, true, hi);
      range(base << 12, layout.base.level_data_extent_bytes);
      if (hi) range(mip << 12, layout.mips_total_extent_bytes);
    }
    draw_dumped = true;
    REXLOG_INFO("Native isolated draw captured to {}", dir.string());
  }
  if (r.Get<reg::RB_MODECONTROL>().edram_mode == EdramMode::kDepthOnly) ps = nullptr;
  Require(!vs.memexport_eM_written() && (!ps || !ps->memexport_eM_written()), "memexport not implemented");
  const bool polygon = draw_util::IsPrimitivePolygonal(r);
  if (!draw_util::IsRasterizationPotentiallyDone(r, polygon) || !count) return;
  Require(r.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select != VGTOutputPath::kTessellationEnable, "tessellation not implemented");
  const auto msaa = r.Get<reg::RB_SURFACE_INFO>().msaa_samples;
  Require(msaa <= MsaaSamples::k4X, "invalid MSAA sample count");
  const bool point = primitive == PrimitiveType::kPointList;
  const bool fan = primitive == PrimitiveType::kTriangleFan;
  const bool quad = primitive == PrimitiveType::kQuadList;
  if (primitive != PrimitiveType::kTriangleList && primitive != PrimitiveType::kTriangleStrip &&
      primitive != PrimitiveType::kRectangleList && !fan && !quad && !point)
    throw std::runtime_error("Native draw: primitive conversion not implemented (primitive=" +
        std::to_string(uint32_t(primitive)) + ", count=" + std::to_string(count) + ")");
  if (view_count_ > 60000 || sampler_count_ > 1700 || transient_.size() > 5000) Flush(kFlushCapacity);
  auto program = r.Get<reg::SQ_PROGRAM_CNTL>();
  uint32_t param = kMaxInterpolators;
  auto mask = ps ? vs.writes_interpolators() & ps->GetInterpolatorInputMask(program, r.Get<reg::SQ_CONTEXT_MISC>(), param) : 0;
  T::Modification vm(shaders.translator.GetDefaultVertexShaderModification(vs.GetDynamicAddressableRegisterCount(program.vs_num_reg)));
  T::Modification pm(shaders.translator.GetDefaultPixelShaderModification(ps ? ps->GetDynamicAddressableRegisterCount(program.ps_num_reg) : 0));
  vm.vertex.interpolator_mask = pm.pixel.interpolator_mask = mask;
  pm.pixel.interpolators_centroid = mask & ~GetInterpolatorSamplingPattern(msaa, r.Get<reg::SQ_CONTEXT_MISC>().sc_sample_cntl, r.Get<reg::SQ_INTERPOLATOR_CNTL>().sampling_pattern);
  auto clip = r.Get<reg::PA_CL_CLIP_CNTL>();
  unsigned planes = clip.clip_disable ? 0 : clip.ucp_ena;
  vm.vertex.user_clip_plane_count = std::popcount(planes);
  vm.vertex.user_clip_plane_cull = planes && clip.ucp_cull_only_ena;
  Require(primitive != PrimitiveType::kRectangleList || !vm.vertex.user_clip_plane_cull, "rectangle cull planes not implemented");
  Require(point || !vs.writes_point_size_edge_flag_kill_vertex(), "point/vertex kill exports not implemented");
  if (point) {
    vm.vertex.point_ps_ucp_mode = clip.ps_ucp_mode;
    vm.vertex.output_point_size = (vs.writes_point_size_edge_flag_kill_vertex() & 1) != 0;
    vm.vertex.vertex_kill_and = (vs.writes_point_size_edge_flag_kill_vertex() & 4) && !clip.vtx_kill_or;
    pm.pixel.param_gen_point = param < kMaxInterpolators;
  }
  if (param < kMaxInterpolators) { pm.pixel.param_gen_enable = 1; pm.pixel.param_gen_interpolator = param; }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = root_.Get();
  {
  Timer shader_timer(stats_enabled_, shader_ns_);
  auto* vt = shaders.Translate(vs, vm.value);
  pd.VS = {vt->translated_binary().data(), vt->translated_binary().size()};
  if (ps) { auto* pt = shaders.Translate(*ps, pm.value); pd.PS = {pt->translated_binary().data(), pt->translated_binary().size()}; }
  else { if (depth_shader_.empty()) depth_shader_ = shaders.translator.CreateDepthOnlyPixelShader(); pd.PS = {depth_shader_.data(), depth_shader_.size()}; }
  if (primitive == PrimitiveType::kRectangleList) { const auto& gs = RectangleShader(std::popcount(mask), std::popcount(planes)); pd.GS = {gs.data(), gs.size()}; }
  if (point) {
    GeometryShaderKey key;
    key.type = PipelineGeometryShader::kPointList;
    key.interpolator_count = std::popcount(mask);
    key.user_clip_plane_count = vm.vertex.user_clip_plane_count;
    key.user_clip_plane_cull = vm.vertex.user_clip_plane_cull;
    key.has_vertex_kill_and = vm.vertex.vertex_kill_and;
    key.has_point_size = vm.vertex.output_point_size;
    key.has_point_coordinates = pm.pixel.param_gen_point;
    key.point_ps_ucp_mode = vm.vertex.point_ps_ucp_mode;
    auto& gs = points_[key.key];
    if (gs.empty()) CreateDxbcGeometryShader(key, gs);
    pd.GS = {gs.data(), gs.size() * sizeof(uint32_t)};
  }
  }
  pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 1;
  pd.PrimitiveTopologyType = point ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  auto mode = r.Get<reg::PA_SU_SC_MODE_CNTL>();
  Require(!mode.persp_corr_dis, "non-perspective interpolation not implemented");
  // Restart is applied to CPU-normalized indices below.
  Require(mode.poly_mode != PolygonModeEnable::kDualMode || (mode.polymode_front_ptype == PolygonType::kTriangles && mode.polymode_back_ptype == PolygonType::kTriangles), "non-solid polygon fill not implemented");
  // Polygon offset is applied by the translated ROV depth test (constants below).
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pd.RasterizerState.CullMode = !polygon ? D3D12_CULL_MODE_NONE : mode.cull_front ? D3D12_CULL_MODE_FRONT : mode.cull_back ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
  pd.RasterizerState.FrontCounterClockwise = !mode.face;
  pd.RasterizerState.DepthClipEnable = !clip.clip_disable;
  pd.RasterizerState.ForcedSampleCount = msaa == MsaaSamples::k1X ? 1 : 4;
  // Pointer identity is stable in the shader cache. The zeroed descriptor also includes every pipeline state.
  std::string key(reinterpret_cast<const char*>(&pd), sizeof(pd));
  auto& pipeline = pipelines_[key];
  if (!pipeline) {
    auto disk = pipeline_store_.Load(pd);
    static const auto pso_dump = REXCVAR_GET(native_pso_dump);
    if (!pso_dump.empty()) {
      const auto dir = std::filesystem::path(pso_dump);
      std::filesystem::create_directories(dir);
      auto save = [&](const char* name, const void* bytes, size_t size) {
        std::ofstream out(dir / name, std::ios::binary);
        out.write(static_cast<const char*>(bytes), size);
        Require(bool(out), "PSO diagnostic write failed");
      };
      auto saved = pd; saved.pRootSignature = nullptr;
      saved.VS.pShaderBytecode = saved.PS.pShaderBytecode = saved.GS.pShaderBytecode = nullptr;
      save("pso.bin", &saved, sizeof(saved));
      save("vs.dxbc", pd.VS.pShaderBytecode, pd.VS.BytecodeLength);
      save("ps.dxbc", pd.PS.pShaderBytecode, pd.PS.BytecodeLength);
      save("gs.dxbc", pd.GS.pShaderBytecode, pd.GS.BytecodeLength);
    }
    const auto start = stats_enabled_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto thread_start = stats_enabled_ ? CpuNs(false) : 0;
    const auto process_start = stats_enabled_ ? CpuNs(true) : 0;
    if (stats_enabled_) REXLOG_INFO("Native PSO begin id={} VS={:016X} PS={:016X} vm={:016X} pm={:016X} primitive={} msaa={}",
        pipelines_.size(), vs.ucode_data_hash(), ps ? ps->ucode_data_hash() : 0, vm.value, pm.value, uint32_t(primitive), uint32_t(msaa));
    pd.CachedPSO = {disk.blob.data(), disk.blob.size()};
    auto hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline));
    if (!disk.blob.empty()) {
      if (SUCCEEDED(hr)) ++pipeline_disk_hits_;
      else {
        ++pipeline_disk_rejects_; pipeline.Reset(); pd.CachedPSO = {};
        // Opaque blobs may become invalid after a driver or adapter change.
        hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline));
      }
    }
    if (SUCCEEDED(hr) && (disk.blob.empty() || !pd.CachedPSO.CachedBlobSizeInBytes)) pipeline_store_.Save(disk, pipeline.Get());
    if (stats_enabled_) {
      auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
      auto thread = CpuNs(false) - thread_start, process = CpuNs(true) - process_start;
      pipeline_ns_ += wall; pipeline_thread_ns_ += thread; pipeline_process_ns_ += process; ++pipeline_creations_;
      REXLOG_INFO("Native PSO end id={} wall_ms={} thread_cpu_ms={} process_cpu_ms={} HRESULT={:08X}",
          pipelines_.size(), wall / 1000000, thread / 1000000, process / 1000000, uint32_t(hr));
    }
    if (FAILED(hr)) {
      REXLOG_ERROR("Native PSO HRESULT={:08X} VS={:016X} PS={:016X} primitive={} MSAA={} clip={} interpolators={:X}",
          uint32_t(hr), vs.ucode_data_hash(), ps ? ps->ucode_data_hash() : 0,
          uint32_t(primitive), uint32_t(msaa), planes, mask);
      if (!REXCVAR_GET(dump_shaders).empty()) {
        const char* names[]{"failed-vs", "failed-ps", "failed-gs"};
        D3D12_SHADER_BYTECODE stages[]{pd.VS, pd.PS, pd.GS};
        for (unsigned i = 0; i < 3; ++i) if (stages[i].BytecodeLength) {
          auto path = std::filesystem::path(REXCVAR_GET(dump_shaders)) / names[i];
          std::ofstream binary(path.string() + ".dxbc", std::ios::binary);
          binary.write(static_cast<const char*>(stages[i].pShaderBytecode), stages[i].BytecodeLength);
          Ptr<ID3DBlob> disassembly;
          if (SUCCEEDED(D3DDisassemble(stages[i].pShaderBytecode, stages[i].BytecodeLength, 0, nullptr, &disassembly))) {
            std::ofstream text(path.string() + ".txt");
            text.write(static_cast<const char*>(disassembly->GetBufferPointer()), disassembly->GetBufferSize());
          }
        }
      }
      throw std::runtime_error("Native draw: graphics pipeline creation failed HRESULT=" + std::to_string(uint32_t(hr)));
    }
  }
  // Ranges this draw reads on the CPU (indices, textures converted on the
  // CPU) must hold earlier resolve results before anything is recorded (a
  // later flush would orphan state). Vertices and GPU-loaded textures read
  // the resident mirror, where resolve results already are.
  if (!writeback_.empty()) {
    if (indices) SyncGuestRead(indices->guest_base, indices->length, kReadIndices);
    for (auto* shader : {&vs, ps}) if (shader)
      for (const auto& t : shader->GetTextureBindingsAfterTranslation()) {
        auto f = r.GetTextureFetch(t.fetch_constant);
        if (!TextureViewActive(f, t.dimension, t.is_signed) || (!texture_cpu_ && !NeedsCpuConversion(f.format, t.is_signed, f.num_format))) continue;
        uint32_t wm, hm, dm, base, mip, lo, hi;
        texture_util::GetSubresourcesFromFetchConstant(f, &wm, &hm, &dm, &base, &mip, &lo, &hi);
        // Same extents BindTextures reads (all levels, conservatively).
        auto layout = texture_util::GetGuestTextureLayout(f.dimension, f.pitch, wm + 1, hm + 1, dm + 1, f.tiled, f.format, f.packed_mips, true, hi);
        if (base) SyncGuestRead(base << 12, (layout.base.level_data_extent_bytes + 3u) & ~3u, kReadCpuTexture);
        if (mip && hi) SyncGuestRead(mip << 12, (layout.mips_total_extent_bytes + 3u) & ~3u, kReadCpuTexture);
      }
  }
  auto& mirror = Mirror(memory);
  for (const auto& b : vs.vertex_bindings()) {
    auto f = r.GetVertexFetch(b.fetch_constant);
    if (f.type == FetchConstantType::kInvalidVertex) {
      // D3D marks unset streams invalid; guest shaders may reference them
      // only under a predicate (4E1DA281CC3D7EDB: vf5 behind c127.x). The
      // translated fetch reads guest memory itself, as the guest program
      // decides; nothing exists to upload for this constant.
      if (invalid_vertex_fetches_++ < 8)
        REXLOG_WARN("Native: draw references invalid vertex fetch {} ({:08X} {:08X}), VS {:016X}; executed as programmed",
            b.fetch_constant, f.dword_0, f.dword_1, vs.ucode_data_hash());
      continue;
    }
    if (f.type != FetchConstantType::kVertex) {
      char text[160];
      std::snprintf(text, sizeof(text), "Native draw: invalid vertex fetch type %u (constant %u dwords %08X %08X, VS %016llX)",
          uint32_t(f.type), b.fetch_constant, f.dword_0, f.dword_1, (unsigned long long)vs.ucode_data_hash());
      throw std::runtime_error(text);
    }
    // Uploads only pages the CPU wrote since they were last valid.
    mirror.RequestRange(f.address << 2, f.size << 2);
  }
  D3D12_INDEX_BUFFER_VIEW ib{};
  std::vector<IndexRange> index_ranges;
  if (indices) {
    Require(indices->count * (indices->format == IndexFormat::kInt16 ? 2ull : 4ull) <= indices->length, "index count outside buffer");
    Require(uint64_t(indices->guest_base) + indices->length <= 0x20000000 && count <= indices->count, "index physical extent invalid");
  }
  if (indices || fan || quad) {
    if ((fan || quad) && count < 3) return;
    std::vector<uint32_t> expanded;
    expanded.reserve((fan || quad) ? size_t(count) * 3 : count);
    auto index = [&](uint32_t i) {
      if (!indices) return i;
      uint32_t value = 0;
      unsigned bytes = indices->format == IndexFormat::kInt16 ? 2 : 4;
      for (unsigned b = 0; b < bytes; ++b) {
        uint32_t offset = i * bytes + b;
        switch(indices->endianness) {
          case Endian::k8in16: offset ^= 1; break;
          case Endian::k8in32: offset ^= 3; break;
          case Endian::k16in32: offset ^= 2; break;
          default: break;
        }
        Require(offset < indices->length, "fan index extent overflow");
        value |= uint32_t(*memory.TranslatePhysical<uint8_t*>(indices->guest_base + offset)) << (b * 8);
      }
      return value;
    };
    for (uint32_t i = 0; i < count; ++i) expanded.push_back(index(i));
    auto converted = ConvertIndices(expanded, primitive, indices && mode.multi_prim_ib_ena,
        r.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx);
    expanded = std::move(converted.values);
    index_ranges = std::move(converted.ranges);
    if (expanded.empty()) return; // No complete primitives between reset markers.
    auto data = Upload(expanded.data(), expanded.size() * 4);
    ib = {data.Address(), UINT(expanded.size() * 4), DXGI_FORMAT_R32_UINT};
    count = uint32_t(expanded.size());
  }
  // Texture loads record uploads and compute work: before the draw state.
  TextureTables pixel_tables, vertex_tables;
  if (ps) pixel_tables = BindTextures(r, memory, *ps);
  vertex_tables = BindTextures(r, memory, vs);
  Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDEX_BUFFER);
  Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); Barrier();
  ID3D12DescriptorHeap* heaps[]{views_.Get(), samplers_.Get()}; list_->SetDescriptorHeaps(2, heaps);
  list_->SetGraphicsRootSignature(root_.Get()); list_->SetPipelineState(pipeline.Get());
  unsigned base = Views(3);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 0x20000000 / 4; srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device_->CreateShaderResourceView(memory_.Get(), &srv, Cpu(base));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 0x20000000 / 4; uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  device_->CreateUnorderedAccessView(nullptr, nullptr, &uav, Cpu(base + 1));
  uav.Format = DXGI_FORMAT_R32_UINT; uav.Buffer.NumElements = kEdramSizeBytes / 4; uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  device_->CreateUnorderedAccessView(edram_.Get(), nullptr, &uav, Cpu(base + 2));
  list_->SetGraphicsRootDescriptorTable(5, Gpu(base));
  auto depth = draw_util::GetNormalizedDepthControl(r);
  draw_util::ViewportInfo viewport;
  draw_util::GetHostViewportInfo(r, 1, 1, true, D3D12_VIEWPORT_BOUNDS_MAX, D3D12_VIEWPORT_BOUNDS_MAX, false, depth, false, false, ps && ps->writes_depth(), viewport);
  D3D12_VIEWPORT vp{float(viewport.xy_offset[0]), float(viewport.xy_offset[1]), float(viewport.xy_extent[0]), float(viewport.xy_extent[1]), viewport.z_min, viewport.z_max};
  list_->RSSetViewports(1, &vp);
  draw_util::Scissor sc; draw_util::GetScissor(r, sc);
  D3D12_RECT rect{LONG(sc.offset[0]), LONG(sc.offset[1]), LONG(sc.offset[0] + sc.extent[0]), LONG(sc.offset[1] + sc.extent[1])};
  list_->RSSetScissorRects(1, &rect);
  T::SystemConstants c{};
  if (point) SetPointConstants(r, viewport, c);
  for (unsigned i = 0, packed = 0; i < 6; ++i) if (planes & (1u << i)) {
    std::memcpy(c.user_clip_planes[packed++], &r[XE_GPU_REG_PA_CL_UCP_0_X + i * 4], 16);
  }
  for (unsigned i = 0; i < 32; ++i)
    c.texture_swizzled_signs[i / 4] |= uint32_t(texture_util::SwizzleSigns(r.GetTextureFetch(i))) << ((i % 4) * 8);
  auto vte = r.Get<reg::PA_CL_VTE_CNTL>(); auto color = r.Get<reg::RB_COLORCONTROL>();
  c.flags = (vte.vtx_xy_fmt ? T::kSysFlag_XYDividedByW : 0) | (vte.vtx_z_fmt ? T::kSysFlag_ZDividedByW : 0) |
      (vte.vtx_w0_fmt ? T::kSysFlag_WNotReciprocal : 0) | (polygon ? T::kSysFlag_PrimitivePolygonal : 0);
  auto alpha = color.alpha_test_enable ? color.alpha_func : CompareFunction::kAlways;
  c.flags |= uint32_t(alpha) << T::kSysFlag_AlphaPassIfLess_Shift;
  c.alpha_test_reference = r.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  c.alpha_to_mask = color.alpha_to_mask_enable ? (color.value >> 24) | 256 : 0;
  c.vertex_index_endian = Endian::kNone; // CPU-normalized, including 16-bit index pair swaps.
  c.vertex_index_offset = r.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  c.vertex_index_min = r.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  c.vertex_index_max = r.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  std::memcpy(c.ndc_scale, viewport.ndc_scale, sizeof(c.ndc_scale));
  std::memcpy(c.ndc_offset, viewport.ndc_offset, sizeof(c.ndc_offset));
  c.sample_count_log2[0] = msaa >= MsaaSamples::k4X;
  c.sample_count_log2[1] = msaa >= MsaaSamples::k2X;
  c.edram_32bpp_tile_pitch_dwords_scaled = (((r.Get<reg::RB_SURFACE_INFO>().surface_pitch << c.sample_count_log2[0]) + 79) / 80) * 1280;
  const auto color_mask = draw_util::GetNormalizedColorMask(r, ps ? ps->writes_color_targets() : 0);
  for (unsigned i = 0; i < 4; ++i) {
    auto info = r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    c.color_exp_bias[i] = std::ldexp(1.0f, info.color_exp_bias);
    c.edram_rt_base_dwords_scaled[i] = info.color_base * 1280;
    c.edram_rt_format_flags[i] = RenderTargetCache::AddPSIColorFormatFlags(info.color_format);
    RenderTargetCache::GetPSIColorFormatInfo(info.color_format, (color_mask >> (i * 4)) & 15,
        c.edram_rt_clamp[i][0], c.edram_rt_clamp[i][1], c.edram_rt_clamp[i][2], c.edram_rt_clamp[i][3], c.edram_rt_keep_mask[i][0], c.edram_rt_keep_mask[i][1]);
    c.edram_rt_blend_factors_ops[i] = r[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1fff1fff;
    c.edram_blend_constant[i] = r.Get<float>(XE_GPU_REG_RB_BLEND_RED + i);
    if (info.color_format == ColorRenderTargetFormat::k_8_8_8_8_GAMMA) c.flags |= T::kSysFlag_ConvertColor0ToGamma << i;
  }
  auto di = r.Get<reg::RB_DEPTH_INFO>(); c.edram_depth_base_dwords_scaled = di.depth_base * 1280;
  if (di.depth_format == DepthRenderTargetFormat::kD24FS8) c.flags |= T::kSysFlag_DepthFloat24;
  // As in the pinned ROV path: depth/stencil aliasing a written color target
  // is disabled rather than corrupting that target.
  bool depth_stencil = depth.z_enable || depth.stencil_enable;
  for (unsigned i = 0; i < 4 && depth_stencil; ++i)
    if (di.depth_base == r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]).color_base &&
        (c.edram_rt_keep_mask[i][0] != UINT32_MAX || c.edram_rt_keep_mask[i][1] != UINT32_MAX)) depth_stencil = false;
  // Front offset applies to non-polygons only with POLY_OFFSET_PARA_ENABLE.
  const bool para = !polygon && mode.poly_offset_para_enable;
  if ((polygon && mode.poly_offset_front_enable) || para) {
    c.edram_poly_offset_front_scale = r.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE) * kPolygonOffsetScaleSubpixelUnit;
    c.edram_poly_offset_front_offset = r.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
  }
  if (polygon && mode.poly_offset_back_enable) {
    c.edram_poly_offset_back_scale = r.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE) * kPolygonOffsetScaleSubpixelUnit;
    c.edram_poly_offset_back_offset = r.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
  } else if (para) {
    c.edram_poly_offset_back_scale = c.edram_poly_offset_front_scale;
    c.edram_poly_offset_back_offset = c.edram_poly_offset_front_offset;
  }
  if (depth_stencil) {
    c.flags |= T::kSysFlag_ROVDepthStencil;
    c.flags |= uint32_t(depth.z_enable ? depth.zfunc : CompareFunction::kAlways) << T::kSysFlag_ROVDepthPassIfLess_Shift;
    if (depth.z_enable && depth.z_write_enable) c.flags |= T::kSysFlag_ROVDepthWrite;
    if (depth.stencil_enable) c.flags |= T::kSysFlag_ROVStencilTest;
    for (unsigned side = 0; side < 2; ++side) {
      const bool back = side && polygon && depth.backface_enable;
      auto stencil = r.Get<reg::RB_STENCILREFMASK>(back ? XE_GPU_REG_RB_STENCILREFMASK_BF : XE_GPU_REG_RB_STENCILREFMASK);
      c.edram_stencil[side][0] = stencil.stencilref; c.edram_stencil[side][1] = stencil.stencilmask;
      c.edram_stencil[side][2] = stencil.stencilwritemask;
      c.edram_stencil[side][3] = (depth.value >> (back ? 20 : 8)) & 4095;
    }
  }
  list_->SetGraphicsRootConstantBufferView(0, Constants(&c, sizeof(c)));
  auto floats = [&](Shader& shader, unsigned start, unsigned root) {
    uint32_t packed[1024]{}; unsigned n = 0;
    const auto& map = shader.constant_register_map();
    for (unsigned i = 0; i < 256; ++i) if ((map.float_bitmap[i / 64] >> (i % 64)) & 1) {
      std::memcpy(packed + n, &r[start + i * 4], 16); n += 4;
    }
    list_->SetGraphicsRootConstantBufferView(root, Constants(packed, std::max(n * 4, 16u)));
  };
  floats(vs, XE_GPU_REG_SHADER_CONSTANT_000_X, 1); if (ps) floats(*ps, XE_GPU_REG_SHADER_CONSTANT_256_X, 2);
  list_->SetGraphicsRootConstantBufferView(3, Constants(&r[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031], 40 * 4));
  list_->SetGraphicsRootConstantBufferView(4, Constants(&r[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], 192 * 4));
  SetTextureTables(pixel_tables, 6); SetTextureTables(vertex_tables, 8);
  list_->SetGraphicsRootDescriptorTable(7, Gpu(0, true));
  list_->SetGraphicsRootDescriptorTable(9, Gpu(0));
  list_->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
  list_->IASetPrimitiveTopology(point ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : primitive == PrimitiveType::kTriangleStrip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (indices || fan || quad) { list_->IASetIndexBuffer(&ib); for (const auto& range : index_ranges) list_->DrawIndexedInstanced(range.count, 1, range.offset, 0, 0); }
  else list_->DrawInstanced(count, 1, 0, 0);
  ++draw_count_;
}
}
