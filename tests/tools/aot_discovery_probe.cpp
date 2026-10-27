// Opt-in corpus probe: consume the SDK's analysis graph, never infer new targets.
#include <rex/codegen/codegen.h>
#include <rex/memory/utils.h>
#include <rex/hash.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: aot_discovery_probe <config.toml> <graph.tsv> [--no-emit] [hex address ...]\n";
    return 2;
  }
  auto pipeline = rex::codegen::CodegenPipeline::Create(argv[1]);
  if (!pipeline) { std::cerr << pipeline.error().message; return 1; }
  auto result = pipeline->RunAnalyze();
  auto& ctx = pipeline->context();
  const bool emit_fingerprints = argc < 4 || std::string_view(argv[3]) != "--no-emit";
  std::ofstream out(argv[2]);
  if (!out) return 1;
  const auto hex = [&](uint32_t a) -> std::ostream& {
    return out << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << a << std::dec;
  };
  out << "analysis\t" << (result.has_value() ? "ok" : "failed") << '\n';
  std::vector<std::pair<uint32_t,std::string>> crt_hooks(ctx.analysisState().crtFiberHooks.begin(),
                                                       ctx.analysisState().crtFiberHooks.end());
  std::sort(crt_hooks.begin(),crt_hooks.end());
  for(const auto& [address,name]:crt_hooks) { out << "crt_hook\t";hex(address) << '\t' << name << '\n'; }
  std::vector<uint32_t> entries;
  std::set<uint32_t> covered;
  size_t tables = 0, slots = 0, nonPdata = 0;
  for (const auto& [a, n] : ctx.graph.functions()) entries.push_back(a);
  std::sort(entries.begin(), entries.end());
  for (auto a : entries) {
    const auto* n = ctx.graph.getFunction(a);
    out << "function\t"; hex(a) << '\t' << rex::codegen::AuthorityName(n->authority())
      << '\t' << n->size() << '\t' << int(n->state()) << '\n';
    if (emit_fingerprints && n->isSealed()) {
      rex::codegen::EmitContext emit{ctx.binary(), ctx.Config(), ctx.graph,
          static_cast<uint32_t>(ctx.analysisState().entryPoint), ctx.resolver()};
      emit.setJmpAddresses = &ctx.analysisState().setJmpAddresses;
      emit.longJmpAddresses = &ctx.analysisState().longJmpAddresses;
      // Same emitter as CodegenWriter, fingerprint only (XXH3-128, not an identity/security hash).
      out << "emit_xxh3\t"; hex(a) << '\t' << rex::hash_bytes(n->emitCpp(emit)) << '\n';
    }
    if (!n->isImport() && !ctx.scan.pdataSizes.contains(a)) ++nonPdata;
    for (const auto& b : n->blocks()) {
      out << "block\t"; hex(a) << '\t'; hex(b.base) << '\t' << b.size << '\n';
      for (uint32_t p = b.base; p < b.end(); p += 4) covered.insert(p);
    }
    auto jt = n->jumpTables();
    std::sort(jt.begin(), jt.end(), [](const auto& x, const auto& y) { return x.bctrAddress < y.bctrAddress; });
    for (const auto& t : jt) {
      ++tables; slots += t.targets.size();
      out << "table\t"; hex(a) << '\t'; hex(t.bctrAddress) << '\t'; hex(t.tableAddress);
      for (auto target : t.targets) { out << '\t'; hex(target); }
      out << '\n';
      out << "table_data\t"; hex(a) << '\t'; hex(t.tableAddress) << '\t'
          << t.tableByteLength << '\n';
    }
  }
  auto errors = ctx.errors.Entries();
  std::sort(errors.begin(), errors.end(), [](const auto& x, const auto& y) {
    return std::tie(x.category, x.address, x.secondaryAddress, x.message) <
           std::tie(y.category, y.address, y.secondaryAddress, y.message);
  });
  for (const auto& e : errors) {
    out << "error\t" << rex::codegen::AnalysisErrors::CategoryName(e.category) << '\t';
    hex(e.address) << '\t'; hex(e.secondaryAddress) << '\t' << e.message << '\n';
  }
  for (const auto& s : ctx.binary().sections()) {
    out << "section\t" << s.name << '\t'; hex(s.baseAddress) << '\t' << s.size << '\t' << s.executable << '\n';
  }
  for (int i = emit_fingerprints ? 3 : 4; i < argc; ++i) {
    const auto a = static_cast<uint32_t>(std::stoul(argv[i], nullptr, 16));
    const auto* section = ctx.binary().findSection(a);
    const auto* owner = ctx.graph.getFunctionContaining(a);
    out << "query\t"; hex(a) << '\t' << (section ? section->name : "outside-image") << '\t';
    hex(owner ? owner->base() : 0) << '\t' << ctx.graph.isEntryPoint(a) << '\n';
  }
  out << "summary\tfunctions\t" << entries.size() << "\tnon_pdata_entries\t" << nonPdata
      << "\tcovered_words\t" << covered.size() << "\ttables\t" << tables
      << "\ttable_slots\t" << slots << "\terrors\t" << errors.size() << '\n';
  out.close();
  if (!out) return 1;
  // Validation failures are preserved in the report and exit code.
  return result.has_value() ? 0 : 1;
}
