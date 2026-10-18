// Read-only launcher-path benchmark. Never creates a title process or window.
#include "catalog.h"
#include "action_trace.h"
#include "sha256.h"
#ifdef X360N_BENCHMARK_BATCH
#include "digest_batch.h"
#endif
#include "../supervisor/preparation_fixture.h"
#include <chrono>
#include <iostream>
using namespace x360n;
using namespace x360n::desktop;
void Fixture(const ProjectPaths& paths) {
  namespace fs = std::filesystem;
  if (fs::exists(paths.root)) throw std::runtime_error("Fixture destination already exists");
  std::vector<fs::path> closure;
  fs::create_directories(paths.root / "closure");
  const std::string bytes(256 * 1024, 's');
  for (int i = 0; i != 512; ++i) {
    auto file = paths.root / "closure" / (std::to_string(i) + ".bin");
    std::ofstream(file, std::ios::binary) << bytes; closure.push_back(file);
  }
  const std::string asset(16 * 1024, 'a');
  const auto hash = Sha256Hex({reinterpret_cast<const uint8_t*>(asset.data()), asset.size()});
  for (int i = 0; i != 4; ++i) {
    const auto id = "fixture" + std::to_string(i);
    const auto dir = paths.root / "titles" / id;
    const auto game = paths.inputs() / id;
    const auto exe = paths.build() / "titles" / id / (id + ".exe");
    fs::create_directories(dir); fs::create_directories(game); fs::create_directories(exe.parent_path());
    std::ofstream(exe) << "synthetic artifact, never executed";
    const auto manifest = dir / (id + "_manifest.toml");
    std::ofstream(manifest) << "[project]\nname='" << id << "'\ngame_root='../../.local/inputs/" << id
      << "'\n[entrypoint]\nfile_path='../../.local/inputs/" << id << "/0.bin'\n"
      << "[x360n]\nuser_data_root='../../.local/user/" << id << "'\n";
    std::ofstream setup(dir / "preparation.toml");
    setup << "label='" << id << "'\nlimitations='synthetic'\nsession=" << (i < 2 ? "true" : "false")
          << "\nkind='" << (i < 2 ? "dashboard" : "game") << "'\ninventory='inventory.tsv'\n"
          << "[[files]]\npath='0.bin'\nsource_name='0.bin'\nsha256='" << hash << "'\n";
    if (i >= 2) setup << "[library]\ntitle_id='0000000" << i << "'\ncontent_type='000D0000'\nfile_name='pkg" << i
          << "'\ndisplay_name='fixture'\nsource_sha256='" << hash << "'\n";
    setup.close();
    std::ofstream inventory(dir / "inventory.tsv");
    auto inputs = closure;
    for (int n = 0; n != 512; ++n) {
      auto file = game / (std::to_string(n) + ".bin");
      std::ofstream(file, std::ios::binary) << asset; inputs.push_back(file);
      if (n) inventory << hash << '\t' << n << ".bin\n";
    }
    inventory.close(); inputs.push_back(dir / "inventory.tsv");
    WriteTestReceipt(paths, id, manifest, exe, std::move(inputs));
  }
}
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  const ProjectPaths paths{std::filesystem::absolute(argv[1])};
  if (argc == 3 && std::string(argv[2]) == "--fixture") Fixture(paths);
  const auto mode = argc == 3 && std::string(argv[2]) == "--preview" ? InspectionMode::Preview : InspectionMode::Full;
  for (int pass = 0; pass != 2; ++pass) {
    const auto start = std::chrono::steady_clock::now();
#ifdef X360N_BENCHMARK_BATCH
    DigestBatch batch;
#endif
    const auto catalog = ReadCatalog(paths);
    std::cout << "catalog pass=" << pass << " ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << std::endl;
    size_t ready = 0, inputs = 0, pending = 0;
    for (const auto& target : catalog) {
      const auto state = Inspect(paths, target, mode);
      for (const auto& issue : state.issues) std::cerr << target.id << ": " << issue << std::endl;
      if (mode == InspectionMode::Full && state.inputs_ok) InspectLibrary(paths, target, catalog);
      if (mode == InspectionMode::Full && !target.catalogued && !target.inputs.empty()) DigestFile(target.inputs.front().destination);
      ready += state.prepared; pending += state.verification_pending; inputs += target.inputs.size();
    }
    std::cout << "snapshot pass=" << pass << " targets=" << catalog.size() << " inputs=" << inputs
              << " ready=" << ready << " pending=" << pending << " ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << std::endl;
#ifdef X360N_BENCHMARK_BATCH
    std::cout << "streams=" << batch.files << " bytes=" << batch.bytes << " reused=" << batch.hits << std::endl;
#endif
  }
}
