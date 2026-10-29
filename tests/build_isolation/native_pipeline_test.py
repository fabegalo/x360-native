"""Native integration of the production pipeline, with a synthetic codegen SDK.

This verifies preparation/provenance/recovery, not PPC translation or gameplay.
All compiler and Ninja invocations are real. Fixture inputs contain only our C++.
"""
import argparse
import hashlib
import json
import os
import platform as host_platform
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
for name in ("supervisor", "cc", "cxx", "cmake", "ninja", "source_root", "git"):
    parser.add_argument("--" + name.replace("_", "-"), required=True)
parser.add_argument("--sysroot")
parser.add_argument("--work-dir", help="Private temporary fixture directory, e.g. the WSL Linux filesystem")
args = parser.parse_args()
source = Path(args.source_root).resolve()
temporary = Path(args.work_dir).resolve() if args.work_dir else source / ".local/tmp"
temporary.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix="native-pipeline-", dir=temporary)) / "João 日本"
root.mkdir()
sdk, project = root / "external/rexglue-sdk", root / "titles/fixture"
sdk.mkdir(parents=True)
project.mkdir(parents=True)
suffix = ".exe" if os.name == "nt" else ""
architecture = "arm64" if host_platform.machine().lower() in ("arm64", "aarch64") else "x64"
platform = ("windows" if os.name == "nt" else "macos" if os.uname().sysname == "Darwin" else "linux") + "-" + architecture
build = root / ".local/build" / platform / "titles/fixture"

def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")

def run(command, *, expected=0, timeout=180, log=None):
    try:
        result = subprocess.run(list(map(str, command)), cwd=root, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        if log:
            output = error.stdout or b""
            write(root / (log + ".log"), output.decode("utf-8", errors="replace") if isinstance(output, bytes) else output)
        raise
    if log:
        write(root / (log + ".log"), result.stdout)
    if result.returncode != expected:
        raise AssertionError(f"exit {result.returncode}, expected {expected}: {command}\n{result.stdout[-12000:]}\nfixture: {root}")
    return result.stdout

def digest(path):
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()

write(sdk / "codegen.cpp", r'''
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
namespace fs=std::filesystem;
std::string read(fs::path p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
  if(argc<3 || fs::path(argv[1])!="codegen") return 64;
  const auto project=fs::path(argv[2]).parent_path(); const auto generated=project/"generated";
  fs::create_directories(generated);
  if(fs::exists(generated/"pause")) std::this_thread::sleep_for(std::chrono::seconds(10));
  std::ofstream(generated/"calls",std::ios::app)<<"codegen\n";
  const auto output=generated/"guest.inc", input=project/"payload.cpp.txt";
  const auto content=read(input);
  if(fs::exists(output) && read(output)==content) return 0;
  const bool existed=fs::exists(output); const auto old=existed?fs::last_write_time(output):fs::file_time_type{};
  std::ofstream(output,std::ios::binary)<<content;
  if(existed) fs::last_write_time(output,old); // challenge content-based invalidation
  return 0;
}
''')
write(sdk / "gpu.cpp", '''#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT void* rex_gpu_create() { return nullptr; }
EXPORT unsigned rex_gpu_abi_version() { return 1; }
''')
write(sdk / "CMakeLists.txt", '''cmake_minimum_required(VERSION 3.25)
project(fixture_sdk LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(rexglue codegen.cpp)
add_library(rexgpu-xenos SHARED gpu.cpp)
''')
run([args.git, "-C", sdk, "init", "-q"])
run([args.git, "-C", sdk, "add", "."])
run([args.git, "-C", sdk, "-c", "user.name=Fixture", "-c", "user.email=fixture@x360-native.invalid", "commit", "-qm", "test(fixture): snapshot generic producer"])
head = run([args.git, "-C", sdk, "rev-parse", "HEAD"]).strip()
write(root / "deps.lock.json", json.dumps({"rexglue_sdk":{"commit":head,"patches":[]}, "llvm":{"version":"22.1.8"}}))
for name in ("x360n_verify_sdk_patches.cmake",):
    target = root / "cmake/sdk" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / "cmake/sdk" / name, target)
target = root / "cmake/build/x360n_build_isolation.cmake"
target.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source / "cmake/build/x360n_build_isolation.cmake", target)
write(root / "cmake/sdk/shared/CMakeLists.txt", '''cmake_minimum_required(VERSION 3.25)
project(fixture_shared LANGUAGES C CXX)
include("${X360N_ROOT}/cmake/build/x360n_build_isolation.cmake")
x360n_guard_ninja()
find_package(Threads REQUIRED) # try_compile must retain its nested working directory
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
export(TARGETS rexglue rexgpu-xenos FILE "${CMAKE_BINARY_DIR}/targets.cmake")
file(WRITE "${CMAKE_BINARY_DIR}/sdk.cmake" "include(\\"${CMAKE_BINARY_DIR}/targets.cmake\\")\\n")
''')
write(project / "fixture_manifest.toml", """[project]
name='fixture'
game_root='.'
[entrypoint]
file_path='payload.cpp.txt'
out_directory_path='generated'
[x360n]
native_exe='../../.local/build/legacy/fixture.exe'
""")
write(project / "payload.cpp.txt", "int fixture_value() { return 7; }\n")
write(project / "main.cpp", '#include "generated/guest.inc"\nint main(){return fixture_value();}\n')
write(project / "CMakeLists.txt", '''cmake_minimum_required(VERSION 3.25)
project(fixture LANGUAGES C CXX)
if(X360N_SHARED_SDK_EXPORT)
 include("${X360N_SHARED_SDK_EXPORT}")
else()
 add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
endif()
include("${CMAKE_SOURCE_DIR}/../../cmake/build/x360n_build_isolation.cmake")
x360n_guard_ninja()
add_executable(fixture main.cpp)
''')
command = [args.supervisor, "--root", root, "prepare", "fixture", "--build-dir", build, "--cc", args.cc, "--cxx", args.cxx, "--cmake", args.cmake, "--ninja", args.ninja]
if args.sysroot:
    command += ["--define", "CMAKE_SYSROOT=" + args.sysroot]
if os.name == "nt":
    command += ["--define", "CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
exe = build / ("fixture" + suffix)
receipt = Path(str(exe) + ".preparation.json")
print("fixture:", root, flush=True)
run(command, log="first")
run([exe], expected=7)
run([args.supervisor, "--root", root, "run", exe, "--timeout", "10"], expected=1, log="verified-launch")
if os.name != "nt":
    native_bytes, native_receipt, native_times = exe.read_bytes(), receipt.read_bytes(), exe.stat()
    exe.write_bytes(b"MZ" + bytes(126))
    legacy = json.loads(native_receipt)
    legacy.pop("native_identity")
    for item in legacy["artifacts"]:
        if Path(item["path"]).resolve() == exe.resolve():
            item["sha256"] = digest(exe)
    write(receipt, json.dumps(legacy))
    assert "Native image OS/architecture mismatch" in run([args.supervisor, "--root", root, "run", exe], expected=3, log="legacy-pe-rejected")
    exe.write_bytes(native_bytes)
    os.utime(exe, ns=(native_times.st_atime_ns, native_times.st_mtime_ns))
    receipt.write_bytes(native_receipt)
old_receipt, old_exe = digest(receipt), digest(exe)
stamp = exe.stat().st_mtime_ns
assert "reused verified preparation" in run(command, log="reuse")
assert digest(receipt) == old_receipt and digest(exe) == old_exe and exe.stat().st_mtime_ns == stamp
run([args.cmake, "--build", build, "--target", "fixture"], log="guarded-noop")
assert not (root / ".local/preparation/fixture.incomplete").exists()
assert "reused verified preparation" in run(command, log="reuse-after-guard")
replacement_driver=root / ".local/tools" / ("x360n-updated" + suffix)
replacement_driver.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(args.supervisor, replacement_driver)
with replacement_driver.open("ab") as stream:
    stream.write(b"\nx360n fixture: harmless version overlay\n")
assert "reused-shared-sdk" in run([replacement_driver] + command[1:], log="driver-update")
assert digest(exe) == old_exe and exe.stat().st_mtime_ns == stamp
calls = (project / "generated/calls").read_text().count("codegen")
host = project / "main.cpp"
old = host.stat()
write(host, host.read_text() + "// host-only edit\n")
os.utime(host, ns=(old.st_atime_ns, old.st_mtime_ns))
assert "reused-codegen" in run(command, log="host-change")
assert (project / "generated/calls").read_text().count("codegen") == calls
payload = project / "payload.cpp.txt"
old = payload.stat()
write(payload, "int fixture_value() { return 8; }\n")
os.utime(payload, ns=(old.st_atime_ns, old.st_mtime_ns))
run(command, log="content-change")
run([exe], expected=8)
obj = next(build.rglob("main.cpp.obj" if os.name == "nt" else "main.cpp.o"))
old = obj.stat()
with obj.open("r+b") as stream:
    stream.write(b"BAD!")
os.utime(obj, ns=(old.st_atime_ns, old.st_mtime_ns))
run(command, log="object-tamper")
assert obj.read_bytes()[:4] != b"BAD!", "modified native object was reused"
run([exe], expected=8)
write(project / "generated/pause", "pause")
write(payload, "int fixture_value() { return 9; }\n")
run(command + ["--timeout", "2"], expected=1, log="timeout")
assert (root / ".local/preparation/fixture.incomplete").exists()
run([args.supervisor, "--root", root, "run", exe], expected=3, log="blocked-interrupted-launch")
(project / "generated/pause").unlink()
run(command, log="recovery")
assert not (root / ".local/preparation/fixture.incomplete").exists()
run([exe], expected=9)
gpu = sdk / "gpu.cpp"
original_gpu = gpu.read_text()
write(gpu, original_gpu.replace("rex_gpu_abi_version", "missing_gpu_abi_version"))
assert "ABI export missing" in run(command, expected=1, log="missing-abi")
assert (root / ".local/preparation/fixture.incomplete").exists()
write(gpu, original_gpu)
run(command, log="abi-recovery")
assert not (root / ".local/preparation/fixture.incomplete").exists()
print("PASS: first build, verified launch/no-op, host-only/codegen reuse, preserved-mtime change, object tamper, timeout/recovery, ABI rejection/recovery", flush=True)
