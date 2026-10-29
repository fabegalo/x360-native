#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "environment.h"
#include "sha256.h"
namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
struct Library {
  void* handle = nullptr;
  explicit Library(const std::vector<fs::path>& names) {
    for (const auto& name : names) {
#ifdef _WIN32
      handle = reinterpret_cast<void*>(LoadLibraryW(name.c_str()));
#else
      handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
      if (handle) break;
    }
  }
  ~Library() {
#ifdef _WIN32
    if (handle) FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    if (handle) dlclose(handle);
#endif
  }
  void* Symbol(const char* name) const {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
  }
};
}  // namespace
std::string ProbeVulkanDevice() {
  std::vector<fs::path> names;
#if defined(_WIN32)
  names = {"vulkan-1.dll"};
#elif defined(__APPLE__)
  // The package carries MoltenVK beside the application; a system loader also works.
  const auto self = SelfExecutable().parent_path();
  names = {self / "libvulkan.1.dylib", self / "../Frameworks/libvulkan.1.dylib", self / "libMoltenVK.dylib",
           self / "../Frameworks/libMoltenVK.dylib", "libvulkan.1.dylib", "libMoltenVK.dylib"};
#else
  names = {"libvulkan.so.1", "libvulkan.so"};
#endif
  Library loader(names);
  if (!loader.handle) return {};
  const auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(loader.Symbol("vkGetInstanceProcAddr"));
  if (!get) return {};
  const auto create = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
  const auto extensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(nullptr, "vkEnumerateInstanceExtensionProperties"));
  if (!create || !extensions) return {};
  uint32_t count = 0;
  extensions(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  if (count) extensions(nullptr, &count, available.data());
  std::vector<const char*> enabled;
  VkInstanceCreateFlags flags = 0;
  for (const auto& e : available)
    if (std::string(e.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
      enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "x360-native environment check";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.flags = flags; info.pApplicationInfo = &app;
  info.enabledExtensionCount = uint32_t(enabled.size()); info.ppEnabledExtensionNames = enabled.data();
  VkInstance instance = VK_NULL_HANDLE;
  if (create(&info, nullptr, &instance) != VK_SUCCESS) return {};
  const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get(instance, "vkDestroyInstance"));
  const auto devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance, "vkEnumeratePhysicalDevices"));
  const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance, "vkGetPhysicalDeviceProperties"));
  std::string found;
  if (devices && properties) {
    uint32_t n = 0;
    devices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> list(n);
    if (n && devices(instance, &n, list.data()) >= 0) {
      // Report a hardware device first; a CPU implementation is named as such.
      for (int pass = 0; pass != 2 && found.empty(); ++pass)
        for (auto device : list) {
          VkPhysicalDeviceProperties p{}; properties(device, &p);
          const bool cpu = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
          if (pass == 0 && cpu) continue;
          found = std::string(p.deviceName) + (cpu ? " (CPU)" : "");
          break;
        }
    }
  }
  if (destroy) destroy(instance, nullptr);
  return found;
}
void DescribeStorage(Environment& r, const fs::path& root) {
  if (root.empty()) return;
  std::error_code ec;
  r.available = fs::space(root, ec).available;
  if (ec) r.available = 0;
  r.description += "Free / livre: " + std::to_string(r.available / (1024ull * 1024 * 1024)) + " GiB\r\n";
  const auto temp = root / ".local/tmp";
  fs::create_directories(temp, ec);
  if (!ec) {
    const auto file = temp / ("permission-" + std::to_string(CurrentProcess()));
#ifdef _WIN32
    FILE* handle = _wfopen(file.c_str(), L"wbx");
#else
    FILE* handle = std::fopen(file.c_str(), "wbx");
#endif
    r.writable = handle != nullptr;
    if (handle) { std::fclose(handle); fs::remove(file, ec); }
  }
  r.description += r.writable ? "Write / gravação: OK\r\n" : "Write / gravação: denied / negada\r\n";
}
}  // namespace x360n::desktop
