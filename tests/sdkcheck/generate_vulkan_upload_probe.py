"""Compile production upload/barrier/replay code in a GPU-free command capture.

Only construction/device allocation is replaced by the probe. Extract verbatim
at build time so a regression in the SDK changes the executable being tested.
This is a CPU command-stream test, not Vulkan validation or a GPU execution.
"""
from pathlib import Path
import sys


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


sdk, output = map(Path, sys.argv[1:])
cp = (sdk / "src/graphics/vulkan/command_processor.cpp").read_text()
texture = (sdk / "src/graphics/vulkan/texture_cache.cpp").read_text()
upload = method(texture, "bool VulkanTextureCache::LoadTextureDataFromResidentMemoryImpl(")
start = upload.index("  vulkan_texture.MarkAsUsed();")
end = upload.index("  for (uint32_t level = level_first;", start)
parts = ["namespace rex::graphics::vulkan {\n"]
for signature in (
    "bool VulkanCommandProcessor::PushImageMemoryBarrier(",
    "bool VulkanCommandProcessor::SubmitBarriers(",
    "void VulkanCommandProcessor::SplitPendingBarrier(",
    "void VulkanCommandProcessor::EndRenderPass(",
):
    parts.append(method(cp, signature))
pass_method = method(cp, "void VulkanCommandProcessor::SubmitBarriersAndEnterRenderTargetCacheRenderPass(")
parts.append("void VulkanCommandProcessor::EndActiveForPass(bool use_dynamic_rendering) {\n" +
             method(pass_method, "  if (in_render_pass_) {") + "}\n")
parts.append(method(texture, "void VulkanTextureCache::GetTextureUsageMasks("))
parts.append("void VulkanTextureCache::RecordUpload(VulkanTexture& vulkan_texture) {\n"
             "auto& command_buffer = command_processor_.deferred_command_buffer_;\n"
             "VkBuffer scratch_buffer = reinterpret_cast<VkBuffer>(uintptr_t(2));\n"
             "uint32_t level_first = 0, level_last = 0;\n")
parts.append(upload[start:end])
parts.append("*copy_regions = {};\ncopy_regions->imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};\n"
             "copy_regions->imageExtent = {1, 1, 1};\n}\n}\n")
# Keep the complete real deferred stream implementation, including hoisting and
# replay. The probe supplies just its command processor/device construction.
deferred = (sdk / "src/graphics/vulkan/deferred_command_buffer.cpp").read_text()
deferred = deferred.replace("#include <rex/graphics/vulkan/command_processor.h>", "")
parts.append(deferred)
output.write_text("".join(parts), encoding="utf-8")
