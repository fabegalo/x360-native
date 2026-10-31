// Headless production VulkanRender workloads. Synthetic throughput is not FPS.
#include "../../src/native/vulkan/vulkan_render.h"
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
uint64_t CpuNs() {
  FILETIME c, e, k, u;
  Require(GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u), "process CPU clock");
  auto ticks=[](FILETIME t) { return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
  return (ticks(k)+ticks(u))*100;
}
DXGI_QUERY_VIDEO_MEMORY_INFO VideoMemory(const rex::ui::vulkan::VulkanDevice& device) {
  VkPhysicalDeviceProperties properties;
  device.vulkan_instance()->functions().vkGetPhysicalDeviceProperties(device.physical_device(),&properties);
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"memory budget DXGI factory");
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for(unsigned i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i) {
    DXGI_ADAPTER_DESC1 desc; Require(SUCCEEDED(adapter->GetDesc1(&desc)),"memory budget adapter");
    if(desc.VendorId!=properties.vendorID||desc.DeviceId!=properties.deviceID) continue;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> queried;
    Require(SUCCEEDED(adapter.As(&queried)),"memory budget interface");
    DXGI_QUERY_VIDEO_MEMORY_INFO result;
    Require(SUCCEEDED(queried->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&result)),"local video memory usage");
    return result;
  }
  throw std::runtime_error("Vulkan adapter missing from DXGI budget enumeration");
}
void CheckResolveContracts(VulkanRender& render, RegisterFile& regs,
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
void CheckPendingContracts(VulkanRender& render, RegisterFile& regs,
    rex::memory::Memory& memory, rex::graphics::Shader* vs, uint32_t output,
    const rex::ui::vulkan::VulkanDevice& device) {
  const uint32_t physical=memory.GetPhysicalAddress(output);
  VkEventCreateInfo info{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
  VkEvent event;
  CheckVulkan(LoadNativeVulkanFunction<PFN_vkCreateEvent>(device,"vkCreateEvent")(
      device.device(),&info,nullptr,&event),"test gate event");
  struct EventOwner {
    const rex::ui::vulkan::VulkanDevice& device; VkEvent event;
    ~EventOwner() { LoadNativeVulkanFunction<PFN_vkDestroyEvent>(device,"vkDestroyEvent")(device.device(),event,nullptr); }
  } owner{device,event};
  VulkanSubmissionRing gate(device);
  auto commands=gate.Begin();
  LoadNativeVulkanFunction<PFN_vkCmdWaitEvents>(device,"vkCmdWaitEvents")(
      commands,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0,nullptr,0,nullptr,0,nullptr);
  gate.Submit();
  std::mutex mutex; std::condition_variable cv; bool release=false, timed_out=false;
  // A bounded watchdog also releases the gate on an exception or regression.
  std::jthread watchdog([&] {
    std::unique_lock lock(mutex);
    timed_out=!cv.wait_for(lock,std::chrono::seconds(2),[&]{return release;});
    CheckVulkan(LoadNativeVulkanFunction<PFN_vkSetEvent>(device,"vkSetEvent")(device.device(),event),"release test gate");
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
  draw(physical+16,0.125f); // Later overlapping submission must win.
  // A disjoint guest read must leave the three gated submissions in flight.
  render.SyncGuestRead(physical+4096,4,0xFF);
  { std::lock_guard lock(mutex); release=true; } cv.notify_all(); watchdog.join();
  Require(!timed_out,"Flush(false) or disjoint read waited for pending GPU writebacks");
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
  gate.Drain();
  std::cout<<"PASS gated in-flight writebacks: disjoint read, ring reuse, CPU epochs, overlap order, counters, events\n";
}
void CheckStagingGrowth(VulkanRender& render, RegisterFile& regs,
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
// Hold real compute creation while shader storage is initialized after Resolve.
// The cache merge must not race the compilation workers (Vulkan host contract).
struct ComputeGate {
  PFN_vkCreateComputePipelines original;
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false, timed_out = false;
  inline static ComputeGate* active = nullptr;
  static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice device, VkPipelineCache cache,
      uint32_t count, const VkComputePipelineCreateInfo* info,
      const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
    auto& gate = *active;
    {
      std::unique_lock lock(gate.mutex);
      gate.entered = true;
      gate.cv.notify_all();
      if (!gate.cv.wait_for(lock, std::chrono::seconds(3), [&] { return gate.release; }))
        gate.timed_out = true;
    }
    return gate.original(device, cache, count, info, allocator, pipelines);
  }
};
void CheckCacheMerge(rex::ui::vulkan::VulkanProvider& provider, RegisterFile& regs,
    rex::memory::Memory& memory, const std::filesystem::path& output) {
  const auto cache = output / "late-cache";
  { VulkanRender seed(provider); seed.SetPipelineDirectory(cache); }
  Require(std::filesystem::file_size(cache / "pipelines.bin") >= 32, "seed driver cache");
  auto& functions = const_cast<rex::ui::vulkan::VulkanDevice::Functions&>(provider.vulkan_device()->functions());
  ComputeGate gate{functions.vkCreateComputePipelines};
  ComputeGate::active = &gate;
  functions.vkCreateComputePipelines = ComputeGate::Create;
  struct Restore {
    rex::ui::vulkan::VulkanDevice::Functions& functions; ComputeGate& gate;
    ~Restore() { functions.vkCreateComputePipelines = gate.original; ComputeGate::active = nullptr; }
  } restore{functions, gate};
  VulkanRender render(provider);
  render.Resolve(regs, memory);
  {
    std::unique_lock lock(gate.mutex);
    Require(gate.cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate.entered; }), "compute worker entered");
  }
  auto merge = std::async(std::launch::async, [&] { render.SetPipelineDirectory(cache); });
  const bool waited = merge.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout;
  { std::lock_guard lock(gate.mutex); gate.release = true; }
  gate.cv.notify_all();
  merge.get();
  render.Flush(true);
  Require(!gate.timed_out && waited, "cache merge did not wait for active compute creation");
  std::cout << "PASS late cache merge waits for compute workers\n";
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
    rex::cvar::SetFlagByName("native_vulkan_perf", measured?"true":"false");
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
    auto provider=rex::ui::vulkan::VulkanProvider::Create(true,true);
    Require(bool(provider),"Vulkan provider");
    std::cout<<"device="<<provider->vulkan_device()->properties().deviceName<<"\n";
    const auto output=memory.SystemHeapAlloc(1<<20,4096,rex::memory::kSystemHeapPhysical);
    const auto vertices=memory.SystemHeapAlloc(4096,4096,rex::memory::kSystemHeapPhysical);
    Require(output&&vertices,"physical allocations");
    const uint32_t physical=memory.GetPhysicalAddress(output);
    std::memset(memory.TranslateVirtual(output),0xCD,1<<20);
    {
      VulkanRender render(*provider);
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
      auto& counters=NativeVulkanPerformance();
      const auto before=counters;
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
      const auto video=VideoMemory(*provider->vulkan_device());
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
          <<",\"blocked_waits\":"<<counters.blocked_waits-before.blocked_waits
          <<",\"wait_ms\":"<<(counters.wait_ns-before.wait_ns)/1e6
          <<",\"submit_ms\":"<<(counters.submit_ns-before.submit_ns)/1e6
          <<",\"gpu_ms\":"<<(counters.gpu_ns-before.gpu_ns)/1e6
          <<",\"gpu_samples\":"<<counters.gpu_samples-before.gpu_samples
          <<",\"readbacks\":"<<counters.readbacks-before.readbacks
          <<",\"readback_bytes\":"<<counters.readback_bytes-before.readback_bytes
          <<",\"buffers\":"<<counters.buffers-before.buffers<<",\"peak_buffer_bytes\":"<<counters.peak_bytes
          <<",\"vram_usage_bytes\":"<<video.CurrentUsage<<",\"vram_budget_bytes\":"<<video.Budget<<"}\n";
      std::cout<<"PASS "<<mode<<" iterations="<<iterations<<" wall_ms="<<wall<<" waits="<<counters.waits-before.waits<<"\n";
      if(argc>6 && std::string(argv[6])=="contracts") {
        CheckPendingContracts(render,draw,memory,vs,output,*provider->vulkan_device());
        CheckStagingGrowth(render,draw,memory,vs);
        CheckResolveContracts(render,resolve,memory,output);
        if (argc <= 7 || std::stoi(argv[7]) != 0) CheckCacheMerge(*provider, resolve, memory, out);
      }
    }
    Require(!measured||NativeVulkanPerformance().live_bytes==0,"Native buffers survived renderer destruction");
    if(argc>6) for(unsigned i=0;i<3;++i) {
      VulkanRender recreated(*provider); recreated.SetSampleCounts({},false); recreated.Flush(false);
    }
    Require(!measured||NativeVulkanPerformance().live_bytes==0,"Native buffers survived renderer recreation");
    memory.SystemHeapFree(output); memory.SystemHeapFree(vertices);
    runtime.Shutdown(); rex::FlushLogging();
    return 0;
  } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
