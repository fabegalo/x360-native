#include "child_process.h"

#include <fmt/format.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace x360n {
namespace {

std::wstring Utf8ToWide(const std::string& s) {
#ifdef _WIN32
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
#else
  return std::wstring(s.begin(), s.end());
#endif
}

}  // namespace

std::wstring BuildCommandLine(const std::filesystem::path& exe, const std::vector<std::string>& args) {
  auto quote = [](const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
      size_t backslashes = 0;
      while (i < arg.size() && arg[i] == L'\\') {
        ++i;
        ++backslashes;
      }
      if (i == arg.size()) {
        out.append(backslashes * 2, L'\\');  // they precede the closing quote
        break;
      }
      if (arg[i] == L'"') {
        out.append(backslashes * 2 + 1, L'\\');
      } else {
        out.append(backslashes, L'\\');
      }
      out.push_back(arg[i]);
    }
    out.push_back(L'"');
    return out;
  };
  std::wstring cmd = quote(exe.wstring());
  for (const auto& a : args) {
    cmd.push_back(L' ');
    cmd += quote(Utf8ToWide(a));
  }
  return cmd;
}

std::string DescribeExitCode(uint32_t code) {
  switch (code) {
    case 0: return "success";
    case 0x80000003: return "STATUS_BREAKPOINT (assert/__debugbreak with no debugger attached)";
    case 0xC0000005: return "STATUS_ACCESS_VIOLATION";
    case 0xC000001D: return "STATUS_ILLEGAL_INSTRUCTION";
    case 0xC0000094: return "STATUS_INTEGER_DIVIDE_BY_ZERO";
    case 0xC00000FD: return "STATUS_STACK_OVERFLOW";
    case 0xC0000135: return "STATUS_DLL_NOT_FOUND";
    case 0xC0000139: return "STATUS_ENTRYPOINT_NOT_FOUND";
    case 0xC0000142: return "STATUS_DLL_INIT_FAILED";
    case 0xC000013A: return "STATUS_CONTROL_C_EXIT";
    case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (also __fastfail / abort)";
    default: break;
  }
  if ((code & 0xC0000000u) == 0xC0000000u) return fmt::format("NTSTATUS error 0x{:08X}", code);
  return fmt::format("exit code {}", int32_t(code));
}

#ifdef _WIN32

namespace {

struct Handle {
  HANDLE h = nullptr;
  Handle() = default;
  explicit Handle(HANDLE v) : h(v == INVALID_HANDLE_VALUE ? nullptr : v) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() {
    if (h) CloseHandle(h);
  }
  explicit operator bool() const { return h != nullptr; }
};

std::string LastError(const char* what) {
  return fmt::format("{} failed (Win32 error {})", what, GetLastError());
}

HANDLE OpenForChild(const std::filesystem::path& p, bool write) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  if (write) {
    return CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  return CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, nullptr);
}

}  // namespace

ProcessResult RunProcess(const ProcessSpec& spec) {
  ProcessResult r;
  Handle out(OpenForChild(spec.stdout_file, true));
  Handle err(OpenForChild(spec.stderr_file, true));
  Handle in(OpenForChild({}, false));
  if (!out || !err || !in) {
    r.launch_error = LastError("opening log files");
    return r;
  }

  Handle job(CreateJobObjectW(nullptr, nullptr));
  if (!job) {
    r.launch_error = LastError("CreateJobObjectW");
    return r;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    r.launch_error = LastError("SetInformationJobObject");
    return r;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in.h;
  si.hStdOutput = out.h;
  si.hStdError = err.h;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = BuildCommandLine(spec.exe, spec.args);
  std::wstring cwd = spec.working_dir.empty() ? std::wstring() : spec.working_dir.wstring();

  // Crash dialogs would block unattended runs; the error mode is inherited.
  UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  auto start = std::chrono::steady_clock::now();
  BOOL ok = CreateProcessW(spec.exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                           CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
                           cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
  SetErrorMode(old_mode);
  if (!ok) {
    r.launch_error = LastError("CreateProcessW");
    return r;
  }
  Handle process(pi.hProcess);
  Handle thread(pi.hThread);
  if (!AssignProcessToJobObject(job.h, process.h)) {
    r.launch_error = LastError("AssignProcessToJobObject");
    TerminateProcess(process.h, 1);
    return r;
  }
  ResumeThread(thread.h);
  r.launched = true;

  DWORD wait_ms = spec.timeout.count() > 0 ? DWORD(spec.timeout.count()) : INFINITE;
  if (WaitForSingleObject(process.h, wait_ms) == WAIT_TIMEOUT) {
    r.timed_out = true;
    TerminateJobObject(job.h, 1);
    WaitForSingleObject(process.h, 10000);
  }
  r.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  DWORD code = 0;
  GetExitCodeProcess(process.h, &code);
  r.exit_code = code;
  return r;
}

#else

ProcessResult RunProcess(const ProcessSpec&) {
  ProcessResult r;
  r.launch_error = "process supervision is only implemented for Windows in this revision";
  return r;
}

#endif

}  // namespace x360n
