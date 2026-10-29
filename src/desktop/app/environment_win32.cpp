#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <fstream>
#include "environment.h"
#include "../model/catalog.h"
#include "child_process.h"
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
std::string Registry(HKEY key, const wchar_t* path, const wchar_t* name) {
  wchar_t value[512]{}; DWORD length = sizeof(value);
  if (RegGetValueW(key, path, name, RRF_RT_REG_SZ, nullptr, value, &length) != ERROR_SUCCESS) return {};
  return Utf8(fs::path(value));
}
}  // namespace
fs::path SystemTool(const std::string& name) {
  wchar_t sys[MAX_PATH]{}; GetSystemDirectoryW(sys, MAX_PATH);
  return fs::path(sys) / PathFromUtf8(name + ".exe");
}
fs::path SelfExecutable() {
  std::wstring self(32768, L'\0');
  self.resize(GetModuleFileNameW(nullptr, self.data(), DWORD(self.size())));
  return self;
}
unsigned long CurrentProcess() { return GetCurrentProcessId(); }
bool OpenPath(const fs::path& path) {
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
bool StartDetached(const fs::path& exe) {
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL)) > 32;
}
Command PreparationCommand(const fs::path& root, const fs::path& supervisor, const std::vector<std::string>& supervisor_args) {
  // cmd keeps the quotes of every argument because the command after /c does
  // not start with one ("call"). Characters cmd would expand stay refused.
  Command command{SystemTool("cmd"), {"/d", "/c", "call", Utf8(root / "scripts/devshell.cmd"), Utf8(supervisor)}};
  command.args.insert(command.args.end(), supervisor_args.begin(), supervisor_args.end());
  for (size_t i = 3; i < command.args.size(); ++i)
    if (command.args[i].find_first_of("%^\"!\r\n") != std::string::npos)
      throw std::runtime_error("O caminho do ambiente contém caracteres que o ambiente de compilação do Windows não aceita (% ^ \" !): " + command.args[i]);
  return command;
}
Environment CheckEnvironment(const fs::path& root) {
  Environment r;
  SYSTEM_INFO sys{}; GetNativeSystemInfo(&sys);
  r.architecture = sys.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
  r.description = r.architecture ? "Windows x64\r\n" : "Unsupported architecture / arquitetura não validada\r\n";
  r.cpu = Registry(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
  while (!r.cpu.empty() && r.cpu.back() == ' ') r.cpu.pop_back();
  MEMORYSTATUSEX memory{sizeof(memory)};
  if (GlobalMemoryStatusEx(&memory)) r.memory = std::to_string((memory.ullTotalPhys + (512ull << 20)) >> 30) + " GiB";
  ComPtr<IDXGIFactory1> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
      if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
        r.d3d12 = true; r.gpu = Utf8(fs::path(desc.Description));
        r.description += "D3D12: " + r.gpu + "\r\n"; break;
      }
      adapter.Reset();
    }
  }
  if (!r.d3d12) r.description += "D3D12 hardware device unavailable / dispositivo indisponível\r\n";
  const auto vulkan = ProbeVulkanDevice();
  r.vulkan = !vulkan.empty();
  r.description += r.vulkan ? "Vulkan: " + vulkan + "\r\n" : "Vulkan: unavailable / indisponível\r\n";
  r.graphics = r.d3d12;
  if (root.empty()) return r;
  DescribeStorage(r, root);
  const auto temp = root / ".local/tmp";
  PWSTR program_files = nullptr;
  bool msvc = false;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, nullptr, &program_files))) {
    // C++ tools alone do not include the Windows SDK. Check the headers,
    // libraries and resource compiler used by the native title build.
    bool windows_sdk = false;
    auto kits = fs::path(program_files) / "Windows Kits/10";
    wchar_t registered[32768]{}; DWORD length = sizeof(registered);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10",
                    RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, registered, &length) == ERROR_SUCCESS)
      kits = registered;
    if (fs::is_directory(kits / "Include")) {
      for (const auto& version : fs::directory_iterator(kits / "Include")) {
        auto id = version.path().filename();
        if (fs::is_regular_file(version.path() / "um/Windows.h") &&
            fs::is_regular_file(version.path() / "um/d3d12.h") &&
            fs::is_regular_file(kits / "Lib" / id / "um/x64/kernel32.lib") &&
            fs::is_regular_file(kits / "Lib" / id / "ucrt/x64/ucrt.lib") &&
            fs::is_regular_file(kits / "bin" / id / "x64/rc.exe")) { windows_sdk = true; break; }
      }
    }
    r.description += windows_sdk ? "Windows SDK: OK\r\n" : "Windows SDK: installation required / instalação necessária\r\n";
    if (!windows_sdk) r.missing_tools.push_back("Windows SDK");
    auto vswhere = fs::path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe";
    CoTaskMemFree(program_files);
    if (fs::is_regular_file(vswhere) && r.writable) {
      std::error_code ec;
      auto out = temp / ("vswhere-" + std::to_string(GetCurrentProcessId()) + ".txt");
      ProcessSpec spec; spec.exe = vswhere;
      spec.args = {"-latest", "-version", "[17.0,18.0)", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"};
      spec.stdout_file = out; spec.stderr_file = out; spec.stderr_file += ".err";
      spec.no_console = true; spec.timeout = std::chrono::seconds(10);
      auto result = RunProcess(spec);
      std::ifstream text(out); std::string first; std::getline(text, first);
      msvc = windows_sdk && result.launched && result.exit_code == 0 && !first.empty();
      text.close(); fs::remove(out, ec); fs::remove(spec.stderr_file, ec);
    }
  }
  if (!msvc) r.missing_tools.push_back("MSVC 2022");
  r.toolchain = msvc;
  r.description += msvc ? "MSVC 2022: OK\r\n" : "MSVC 2022: installation required / instalação necessária\r\n";
  return r;
}
}  // namespace x360n::desktop
