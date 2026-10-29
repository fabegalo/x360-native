// Native Linux / Darwin backend of the shared supervisor API. No shell and no
// application code between fork and exec: posix_spawn is safe with UI workers.
#include "child_process.h"
#include "action_trace.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <map>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <crt_externs.h>
#endif

#ifndef __APPLE__
extern char** environ;
#endif

namespace x360n {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string Error(const char* operation, int error = errno) {
  return std::string(operation) + ": " + std::strerror(error) + " (" + std::to_string(error) + ")";
}

// Keep the group leader waitable until group cleanup completes. Reaping it
// first would allow its PID / process group ID to be recycled before killpg.
class Child {
 public:
  explicit Child(pid_t pid, pid_t group) : pid_(pid), group_(group) {}
  ~Child() { if (pid_ > 0) { Signal(SIGKILL); Reap(); } }
  void Signal(int signal) const noexcept { kill(group_ > 0 ? -group_ : pid_, signal); }
  int Reap() noexcept {
    int status = 0;
    pid_t result;
    do { result = waitpid(pid_, &status, 0); } while (result < 0 && errno == EINTR);
    pid_ = -1;
    return result < 0 ? -1 : status;
  }
 private:
  pid_t pid_;
  pid_t group_;
};

// A sleeping group leader owns the PGID until cleanup. Its lifetime pipe is
// close-on-exec, so supervisor death (including SIGKILL) closes the only writer
// and kills the group. No polling, detached daemon, or recyclable PID window.
// After fork the child uses only async-signal-safe POSIX operations and _exit.
class ProcessGroup {
 public:
  explicit ProcessGroup(bool create) {
    if (!create) return;
    const long descriptor_limit=sysconf(_SC_OPEN_MAX);
    if (descriptor_limit<5) throw std::runtime_error("Cannot identify process descriptor limit");
    int life[2]{-1,-1}, ready[2]{-1,-1};
    const auto pipe_fds=[](int* fds) {
#ifdef __linux__
      if (pipe2(fds,O_CLOEXEC)<0) throw std::runtime_error(Error("lifetime pipe"));
#else
      if (pipe(fds)<0) throw std::runtime_error(Error("lifetime pipe"));
      for (unsigned i=0;i<2;++i) if (fcntl(fds[i],F_SETFD,FD_CLOEXEC)<0) {
        close(fds[0]);close(fds[1]);fds[0]=fds[1]=-1;throw std::runtime_error(Error("lifetime pipe flags"));
      }
#endif
    };
    try {
      pipe_fds(life); pipe_fds(ready);
      pid_=fork();
      if (pid_<0) throw std::runtime_error(Error("process group guardian"));
      if (pid_==0) {
        close(life[1]); close(ready[0]);
        const int lifetime_copy=fcntl(life[0],F_DUPFD_CLOEXEC,5);
        const int ready_copy=fcntl(ready[1],F_DUPFD_CLOEXEC,5);
        if (lifetime_copy<0 || ready_copy<0 || dup2(lifetime_copy,3)<0 || dup2(ready_copy,4)<0) _exit(125);
        close(0);close(1);close(2);
        bool closed=false;
#if defined(__linux__) && defined(SYS_close_range)
        closed=syscall(SYS_close_range,5u,~0u,0u)==0;
#endif
        if (!closed) for (long fd=5;fd<descriptor_limit;++fd) close(int(fd));
        if (setpgid(0,0)<0) _exit(125);
        struct sigaction ignore{}; ignore.sa_handler=SIG_IGN; sigemptyset(&ignore.sa_mask);
        sigaction(SIGTERM,&ignore,nullptr); sigaction(SIGINT,&ignore,nullptr); sigaction(SIGHUP,&ignore,nullptr);
        if (write(4,"x",1)!=1) _exit(125);
        close(4);
        char buffer[8]; ssize_t count;
        do { count=read(3,buffer,sizeof(buffer)); } while (count>0 || (count<0 && errno==EINTR));
        kill(-getpid(),SIGKILL); _exit(125);
      }
      close(life[0]); life[0]=-1; close(ready[1]); ready[1]=-1;
      pollfd event{ready[0],POLLIN,0}; int status;
      do { status=poll(&event,1,3000); } while (status<0 && errno==EINTR);
      char ack=0;
      if (status<=0 || read(ready[0],&ack,1)!=1 || ack!='x') throw std::runtime_error("Could not establish supervised process group");
      close(ready[0]); ready[0]=-1; lifetime_=life[1]; life[1]=-1;
    } catch (...) {
      for (int fd : {life[0],life[1],ready[0],ready[1]}) if (fd>=0) close(fd);
      if (pid_>0) { kill(pid_,SIGKILL); int status; while(waitpid(pid_,&status,0)<0 && errno==EINTR) {} }
      throw;
    }
  }
  ~ProcessGroup() {
    if (lifetime_>=0) close(lifetime_);
    if (pid_>0) { int status; while(waitpid(pid_,&status,0)<0 && errno==EINTR) {} }
  }
  pid_t id() const { return pid_>0 ? pid_ : 0; }
 private:
  pid_t pid_=-1;
  int lifetime_=-1;
};

// Wait on actual process exit when supported, avoiding periodic wakeups while
// a title runs. Old Linux kernels fall back to bounded waitid polling.
class ExitWatch {
 public:
  explicit ExitWatch(pid_t pid) {
#if defined(__linux__) && defined(SYS_pidfd_open)
    fd_ = int(syscall(SYS_pidfd_open, pid, 0));
#elif defined(__APPLE__)
    fd_ = kqueue();
    if (fd_ < 0) return;
    if (fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0) { close(fd_); fd_ = -1; return; }
    struct kevent event;
    EV_SET(&event, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (kevent(fd_, &event, 1, nullptr, 0, nullptr) < 0) { close(fd_); fd_ = -1; }
#endif
  }
  ~ExitWatch() { if (fd_ >= 0) close(fd_); }
  int fd() const { return fd_; }
 private:
  int fd_ = -1;
};

class SpawnSetup {
 public:
  SpawnSetup() {
    const int result = posix_spawn_file_actions_init(&actions);
    if (result) throw std::runtime_error(Error("spawn file actions", result));
    const int attributes_result = posix_spawnattr_init(&attributes);
    if (attributes_result) {
      posix_spawn_file_actions_destroy(&actions);
      throw std::runtime_error(Error("spawn attributes", attributes_result));
    }
  }
  ~SpawnSetup() {
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
  }
  posix_spawn_file_actions_t actions{};
  posix_spawnattr_t attributes{};
};

// POSIX wait handles are borrowed nonblocking pipe/FIFO read descriptors + 1;
// zero retains its existing meaning of "no event". Never close them here.
int EventFd(uintptr_t handle) {
  if (handle > uintptr_t(INT_MAX)) throw std::runtime_error("Invalid POSIX process event");
  return handle ? int(handle - 1) : -1;
}

bool ContainsNull(const std::string& value) { return value.find('\0') != std::string::npos; }

struct LogFile {
  int fd = -1;
  explicit LogFile(const std::string& path) {
    // Open nonblocking before checking the type: a substituted FIFO must not
    // block posix_spawn before the timeout/cancellation loop even starts.
    int opened = open(path.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (opened < 0) throw std::runtime_error(Error("open process log"));
    const auto fail = [&](const std::string& message) { close(opened); throw std::runtime_error(message); };
    struct stat info{};
    if (fstat(opened, &info) < 0) fail(Error("stat process log"));
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1) fail("Process logs must be ordinary files without hardlink aliases");
    if (ftruncate(opened, 0) < 0) fail(Error("truncate process log"));
    // File actions use standard descriptors 0/1/2. Keep our source descriptors
    // out of that range even if the supervisor itself was started without stdio.
    if (opened < 3) {
      const int duplicate = fcntl(opened, F_DUPFD_CLOEXEC, 3);
      if (duplicate < 0) fail(Error("duplicate process log"));
      close(opened); opened = duplicate;
    }
    fd = opened;
  }
  ~LogFile() { if (fd >= 0) close(fd); }
};
}  // namespace

ProcessResult RunProcess(const ProcessSpec& spec) {
  ActionTrace trace("process.wait");
  ProcessResult result;
  Clock::time_point started{};
  if (spec.should_cancel && spec.should_cancel()) { result.cancelled = true; return result; }
  try {
    if (spec.capture_crash || spec.crash_guest_memory_base)
      throw std::runtime_error("Native crash stack capture is not implemented on POSIX; no process launched");
    if (spec.collect_metrics)
      throw std::runtime_error("Process tree resource accounting is not implemented on POSIX; no process launched");
    if (spec.timeout < 0ms) throw std::runtime_error("Negative process timeout");
    const int cancel_fd = EventFd(spec.cancel_event), notification_fd = EventFd(spec.notification_event);
    for (int fd : {cancel_fd, notification_fd}) {
      if (fd >= 0) {
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || !(flags & O_NONBLOCK)) throw std::runtime_error("POSIX process events must be valid nonblocking descriptors");
      }
    }
    const auto cancelled = [&] {
      if (spec.should_cancel && spec.should_cancel()) return true;
      pollfd event{cancel_fd, POLLIN, 0};
      if (cancel_fd < 0 || poll(&event, 1, 0) <= 0) return false;
      // Closing a cancellation channel also cancels; never busy-loop on HUP.
      return bool(event.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL));
    };
    if (cancelled()) { result.cancelled = true; return result; }
    const auto exe = std::filesystem::absolute(spec.exe).native();
    const auto cwd = spec.working_dir.native();
    const auto out = std::filesystem::absolute(spec.stdout_file).native();
    const auto err = std::filesystem::absolute(spec.stderr_file).native();
    if (spec.exe.empty() || spec.stdout_file.empty() || spec.stderr_file.empty() ||
        ContainsNull(exe) || ContainsNull(cwd) || ContainsNull(out) || ContainsNull(err) ||
        std::any_of(spec.args.begin(), spec.args.end(), ContainsNull))
      throw std::runtime_error("Invalid process path or argument");
    if (out == err) throw std::runtime_error("Process stdout and stderr require separate files");
    struct sigaction child_action{};
    if (sigaction(SIGCHLD, nullptr, &child_action) < 0) throw std::runtime_error(Error("query SIGCHLD"));
    if (child_action.sa_handler == SIG_IGN || (child_action.sa_flags & SA_NOCLDWAIT))
      throw std::runtime_error("Process supervision requires waitable children (SIGCHLD)");

    std::vector<char*> argv;
    argv.reserve(spec.args.size() + 2);
    argv.push_back(const_cast<char*>(exe.c_str()));
    for (const auto& arg : spec.args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    SpawnSetup setup;
    LogFile stdout_log(out), stderr_log(err);
    const auto check = [](int code, const char* operation) {
      if (code) throw std::runtime_error(Error(operation, code));
    };
    check(posix_spawn_file_actions_addopen(&setup.actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0), "redirect stdin");
    check(posix_spawn_file_actions_adddup2(&setup.actions, stdout_log.fd, STDOUT_FILENO), "redirect stdout");
    check(posix_spawn_file_actions_adddup2(&setup.actions, stderr_log.fd, STDERR_FILENO), "redirect stderr");
    if (!cwd.empty()) check(posix_spawn_file_actions_addchdir_np(&setup.actions, cwd.c_str()), "set working directory");
#if defined(__linux__)
    check(posix_spawn_file_actions_addclosefrom_np(&setup.actions, 3), "close inherited descriptors");
#endif
    sigset_t empty, defaults;
    sigemptyset(&empty); sigfillset(&defaults);
    check(posix_spawnattr_setsigmask(&setup.attributes, &empty), "set signal mask");
    check(posix_spawnattr_setsigdefault(&setup.attributes, &defaults), "set signal defaults");
    check(posix_spawnattr_setpgroup(&setup.attributes, 0), "set process group");
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (!spec.inherit_process_group) flags |= POSIX_SPAWN_SETPGROUP;
#ifdef __APPLE__
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    check(posix_spawnattr_setflags(&setup.attributes, flags), "set spawn flags");
#ifdef __APPLE__
    char** environment = *_NSGetEnviron();
#else
    char** environment = environ;
#endif
    std::map<std::string, std::string> variables;
    std::vector<std::string> environment_values;
    std::vector<char*> environment_pointers;
    if (!spec.environment.empty()) {
      for (auto** current = environment; *current; ++current) {
        const std::string entry(*current); const auto equals = entry.find('=');
        if (equals != std::string::npos) variables[entry.substr(0, equals)] = entry.substr(equals + 1);
      }
      for (const auto& [key, value] : spec.environment) {
        if (key.empty() || key.find_first_of("=\0", 0, 2) != std::string::npos || ContainsNull(value))
          throw std::runtime_error("Invalid child environment override");
        variables[key] = value;
      }
      for (const auto& [key, value] : variables) environment_values.push_back(key + "=" + value);
      for (auto& value : environment_values) environment_pointers.push_back(value.data());
      environment_pointers.push_back(nullptr); environment = environment_pointers.data();
    }
    const auto start = Clock::now();
    started = start;
    ProcessGroup group(!spec.inherit_process_group);
    if (group.id()) check(posix_spawnattr_setpgroup(&setup.attributes,group.id()),"set supervised group");
    pid_t pid = -1;
    check(posix_spawn(&pid, exe.c_str(), &setup.actions, &setup.attributes, argv.data(), environment), "posix_spawn");
    Child child(pid,group.id());
    result.launched = true;
    ActionTrace::Event("process.created", start);
    ExitWatch watch(pid);
    if (spec.on_started) spec.on_started();
    auto deadline = spec.timeout > 0ms ? start + spec.timeout : Clock::time_point::max();
    auto next_tick = start;
    auto stop_deadline = Clock::time_point::max();
    bool stopping = false;
    int notification = notification_fd;
    for (;;) {
      siginfo_t info{};
      int rc;
      do { rc = waitid(P_PID, id_t(pid), &info, WEXITED | WNOHANG | WNOWAIT); } while (rc < 0 && errno == EINTR);
      if (rc < 0) throw std::runtime_error(Error("waitid"));
      if (info.si_pid == pid) break;
      const auto now = Clock::now();
      if (!stopping) {
        result.cancelled = cancelled();
        result.timed_out = !result.cancelled && now >= deadline;
        if (result.cancelled || result.timed_out) {
          // Give the whole group a bounded opportunity to flush, then force it
          // down even if descendants ignore SIGTERM or the leader exits first.
          child.Signal(SIGTERM);
          stopping = true;
          stop_deadline = now + 250ms;
        }
      }
      if (stopping && now >= stop_deadline) { child.Signal(SIGKILL); stop_deadline = Clock::time_point::max(); }
      if (spec.on_tick && now >= next_tick) { spec.on_tick(); next_tick = now + 100ms; }
      auto wake = stopping ? stop_deadline : deadline;
      if (spec.should_cancel || spec.on_tick) wake = std::min(wake, now + 100ms);
      // Fallback only for hosts without a pollable process-exit primitive.
      if (watch.fd() < 0) wake = std::min(wake, now + 10ms);
      const auto remaining = wake == Clock::time_point::max() ? int64_t(INT_MAX) :
          std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(wake - Clock::now()).count());
      pollfd events[]{{watch.fd(), POLLIN, 0}, {stopping ? -1 : cancel_fd, POLLIN, 0}, {notification, POLLIN, 0}};
      rc = poll(events, 3, int(std::min<int64_t>(remaining, INT_MAX)));
      if (rc < 0 && errno != EINTR) throw std::runtime_error(Error("poll child"));
      if (events[2].revents & POLLIN) {
        char bytes[64];
        // One bounded consume: notifications coalesce, cancellation is sticky.
        const auto count = read(notification, bytes, sizeof(bytes));
        if (count > 0 && spec.on_notification) spec.on_notification();
      }
      if (events[2].revents & (POLLHUP | POLLERR | POLLNVAL)) notification = -1;
    }
    // Also clean descendants of a normally exiting leader, just like closing a
    // Windows job. They must not outlive a title handoff.
    child.Signal(SIGKILL);
    const int status = child.Reap();
    if (status < 0) throw std::runtime_error(Error("waitpid"));
    if (WIFSIGNALED(status)) {
      result.termination_signal = WTERMSIG(status);
      result.exit_code = uint32_t(128 + result.termination_signal);
    } else if (WIFEXITED(status)) {
      result.exit_code = uint32_t(WEXITSTATUS(status));
    } else {
      throw std::runtime_error("Child did not return a terminal process status");
    }
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    ActionTrace::Event("process.exited");
  } catch (const std::exception& error) {
    result.launch_error = error.what();
    // A callback/wait failure after spawn must never look like successful exit.
    if (result.launched) {
      result.exit_code = 1;
      result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
    }
  }
  return result;
}
}  // namespace x360n
