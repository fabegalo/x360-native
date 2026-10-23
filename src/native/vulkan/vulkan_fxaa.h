#pragma once
#include "vulkan_core.h"
#include "vulkan_fxaa_source.h"
#include <rex/ui/vulkan/presenter.h>
#include <array>
#include <bit>
namespace x360n::native {
class VulkanFxaa {
  using Device=rex::ui::vulkan::VulkanDevice;
  using Presenter=rex::ui::vulkan::VulkanPresenter;
  const Device& d_;
  VkDescriptorSetLayout sets_[2]{};
  VkDescriptorPool pool_{}; VkPipelineLayout layout_{}; VkPipeline pipelines_[2]{}; VkSampler sampler_{};
  struct Slot { VkImage image{};VkDeviceMemory memory{};VkImageView view{};VkDescriptorSet sets[2]{};uint32_t w=0,h=0; };
  std::array<Slot,3> slots_;
  void FreeImage(Slot& s) { const auto& f=d_.functions();auto vk=d_.device();
    if(s.view) f.vkDestroyImageView(vk,s.view,nullptr);if(s.image) f.vkDestroyImage(vk,s.image,nullptr);if(s.memory) f.vkFreeMemory(vk,s.memory,nullptr);
    s.image={};s.view={};s.memory={};s.w=s.h=0;
  }
  void Destroy() { const auto& f=d_.functions();auto vk=d_.device();for(auto& s:slots_) FreeImage(s);
    for(auto p:pipelines_) if(p) f.vkDestroyPipeline(vk,p,nullptr);if(layout_) f.vkDestroyPipelineLayout(vk,layout_,nullptr);
    if(pool_) f.vkDestroyDescriptorPool(vk,pool_,nullptr);for(auto s:sets_) if(s) f.vkDestroyDescriptorSetLayout(vk,s,nullptr);if(sampler_) f.vkDestroySampler(vk,sampler_,nullptr);
  }
 public:
  static constexpr VkFormat kFormat=VK_FORMAT_R16G16B16A16_UNORM;
  explicit VulkanFxaa(const Device& d):d_(d) { try {
    const auto& f=d_.functions();auto vk=d_.device();
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CheckVulkan(f.vkCreateSampler(vk,&sampler,nullptr,&sampler_),"create FXAA sampler");
    const VkDescriptorType types[]{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for(unsigned i=0;i<2;++i) {
      VkDescriptorSetLayoutBinding binding{0,types[i],1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
      VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.bindingCount=1;info.pBindings=&binding;
      CheckVulkan(f.vkCreateDescriptorSetLayout(vk,&info,nullptr,&sets_[i]),"create FXAA descriptor layout");
    }
    const VkDescriptorPoolSize sizes[]{{types[0],3},{types[1],3}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=6;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    CheckVulkan(f.vkCreateDescriptorPool(vk,&pool,nullptr,&pool_),"create FXAA descriptor pool");
    for(auto& s:slots_) {VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=pool_;info.descriptorSetCount=2;info.pSetLayouts=sets_;
      CheckVulkan(f.vkAllocateDescriptorSets(vk,&info,s.sets),"allocate FXAA descriptors");}
    const VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=2;layout.pSetLayouts=sets_;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&constants;
    CheckVulkan(f.vkCreatePipelineLayout(vk,&layout,nullptr,&layout_),"create FXAA pipeline layout");
    for(unsigned i=0;i<2;++i) {
      auto module=CreateNativeShaderModule(d_,CompileNativeGlsl(VK_SHADER_STAGE_COMPUTE_BIT,GetNativeFxaaComputeSource(i!=0)));
      VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=layout_;p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};p.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;p.stage.module=module;p.stage.pName="main";
      auto result=f.vkCreateComputePipelines(vk,VK_NULL_HANDLE,1,&p,nullptr,&pipelines_[i]);f.vkDestroyShaderModule(vk,module,nullptr);CheckVulkan(result,"create FXAA pipeline");
    }
  } catch(...) {Destroy();throw;} }
  ~VulkanFxaa() {Destroy();}
  VkImageView Begin(unsigned index,uint32_t w,uint32_t h,VkCommandBuffer commands) {
    auto& s=slots_[index];const auto& f=d_.functions();auto vk=d_.device();const bool existed=s.image!=VK_NULL_HANDLE;
    if(s.w!=w||s.h!=h) {
      FreeImage(s);VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=kFormat;image.extent={w,h,1};image.mipLevels=image.arrayLayers=1;
      image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      CheckVulkan(f.vkCreateImage(vk,&image,nullptr,&s.image),"create FXAA image");
      VkMemoryRequirements requirements;f.vkGetImageMemoryRequirements(vk,s.image,&requirements);
      auto types=requirements.memoryTypeBits&d_.memory_types().device_local;if(!types) types=requirements.memoryTypeBits;
      if(!types) throw std::runtime_error("Native FXAA image has no compatible memory type");
      VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mem.allocationSize=requirements.size;mem.memoryTypeIndex=std::countr_zero(types);
      CheckVulkan(f.vkAllocateMemory(vk,&mem,nullptr,&s.memory),"allocate FXAA image");CheckVulkan(f.vkBindImageMemory(vk,s.image,s.memory,0),"bind FXAA image");
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=s.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=kFormat;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      CheckVulkan(f.vkCreateImageView(vk,&view,nullptr,&s.view),"create FXAA image view");s.w=w;s.h=h;
    }
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=existed?VK_ACCESS_SHADER_READ_BIT:0;b.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=s.image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    f.vkCmdPipelineBarrier(commands,existed?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&b);
    return s.view;
  }
  void Apply(unsigned index,VkCommandBuffer commands,VkImage output,VkImageView output_view,bool output_used,bool extreme) {
    auto& s=slots_[index];const auto& f=d_.functions();
    VkImageMemoryBarrier barriers[2]{};for(auto& b:barriers) {b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
    barriers[0].image=s.image;barriers[0].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barriers[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barriers[0].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barriers[1].image=output;barriers[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barriers[1].newLayout=VK_IMAGE_LAYOUT_GENERAL;barriers[1].srcAccessMask=output_used?Presenter::kGuestOutputInternalAccessMask:0;barriers[1].dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    f.vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|Presenter::kGuestOutputInternalStageMask,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers);
    const VkDescriptorImageInfo images[]{{sampler_,s.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,output_view,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};for(unsigned i=0;i<2;++i) {writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=s.sets[i];writes[i].descriptorCount=1;writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&images[i];}
    f.vkUpdateDescriptorSets(d_.device(),2,writes,0,nullptr);f.vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines_[extreme]);f.vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,layout_,0,2,s.sets,0,nullptr);
    const uint32_t constants[]{s.w,s.h,std::bit_cast<uint32_t>(1.0f/s.w),std::bit_cast<uint32_t>(1.0f/s.h)};f.vkCmdPushConstants(commands,layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,16,constants);f.vkCmdDispatch(commands,(s.w+15)/16,(s.h+7)/8,1);
    auto& b=barriers[1];b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=Presenter::kGuestOutputInternalLayout;b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=Presenter::kGuestOutputInternalAccessMask;
    f.vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,Presenter::kGuestOutputInternalStageMask,0,0,nullptr,0,nullptr,1,&b);
  }
};
}
