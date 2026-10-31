#pragma once
// Entry point of headless probes on every host: wide arguments on Windows
// (non-ASCII output paths), narrow UTF-8 arguments elsewhere.
#include <string_view>
#if defined(_WIN32)
using ProbeChar = wchar_t;
#define X360N_PROBE_MAIN int wmain(int argc, wchar_t** argv)
#define X360N_PROBE_ARG(text) L##text
#else
using ProbeChar = char;
#define X360N_PROBE_MAIN int main(int argc, char** argv)
#define X360N_PROBE_ARG(text) text
#endif
using ProbeArg = std::basic_string_view<ProbeChar>;
