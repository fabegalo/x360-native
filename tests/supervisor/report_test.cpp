#include <catch2/catch_test_macros.hpp>

#include <string>

#include "child_process.h"
#include "report.h"

using namespace x360n;

TEST_CASE("JSON strings are escaped", "[report]") {
  CHECK(JsonEscape("plain") == "\"plain\"");
  CHECK(JsonEscape("a\"b\\c") == "\"a\\\"b\\\\c\"");
  CHECK(JsonEscape("C:\\x\n") == "\"C:\\\\x\\n\"");
  CHECK(JsonEscape(std::string("\x01", 1)) == "\"\\u0001\"");
  CHECK(JsonEscape("\xC3\xA7") == "\"\xC3\xA7\"");  // UTF-8 passes through
}

TEST_CASE("JsonWriter nests objects with commas in the right places", "[report]") {
  JsonWriter w;
  w.BeginObject().Number("a", 1).BeginObject("b").Bool("c", true).Null("d").EndObject().String("e", "f").EndObject();
  CHECK(w.Take() == "{\n  \"a\": 1,\n  \"b\": {\n    \"c\": true,\n    \"d\": null\n  },\n  \"e\": \"f\"\n}\n");
}

TEST_CASE("JsonWriter arrays hold values and objects without keys", "[report]") {
  JsonWriter w;
  w.BeginObject().BeginArray("xs").String({}, "a").BeginObject().Number("n", 2).EndObject().EndArray().EndObject();
  CHECK(w.Take() == "{\n  \"xs\": [\n    \"a\",\n    {\n      \"n\": 2\n    }\n  ]\n}\n");
}

TEST_CASE("log scanner counts REX_STUB lines by export name", "[report]") {
  auto s = ScanLogText(
      "[krnl] [warning] __imp__XamFoo STUB\r\n"
      "[krnl] [warning] __imp__XamFoo STUB\n"
      "[krnl] [warning] __imp__KeBar STUB - returning 0x0\n"
      "a word STUBBED is not a stub\n"
      " STUB\n");
  CHECK(s.stub_lines == 3);
  CHECK(s.stubs_by_name["__imp__XamFoo"] == 2);
  CHECK(s.stubs_by_name["__imp__KeBar"] == 1);
  CHECK(s.stubs_by_name.size() == 2);
  CHECK(s.unimplemented_messages.empty());
  CHECK(s.critical_lines == 0);
}

TEST_CASE("log scanner counts unimplemented XAM app messages and critical lines", "[report]") {
  // Formats from src/kernel/xam/apps/*.cpp and the FATAL seen in docs/reports/titles/validation-iruka.md.
  auto s = ScanLogText(
      "[2026-10-11 04:02:55.100] [warning] [krnl] [t1] Unimplemented XMP message app=000000FA, "
      "msg=00070044, arg1=4000A000, arg2=00000010\r\n"
      "[2026-10-11 04:02:55.101] [warning] [krnl] [t1] Unimplemented XMP message app=000000FA, "
      "msg=00070044, arg1=4000A000, arg2=00000010\n"
      "[2026-10-11 04:02:55.102] [warning] [krnl] [t1] Unimplemented XLIVEBASE message app=000000FC, "
      "msg=0005008C, arg1=00000000, arg2=00000000\n"
      "[2026-10-11 04:02:55.186] [critical] [core] [t45192] [FATAL] Call to invalid or unregistered "
      "function at guest address 0x82198CD8\n"
      "[debug] XGI 0x000B0071, unimplemented\n");
  CHECK(s.unimplemented_messages.size() == 2);
  CHECK(s.unimplemented_messages["XMP msg=00070044"] == 2);
  CHECK(s.unimplemented_messages["XLIVEBASE msg=0005008C"] == 1);
  CHECK(s.critical_lines == 1);
  CHECK(s.stub_lines == 0);
  CHECK(s.unregistered_calls["0x82198CD8"] == 1);
}

TEST_CASE("log scanner records failed opens and unimplemented variable imports", "[report]") {
  // Formats from xboxkrnl_io.cpp (REXKRNL_IMPORT_FAIL) and xex_module.cpp:1111.
  auto s = ScanLogText(
      "[2026-10-11 17:12:20.001] [warning] [krnl] [t3] [NtCreateFile] FAILED: "
      "path='game:\\shaders\\SobelEdge.xpu' -> 0xc000000f\r\n"
      "[2026-10-11 17:12:20.002] [warning] [krnl] [t3] [NtCreateFile] FAILED: "
      "path='game:\\shaders\\SobelEdge.xpu' -> 0xc000000f\n"
      "[2026-10-11 17:12:20.003] [warning] [krnl] [t3] [NtCreateFile] FAILED: "
      "path='media:\\a' -> b.xtt' -> 0xc0000034\n"
      "[2026-10-11 17:36:43.000] [warning] [core] Variable import xam:0x3db (g_XuiAutomation) not "
      "implemented\n"
      "[2026-10-11 17:36:43.000] [warning] [core] Variable import xboxkrnl:0x157 "
      "(XboxKrnlBaseVersion) not implemented\n"
      "[debug] [NtCreateFile] -> 0x0 handle=0xF8000010\n");
  CHECK(s.failed_opens.size() == 2);
  CHECK(s.failed_opens["game:\\shaders\\SobelEdge.xpu -> 0xc000000f"] == 2);
  // The status is taken after the last "' -> ", so a path containing "' -> " stays whole.
  CHECK(s.failed_opens["media:\\a' -> b.xtt -> 0xc0000034"] == 1);
  CHECK(s.unimplemented_variables.size() == 2);
  CHECK(s.unimplemented_variables["xam:0x3db (g_XuiAutomation)"] == 1);
  CHECK(s.unimplemented_variables["xboxkrnl:0x157 (XboxKrnlBaseVersion)"] == 1);
  CHECK(s.critical_lines == 0);
}

TEST_CASE("log scanner keeps error lines in order, message text only", "[report]") {
  // Lines from the first dashboard 17559 run (docs/dashboard.md §0.6).
  auto s = ScanLogText(
      "[2026-10-11 19:04:07.328] [info] [sys] [t25196] Guest memory arena mapped\n"
      "[2026-10-11 19:04:07.373] [error] [sys] [t25196] Failed to allocate function table at "
      "93028000\r\n"
      "[2026-10-11 19:04:07.373] [error] [core] [t25196] Failed to initialize guest memory "
      "function table\n"
      "[2026-10-11 19:04:11.506] [error] [core] [t25196] Runtime setup failed: C0000001\n");
  CHECK(s.error_lines == 3);
  REQUIRE(s.first_errors.size() == 3);
  CHECK(s.first_errors[0] == "Failed to allocate function table at 93028000");
  CHECK(s.first_errors[2] == "Runtime setup failed: C0000001");
  CHECK(s.critical_lines == 0);

  std::string many;
  for (int i = 0; i < 25; ++i) many += "[t] [error] [core] [t1] e" + std::to_string(i) + "\n";
  auto m = ScanLogText(many);
  CHECK(m.error_lines == 25);
  CHECK(m.first_errors.size() == LogScan::kMaxFirstErrors);
  CHECK(m.first_errors.front() == "e0");
}

TEST_CASE("command lines follow CommandLineToArgvW quoting", "[process]") {
  CHECK(BuildCommandLine("C:\\a b\\x.exe", {"plain", "two words", "q\"uote", "tail\\", "sp ace\\", ""}) ==
        L"\"C:\\a b\\x.exe\" plain \"two words\" \"q\\\"uote\" tail\\ \"sp ace\\\\\" \"\"");
}

TEST_CASE("exit codes are described", "[process]") {
  CHECK(DescribeExitCode(0) == "success");
  CHECK(DescribeExitCode(7) == "exit code 7");
  CHECK(DescribeExitCode(0xC0000005).starts_with("STATUS_ACCESS_VIOLATION"));
  CHECK(DescribeExitCode(0x80000003).starts_with("STATUS_BREAKPOINT"));
  CHECK(DescribeExitCode(0xC0001234) == "NTSTATUS error 0xC0001234");
}

TEST_CASE("log scanner preserves experimental compatibility disclosures", "[report]") {
  const std::string message = "dash17559_experimental_region=enabled; região de compatibilidade experimental; conteúdo real desconhecido";
  auto scan = ScanLogText("[t] [warning] [core] [compatibility-experiment] " + message + "\r\n");
  REQUIRE(scan.compatibility_experiments.size() == 1);
  CHECK(scan.compatibility_experiments.at(message) == 1);
  LogScan merged;
  merged.Merge(scan);
  merged.Merge(scan);
  CHECK(merged.compatibility_experiments.at(message) == 2);
}
