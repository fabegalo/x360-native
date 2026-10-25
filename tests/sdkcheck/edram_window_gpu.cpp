// Bounded synthetic rasterization/readback of the renderer's viewport/scissor
// transforms and Xenos sample positions. No guest program or game is loaded.
// Ownership planning is covered separately by edram_window_offsets.cpp; this
// standalone API probe does not execute complete ROV/FSI command processors.
#include <SPIRV/GlslangToSpv.h>
#include <StandAlone/ResourceLimits.h>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <glslang/Public/ShaderLang.h>
#include <iostream>
#include <rex/graphics/util/draw.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using namespace rex::graphics;
using Microsoft::WRL::ComPtr;
constexpr unsigned W = 160, H = 64;
bool omit_window_transform = false; // Test-only fault injection for readback.
void Need(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
template <class T> void Set(RegisterFile &r, T v) {
  r[T::register_index] = v.value;
}
void Float(RegisterFile &r, unsigned i, float v) { std::memcpy(&r[i], &v, 4); }
struct State {
  draw_util::ViewportInfo vp;
  draw_util::Scissor sc;
  float offset[2];
};
State Transform(xenos::MsaaSamples mode, unsigned row, unsigned height,
                bool carry) {
  if (omit_window_transform)
    carry = false;
  RegisterFile r;
  std::memset(r.values, 0, sizeof(r.values));
  reg::RB_SURFACE_INFO surface{};
  surface.surface_pitch = W;
  surface.msaa_samples = mode;
  Set(r, surface);
  reg::PA_SU_SC_MODE_CNTL m{};
  m.vtx_window_offset_enable = 1;
  Set(r, m);
  reg::PA_SC_WINDOW_OFFSET offset{};
  offset.window_y_offset = -int(row);
  Set(r, offset);
  reg::PA_SC_WINDOW_SCISSOR_TL tl{};
  tl.tl_y = row;
  Set(r, tl);
  reg::PA_SC_WINDOW_SCISSOR_BR br{};
  br.br_x = W;
  br.br_y = row + height;
  Set(r, br);
  reg::PA_SC_SCREEN_SCISSOR_BR screen{};
  screen.br_x = W;
  screen.br_y = H;
  Set(r, screen);
  reg::PA_SU_VTX_CNTL center{};
  center.pix_center = xenos::PixelCenter(1);
  Set(r, center);
  reg::PA_CL_VTE_CNTL v{};
  v.vport_x_scale_ena = v.vport_y_scale_ena = v.vport_x_offset_ena =
      v.vport_y_offset_ena = 1;
  Set(r, v);
  Float(r, XE_GPU_REG_PA_CL_VPORT_XSCALE, W * .5f);
  Float(r, XE_GPU_REG_PA_CL_VPORT_XOFFSET, W * .5f);
  Float(r, XE_GPU_REG_PA_CL_VPORT_YSCALE, -float(H) * .5f);
  Float(r, XE_GPU_REG_PA_CL_VPORT_YOFFSET, H * .5f);
  reg::RB_DEPTHCONTROL depth{};
  depth.z_enable = 1;
  bool kept = false;
  auto tiles = draw_util::GetWindowOffsetTiles(r, depth, 15, true, kept);
  if (carry && row) {
    Need(tiles < 0 && !kept, "eligible GPU tile offset");
    unsigned pitch = W / (mode == xenos::MsaaSamples::k4X ? 40 : 80);
    unsigned th = mode == xenos::MsaaSamples::k1X ? 16 : 8;
    Need(draw_util::AddWindowOffsetToEdramBase(row / th * pitch, tiles,
                                               false) == 0,
         "tile and prepass share EDRAM base");
  }
  State s{};
  draw_util::GetHostViewportInfo(r, 1, 1, false, 16384, 16384, false, depth,
                                 false, false, false, s.vp, true, carry);
  draw_util::GetScissor(r, s.sc, true, carry);
  s.offset[1] = carry ? -float(row) : 0;
  return s;
}
unsigned Compare(const void *bytes, unsigned pitch, unsigned tile_h) {
  auto *p = static_cast<const unsigned char *>(bytes);
  unsigned errors = 0;
  for (unsigned y = 0; y < H; ++y)
    for (unsigned x = 0; x < W; ++x) {
      auto *v = p + y * pitch + x * 4;
      errors +=
          v[0] != x + 1 || v[1] != y % tile_h + 1 || v[2] != 128 || v[3] != 255;
    }
  return errors;
}
std::vector<uint32_t> Glsl(const char *source, EShLanguage stage) {
  glslang::TShader shader(stage);
  shader.setStrings(&source, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan,
                     450);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
  auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
  if (!shader.parse(&glslang::DefaultTBuiltInResource, 450, false, messages))
    throw std::runtime_error(shader.getInfoLog());
  glslang::TProgram program;
  program.addShader(&shader);
  Need(program.link(messages), "GLSL link");
  std::vector<uint32_t> code;
  glslang::GlslangToSpv(*program.getIntermediate(stage), code);
  return code;
}
void D3D12Raster() {
  ComPtr<ID3D12Device> device;
  Need(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(&device))),
       "D3D device");
  D3D12_COMMAND_QUEUE_DESC qd{};
  ComPtr<ID3D12CommandQueue> queue;
  Need(SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))),
       "D3D queue");
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
  Need(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2,
                                             &options, sizeof(options))),
       "D3D sample-position feature query");
  std::cout << "D3D12 sample-position tier="
            << options.ProgrammableSamplePositionsTier << '\n';
  HMODULE compiler = LoadLibraryW(L"D3DCompiler_47.dll");
  Need(compiler != nullptr, "HLSL compiler");
  struct CompilerLibrary {
    HMODULE handle;
    ~CompilerLibrary() { FreeLibrary(handle); }
  } compiler_library{compiler};
  auto compile =
      reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  Need(compile != nullptr, "D3DCompile");
  const char *source = R"(cbuffer C:register(b0){float2 offset;}
float4 VS(uint id:SV_VertexID):SV_Position {return float4((id==1?3:-1),(id==2?-3:1),0.25,1);}
float4 PS(float4 p:SV_Position):SV_Target{return float4((abs(floor(p.xy)+offset)+1)/255,128.0/255,1);})";
  ComPtr<ID3DBlob> vs, ps, err;
  Need(SUCCEEDED(compile(source, strlen(source), nullptr, nullptr, nullptr,
                         "VS", "vs_5_0", 0, 0, &vs, &err)),
       "D3D VS");
  Need(SUCCEEDED(compile(source, strlen(source), nullptr, nullptr, nullptr,
                         "PS", "ps_5_0", 0, 0, &ps, &err)),
       "D3D PS");
  D3D12_ROOT_PARAMETER rp{};
  rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rp.Constants = {0, 0, 2};
  rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC rsd{};
  rsd.NumParameters = 1;
  rsd.pParameters = &rp;
  ComPtr<ID3DBlob> rsblob;
  Need(SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &rsblob, &err)),
       "D3D root serialize");
  ComPtr<ID3D12RootSignature> root;
  Need(SUCCEEDED(device->CreateRootSignature(0, rsblob->GetBufferPointer(),
                                             rsblob->GetBufferSize(),
                                             IID_PPV_ARGS(&root))),
       "D3D root");
  for (bool programmable : {false, true})
    for (unsigned samples : {1u, 2u, 4u}) {
      if (programmable && samples == 1)
        continue;
      auto mode = samples == 1   ? xenos::MsaaSamples::k1X
                  : samples == 2 ? xenos::MsaaSamples::k2X
                                 : xenos::MsaaSamples::k4X;
      unsigned th = samples == 1 ? 16 : 8;
      D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
      pd.pRootSignature = root.Get();
      pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
      pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
      pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
      pd.SampleMask = ~0u;
      pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      pd.RasterizerState.DepthClipEnable = TRUE;
      pd.RasterizerState.MultisampleEnable = TRUE;
      pd.DepthStencilState.DepthEnable = TRUE;
      pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
      pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      pd.NumRenderTargets = 1;
      pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      pd.SampleDesc.Count = samples;
      ComPtr<ID3D12PipelineState> prepass, tile;
      Need(SUCCEEDED(device->CreateGraphicsPipelineState(
               &pd, IID_PPV_ARGS(&prepass))),
           "D3D prepass PSO");
      pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 15;
      pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
      Need(SUCCEEDED(
               device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&tile))),
           "D3D tile PSO");
      D3D12_HEAP_PROPERTIES heap{};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC td{};
      td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      td.Width = W;
      td.Height = H;
      td.DepthOrArraySize = td.MipLevels = 1;
      td.SampleDesc.Count = samples;
      td.Format = pd.RTVFormats[0];
      td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      ComPtr<ID3D12Resource> color, depth, resolved, readback;
      Need(SUCCEEDED(device->CreateCommittedResource(
               &heap, D3D12_HEAP_FLAG_NONE, &td,
               D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
               IID_PPV_ARGS(&color))),
           "D3D color");
      td.Format = pd.DSVFormat;
      td.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
      Need(SUCCEEDED(
               device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td,
                                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                               nullptr, IID_PPV_ARGS(&depth))),
           "D3D depth");
      td.Format = pd.RTVFormats[0];
      td.Flags = D3D12_RESOURCE_FLAG_NONE;
      td.SampleDesc.Count = 1;
      Need(SUCCEEDED(device->CreateCommittedResource(
               &heap, D3D12_HEAP_FLAG_NONE, &td,
               D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr,
               IID_PPV_ARGS(&resolved))),
           "D3D resolve");
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
      UINT64 size;
      device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &size);
      D3D12_RESOURCE_DESC bd{};
      bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      bd.Width = size;
      bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
      bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      heap.Type = D3D12_HEAP_TYPE_READBACK;
      Need(SUCCEEDED(device->CreateCommittedResource(
               &heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
               nullptr, IID_PPV_ARGS(&readback))),
           "D3D readback");
      ComPtr<ID3D12DescriptorHeap> rtv, dsv;
      D3D12_DESCRIPTOR_HEAP_DESC hd{};
      hd.NumDescriptors = 1;
      hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv));
      hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
      device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv));
      device->CreateRenderTargetView(color.Get(), nullptr,
                                     rtv->GetCPUDescriptorHandleForHeapStart());
      device->CreateDepthStencilView(depth.Get(), nullptr,
                                     dsv->GetCPUDescriptorHandleForHeapStart());
      ComPtr<ID3D12CommandAllocator> allocator;
      ComPtr<ID3D12GraphicsCommandList> cmd;
      device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     IID_PPV_ARGS(&allocator));
      device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                allocator.Get(), nullptr, IID_PPV_ARGS(&cmd));
      ComPtr<ID3D12GraphicsCommandList1> cmd1;
      cmd.As(&cmd1);
      const bool programmed = programmable && samples > 1 && cmd1 &&
                              options.ProgrammableSamplePositionsTier;
      if (programmed) {
        int8_t xy[4][2];
        draw_util::GetHostSampleXenosPositions(mode, true, false, xy);
        D3D12_SAMPLE_POSITION p[4];
        for (unsigned i = 0; i < samples; ++i)
          p[i] = {xy[i][0], xy[i][1]};
        cmd1->SetSamplePositions(samples, 1, p);
      }
      auto rh = rtv->GetCPUDescriptorHandleForHeapStart(),
           dh = dsv->GetCPUDescriptorHandleForHeapStart();
      float clear[4]{};
      cmd->ClearRenderTargetView(rh, clear, 0, nullptr);
      cmd->ClearDepthStencilView(dh, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
      cmd->OMSetRenderTargets(1, &rh, FALSE, &dh);
      cmd->SetGraphicsRootSignature(root.Get());
      cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      auto draw = [&](State s, ID3D12PipelineState *pso) {
        D3D12_VIEWPORT v{float(s.vp.xy_offset[0]),
                         float(s.vp.xy_offset[1]),
                         float(s.vp.xy_extent[0]),
                         float(s.vp.xy_extent[1]),
                         0,
                         1};
        D3D12_RECT sc{LONG(s.sc.offset[0]), LONG(s.sc.offset[1]),
                      LONG(s.sc.offset[0] + s.sc.extent[0]),
                      LONG(s.sc.offset[1] + s.sc.extent[1])};
        cmd->RSSetViewports(1, &v);
        cmd->RSSetScissorRects(1, &sc);
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRoot32BitConstants(0, 2, s.offset, 0);
        cmd->DrawInstanced(3, 1, 0, 0);
      };
      draw(Transform(mode, 0, H, false), prepass.Get());
      for (unsigned y = 0; y < H; y += th)
        draw(Transform(mode, y, th, true), tile.Get());
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition = {color.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            samples > 1 ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE
                                        : D3D12_RESOURCE_STATE_COPY_SOURCE};
      cmd->ResourceBarrier(1, &barrier);
      ID3D12Resource *output = color.Get();
      if (samples > 1) {
        cmd->ResolveSubresource(resolved.Get(), 0, color.Get(), 0,
                                pd.RTVFormats[0]);
        barrier.Transition = {resolved.Get(), 0,
                              D3D12_RESOURCE_STATE_RESOLVE_DEST,
                              D3D12_RESOURCE_STATE_COPY_SOURCE};
        cmd->ResourceBarrier(1, &barrier);
        output = resolved.Get();
      }
      D3D12_TEXTURE_COPY_LOCATION from{};
      from.pResource = output;
      from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      D3D12_TEXTURE_COPY_LOCATION to{};
      to.pResource = readback.Get();
      to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      to.PlacedFootprint = fp;
      cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Need(SUCCEEDED(cmd->Close()), "D3D close");
      ID3D12CommandList *lists[] = {cmd.Get()};
      queue->ExecuteCommandLists(1, lists);
      ComPtr<ID3D12Fence> fence;
      device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
      queue->Signal(fence.Get(), 1);
      HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      fence->SetEventOnCompletion(1, event);
      auto wait = WaitForSingleObject(event, 5000);
      CloseHandle(event);
      Need(wait == WAIT_OBJECT_0 && SUCCEEDED(device->GetDeviceRemovedReason()),
           "D3D bounded GPU wait");
      void *data;
      D3D12_RANGE range{0, SIZE_T(size)};
      Need(SUCCEEDED(readback->Map(0, &range, &data)), "D3D map");
      auto errors = Compare(data, fp.Footprint.RowPitch, th);
      readback->Unmap(0, nullptr);
      Need(errors == 0, "D3D raster/depth/tile readback mismatch");
      std::cout << "D3D12 raster depth prepass + tiles " << samples
                << "x programmed=" << programmed << ": " << W * H
                << " pixels match\n";
    }
}

void VulkanRaster() {
  using namespace rex::ui::vulkan;
  auto instance = VulkanInstance::Create(false, true);
  Need(bool(instance), "Vulkan instance");
  std::vector<VkPhysicalDevice> physical;
  instance->EnumeratePhysicalDevices(physical);
  Need(!physical.empty(), "Vulkan GPU");
  auto device =
      VulkanDevice::CreateIfSupported(instance.get(), physical[0], true, false);
  Need(bool(device), "Vulkan Xenos device");
  const auto &f = device->functions();
  const auto &inf = instance->functions();
  auto vk = device->device();
  std::cout << "Vulkan GPU=" << device->properties().deviceName << '\n';
  uint32_t layer_count = 0;
  inf.vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  std::vector<VkLayerProperties> layers(layer_count);
  inf.vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
  bool validation = false;
  for (auto &layer : layers)
    validation |= !strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation");
  std::cout << "Khronos validation available=" << validation
            << "; synchronization validation requested via environment\n";
  VkPhysicalDeviceMemoryProperties memory;
  inf.vkGetPhysicalDeviceMemoryProperties(physical[0], &memory);
  auto allocate = [&](VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
    unsigned i = 0;
    for (; i < memory.memoryTypeCount; ++i)
      if ((req.memoryTypeBits & (1u << i)) &&
          (memory.memoryTypes[i].propertyFlags & flags) == flags)
        break;
    Need(i < memory.memoryTypeCount, "Vulkan memory type");
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = req.size;
    a.memoryTypeIndex = i;
    VkDeviceMemory m;
    Need(f.vkAllocateMemory(vk, &a, nullptr, &m) == VK_SUCCESS,
         "Vulkan allocate");
    return m;
  };
  glslang::InitializeProcess();
  auto vcode = Glsl(R"(#version 450
void main(){gl_Position=vec4(gl_VertexIndex==1?3:-1,gl_VertexIndex==2?3:-1,0.25,1);})",
                    EShLangVertex);
  auto pcode = Glsl(R"(#version 450
layout(push_constant) uniform C{vec2 offset;};layout(location=0)out vec4 color;
void main(){color=vec4((abs(floor(gl_FragCoord.xy)+offset)+1)/255,128.0/255,1);})",
                    EShLangFragment);
  glslang::FinalizeProcess();
  auto module = [&](const auto &code) {
    VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    i.codeSize = code.size() * 4;
    i.pCode = code.data();
    VkShaderModule m;
    Need(f.vkCreateShaderModule(vk, &i, nullptr, &m) == VK_SUCCESS,
         "Vulkan shader");
    return m;
  };
  auto vs = module(vcode), ps = module(pcode);
  VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8};
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &range;
  VkPipelineLayout layout;
  Need(f.vkCreatePipelineLayout(vk, &li, nullptr, &layout) == VK_SUCCESS,
       "Vulkan layout");
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = device->queue_family_graphics_compute();
  VkCommandPool pool;
  Need(f.vkCreateCommandPool(vk, &pci, nullptr, &pool) == VK_SUCCESS,
       "Vulkan pool");
  auto image_format =
      reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
          inf.vkGetInstanceProcAddr(
              instance->instance(),
              "vkGetPhysicalDeviceImageFormatProperties"));
  for (unsigned scenario : {0u, 1u, 2u})
    for (unsigned requested : {1u, 2u, 4u}) {
      if ((scenario == 1 && requested == 1) ||
          (scenario == 2 && requested != 2))
        continue;
      unsigned samples = requested;
      VkImageFormatProperties support{};
      Need(image_format(physical[0], VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0,
                        &support) == VK_SUCCESS,
           "Vulkan depth format");
      bool native2 = scenario != 2;
      if (samples == 2 && (!native2 || !(support.sampleCounts & 2))) {
        samples = 4;
        native2 = false;
      }
      Need((support.sampleCounts & samples) != 0, "Vulkan sample count");
      auto mode = requested == 1   ? xenos::MsaaSamples::k1X
                  : requested == 2 ? xenos::MsaaSamples::k2X
                                   : xenos::MsaaSamples::k4X;
      unsigned th = requested == 1 ? 16 : 8;
      bool programmed =
          scenario != 0 && samples > 1 &&
          device->extensions().ext_EXT_sample_locations &&
          (device->properties().sampleLocationSampleCounts & samples);
      int8_t xy[4][2]{};
      VkSampleLocationEXT locations[4]{};
      if (samples > 1)
        draw_util::GetHostSampleXenosPositions(mode, native2, false, xy);
      for (unsigned i = 0; i < samples; ++i)
        locations[i] = {.5f + xy[i][0] / 16.f, .5f + xy[i][1] / 16.f};
      VkSampleLocationsInfoEXT sl{VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT};
      sl.sampleLocationsPerPixel = VkSampleCountFlagBits(samples);
      sl.sampleLocationGridSize = {1, 1};
      sl.sampleLocationsCount = samples;
      sl.pSampleLocations = locations;
      VkPipelineSampleLocationsStateCreateInfoEXT slstate{
          VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT};
      slstate.sampleLocationsEnable = programmed;
      slstate.sampleLocationsInfo = sl;
      struct Image {
        VkImage handle;
        VkDeviceMemory memory;
        VkImageView view;
      };
      auto make_image = [&](VkFormat format, unsigned n,
                            VkImageUsageFlags usage,
                            VkImageAspectFlags aspect) {
        Image result{};
        VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        i.flags =
            programmed && aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                ? VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT
                : 0;
        i.imageType = VK_IMAGE_TYPE_2D;
        i.format = format;
        i.extent = {W, H, 1};
        i.mipLevels = i.arrayLayers = 1;
        i.samples = VkSampleCountFlagBits(n);
        i.tiling = VK_IMAGE_TILING_OPTIMAL;
        i.usage = usage;
        Need(f.vkCreateImage(vk, &i, nullptr, &result.handle) == VK_SUCCESS,
             "Vulkan image");
        VkMemoryRequirements req;
        f.vkGetImageMemoryRequirements(vk, result.handle, &req);
        result.memory = allocate(req, 0);
        Need(f.vkBindImageMemory(vk, result.handle, result.memory, 0) ==
                 VK_SUCCESS,
             "Vulkan image bind");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = result.handle;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {aspect, 0, 1, 0, 1};
        Need(f.vkCreateImageView(vk, &vi, nullptr, &result.view) == VK_SUCCESS,
             "Vulkan view");
        return result;
      };
      auto color = make_image(VK_FORMAT_R8G8B8A8_UNORM, samples,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
      auto depth = make_image(VK_FORMAT_D32_SFLOAT, samples,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
      auto output = make_image(VK_FORMAT_R8G8B8A8_UNORM, 1,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT);
      VkAttachmentDescription attachments[3]{};
      for (unsigned i = 0; i < 3; ++i) {
        attachments[i].format =
            i == 1 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        attachments[i].samples = VkSampleCountFlagBits(i == 2 ? 1 : samples);
        attachments[i].loadOp = i == 2 ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                       : VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].finalLayout =
            i == 1 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                   : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      }
      VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
          dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
          rr{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
      VkSubpassDescription sub{};
      sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      sub.colorAttachmentCount = 1;
      sub.pColorAttachments = &cr;
      sub.pDepthStencilAttachment = &dr;
      sub.pResolveAttachments = samples > 1 ? &rr : nullptr;
      VkSubpassDependency dependency{};
      dependency.srcSubpass = 0;
      dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
      dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
      dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
      ri.attachmentCount = samples > 1 ? 3 : 2;
      ri.pAttachments = attachments;
      ri.subpassCount = 1;
      ri.pSubpasses = &sub;
      ri.dependencyCount = 1;
      ri.pDependencies = &dependency;
      VkRenderPass pass;
      Need(f.vkCreateRenderPass(vk, &ri, nullptr, &pass) == VK_SUCCESS,
           "Vulkan pass");
      VkImageView views[] = {color.view, depth.view, output.view};
      VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fi.renderPass = pass;
      fi.attachmentCount = ri.attachmentCount;
      fi.pAttachments = views;
      fi.width = W;
      fi.height = H;
      fi.layers = 1;
      VkFramebuffer fb;
      Need(f.vkCreateFramebuffer(vk, &fi, nullptr, &fb) == VK_SUCCESS,
           "Vulkan framebuffer");
      VkPipelineShaderStageCreateInfo stages[2]{};
      for (unsigned i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage =
            i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? ps : vs;
        stages[i].pName = "main";
      }
      VkPipelineVertexInputStateCreateInfo vertex{
          VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
      VkPipelineInputAssemblyStateCreateInfo ia{
          VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
      ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      VkPipelineViewportStateCreateInfo vp{
          VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
      vp.viewportCount = vp.scissorCount = 1;
      VkPipelineRasterizationStateCreateInfo raster{
          VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
      raster.polygonMode = VK_POLYGON_MODE_FILL;
      raster.cullMode = VK_CULL_MODE_NONE;
      raster.lineWidth = 1;
      VkPipelineMultisampleStateCreateInfo ms{
          VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
      ms.rasterizationSamples = VkSampleCountFlagBits(samples);
      ms.pNext = programmed ? &slstate : nullptr;
      VkPipelineDepthStencilStateCreateInfo ds{
          VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
      ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
      ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
      VkPipelineColorBlendAttachmentState blend{};
      VkPipelineColorBlendStateCreateInfo bs{
          VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
      bs.attachmentCount = 1;
      bs.pAttachments = &blend;
      VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                  VK_DYNAMIC_STATE_SCISSOR};
      VkPipelineDynamicStateCreateInfo dy{
          VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
      dy.dynamicStateCount = 2;
      dy.pDynamicStates = dynamic;
      VkGraphicsPipelineCreateInfo pi{
          VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
      pi.stageCount = 2;
      pi.pStages = stages;
      pi.pVertexInputState = &vertex;
      pi.pInputAssemblyState = &ia;
      pi.pViewportState = &vp;
      pi.pRasterizationState = &raster;
      pi.pMultisampleState = &ms;
      pi.pDepthStencilState = &ds;
      pi.pColorBlendState = &bs;
      pi.pDynamicState = &dy;
      pi.layout = layout;
      pi.renderPass = pass;
      VkPipeline prepass, tile;
      Need(f.vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &pi, nullptr,
                                       &prepass) == VK_SUCCESS,
           "Vulkan depth PSO");
      blend.colorWriteMask = 15;
      ds.depthWriteEnable = VK_FALSE;
      Need(f.vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &pi, nullptr,
                                       &tile) == VK_SUCCESS,
           "Vulkan tile PSO");
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bi.size = W * H * 4;
      bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      VkBuffer readback;
      Need(f.vkCreateBuffer(vk, &bi, nullptr, &readback) == VK_SUCCESS,
           "Vulkan readback");
      VkMemoryRequirements req;
      f.vkGetBufferMemoryRequirements(vk, readback, &req);
      auto read_memory =
          allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      f.vkBindBufferMemory(vk, readback, read_memory, 0);
      VkCommandBufferAllocateInfo ai{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      ai.commandPool = pool;
      ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      ai.commandBufferCount = 1;
      VkCommandBuffer cmd;
      Need(f.vkAllocateCommandBuffers(vk, &ai, &cmd) == VK_SUCCESS,
           "Vulkan commands");
      VkCommandBufferBeginInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      f.vkBeginCommandBuffer(cmd, &ci);
      VkAttachmentSampleLocationsEXT initial{1, sl};
      VkSubpassSampleLocationsEXT post{0, sl};
      VkRenderPassSampleLocationsBeginInfoEXT begin_sl{
          VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT};
      begin_sl.attachmentInitialSampleLocationsCount =
          begin_sl.postSubpassSampleLocationsCount = 1;
      begin_sl.pAttachmentInitialSampleLocations = &initial;
      begin_sl.pPostSubpassSampleLocations = &post;
      VkClearValue clears[3]{};
      clears[1].depthStencil = {1, 0};
      VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      begin.pNext = programmed ? &begin_sl : nullptr;
      begin.renderPass = pass;
      begin.framebuffer = fb;
      begin.renderArea.extent = {W, H};
      begin.clearValueCount = ri.attachmentCount;
      begin.pClearValues = clears;
      f.vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
      auto draw = [&](State s, VkPipeline pipeline) {
        VkViewport viewport{float(s.vp.xy_offset[0]),
                            float(s.vp.xy_offset[1]),
                            float(s.vp.xy_extent[0]),
                            float(s.vp.xy_extent[1]),
                            0,
                            1};
        VkRect2D scissor{{int32_t(s.sc.offset[0]), int32_t(s.sc.offset[1])},
                         {s.sc.extent[0], s.sc.extent[1]}};
        f.vkCmdSetViewport(cmd, 0, 1, &viewport);
        f.vkCmdSetScissor(cmd, 0, 1, &scissor);
        f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        f.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8,
                             s.offset);
        f.vkCmdDraw(cmd, 3, 1, 0, 0);
      };
      draw(Transform(mode, 0, H, false), prepass);
      for (unsigned y = 0; y < H; y += th)
        draw(Transform(mode, y, th, true), tile);
      f.vkCmdEndRenderPass(cmd);
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {W, H, 1};
      f.vkCmdCopyImageToBuffer(cmd, samples > 1 ? output.handle : color.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback,
                               1, &copy);
      VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
          VK_QUEUE_FAMILY_IGNORED;
      barrier.buffer = readback;
      barrier.size = VK_WHOLE_SIZE;
      f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &barrier, 0, nullptr);
      Need(f.vkEndCommandBuffer(cmd) == VK_SUCCESS, "Vulkan end");
      VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      VkFence fence;
      f.vkCreateFence(vk, &fence_info, nullptr, &fence);
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cmd;
      {
        auto queue = device->AcquireQueue(pci.queueFamilyIndex, 0);
        Need(f.vkQueueSubmit(queue.queue(), 1, &submit, fence) == VK_SUCCESS,
             "Vulkan submit");
      }
      Need(f.vkWaitForFences(vk, 1, &fence, VK_TRUE, 5'000'000'000ull) ==
               VK_SUCCESS,
           "Vulkan bounded GPU wait");
      void *data;
      Need(f.vkMapMemory(vk, read_memory, 0, VK_WHOLE_SIZE, 0, &data) ==
               VK_SUCCESS,
           "Vulkan map");
      auto errors = Compare(data, W * 4, th);
      f.vkUnmapMemory(vk, read_memory);
      Need(errors == 0, "Vulkan raster/depth/tile readback mismatch");
      std::cout << "Vulkan raster depth prepass + tiles guest=" << requested
                << "x host=" << samples << "x programmed=" << programmed << ": "
                << W * H << " pixels match\n";
      f.vkDestroyFence(vk, fence, nullptr);
      f.vkDestroyBuffer(vk, readback, nullptr);
      f.vkFreeMemory(vk, read_memory, nullptr);
      f.vkDestroyPipeline(vk, tile, nullptr);
      f.vkDestroyPipeline(vk, prepass, nullptr);
      f.vkDestroyFramebuffer(vk, fb, nullptr);
      f.vkDestroyRenderPass(vk, pass, nullptr);
      for (auto image : {color, depth, output}) {
        f.vkDestroyImageView(vk, image.view, nullptr);
        f.vkDestroyImage(vk, image.handle, nullptr);
        f.vkFreeMemory(vk, image.memory, nullptr);
      }
    }
  f.vkDestroyCommandPool(vk, pool, nullptr);
  f.vkDestroyPipelineLayout(vk, layout, nullptr);
  f.vkDestroyShaderModule(vk, ps, nullptr);
  f.vkDestroyShaderModule(vk, vs, nullptr);
}
int main(int argc, char **) {
  omit_window_transform = argc > 1;
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  try {
    std::cout << "Starting D3D12 raster probe\n";
    D3D12Raster();
    VulkanRaster();
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
  return 0;
}
