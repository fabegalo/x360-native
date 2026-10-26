#include "vulkan_render.h"
#include "vulkan_tessellation.h"
#include "vulkan_polygon_points.h"
#include "../commands/primitive_indices.h"
#include "../commands/memexport.h"
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/flags.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using T = SpirvShaderTranslator;
namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(std::string("Native Vulkan draw: ") + message);
}
}
Shader* VulkanRender::LoadShader(ShaderType type, const uint32_t* data, uint32_t count) {
  if (!shaders_) shaders_ = std::make_unique<VulkanShaderCache>(device_, true, scale_x_, scale_y_);
  return shaders_->Load(type, data, count);
}
void VulkanRender::DestroyDraw() {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  for (const auto& [key, pipeline] : draw_pipelines_) {
    if (pipeline.pipeline) fn.vkDestroyPipeline(vk, pipeline.pipeline, nullptr);
    if (pipeline.layout) fn.vkDestroyPipelineLayout(vk, pipeline.layout, nullptr);
  }
  draw_pipelines_.clear();
  for (const auto& [key, module] : tessellation_modules_) fn.vkDestroyShaderModule(vk, module, nullptr);
  tessellation_modules_.clear();
  if (draw_framebuffer_) fn.vkDestroyFramebuffer(vk, draw_framebuffer_, nullptr);
  if (draw_pass_) fn.vkDestroyRenderPass(vk, draw_pass_, nullptr);
  if (memory_layout_) fn.vkDestroyDescriptorSetLayout(vk, memory_layout_, nullptr);
  if (constants_layout_) fn.vkDestroyDescriptorSetLayout(vk, constants_layout_, nullptr);
  for (auto module : depth_only_) if (module) fn.vkDestroyShaderModule(vk, module, nullptr);
  draw_framebuffer_ = VK_NULL_HANDLE;
  draw_pass_ = VK_NULL_HANDLE;
  memory_layout_ = constants_layout_ = VK_NULL_HANDLE;
  depth_only_.fill(VK_NULL_HANDLE);
  shaders_.reset();
  memexport_addresses_known_.clear();
}
void VulkanRender::InitializeDraw() {
  if (draw_framebuffer_) return;
  if (!shaders_) shaders_ = std::make_unique<VulkanShaderCache>(device_, true, scale_x_, scale_y_);
  Require(device_.properties().imageViewFormatSwizzle, "host texture swizzle support is required");
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  const uint32_t guest_bindings = 1u << shaders_->translator.GetSharedMemoryStorageBufferCountLog2();
  const VkDescriptorSetLayoutBinding memory_bindings[]{
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, guest_bindings, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.bindingCount = 2;
  info.pBindings = memory_bindings;
  CheckVulkan(fn.vkCreateDescriptorSetLayout(vk, &info, nullptr, &memory_layout_), "create draw memory layout");
  VkDescriptorSetLayoutBinding constant_bindings[T::kConstantBufferCount]{};
  for (uint32_t i = 0; i < T::kConstantBufferCount; ++i)
    constant_bindings[i] = {i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  info.bindingCount = T::kConstantBufferCount;
  info.pBindings = constant_bindings;
  CheckVulkan(fn.vkCreateDescriptorSetLayout(vk, &info, nullptr, &constants_layout_), "create draw constant layout");
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  const VkSubpassDependency dependencies[]{
      {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
       VK_DEPENDENCY_BY_REGION_BIT},
      {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
       VK_DEPENDENCY_BY_REGION_BIT}};
  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  pass.subpassCount = 1;
  pass.pSubpasses = &subpass;
  pass.dependencyCount = 2;
  pass.pDependencies = dependencies;
  CheckVulkan(fn.vkCreateRenderPass(vk, &pass, nullptr, &draw_pass_), "create Native FSI render pass");
  draw_extent_ = {std::min(8192u * scale_x_, device_.properties().maxFramebufferWidth),
                  std::min(8192u * scale_y_, device_.properties().maxFramebufferHeight)};
  VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer.renderPass = draw_pass_;
  framebuffer.width = draw_extent_.width;
  framebuffer.height = draw_extent_.height;
  framebuffer.layers = 1;
  CheckVulkan(fn.vkCreateFramebuffer(vk, &framebuffer, nullptr, &draw_framebuffer_), "create Native FSI framebuffer");
  for (unsigned counted = 0; counted < depth_only_.size(); ++counted) {
    const auto depth = shaders_->translator.CreateDepthOnlyFragmentShader(
        T::Modification::DepthStencilMode::kNoModifiers, counted != 0, counted == 2);
    std::vector<uint32_t> words(depth.size() / 4);
    Require(depth.size() % 4 == 0, "invalid depth-only SPIR-V");
    std::memcpy(words.data(), depth.data(), depth.size());
    depth_only_[counted] = CreateNativeShaderModule(device_, words);
  }
}
VulkanRender::DrawPipeline& VulkanRender::Pipeline(SpirvShader* vs, SpirvShader* ps,
    VkShaderModule vertex, VkShaderModule pixel, VkPrimitiveTopology topology, VkSampleCountFlagBits samples,
    VkCullModeFlags cull, VkFrontFace front, bool clamp, bool restart, bool discard, const Tessellation& tess, VkPolygonMode fill, uint32_t point_key) {
  const VkDescriptorSetLayout layouts[]{memory_layout_, constants_layout_, TextureLayout(vs), TextureLayout(ps)};
  const uint64_t values[]{uint64_t(vertex), uint64_t(pixel), uint64_t(layouts[2]), uint64_t(layouts[3]),
      uint64_t(topology), uint64_t(samples), uint64_t(cull), uint64_t(front), uint64_t(clamp), uint64_t(restart), uint64_t(discard), uint64_t(tess.host), uint64_t(tess.mode), uint64_t(tess.control_points), uint64_t(fill), uint64_t(point_key)};
  const std::string key(reinterpret_cast<const char*>(values), sizeof(values));
  if (auto found = draw_pipelines_.find(key); found != draw_pipelines_.end()) return found->second;
  DrawPipeline result;
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 4;
  layout.pSetLayouts = layouts;
  CheckVulkan(fn.vkCreatePipelineLayout(vk, &layout, nullptr, &result.layout), "create Native draw pipeline layout");
  try {
    VkPipelineShaderStageCreateInfo stages[5]{};
    for (auto& stage : stages) {
      stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pixel;
    VkPipelineTessellationStateCreateInfo tessellation{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    if (tess) {
      auto module = [&](uint32_t key, VkShaderStageFlagBits stage, const std::string& source) {
        auto found = tessellation_modules_.find(key);
        if (found != tessellation_modules_.end()) return found->second;
        auto value = CreateNativeShaderModule(device_, CompileNativeGlsl(stage, source));
        tessellation_modules_.emplace(key, value);
        return value;
      };
      stages[3] = stages[1];
      stages[0].module = module(tess.adaptive ? 1 : 0, VK_SHADER_STAGE_VERTEX_BIT,
          tess.adaptive ? GetTessellationAdaptiveVertexShaderGlsl() : GetTessellationIndexedVertexShaderGlsl());
      stages[1].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
      const uint32_t outputs = tess.adaptive || IsPatchIndexedHostVertexShaderType(tess.host) ? 1 : tess.control_points;
      stages[1].module = module(2 + uint32_t(tess.host) * 4 + uint32_t(tess.mode), VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          GetTessellationControlShaderGlsl(tess.host, tess.mode, tess.control_points, outputs));
      stages[2].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
      stages[2].module = vertex;
      tessellation.patchControlPoints = tess.control_points;
    }
    uint32_t pixel_stage = tess ? 3 : 1;
    if (fill == VK_POLYGON_MODE_POINT && !discard) {
      auto& module = tessellation_modules_[0x10000u + point_key];
      if (!module) module = CreateNativeShaderModule(device_, CompileNativeGlsl(VK_SHADER_STAGE_GEOMETRY_BIT, PolygonPointGeometryGlsl(point_key)));
      stages[pixel_stage + 1] = stages[pixel_stage];
      stages[pixel_stage].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
      stages[pixel_stage].module = module;
      ++pixel_stage;
    }
    const VkPipelineVertexInputStateCreateInfo inputs{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = topology;
    assembly.primitiveRestartEnable = restart;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = clamp;
    raster.rasterizerDiscardEnable = discard;
    raster.polygonMode = fill == VK_POLYGON_MODE_POINT ? VK_POLYGON_MODE_FILL : fill;
    raster.cullMode = cull;
    raster.frontFace = front;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = samples;
    const VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = pixel_stage + (discard ? 0 : 1);
    pipeline.pTessellationState = tess ? &tessellation : nullptr;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &inputs;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = result.layout;
    pipeline.renderPass = draw_pass_;
    const bool diagnose = !REXCVAR_GET(dump_shaders).empty();
    if (diagnose) {
      REXLOG_INFO("Native Vulkan PSO begin id={} VS={:016X} PS={:016X} stages={} topology={} samples={} discard={} tess={} fill={}",
          draw_pipelines_.size(), vs ? vs->ucode_data_hash() : 0, ps ? ps->ucode_data_hash() : 0,
          pipeline.stageCount, uint32_t(topology), uint32_t(samples), discard, uint32_t(tess.host), uint32_t(fill));
      for (uint32_t i = 0; i < pipeline.stageCount; ++i)
        REXLOG_INFO("Native Vulkan PSO stage={} module={:016X} entry={}", uint32_t(stages[i].stage), uint64_t(stages[i].module), stages[i].pName);
    }
    CheckVulkan(fn.vkCreateGraphicsPipelines(vk, pipeline_cache_, 1, &pipeline, nullptr, &result.pipeline),
                "create Native draw graphics pipeline");
    if (diagnose) REXLOG_INFO("Native Vulkan PSO end id={} pipeline={:016X}", draw_pipelines_.size(), uint64_t(result.pipeline));
    return draw_pipelines_.emplace(key, result).first->second;
  } catch (...) {
    if (result.pipeline) fn.vkDestroyPipeline(vk, result.pipeline, nullptr);
    fn.vkDestroyPipelineLayout(vk, result.layout, nullptr);
    throw;
  }
}
void VulkanRender::Draw(const RegisterFile& regs, rex::memory::Memory& memory, Shader* vertex, Shader* pixel,
    PrimitiveType primitive, uint32_t count, const DrawIndices* indices, DrawPurpose purpose) {
  Require(vertex, "missing vertex shader");
  auto& vs = *static_cast<SpirvShader*>(vertex);
  auto* ps = static_cast<SpirvShader*>(pixel);
  if (regs.Get<reg::RB_MODECONTROL>().edram_mode == EdramMode::kDepthOnly) ps = nullptr;
  const bool polygon = draw_util::IsPrimitivePolygonal(regs);
  const auto face_mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (polygon && face_mode.poly_mode == PolygonModeEnable::kDualMode &&
      face_mode.polymode_front_ptype != face_mode.polymode_back_ptype &&
      !face_mode.cull_front && !face_mode.cull_back && draw_util::IsRasterizationPotentiallyDone(regs, polygon)) {
    // One host fill mode per pipeline. Both passes retain the original winding;
    // only the first executes guest vertex memory exports.
    auto pass = std::make_unique<RegisterFile>(regs);
    auto mode = face_mode; mode.cull_back = 1; mode.polymode_back_ptype = mode.polymode_front_ptype;
    (*pass)[reg::PA_SU_SC_MODE_CNTL::register_index] = mode.value;
    Draw(*pass, memory, vertex, pixel, primitive, count, indices, purpose);
    mode = face_mode; mode.cull_front = 1; mode.polymode_front_ptype = mode.polymode_back_ptype;
    (*pass)[reg::PA_SU_SC_MODE_CNTL::register_index] = mode.value;
    const auto second_purpose = purpose == DrawPurpose::kGuest || purpose == DrawPurpose::kGuestWithoutVertexExports
        ? DrawPurpose::kGuestWithoutVertexExports : DrawPurpose::kVisibility;
    Draw(*pass, memory, vertex, pixel, primitive, count, indices, second_purpose);
    return;
  }
  const auto fill = polygon && face_mode.poly_mode == PolygonModeEnable::kDualMode
      ? (face_mode.cull_front ? face_mode.polymode_back_ptype : face_mode.polymode_front_ptype)
      : PolygonType::kTriangles;

  bool rasterize = draw_util::IsRasterizationPotentiallyDone(regs, polygon);
  const bool vertex_export = purpose != DrawPurpose::kVisibility && purpose != DrawPurpose::kGuestWithoutVertexExports && vs.memexport_eM_written() != 0;
  if (!count || (!rasterize && !vertex_export)) return;
  if (!rasterize) ps = nullptr;
  const bool pixel_export = ps && ps->memexport_eM_written() != 0;
  Require(!vertex_export || device_.properties().vertexPipelineStoresAndAtomics,
          "host does not support vertex memory export");
  Require(!pixel_export || device_.properties().fragmentStoresAndAtomics,
          "host does not support pixel memory export");
  std::vector<MemExportRange> exports;
  for (auto* shader : {&vs, ps}) if (shader && shader->memexport_eM_written() && (shader != &vs || vertex_export)) {
    auto [entry, inserted] = memexport_addresses_known_.try_emplace(shader, false);
    if (inserted) entry->second = MemExportAddressesKnown(*shader);
    AddMemExportRanges(regs, *shader, entry->second, exports);
  }
  MergeMemExportRanges(exports);
  uint64_t export_bytes = 0;
  for (auto& range : exports) {
    range.length = (range.length + 3) & ~3u;
    Require(uint64_t(range.address) + range.length <= GuestMemoryMirror::kSize,
            "memory export range exceeds physical memory");
    export_bytes += range.length;
  }
  const auto tess = GetTessellation(regs, primitive);
  Require(!tess || device_.properties().tessellationShader, "host tessellation shaders are unavailable");
  const bool point = rasterize && primitive == PrimitiveType::kPointList;
  const bool rectangle = rasterize && primitive == PrimitiveType::kRectangleList;
  const bool expand = point || rectangle;
  const bool line = rasterize && IsLinePrimitive(primitive);
  const bool convert = rasterize && !tess && NeedsPrimitiveConversion(primitive);
  Require(!rasterize || tess || primitive == PrimitiveType::kTriangleList || primitive == PrimitiveType::kTriangleStrip ||
          expand || convert || line || primitive == PrimitiveType::k2DTriStrip, "primitive conversion is not implemented");
  const auto mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  Require(!rasterize || fill != PolygonType::kLines || device_.properties().fillModeNonSolid,
          "host does not support non-solid polygon fill");
  Require(!rasterize || fill != PolygonType::kPoints || device_.properties().geometryShader, "host geometry shaders are required for polygon point fill");
  const auto msaa = rasterize ? regs.Get<reg::RB_SURFACE_INFO>().msaa_samples : MsaaSamples::k1X;
  Require(msaa <= MsaaSamples::k4X, "invalid sample count");
  InitializeDraw();
  const bool native_2x = (device_.properties().framebufferNoAttachmentsSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0;
  const auto samples = msaa == MsaaSamples::k1X ? VK_SAMPLE_COUNT_1_BIT :
      msaa == MsaaSamples::k2X && native_2x ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_4_BIT;
  Require((device_.properties().framebufferNoAttachmentsSampleCounts & samples) != 0, "host MSAA sample count unavailable");
  const auto program = regs.Get<reg::SQ_PROGRAM_CNTL>();
  uint32_t param = kMaxInterpolators;
  const auto mask = ps ? vs.writes_interpolators() & ps->GetInterpolatorInputMask(program, regs.Get<reg::SQ_CONTEXT_MISC>(), param) : 0;
  const auto host_type = tess ? tess.host : point ? Shader::HostVertexShaderType::kPointListAsTriangleStrip :
      rectangle ? Shader::HostVertexShaderType::kRectangleListAsTriangleStrip : Shader::HostVertexShaderType::kVertex;
  T::Modification vm(shaders_->translator.GetDefaultVertexShaderModification(
      vs.GetDynamicAddressableRegisterCount(program.vs_num_reg), host_type));
  if (tess) vm.vertex.tessellation_mode = uint32_t(tess.mode);
  T::Modification pm(shaders_->translator.GetDefaultPixelShaderModification(
      ps ? ps->GetDynamicAddressableRegisterCount(program.ps_num_reg) : 0));
  if (vertex_export && !memexport_addresses_known_.at(&vs)) vm.value |= T::Modification::kMemExportDynamicAddress;
  if (pixel_export && !memexport_addresses_known_.at(ps)) pm.value |= T::Modification::kMemExportDynamicAddress;
  if ((purpose == DrawPurpose::kVisibility || purpose == DrawPurpose::kGuestWithoutVertexExports)) vm.value |= T::Modification::kMemExportDisabled;
  pm.pixel.fsi_sample_counts = counting_samples_;
  if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().persp_corr_dis) pm.value |= T::Modification::kInterpolatorNoPerspective;
  vm.vertex.interpolator_mask = pm.pixel.interpolator_mask = mask;
  pm.pixel.interpolators_centroid = mask & ~GetInterpolatorSamplingPattern(msaa,
      regs.Get<reg::SQ_CONTEXT_MISC>().sc_sample_cntl, regs.Get<reg::SQ_INTERPOLATOR_CNTL>().sampling_pattern);
  const auto clip = regs.Get<reg::PA_CL_CLIP_CNTL>();
  const auto planes = !rasterize || clip.clip_disable ? 0u : uint32_t(clip.ucp_ena);
  vm.vertex.user_clip_plane_count = std::popcount(planes);
  vm.vertex.user_clip_plane_cull = planes && clip.ucp_cull_only_ena;
  Require(!rasterize || !clip.clip_disable || device_.properties().depthClamp, "depth clamp is unavailable");
  if (point) {
    vm.vertex.point_ps_ucp_mode = clip.ps_ucp_mode;
    vm.vertex.output_point_parameters = param < kMaxInterpolators;
    pm.pixel.param_gen_point = param < kMaxInterpolators;
  } else vm.vertex.output_point_parameters = (vs.writes_point_size_edge_flag_kill_vertex() & 1) != 0;
  vm.vertex.vertex_kill_and = (vs.writes_point_size_edge_flag_kill_vertex() & 4) && !clip.vtx_kill_or;
  if (param < kMaxInterpolators) { pm.pixel.param_gen_enable = 1; pm.pixel.param_gen_interpolator = param; }
  const auto translated_vs = shaders_->Translate(vs, vm.value);
  const auto translated_ps = ps ? shaders_->Translate(*ps, pm.value).module : depth_only_[visibility_only_ ? 2 : counting_samples_];

  // CPU reads must complete earlier resolves before any per-draw bindings.
  if (indices) {
    Require(uint64_t(indices->guest_base) + indices->length <= GuestMemoryMirror::kSize && count <= indices->count,
            "index buffer extent is invalid");
    Require(uint64_t(indices->count) * (indices->format == IndexFormat::kInt16 ? 2 : 4) <= indices->length,
            "index count exceeds buffer");
    if (!expand) SyncGuestRead(indices->guest_base, indices->length, 0xFF);
  }
  SyncTextureReads(regs, &vs);
  SyncTextureReads(regs, ps);
  Begin();
  const size_t image_count = vs.GetTextureBindingsAfterTranslation().size() +
      (ps ? ps->GetTextureBindingsAfterTranslation().size() : 0);
  const size_t sampler_count = vs.GetSamplerBindingsAfterTranslation().size() +
      (ps ? ps->GetSamplerBindingsAfterTranslation().size() : 0);
  const auto& arena = arenas_[submissions_.current_slot()];
  if (arenas_[submissions_.current_slot()].descriptor_count > 4000 ||
      arena.image_descriptors + image_count > 32768 || arena.sampler_descriptors + sampler_count > 32768 ||
      readback_used_ + export_bytes > readback_->size()) Flush(false);
  if (samplers_.size() + sampler_count > device_.properties().maxSamplerAllocationCount) {
    Flush(true);
    for (const auto& [key, sampler] : samplers_)
      device_.functions().vkDestroySampler(device_.device(), sampler, nullptr);
    samplers_.clear();
  }
  ReserveReadback(export_bytes);
  auto& mirror = Mirror(memory);
  for (const auto& range : exports) mirror.RequestRange(range.address, range.length);
  for (const auto& binding : vs.vertex_bindings()) {
    const auto fetch = regs.GetVertexFetch(binding.fetch_constant);
    // Invalid/predicated streams are executed by the translated shader. They
    // do not describe a valid memory upload, as in Native D3D12.
    if (fetch.type == FetchConstantType::kInvalidVertex) continue;
    Require(fetch.type == FetchConstantType::kVertex, "invalid vertex fetch type");
    mirror.RequestRange(fetch.address << 2, fetch.size << 2);
  }
  if (expand && indices) mirror.RequestRange(indices->guest_base, indices->length);

  std::vector<uint32_t> normalized;
  std::vector<IndexRange> ranges;
  VkPrimitiveTopology topology = primitive == PrimitiveType::kTriangleStrip || primitive == PrimitiveType::k2DTriStrip || expand
      ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  if (line) topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  if (!rasterize) topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  if (tess) topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
  if (expand) {
    const uint32_t primitive_count = rectangle ? count / 3 : count;
    Require(primitive_count <= UINT32_MAX / 5, "expanded index count overflow");
    normalized.reserve(size_t(primitive_count) * 5);
    for (uint32_t i = 0; i < primitive_count; ++i) {
      for (uint32_t corner = 0; corner < 4; ++corner) normalized.push_back(i * 4 + corner);
      if (i + 1 < primitive_count) normalized.push_back(UINT32_MAX);
    }
    if (normalized.empty()) return;
    ranges.push_back({0, uint32_t(normalized.size())});
  } else if (indices || convert) {
    normalized.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t value = i;
      if (indices) {
        value = 0;
        const unsigned bytes = indices->format == IndexFormat::kInt16 ? 2 : 4;
        for (unsigned b = 0; b < bytes; ++b) {
          uint32_t offset = i * bytes + b;
          if (indices->endianness == Endian::k8in16) offset ^= 1;
          if (indices->endianness == Endian::k8in32) offset ^= 3;
          if (indices->endianness == Endian::k16in32) offset ^= 2;
          Require(offset < indices->length, "index endianness crosses buffer extent");
          value |= uint32_t(*memory.TranslatePhysical<const uint8_t*>(indices->guest_base + offset)) << (b * 8);
        }
      }
      normalized.push_back(value);
    }
    // Even without rasterization, strip/fan restart tokens do not denote
    // guest vertices. Keep the surviving indices once for memory exports.
    const bool restart = indices && mode.multi_prim_ib_ena;
    const uint32_t reset = regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
    auto converted = tess ? VertexOnlyIndices(normalized, primitive, false, 0) : rasterize ? ConvertIndices(normalized, primitive, restart, reset)
                               : VertexOnlyIndices(normalized, primitive, restart, reset);
    normalized = std::move(converted.values);
    ranges = std::move(converted.ranges);
    if (normalized.empty()) return;
  }
  for (const auto index : normalized)
    Require(device_.properties().fullDrawIndexUint32 || index <= 0xFFFFFF || (expand && index == UINT32_MAX),
            "32-bit vertex index unsupported by host");
  UploadSlice index_buffer{};
  if (!normalized.empty()) index_buffer = Upload(normalized.data(), normalized.size() * sizeof(uint32_t), 4);
  textures_resolution_scaled_ = 0;
  const auto vertex_textures = BindTextures(regs, memory, &vs);
  const auto pixel_textures = BindTextures(regs, memory, ps);
  const auto depth = draw_util::GetNormalizedDepthControl(regs);
  draw_util::ViewportInfo viewport;
  draw_util::GetHostViewportInfo(regs, scale_x_, scale_y_, false, device_.properties().maxViewportDimensions[0],
      device_.properties().maxViewportDimensions[1], true, depth, false, false, ps && ps->writes_depth(), viewport);
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  scissor.offset[0] *= scale_x_; scissor.extent[0] *= scale_x_;
  scissor.offset[1] *= scale_y_; scissor.extent[1] *= scale_y_;
  if (!viewport.xy_extent[0] || !viewport.xy_extent[1] || !scissor.extent[0] || !scissor.extent[1]) {
    if (!vertex_export) return;
    rasterize = false;
  }
  T::SystemConstants constants{};
  constants.textures_resolution_scaled = textures_resolution_scaled_;
  constants.tessellation_factor_range_min = regs.Get<float>(XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL) + 1.0f;
  constants.tessellation_factor_range_max = regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL) + 1.0f;
  for (unsigned i = 0, packed = 0; i < 6; ++i) if (planes & (1u << i))
    std::memcpy(constants.user_clip_planes[packed++], &regs[XE_GPU_REG_PA_CL_UCP_0_X + i * 4], 16);
  const auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
  const auto color = regs.Get<reg::RB_COLORCONTROL>();
  constants.flags = (vte.vtx_xy_fmt ? T::kSysFlag_XYDividedByW : 0) |
      (vte.vtx_z_fmt ? T::kSysFlag_ZDividedByW : 0) | (vte.vtx_w0_fmt ? T::kSysFlag_WNotReciprocal : 0) |
      (polygon ? T::kSysFlag_PrimitivePolygonal : 0) |
      (draw_util::IsPrimitiveLine(regs) ? T::kSysFlag_PrimitiveLine : 0) | (uint32_t(msaa) << T::kSysFlag_MsaaSamples_Shift);
  constants.flags |= uint32_t(color.alpha_test_enable ? color.alpha_func : CompareFunction::kAlways) << T::kSysFlag_AlphaPassIfLess_Shift;
  constants.alpha_test_reference = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  constants.alpha_to_mask = color.alpha_to_mask_enable ? (color.value >> 24) | 256 : 0;
  constants.vertex_base_index = regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);
  constants.vertex_index_min = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  constants.vertex_index_max = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  if (expand && indices) {
    constants.flags |= T::kSysFlag_ComputeOrPrimitiveVertexIndexLoad;
    if (indices->format == IndexFormat::kInt32) constants.flags |= T::kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit;
    constants.vertex_index_load_address = indices->guest_base;
    constants.vertex_index_endian = indices->endianness;
    if (indices->format == IndexFormat::kInt16 && indices->endianness != Endian::kNone && indices->endianness != Endian::k8in16)
      constants.vertex_index_endian = indices->endianness == Endian::k8in32 ? Endian::k8in16 : Endian::kNone;
  }
  std::memcpy(constants.ndc_scale, viewport.ndc_scale, sizeof(constants.ndc_scale));
  std::memcpy(constants.ndc_offset, viewport.ndc_offset, sizeof(constants.ndc_offset));
  if (point || fill == PolygonType::kPoints) {
    const auto minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    const auto size = regs.Get<reg::PA_SU_POINT_SIZE>();
    constants.point_vertex_diameter_min = float(minmax.min_size) * (2.0f / 16.0f);
    constants.point_vertex_diameter_max = float(minmax.max_size) * (2.0f / 16.0f);
    constants.point_constant_diameter[0] = float(size.width) * (2.0f / 16.0f);
    constants.point_constant_diameter[1] = float(size.height) * (2.0f / 16.0f);
    constants.point_screen_diameter_to_ndc_radius[0] = float(scale_x_) / std::max(1u, viewport.xy_extent[0]);
    constants.point_screen_diameter_to_ndc_radius[1] = float(scale_y_) / std::max(1u, viewport.xy_extent[1]);
  }
  for (unsigned i = 0; i < 32; ++i)
    constants.texture_swizzled_signs[i / 4] |= uint32_t(texture_util::SwizzleSigns(regs.GetTextureFetch(i))) << ((i % 4) * 8);
  constants.edram_32bpp_tile_pitch_dwords_scaled =
      ((regs.Get<reg::RB_SURFACE_INFO>().surface_pitch * (msaa >= MsaaSamples::k4X ? 2 : 1) + 79) / 80) * 1280 * scale_x_ * scale_y_;
  const auto color_mask = draw_util::GetNormalizedColorMask(regs, ps ? ps->writes_color_targets() : 0);
  for (unsigned i = 0; i < 4; ++i) {
    const auto info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    constants.color_exp_bias[i] = std::ldexp(1.0f, info.color_exp_bias);
    constants.edram_rt_base_dwords_scaled[i] = info.color_base * 1280 * scale_x_ * scale_y_;
    constants.edram_rt_format_flags[i] = RenderTargetCache::AddPSIColorFormatFlags(info.color_format);
    RenderTargetCache::GetPSIColorFormatInfo(info.color_format, (color_mask >> (i * 4)) & 15,
        constants.edram_rt_clamp[i][0], constants.edram_rt_clamp[i][1], constants.edram_rt_clamp[i][2],
        constants.edram_rt_clamp[i][3], constants.edram_rt_keep_mask[i][0], constants.edram_rt_keep_mask[i][1]);
    constants.edram_rt_blend_factors_ops[i] = regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF;
    constants.edram_blend_constant[i] = regs.Get<float>(XE_GPU_REG_RB_BLEND_RED + i);
    if (info.color_format == ColorRenderTargetFormat::k_8_8_8_8_GAMMA) constants.flags |= T::kSysFlag_ConvertColor0ToGamma << i;
  }
  const auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  constants.edram_depth_base_dwords_scaled = depth_info.depth_base * 1280 * scale_x_ * scale_y_;
  if (depth_info.depth_format == DepthRenderTargetFormat::kD24FS8) constants.flags |= T::kSysFlag_DepthFloat24;
  bool depth_stencil = depth.z_enable || depth.stencil_enable;
  for (unsigned i = 0; i < 4 && depth_stencil; ++i)
    if (depth_info.depth_base == regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]).color_base &&
        (constants.edram_rt_keep_mask[i][0] != UINT32_MAX || constants.edram_rt_keep_mask[i][1] != UINT32_MAX)) depth_stencil = false;
  const bool para = !polygon && mode.poly_offset_para_enable;
  if ((polygon && mode.poly_offset_front_enable) || para) {
    constants.edram_poly_offset_front_scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE) * kPolygonOffsetScaleSubpixelUnit * float(std::max(scale_x_, scale_y_));
    constants.edram_poly_offset_front_offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
  }
  if (polygon && mode.poly_offset_back_enable) {
    constants.edram_poly_offset_back_scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE) * kPolygonOffsetScaleSubpixelUnit * float(std::max(scale_x_, scale_y_));
    constants.edram_poly_offset_back_offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
  } else if (para) {
    constants.edram_poly_offset_back_scale = constants.edram_poly_offset_front_scale;
    constants.edram_poly_offset_back_offset = constants.edram_poly_offset_front_offset;
  }
  if (depth_stencil) {
    constants.flags |= T::kSysFlag_FSIDepthStencil;
    constants.flags |= uint32_t(depth.z_enable ? depth.zfunc : CompareFunction::kAlways) << T::kSysFlag_FSIDepthPassIfLess_Shift;
    if (depth.z_enable && depth.z_write_enable) constants.flags |= T::kSysFlag_FSIDepthWrite;
    if (depth.stencil_enable) constants.flags |= T::kSysFlag_FSIStencilTest;
    for (unsigned side = 0; side < 2; ++side) {
      const bool back = side && polygon && depth.backface_enable;
      const uint32_t reference = regs.Get<reg::RB_STENCILREFMASK>(back ? XE_GPU_REG_RB_STENCILREFMASK_BF : XE_GPU_REG_RB_STENCILREFMASK).value & 0xFFFFFF;
      const uint32_t operations = (depth.value >> (back ? 20 : 8)) & 4095;
      if (side) { constants.edram_stencil_back[0] = reference; constants.edram_stencil_back[1] = operations; }
      else { constants.edram_stencil_front[0] = reference; constants.edram_stencil_front[1] = operations; }
    }
  }
  std::array<uint32_t, 22> edram_layout{uint32_t(msaa), 0, constants.edram_32bpp_tile_pitch_dwords_scaled,
      constants.edram_depth_base_dwords_scaled, uint32_t(depth_stencil), constants.flags & T::kSysFlag_DepthFloat24};
  for (unsigned i = 0; i < 4; ++i) {
    edram_layout[6 + i * 4] = constants.edram_rt_base_dwords_scaled[i];
    edram_layout[7 + i * 4] = constants.edram_rt_format_flags[i];
    edram_layout[8 + i * 4] = constants.edram_rt_keep_mask[i][0];
    edram_layout[9 + i * 4] = constants.edram_rt_keep_mask[i][1];
  }
  UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
             VK_ACCESS_SHADER_READ_BIT | (vertex_export || pixel_export ? VK_ACCESS_SHADER_WRITE_BIT : 0));
  if (!edram_draw_layout_ || *edram_draw_layout_ != edram_layout)
    UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, true);
  edram_draw_layout_ = edram_layout;
  const auto memory_set = AllocateDescriptors(memory_layout_);
  const uint32_t chunks = 1u << shaders_->translator.GetSharedMemoryStorageBufferCountLog2();
  std::vector<VkDescriptorBufferInfo> guest_infos(chunks);
  const uint64_t chunk_size = GuestMemoryMirror::kSize / chunks;
  for (uint32_t i = 0; i < chunks; ++i) guest_infos[i] = {guest_buffer_->handle(), chunk_size * i, chunk_size};
  const VkDescriptorBufferInfo edram_info{edram_buffer_->handle(), 0, edram_buffer_->size()};
  VkWriteDescriptorSet memory_writes[2]{};
  for (auto& write : memory_writes) {
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = memory_set;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  memory_writes[0].descriptorCount = chunks;
  memory_writes[0].pBufferInfo = guest_infos.data();
  memory_writes[1].dstBinding = 1;
  memory_writes[1].descriptorCount = 1;
  memory_writes[1].pBufferInfo = &edram_info;
  const auto& fn = device_.functions();
  fn.vkUpdateDescriptorSets(device_.device(), 2, memory_writes, 0, nullptr);
  const auto constants_set = AllocateDescriptors(constants_layout_);
  VkDescriptorBufferInfo constant_infos[T::kConstantBufferCount]{};
  auto constant = [&](unsigned index, const void* data, size_t bytes) {
    const auto upload = Upload(data, bytes, device_.properties().minUniformBufferOffsetAlignment);
    constant_infos[index] = {upload.buffer, upload.offset, bytes};
  };
  constant(T::kConstantBufferSystem, &constants, sizeof(constants));
  auto floats = [&](SpirvShader* shader, unsigned start, unsigned slot) {
    uint32_t packed[1024]{};
    unsigned n = 0;
    if (shader) for (unsigned i = 0; i < 256; ++i)
      if ((shader->constant_register_map().float_bitmap[i / 64] >> (i % 64)) & 1) {
        std::memcpy(packed + n, &regs[start + i * 4], 16);
        n += 4;
      }
    constant(slot, packed, std::max(n * 4, 16u));
  };
  const uint32_t vs_constants = FloatConstantBase(regs, true), ps_constants = FloatConstantBase(regs, false);
  Require(vs_constants + 256 <= 512 && (!ps || ps_constants + 256 <= 512), "float constant bank outside register file");
  floats(&vs, XE_GPU_REG_SHADER_CONSTANT_000_X + vs_constants * 4, T::kConstantBufferFloatVertex);
  floats(ps, XE_GPU_REG_SHADER_CONSTANT_000_X + ps_constants * 4, T::kConstantBufferFloatPixel);
  constant(T::kConstantBufferBoolLoop, &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031], 40 * 4);
  constant(T::kConstantBufferFetch, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], 192 * 4);
  VkWriteDescriptorSet constant_writes[T::kConstantBufferCount]{};
  for (unsigned i = 0; i < T::kConstantBufferCount; ++i) {
    auto& write = constant_writes[i];
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = constants_set;
    write.dstBinding = i;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &constant_infos[i];
  }
  fn.vkUpdateDescriptorSets(device_.device(), T::kConstantBufferCount, constant_writes, 0, nullptr);
  const auto cull = !polygon ? VK_CULL_MODE_NONE :
      VkCullModeFlags((mode.cull_front ? VK_CULL_MODE_FRONT_BIT : 0) | (mode.cull_back ? VK_CULL_MODE_BACK_BIT : 0));
  const uint32_t point_key = std::popcount(mask) |
      ((vm.vertex.user_clip_plane_cull ? 0 : vm.vertex.user_clip_plane_count) << 5) |
      (((vm.vertex.user_clip_plane_cull ? vm.vertex.user_clip_plane_count : 0) + vm.vertex.vertex_kill_and) << 9) |
      (uint32_t(vm.vertex.output_point_parameters) << 13);
  auto& pipeline = Pipeline(&vs, ps, translated_vs.module, translated_ps, topology, samples, cull,
      mode.face ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
      rasterize && clip.clip_disable, expand, !rasterize, tess,
      !rasterize ? VK_POLYGON_MODE_FILL : fill == PolygonType::kPoints ? VK_POLYGON_MODE_POINT : fill == PolygonType::kLines ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL, point_key);
  if (!rendering_) {
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = draw_pass_;
    pass.framebuffer = draw_framebuffer_;
    pass.renderArea = {{0, 0}, draw_extent_};
    fn.vkCmdBeginRenderPass(commands_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    rendering_ = true;
  }
  fn.vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
  const VkDescriptorSet sets[]{memory_set, constants_set, vertex_textures, pixel_textures};
  fn.vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 4, sets, 0, nullptr);
  const VkViewport host_viewport = rasterize
      ? VkViewport{float(viewport.xy_offset[0]), float(viewport.xy_offset[1]),
          float(viewport.xy_extent[0]), float(viewport.xy_extent[1]), viewport.z_min, viewport.z_max}
      : VkViewport{0, 0, 1, 1, 0, 1};
  fn.vkCmdSetViewport(commands_, 0, 1, &host_viewport);
  VkRect2D host_scissor{{int32_t(scissor.offset[0]), int32_t(scissor.offset[1])},
      {scissor.extent[0], scissor.extent[1]}};
  host_scissor.extent.width = host_scissor.offset.x < int32_t(draw_extent_.width)
      ? std::min(host_scissor.extent.width, draw_extent_.width - host_scissor.offset.x) : 0;
  host_scissor.extent.height = host_scissor.offset.y < int32_t(draw_extent_.height)
      ? std::min(host_scissor.extent.height, draw_extent_.height - host_scissor.offset.y) : 0;
  if (!rasterize) host_scissor = {{0, 0}, {1, 1}};
  fn.vkCmdSetScissor(commands_, 0, 1, &host_scissor);
  if (!normalized.empty()) {
    fn.vkCmdBindIndexBuffer(commands_, index_buffer.buffer, index_buffer.offset, VK_INDEX_TYPE_UINT32);
    for (const auto& range : ranges) fn.vkCmdDrawIndexed(commands_, range.count, 1, range.offset, 0, 0);
  } else fn.vkCmdDraw(commands_, count, 1, 0, 0);
  if (!exports.empty()) {
    EndRendering();
    UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const auto first = readback_used_;
    for (const auto& range : exports) {
      const uint32_t address = range.address;
      std::vector<uint64_t> cpu_generations;
      mirror.RangeWrittenByGpu(address, range.length, &cpu_generations);
      const VkBufferCopy copy{address, readback_used_, range.length};
      fn.vkCmdCopyBuffer(commands_, guest_buffer_->handle(), readback_->handle(), 1, &copy);
      writeback_.Add(address, range.length, readback_used_, std::move(cpu_generations));
      readback_used_ += range.length;
    }
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback_->handle();
    host.offset = first;
    host.size = readback_used_ - first;
    fn.vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &host, 0, nullptr);
  }
}
}
