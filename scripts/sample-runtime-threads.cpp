// Diagnostic only: wall-clock instruction-pointer samples, including waits.
// Suspends each owned benchmark thread only to read CONTEXT_CONTROL, then
// resumes it before symbol lookup or I/O. Never use this run as performance
// evidence. Arguments: PID, duration (1..30 seconds), local PDB search path.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
struct T {
  DWORD id;
  HANDLE h;
  unsigned long long first, last;
};
unsigned long long cpu(HANDLE h) {
  FILETIME c, e, k, u;
  if (!GetThreadTimes(h, &c, &e, &k, &u))
    return 0;
  return ((unsigned long long)k.dwHighDateTime << 32) + k.dwLowDateTime +
         ((unsigned long long)u.dwHighDateTime << 32) + u.dwLowDateTime;
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  DWORD pid = strtoul(argv[1], 0, 10);
  int seconds = atoi(argv[2]);
  if (seconds < 1 || seconds > 30)
    return 2;
  HANDLE p = OpenProcess(
      PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
  if (!p) {
    fprintf(stderr, "OpenProcess %lu\n", GetLastError());
    return 3;
  }
  std::vector<T> ts;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 te{sizeof(te)};
  if (Thread32First(snap, &te))
    do {
      if (te.th32OwnerProcessID == pid) {
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                  THREAD_QUERY_INFORMATION,
                              FALSE, te.th32ThreadID);
        if (h) {
          auto c = cpu(h);
          ts.push_back({te.th32ThreadID, h, c, c});
        }
      }
    } while (Thread32Next(snap, &te));
  CloseHandle(snap);
  std::map<std::pair<DWORD, DWORD64>, unsigned> counts;
  unsigned errors = 0;
  ULONGLONG begin = GetTickCount64();
  while (GetTickCount64() - begin < (unsigned)seconds * 1000 &&
         WaitForSingleObject(p, 0) == WAIT_TIMEOUT) {
    for (auto &t : ts) {
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_CONTROL;
      if (SuspendThread(t.h) == DWORD(-1)) {
        ++errors;
        continue;
      }
      BOOL ok = GetThreadContext(t.h, &ctx);
      ResumeThread(t.h);
      if (ok)
        ++counts[{t.id, ctx.Rip}];
      else
        ++errors;
    }
    Sleep(11);
  }
  for (auto &t : ts)
    t.last = cpu(t.h);
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(p, argv[3], TRUE)) {
    fprintf(stderr, "SymInitialize %lu\n", GetLastError());
    return 4;
  }
  printf("kind,thread,cpu_ms,samples,module,symbol,address,line\n");
  for (auto &t : ts)
    printf("thread,%lu,%.3f,0,,,0,0\n", t.id, (t.last - t.first) / 10000.0);
  for (auto &[key, n] : counts) {
    alignas(SYMBOL_INFO) char b[sizeof(SYMBOL_INFO) + 1024]{};
    auto *s = (SYMBOL_INFO *)b;
    s->SizeOfStruct = sizeof(SYMBOL_INFO);
    s->MaxNameLen = 1024;
    DWORD64 disp = 0;
    IMAGEHLP_MODULE64 m{};
    m.SizeOfStruct = sizeof(m);
    SymGetModuleInfo64(p, key.second, &m);
    bool ok = SymFromAddr(p, key.second, &disp, s);
    IMAGEHLP_LINE64 l{};
    l.SizeOfStruct = sizeof(l);
    DWORD ld = 0;
    SymGetLineFromAddr64(p, key.second, &ld, &l);
    printf("sample,%lu,0,%u,\"%s\",\"%s\",0x%llx,%lu\n", key.first, n,
           m.ModuleName, ok ? s->Name : "?", key.second, l.LineNumber);
  }
  fprintf(stderr, "threads=%zu errors=%u wall_ms=%llu\n", ts.size(), errors,
          GetTickCount64() - begin);
  SymCleanup(p);
  for (auto &t : ts)
    CloseHandle(t.h);
  CloseHandle(p);
}
