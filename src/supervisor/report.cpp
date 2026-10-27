#include "report.h"

#include <fstream>
#include <sstream>

#include <fmt/format.h>

namespace x360n {

std::filesystem::path CreateRunDirectory(const std::filesystem::path& runs,
    const std::string& stamp, const std::string& label, std::error_code& error) {
  namespace fs = std::filesystem;
  fs::create_directories(runs, error);
  if (error) return {};
  for (unsigned attempt = 0; attempt < 1024; ++attempt) {
    const auto dir = runs / (stamp + "-" +
        (attempt ? std::to_string(attempt) + "-" : "") + label);
    if (fs::create_directory(dir, error)) return dir;
    if (error) return {};
  }
  error = std::make_error_code(std::errc::file_exists);
  return {};
}

std::string JsonEscape(std::string_view s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          o += fmt::format("\\u{:04x}", c);
        } else {
          o += char(c);  // UTF-8 passes through unchanged
        }
    }
  }
  o += '"';
  return o;
}

void JsonWriter::Key(std::string_view key) {
  if (!first_) out_ += ',';
  out_ += '\n';
  out_.append(size_t(depth_) * 2, ' ');
  first_ = false;
  if (!key.empty()) {
    out_ += JsonEscape(key);
    out_ += ": ";
  }
}

JsonWriter& JsonWriter::BeginObject(std::string_view key) {
  if (depth_ > 0) Key(key);
  out_ += '{';
  ++depth_;
  first_ = true;
  return *this;
}

JsonWriter& JsonWriter::EndObject() {
  --depth_;
  out_ += '\n';
  out_.append(size_t(depth_) * 2, ' ');
  out_ += '}';
  first_ = false;
  return *this;
}

JsonWriter& JsonWriter::BeginArray(std::string_view key) {
  Key(key);
  out_ += '[';
  ++depth_;
  first_ = true;
  return *this;
}

JsonWriter& JsonWriter::EndArray() {
  --depth_;
  out_ += '\n';
  out_.append(size_t(depth_) * 2, ' ');
  out_ += ']';
  first_ = false;
  return *this;
}

JsonWriter& JsonWriter::String(std::string_view key, std::string_view value) {
  Key(key);
  out_ += JsonEscape(value);
  return *this;
}

JsonWriter& JsonWriter::Number(std::string_view key, int64_t value) {
  Key(key);
  out_ += std::to_string(value);
  return *this;
}

JsonWriter& JsonWriter::Bool(std::string_view key, bool value) {
  Key(key);
  out_ += value ? "true" : "false";
  return *this;
}

JsonWriter& JsonWriter::Null(std::string_view key) {
  Key(key);
  out_ += "null";
  return *this;
}

std::string JsonWriter::Take() {
  out_ += '\n';
  return std::move(out_);
}

void LogScan::Merge(const LogScan& o) {
  stub_lines += o.stub_lines;
  critical_lines += o.critical_lines;
  for (auto& [k, v] : o.compatibility_experiments) compatibility_experiments[k] += v;
  for (auto& [k, v] : o.stubs_by_name) stubs_by_name[k] += v;
  for (auto& [k, v] : o.unimplemented_messages) unimplemented_messages[k] += v;
  for (auto& [k, v] : o.failed_opens) failed_opens[k] += v;
  for (auto& [k, v] : o.unregistered_calls) unregistered_calls[k] += v;
  for (auto& [k, v] : o.unimplemented_variables) unimplemented_variables[k] += v;
  error_lines += o.error_lines;
  for (const auto& e : o.first_errors)
    if (first_errors.size() < kMaxFirstErrors) first_errors.push_back(e);
}

namespace {

void ScanStubLine(std::string_view line, LogScan& s) {
  size_t at = line.find(" STUB");
  if (at == std::string_view::npos) return;
  size_t after = at + 5;
  if (after < line.size() && line[after] != ' ' && line[after] != '\r') return;  // e.g. "STUBBED"
  size_t begin = line.find_last_of(" \t]", at == 0 ? 0 : at - 1);
  begin = begin == std::string_view::npos ? 0 : begin + 1;
  if (begin >= at) return;
  ++s.stub_lines;
  ++s.stubs_by_name[std::string(line.substr(begin, at - begin))];
}

// "Unimplemented XMP message app=FA, msg=00070044, ..." -> "XMP msg=00070044"
void ScanMessageLine(std::string_view line, LogScan& s) {
  constexpr std::string_view kPrefix = "Unimplemented ";
  size_t at = line.find(kPrefix);
  if (at == std::string_view::npos) return;
  size_t app_begin = at + kPrefix.size();
  size_t app_end = line.find(" message", app_begin);
  size_t msg = line.find("msg=", app_begin);
  if (app_end == std::string_view::npos || msg == std::string_view::npos || app_end > msg) return;
  size_t msg_end = line.find_first_of(", \r", msg + 4);
  std::string_view app = line.substr(app_begin, app_end - app_begin);
  std::string_view value = line.substr(msg, msg_end == std::string_view::npos ? msg_end : msg_end - msg);
  ++s.unimplemented_messages[std::string(app) + " " + std::string(value)];
}

std::string_view TrimEnd(std::string_view v) {
  while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.remove_suffix(1);
  return v;
}

// "[NtCreateFile] FAILED: path='game:\x.xzp' -> 0xc000000f" -> "game:\x.xzp -> 0xc000000f"
void ScanOpenFailure(std::string_view line, LogScan& s) {
  constexpr std::string_view kPrefix = "[NtCreateFile] FAILED: path='";
  size_t at = line.find(kPrefix);
  if (at == std::string_view::npos) return;
  size_t begin = at + kPrefix.size();
  size_t end = line.rfind("' -> ");
  if (end == std::string_view::npos || end < begin) return;
  std::string_view status = TrimEnd(line.substr(end + 5));
  ++s.failed_opens[std::string(line.substr(begin, end - begin)) + " -> " + std::string(status)];
}

void ScanAfter(std::string_view line, std::string_view prefix, std::string_view stop,
               std::map<std::string, uint64_t>& out) {
  size_t at = line.find(prefix);
  if (at == std::string_view::npos) return;
  std::string_view rest = line.substr(at + prefix.size());
  if (!stop.empty()) {
    size_t end = rest.find(stop);
    if (end == std::string_view::npos) return;
    rest = rest.substr(0, end);
  }
  rest = TrimEnd(rest);
  if (!rest.empty()) ++out[std::string(rest)];
}

}  // namespace

LogScan ScanLogText(std::string_view text) {
  LogScan s;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    std::string_view line = text.substr(pos, eol == std::string_view::npos ? eol : eol - pos);
    pos = eol == std::string_view::npos ? text.size() : eol + 1;
    ScanStubLine(line, s);
    ScanMessageLine(line, s);
    ScanAfter(line, "[compatibility-experiment] ", {}, s.compatibility_experiments);
    if (line.find("] [critical] ") != std::string_view::npos) ++s.critical_lines;
    ScanOpenFailure(line, s);
    ScanAfter(line, "unregistered function at guest address ", {}, s.unregistered_calls);
    ScanAfter(line, "Variable import ", " not implemented", s.unimplemented_variables);
    // "[time] [error] [cat] [thread] message": keep the text after the last "] ".
    if (size_t at = line.find("] [error] "); at != std::string_view::npos) {
      ++s.error_lines;
      if (s.first_errors.size() < LogScan::kMaxFirstErrors) {
        std::string_view msg = line.substr(at + 2);
        while (!msg.empty() && msg.front() == '[') {
          size_t close = msg.find("] ");
          if (close == std::string_view::npos) break;
          msg.remove_prefix(close + 2);
        }
        s.first_errors.emplace_back(TrimEnd(msg));
      }
    }
  }
  return s;
}

LogScan ScanLog(const std::filesystem::path& log) {
  std::ifstream in(log, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ScanLogText(ss.str());
}

}  // namespace x360n
