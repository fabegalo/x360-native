#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <fstream>
#include "environment_win32.h"
#include "catalog.h"
#include "child_process.h"
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace x360n::desktop {
Environment CheckEnvironment(const fs::path& root) {
  Environment r;
  SYSTEM_INFO sys{}; GetNativeSystemInfo(&sys);
  r.architecture = sys.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
  r.description = r.architecture ? "Windows x64\r\n" : "Unsupported architecture / arquitetura não validada\r\n";
  ComPtr<IDXGIFactory1> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
      if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
        r.graphics = true;
        r.description += "D3D12: " + Utf8(fs::path(desc.Description)) + "\r\n"; break;
      }
      adapter.Reset();
    }
  }
  if (!r.graphics) r.description += "D3D12 hardware device unavailable / dispositivo indisponível\r\n";
  if (root.empty()) return r;
  std::error_code ec;
  r.available = fs::space(root, ec).available;
  if (ec) r.available = 0;
  r.description += "Free / livre: " + std::to_string(r.available / (1024ull * 1024 * 1024)) + " GiB\r\n";
  auto temp = root / ".local/tmp";
  fs::create_directories(temp, ec);
  if (!ec) {
    auto file = temp / ("permission-" + std::to_string(GetCurrentProcessId()));
    auto handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    r.writable = handle != INVALID_HANDLE_VALUE;
    if (r.writable) CloseHandle(handle);
  }
  r.description += r.writable ? "Write / gravação: OK\r\n" : "Write / gravação: denied / negada\r\n";
  PWSTR program_files = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, nullptr, &program_files))) {
    auto vswhere = fs::path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe";
    CoTaskMemFree(program_files);
    if (fs::is_regular_file(vswhere) && r.writable) {
      auto out = temp / ("vswhere-" + std::to_string(GetCurrentProcessId()) + ".txt");
      ProcessSpec spec; spec.exe = vswhere;
      spec.args = {"-latest", "-version", "[17.0,18.0)", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"};
      spec.stdout_file = out; spec.stderr_file = out; spec.stderr_file += ".err";
      spec.no_console = true; spec.timeout = std::chrono::seconds(10);
      auto result = RunProcess(spec);
      std::ifstream text(out); std::string first; std::getline(text, first);
      r.msvc = result.launched && result.exit_code == 0 && !first.empty();
      text.close(); fs::remove(out, ec); fs::remove(spec.stderr_file, ec);
    }
  }
  r.description += r.msvc ? "MSVC 2022: OK\r\n" : "MSVC 2022: installation required / instalação necessária\r\n";
  return r;
}
}
