#include "report.h"

#include <fstream>
#include <sstream>

#include <fmt/format.h>

namespace x360n {

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
  for (auto& [k, v] : o.stubs_by_name) stubs_by_name[k] += v;
  for (auto& [k, v] : o.unimplemented_messages) unimplemented_messages[k] += v;
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
    if (line.find("] [critical] ") != std::string_view::npos) ++s.critical_lines;
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
