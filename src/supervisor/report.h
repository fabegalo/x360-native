// Minimal JSON emission and log diagnostics for run reports.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace x360n {

std::string JsonEscape(std::string_view s);  // returns a quoted JSON string

// Tiny writer for nested objects; keeps emission order. Not a general library.
class JsonWriter {
 public:
  // `key` is ignored for elements of an array (pass {}).
  JsonWriter& BeginObject(std::string_view key = {});
  JsonWriter& EndObject();
  JsonWriter& BeginArray(std::string_view key);
  JsonWriter& EndArray();
  JsonWriter& String(std::string_view key, std::string_view value);
  JsonWriter& Number(std::string_view key, int64_t value);
  JsonWriter& Bool(std::string_view key, bool value);
  JsonWriter& Null(std::string_view key);
  std::string Take();

 private:
  void Key(std::string_view key);
  std::string out_;
  int depth_ = 0;
  bool first_ = true;
};

// Signals of missing system functionality in a ReXGlue log:
//  - "<name> STUB": REX_STUB* (include/rex/hook.h) for an unimplemented kernel
//    export; such calls do not set r3, so the guest sees an unchanged register.
//  - "Unimplemented <APP> message app=..., msg=...": XAM app message with no
//    handler (src/kernel/xam/apps/*.cpp); the caller gets X_E_FAIL. Messages to
//    an app the SDK does not have at all return X_E_NOTFOUND without any log.
//  - "[critical]" lines, e.g. "[FATAL] Call to invalid or unregistered function".
//  - "[NtCreateFile] FAILED: path='<p>' -> <status>" (xboxkrnl_io.cpp, logged at
//    warning level for every failed open, NtOpenFile included); successful opens
//    are only traced with --log_level=trace --log_noisy=true.
//  - "Call to invalid or unregistered function at guest address <a>"
//    (function_dispatcher.cpp): the indirect target, not the caller.
//  - "Variable import <lib>:<ord> (<name>) not implemented" (xex_module.cpp): the
//    loader wrote 0xD000BEEF|ordinal into the guest's slot.
struct LogScan {
  uint64_t stub_lines = 0;
  std::map<std::string, uint64_t> compatibility_experiments;
  std::map<std::string, uint64_t> stubs_by_name;
  std::map<std::string, uint64_t> unimplemented_messages;  // "XMP msg=00070044"
  uint64_t critical_lines = 0;
  std::map<std::string, uint64_t> failed_opens;             // "game:\a.xzp -> 0xc000000f"
  std::map<std::string, uint64_t> unregistered_calls;       // "0x00000000"
  std::map<std::string, uint64_t> unimplemented_variables;  // "xam:0x3db (g_XuiAutomation)"
  // "[error]" lines (e.g. "Runtime setup failed: C0000001") and the first few of
  // them, message text only, in log order.
  uint64_t error_lines = 0;
  std::vector<std::string> first_errors;
  static constexpr size_t kMaxFirstErrors = 10;
  void Merge(const LogScan& other);
};
LogScan ScanLog(const std::filesystem::path& log);
LogScan ScanLogText(std::string_view text);

}  // namespace x360n
