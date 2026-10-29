#include "child_process.h"

#include <fmt/format.h>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include "action_trace.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// DbgHelp after windows.h.
#include <dbghelp.h>
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
#ifdef _WIN32
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
#endif
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

std::string WideToUtf8(const wchar_t* w) {
  if (!w || !*w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

// Called while the child is stopped at a second-chance exception: every thread
// is suspended by the debug API, so its memory and the thread context are stable.
void CaptureCrash(HANDLE process, const DEBUG_EVENT& ev, const std::filesystem::path& exe,
                  uint64_t guest_base, CrashInfo& crash) {
  const EXCEPTION_RECORD& er = ev.u.Exception.ExceptionRecord;
  crash.captured = true;
  crash.code = er.ExceptionCode;
  crash.address = uint64_t(reinterpret_cast<uintptr_t>(er.ExceptionAddress));
  crash.thread_id = ev.dwThreadId;
  if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2) {
    const char* kind = er.ExceptionInformation[0] == 0   ? "read"
                       : er.ExceptionInformation[0] == 8 ? "execute"
                                                         : "write";
    crash.access = fmt::format("{} 0x{:016X}", kind, uint64_t(er.ExceptionInformation[1]));
  }

  Handle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ev.dwThreadId));
  if (!thread) {
    crash.frames.push_back(LastError("OpenThread"));
    return;
  }
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(thread.h, &ctx)) {
    crash.frames.push_back(LastError("GetThreadContext"));
    return;
  }

  if (guest_base && guest_base <= UINT64_MAX - UINT32_MAX) {
#if defined(_M_ARM64) || defined(__aarch64__)
    for (unsigned i = 0; i < 29; ++i)
      crash.native_registers.emplace_back("x" + std::to_string(i), ctx.X[i]);
    crash.native_registers.insert(crash.native_registers.end(),
        {{"fp", ctx.Fp}, {"lr", ctx.Lr}, {"sp", ctx.Sp}, {"pc", ctx.Pc}});
#else
    crash.native_registers = {{"rax",ctx.Rax},{"rbx",ctx.Rbx},{"rcx",ctx.Rcx},{"rdx",ctx.Rdx},
        {"rsi",ctx.Rsi},{"rdi",ctx.Rdi},{"rbp",ctx.Rbp},{"rsp",ctx.Rsp},
        {"r8",ctx.R8},{"r9",ctx.R9},{"r10",ctx.R10},{"r11",ctx.R11},
        {"r12",ctx.R12},{"r13",ctx.R13},{"r14",ctx.R14},{"r15",ctx.R15}};
#endif
    std::unordered_set<uint64_t> seen;
    unsigned attempts = 0;
    auto capture = [&](uint64_t address) {
      if (crash.memory.size() >= 64 || attempts >= 1024 || address < 4096 ||
          address > UINT64_MAX - 256 || !seen.insert(address).second) return;
      ++attempts;
      CrashInfo::MemoryBlock block{address,std::vector<uint8_t>(256)};
      SIZE_T count = 0;
      if (ReadProcessMemory(process,reinterpret_cast<const void*>(uintptr_t(address)),
                            block.bytes.data(),block.bytes.size(),&count) && count == block.bytes.size())
        crash.memory.push_back(std::move(block));
    };
    // Native register roots may contain the live AOT PPCContext or a stack
    // frame. Inspect them before StackWalk mutates the native CONTEXT.
    for (const auto& [name,value] : crash.native_registers) capture(value);
    const size_t roots = crash.memory.size();
    for (size_t root = 0; root < roots; ++root) {
      // A PPCContext stores its GPR values as native 64-bit words. Snapshot
      // possible guest pointers without assuming which host register holds it.
      const auto bytes = crash.memory[root].bytes;
      for (size_t offset = 0; offset + 8 <= bytes.size(); offset += 8) {
        uint64_t value = 0; std::memcpy(&value,bytes.data()+offset,8);
        if (value >= 4096 && value <= UINT32_MAX) capture(guest_base+value);
      }
    }
    const size_t direct = crash.memory.size();
    for (size_t block = roots; block < direct; ++block) {
      // Guest objects contain big-endian pointers. Follow one level only,
      // with the same hard record and attempt bounds, including failed reads.
      const auto bytes = crash.memory[block].bytes;
      for (size_t offset = 0; offset + 4 <= bytes.size(); offset += 4) {
        const uint32_t value = uint32_t(bytes[offset])<<24 | uint32_t(bytes[offset+1])<<16 |
            uint32_t(bytes[offset+2])<<8 | bytes[offset+3];
        if (value >= 4096) capture(guest_base+value);
      }
    }
  }

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
  std::wstring search = exe.parent_path().wstring();
  if (!SymInitializeW(process, search.c_str(), TRUE)) {  // TRUE: enumerate the loaded modules
    crash.frames.push_back(LastError("SymInitializeW"));
    return;
  }
  STACKFRAME64 frame{};
#if defined(_M_ARM64) || defined(__aarch64__)
  constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
  frame.AddrPC.Offset = ctx.Pc;
  frame.AddrFrame.Offset = ctx.Fp;
  frame.AddrStack.Offset = ctx.Sp;
#else
  constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrStack.Offset = ctx.Rsp;
#endif
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  alignas(SYMBOL_INFOW) char sym_buf[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)];
  for (int i = 0; i < 64; ++i) {
    if (!StackWalk64(machine, process, thread.h, &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        frame.AddrPC.Offset == 0) {
      break;
    }
    DWORD64 pc = frame.AddrPC.Offset;
    std::string line = fmt::format("#{:<2} 0x{:016X}", i, pc);
    IMAGEHLP_MODULEW64 mod{};
    mod.SizeOfStruct = sizeof(mod);
    std::string module = SymGetModuleInfoW64(process, pc, &mod) ? WideToUtf8(mod.ModuleName) : "?";
    auto* sym = reinterpret_cast<SYMBOL_INFOW*>(sym_buf);
    *sym = SYMBOL_INFOW{};
    sym->SizeOfStruct = sizeof(SYMBOL_INFOW);
    sym->MaxNameLen = 512;
    DWORD64 disp = 0;
    if (SymFromAddrW(process, pc, &disp, sym)) {
      line += fmt::format(" {}!{}+0x{:X}", module, WideToUtf8(sym->Name), disp);
    } else {
      line += fmt::format(" {}+0x{:X}", module, pc - (mod.BaseOfImage ? mod.BaseOfImage : pc));
    }
    IMAGEHLP_LINEW64 src{};
    src.SizeOfStruct = sizeof(src);
    DWORD line_disp = 0;
    if (SymGetLineFromAddrW64(process, pc, &line_disp, &src)) {
      line += fmt::format(" ({}:{})", WideToUtf8(src.FileName), src.LineNumber);
    }
    crash.frames.push_back(std::move(line));
  }
  SymCleanup(process);
}

}  // namespace

ProcessResult RunProcess(const ProcessSpec& spec) {
  ActionTrace trace("process.wait");
  ProcessResult r;
  auto cancelled = [&] {
    return (spec.cancel_event && WaitForSingleObject(reinterpret_cast<HANDLE>(spec.cancel_event), 0) == WAIT_OBJECT_0) ||
           (spec.should_cancel && spec.should_cancel());
  };
  if (cancelled()) { r.cancelled = true; return r; }
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
  DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
  if (spec.no_console) flags |= CREATE_NO_WINDOW;
  if (spec.capture_crash) flags |= DEBUG_ONLY_THIS_PROCESS;
  BOOL ok = CreateProcessW(spec.exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, flags, nullptr,
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
  ActionTrace::Event("process.created", start);
  if (spec.on_started) spec.on_started();

  if (spec.capture_crash) {
    // Debug loop: the debug events must be pumped by this thread until the
    // child exits; a timeout kills the job and keeps pumping until the exit event.
    auto deadline = spec.timeout.count() > 0 ? start + spec.timeout
                                             : std::chrono::steady_clock::time_point::max();
    bool initial_breakpoint = true;
    for (bool exited = false; !exited;) {
      if (!r.cancelled && !r.timed_out) {
        r.cancelled = cancelled();
        r.timed_out = !r.cancelled && std::chrono::steady_clock::now() >= deadline;
        if (r.cancelled || r.timed_out) TerminateJobObject(job.h, 1);
      }
      if (spec.on_tick) spec.on_tick();
      if (spec.notification_event && WaitForSingleObject(reinterpret_cast<HANDLE>(spec.notification_event), 0) == WAIT_OBJECT_0 && spec.on_notification) spec.on_notification();
      DEBUG_EVENT ev{};
      if (!WaitForDebugEvent(&ev, 100)) {
        if (!r.timed_out && std::chrono::steady_clock::now() >= deadline) {
          r.timed_out = true;
          TerminateJobObject(job.h, 1);
        }
        continue;
      }
      DWORD cont = DBG_CONTINUE;
      switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
          if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
          break;
        case LOAD_DLL_DEBUG_EVENT:
          if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
          break;
        case EXCEPTION_DEBUG_EVENT:
          if (initial_breakpoint && ev.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
            initial_breakpoint = false;  // the loader's breakpoint for an attached debugger
            break;
          }
          cont = DBG_EXCEPTION_NOT_HANDLED;  // let the child's own handlers run
          if (!ev.u.Exception.dwFirstChance && !r.crash.captured) {
            CaptureCrash(process.h, ev, spec.exe, spec.crash_guest_memory_base, r.crash);
          }
          break;
        case EXIT_PROCESS_DEBUG_EVENT:
          if (ev.dwProcessId == pi.dwProcessId) exited = true;
          break;
        default:
          break;
      }
      ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    WaitForSingleObject(process.h, 10000);
  } else {
    const auto deadline = spec.timeout.count() > 0 ? start + spec.timeout
        : std::chrono::steady_clock::time_point::max();
    HANDLE waits[]{process.h, nullptr, nullptr};
    DWORD count = 1, cancel_index = MAXDWORD, notification_index = MAXDWORD;
    if (spec.cancel_event) { cancel_index = count; waits[count++] = reinterpret_cast<HANDLE>(spec.cancel_event); }
    if (spec.notification_event) { notification_index = count; waits[count++] = reinterpret_cast<HANDLE>(spec.notification_event); }
    for (;;) {
      const auto remaining = spec.timeout.count() > 0 ?
          std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()) : int64_t(INFINITE);
      const auto interval = (spec.should_cancel || spec.on_tick || spec.capture_crash) ? std::min<int64_t>(100, remaining) : remaining;
      const auto wait = WaitForMultipleObjects(count, waits, FALSE,
          static_cast<DWORD>(std::min<int64_t>(interval, INFINITE)));
      if (wait == WAIT_OBJECT_0) break;
      if (notification_index != MAXDWORD && wait == WAIT_OBJECT_0 + notification_index) {
        if (spec.on_notification) spec.on_notification();
        if (!cancelled() && std::chrono::steady_clock::now() < deadline) continue;
      }
      if (wait == WAIT_FAILED) { r.launch_error = LastError("waiting for child"); TerminateJobObject(job.h, 1); WaitForSingleObject(process.h, 10000); break; }
      r.cancelled = (cancel_index != MAXDWORD && wait == WAIT_OBJECT_0 + cancel_index) || cancelled();
      r.timed_out = !r.cancelled && std::chrono::steady_clock::now() >= deadline;
      if (r.cancelled || r.timed_out) {
        TerminateJobObject(job.h, 1);
        WaitForSingleObject(process.h, 10000);
        break;
      }
      if (spec.on_tick) spec.on_tick();
    }
  }
  ActionTrace::Event("process.exited");
  r.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  DWORD code = 0;
  GetExitCodeProcess(process.h, &code);
  r.exit_code = code;
  if (spec.collect_metrics) {
    JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION accounting{};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory{};
    if (QueryInformationJobObject(job.h, JobObjectBasicAndIoAccountingInformation, &accounting, sizeof(accounting), nullptr) &&
        QueryInformationJobObject(job.h, JobObjectExtendedLimitInformation, &memory, sizeof(memory), nullptr)) {
      r.metrics = ProcessResult::Metrics{
          uint64_t(accounting.BasicInfo.TotalUserTime.QuadPart + accounting.BasicInfo.TotalKernelTime.QuadPart) / 10,
          accounting.IoInfo.ReadTransferCount, accounting.IoInfo.ReadOperationCount,
          accounting.IoInfo.WriteTransferCount, uint64_t(memory.PeakJobMemoryUsed)};
    }
  }
  return r;
}

#endif

}  // namespace x360n
