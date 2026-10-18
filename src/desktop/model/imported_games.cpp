#include "catalog.h"
#include "sha256.h"
#include "workspace_lease.h"
#include <nlohmann/json.hpp>
#include <rex/filesystem/devices/stfs_xbox.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <fmt/format.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace x360n::desktop {
namespace {
using Json = nlohmann::json;
bool HashValid(const std::string& s) {
  return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string Key(std::string name) {
  for (auto& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return name;
}
fs::path Relative(const std::string& s) {
  auto p = PathFromUtf8(s);
  if (p.empty() || p.is_absolute()) throw std::runtime_error("Invalid catalog path");
  for (const auto& part : p) if (!IsSafePackageEntryName(Utf8(part))) throw std::runtime_error("Invalid catalog component");
  return p;
}
void Safe(const fs::path& file, const fs::path& root) {
  auto rel = fs::weakly_canonical(file).lexically_relative(fs::weakly_canonical(root));
  if (rel.empty() || rel.is_absolute() || *rel.begin() == "..") throw std::runtime_error("Catalog destination redirected outside its root");
  for (auto p = file; p != root && !p.empty(); p = p.parent_path())
    if (fs::is_symlink(p)) throw std::runtime_error("Catalog path contains a link");
}
std::string Fingerprint(const Json& files) {
  Sha256 hash;
  for (const auto& f : files) {
    const auto line = f.at("path").get<std::string>() + "\t" + f.at("sha256").get<std::string>() + "\n";
    hash.Update({reinterpret_cast<const uint8_t*>(line.data()), line.size()});
  }
  return ToHex(hash.Finish());
}
void Save(const ProjectPaths& paths, const std::string& id, const Json& record) {
  WorkspaceLease lease(paths);
  const auto root = paths.local() / "catalog";
  const auto file = root / (id + ".json");
  Safe(file, paths.local()); fs::create_directories(root);
  const auto tmp = root / (id + ".tmp"); Safe(tmp, paths.local());
  // Validate serialization before opening/truncating a recoverable staging file.
  const auto text = record.dump(2);
  std::ofstream out(tmp, std::ios::binary); out << text << '\n'; out.close();
  if (!out) throw std::runtime_error("Could not persist the game catalog");
#ifdef _WIN32
  if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Could not publish the game catalog");
#else
  fs::rename(tmp, file);
#endif
}
// Match the exact executable AND all recipe assets, never just a Title ID/name.
const Target* Recipe(const std::vector<Target>& targets, const Json& record) {
  std::map<std::string, std::string> files;
  for (const auto& f : record.at("files")) files.emplace(Key(f.at("path").get<std::string>()), f.at("sha256"));
  for (const auto& t : targets) {
    if (!t.aot || t.kind != "game" || t.inputs.empty() || !t.importable) continue;
    if (t.inputs.front().sha256 != record.at("xex_sha256").get<std::string>()) continue;
    std::set<std::string> required;
    for (const auto& i : t.inputs) required.insert(Key(Utf8(i.destination.lexically_relative(t.game_root))));
    // Two inventories must not share a writable compatibility-profile root.
    // Additional or changed assets select an independent generic project.
    if (required.size() != files.size()) continue;
    bool match = true;
    for (const auto& i : t.inputs) {
      const auto path = i.destination.lexically_relative(t.game_root).generic_u8string();
      const auto key = Key(std::string(path.begin(), path.end()));
      auto it = files.find(key);
      if (it == files.end() || it->second != i.sha256) { match = false; break; }
    }
    if (match) return &t;
  }
  return nullptr;
}
template<class Units> std::string Name(const Units& units) {
  std::u16string s;
  for (auto v : units) { if (!uint16_t(v)) break; s.push_back(char16_t(uint16_t(v))); }
  return s.empty() ? "" : Utf8(fs::path(s));
}
void PackageMetadata(const fs::path& original, Json& r) {
  if (!fs::is_regular_file(original)) return;
  rex::filesystem::StfsHeader header{};
  std::ifstream in(original, std::ios::binary);
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (in.gcount() != sizeof(header)) return;
  const auto magic = header.header.magic.get();
  using Type = rex::filesystem::XContentPackageType;
  if (magic != Type::kCon && magic != Type::kLive && magic != Type::kPirs) return;
  const auto& md = header.metadata;
  r["package_title_id"] = fmt::format("{:08X}", uint32_t(md.execution_info.title_id));
  r["content_type"] = fmt::format("{:08X}", uint32_t(md.content_type.get()));
  r["publisher"] = Name(md.publisher_raw.uint);
  r["name"] = Name(md.title_name_raw.uint);
  Json names = Json::array();
  for (const auto& language : md.display_name_raw.uint) names.push_back(Name(language));
  r["display_names"] = names;
  if (r["name"] == "") for (const auto& name : names) if (name != "") { r["name"] = name; break; }
}
}

std::string ImportGame(const ProjectPaths& paths, const std::vector<Target>& targets,
    const fs::path& source, const fs::path& original, const std::function<bool()>& cancelled) {
  const auto stop = [&] { if (cancelled && cancelled()) throw std::runtime_error("Import cancelled; retry with the same content to resume."); };
  stop();
  if (!fs::is_directory(source)) throw std::runtime_error("Select an extracted game folder, ISO or STFS package.");
  // Only a game root is accepted. Update partitions/auxiliary modules never
  // turn into separate games merely because they contain an XEX.
  const auto anchor = source / "default.xex";
  const auto identities = IdentifyContent(targets, anchor, cancelled);
  if (identities.size() != 1 || !identities.front().valid || identities.front().kind != "game")
    throw std::runtime_error("The folder needs a valid Xbox 360 default.xex. No game was imported.");
  const auto& identity = identities.front();
  std::map<std::string, std::string> inventory;
  Json modules = Json::array();
  std::set<std::string> names;
  for (const auto& f : fs::recursive_directory_iterator(source)) {
    stop(); Safe(f.path(), source);
    if (!f.is_regular_file()) continue;
    const auto rel = f.path().lexically_relative(source).generic_u8string();
    const std::string name(rel.begin(), rel.end()); Relative(name);
    auto folded = name; for (auto& c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (!names.insert(folded).second) throw std::runtime_error("Duplicate case-insensitive game path");
    auto digest = DigestFile(f.path()); if (!digest) throw std::runtime_error("Could not read game content");
    inventory.emplace(name, digest->sha256);
    std::ifstream signature(f.path(), std::ios::binary);
    char magic[4]{}; signature.read(magic, 4);
    if (std::string_view(magic, 3) == "XEX") {
      auto found = IdentifyContent({}, f.path(), cancelled);
      if (!found.empty() && found[0].valid) modules.push_back({{"path", name}, {"sha256", digest->sha256},
        {"title_id", found[0].title_id}, {"version", found[0].version}, {"imports", found[0].imports},
        {"module_flags", found[0].module_flags}, {"entry_point", found[0].entry_point},
        {"image_base", found[0].image_base}, {"image_size", found[0].image_size}});
    }
  }
  Json files = Json::array();
  for (const auto& [path, hash] : inventory) files.push_back({{"path", path}, {"sha256", hash}});
  const auto anchor_file = std::find_if(inventory.begin(), inventory.end(), [](const auto& f) { return Key(f.first) == "default.xex"; });
  if (anchor_file == inventory.end() || anchor_file->second != identity.sha256) throw std::runtime_error("Game changed during identification");
  const auto id = Fingerprint(files);
  Json record{{"schema", 1}, {"id", id}, {"files", files}, {"xex_sha256", identity.sha256},
    {"title_id", identity.title_id}, {"version", identity.version}, {"media_id", identity.media_id},
    {"original_name", identity.original_name}, {"base_version", identity.base_version}, {"region", identity.region},
    {"disc_number", identity.disc_number}, {"disc_count", identity.disc_count},
    {"imports", identity.imports}, {"modules", modules}, {"name", ""}, {"source", Utf8(original)}, {"complete", false}};
  PackageMetadata(original, record);
  Target target; target.id = id; target.aot = false;
  // Discs and content revisions share a title directory, never a writable root.
  // DLC packages may live alongside them under dlc/, outside every game root.
  const auto disc = identity.disc_number ? "disc-" + std::to_string(identity.disc_number) + "-" : "";
  target.game_root = paths.inputs() / "games" /
      (identity.title_id.empty() ? "unknown" : identity.title_id) / (disc + id);
  if (const auto* recipe = Recipe(targets, record)) {
    target = *recipe;
    if (record["name"] == "") record["name"] = recipe->library.display_name;
  }
  // Store every asset, including files beyond an AOT recipe's minimum inventory.
  target.inputs.clear();
  for (const auto& f : files) {
    auto rel = Relative(f.at("path"));
    target.inputs.push_back({target.game_root / rel, Utf8(rel.filename()), f.at("sha256"), rel});
  }
  // An existing successful duplicate stays successful even if a retry is cancelled.
  const auto previous = paths.local() / "catalog" / (id + ".json");
  bool previously_complete = false;
  if (fs::is_regular_file(previous)) {
    Safe(previous, paths.local());
    std::ifstream in(previous); auto old = Json::parse(in);
    if (old.at("files") != files) throw std::runtime_error("Catalog identity conflict; existing record preserved");
    // Existing catalogs, including interrupted imports in the old layout, keep
    // their exact location. Reimport must not duplicate or relocate local data.
    target.game_root = paths.inputs() / Relative(old.at("content_root"));
    Safe(target.game_root, paths.inputs());
    for (auto& input : target.inputs) input.destination = target.game_root / input.source_relative;
    previously_complete = old.value("complete", false);
    if (record["name"] == "") record["name"] = old.value("name", "");
  }
  record["content_root"] = Utf8(target.game_root.lexically_relative(paths.inputs()));
  if (!previously_complete) Save(paths, id, record); // durable interruption marker before copying
  try {
    Import(paths, target, source, cancelled);
    stop(); record["complete"] = true; record.erase("error"); Save(paths, id, record);
  } catch (const fs::filesystem_error& e) {
    // MSVC's what() contains local-code-page Windows diagnostics. JSON and the
    // desktop use UTF-8; retain the actual code and Unicode paths without letting
    // serialization replace the original failure with invalid-UTF-8 errors.
    auto message = "Erro de arquivo durante a importação (código " + std::to_string(e.code().value()) + ").";
    if (!e.path1().empty()) message += " Caminho: " + Utf8(e.path1());
    if (!e.path2().empty()) message += " Destino: " + Utf8(e.path2());
    if (!previously_complete) { record["error"] = message; Save(paths, id, record); }
    throw std::runtime_error(message);
  } catch (const std::exception& e) {
    if (!previously_complete) { record["error"] = e.what(); Save(paths, id, record); }
    throw;
  }
  return target.aot ? target.id : "g" + id.substr(0, 63);
}

void ReadImportedGames(const ProjectPaths& paths, std::vector<Target>& targets) {
  const auto root = paths.local() / "catalog";
  if (!fs::is_directory(root)) return;
  for (const auto& file : fs::directory_iterator(root)) {
    if (file.path().extension() != ".json") continue;
    Target t; t.id = Utf8(file.path().stem()); t.aot = false; t.catalogued = true; t.importable = false;
    t.label = t.label_en = t.id;
    try {
      Safe(file.path(), paths.local());
      if (fs::file_size(file.path()) > 64 * 1024 * 1024) throw std::runtime_error("Catalog record too large");
      std::ifstream in(file.path()); const auto r = Json::parse(in);
      if (r.at("schema") != 1 || r.at("id") != t.id || !r.at("files").is_array() || r.at("files").empty() ||
          t.id != Fingerprint(r.at("files"))) throw std::runtime_error("Invalid catalog identity");
      t.game_root = paths.inputs() / Relative(r.at("content_root")); Safe(t.game_root, paths.inputs());
      std::set<fs::path> seen;
      for (const auto& f : r.at("files")) {
        auto rel = Relative(f.at("path")); const auto hash = f.at("sha256").get<std::string>();
        if (!HashValid(hash) || !seen.insert(rel).second) throw std::runtime_error("Invalid catalog inventory");
        Safe(t.game_root / rel, paths.inputs());
        t.inputs.push_back({t.game_root / rel, Utf8(rel.filename()), hash, rel});
      }
      t.title_id = r.at("title_id"); t.version = r.at("version"); t.media_id = r.at("media_id");
      t.original_name = r.at("original_name");
      auto name = r.at("name").get<std::string>();
      if (name.empty()) name = "Xbox 360" + (t.title_id.empty() ? " (unknown title)" : " " + t.title_id);
      t.label = t.label_en = name + (t.version.empty() ? "" : " — " + t.version);
      if (!r.value("complete", false)) t.import_error = r.value("error", "Import interrupted; select the same source to resume.");
      if (const auto* recipe = Recipe(targets, r)) {
        auto& existing = targets[size_t(recipe - targets.data())];
        if (recipe->game_root != t.game_root) existing.preparation_source = t.game_root;
        existing.catalogued = true; existing.title_id = t.title_id; existing.version = t.version;
        existing.media_id = t.media_id; existing.original_name = t.original_name; existing.import_error = t.import_error;
        existing.label = existing.label_en = t.label;
        continue;
      }
      const auto content_id = t.id;
      t.id = "g" + content_id.substr(0, 63);
      t.aot = true; t.generic_aot = true;
      t.manifest = paths.local() / "aot" / t.id / (t.id + "_manifest.toml");
      // Keep the full catalog/title identity, but leave room for SDK try_compile
      // directories in an extracted package with a long installation path.
      t.exe = paths.build() / "aot" / t.id.substr(0, 17) / (t.id + ".exe");
      t.user_root = UserDataRoot(paths, paths.local() / "user" / t.id);
      if (!t.title_id.empty())
        t.library = {t.title_id, r.value("content_type", std::string("00004000")), content_id.substr(0, 40), name, content_id};
      t.limitations = "Preparação AOT automática disponível. Compilar não comprova compatibilidade. Inicie pela My Games original.";
      t.limitations_en = "Automatic AOT preparation available. Compilation does not prove compatibility. Launch through original My Games.";
      for (const auto& dep : r.value("imports", std::vector<std::string>{})) {
        t.limitations += "\nImport: " + dep; t.limitations_en += "\nImport: " + dep;
      }
    } catch (const std::exception& e) { t.import_error = std::string("Catalog error; record preserved: ") + e.what(); t.inputs.clear(); }
    targets.push_back(std::move(t));
  }
}
}
