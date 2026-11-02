#include "catalog.h"
#include "catalog_paths.h"
#include "action_trace.h"
#include "digest_batch.h"
#include "sha256.h"
#include "workspace_lease.h"
#include "host_platform.h"
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>
#include <rex/filesystem/devices/stfs_xbox.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
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
  auto rel = CatalogVerificationPath(file).lexically_relative(CatalogVerificationPath(root));
  if (rel.empty() || rel.is_absolute() || *rel.begin() == "..") throw std::runtime_error("Catalog destination redirected outside its root");
  if (CatalogLaunchVerificationActive() && HasProtectedVerificationPath(file)) return;
  for (auto p = file; p != root && !p.empty();) {
    if (fs::is_symlink(p)) throw std::runtime_error("Catalog path contains a link");
    const auto parent = p.parent_path();
    // Equivalent roots can have different spellings (case, separators, '.').
    // A filesystem root is its own parent; never spin there if the lexical
    // comparison did not match. Canonical containment was checked above, and
    // every visited ancestor still receives the original link check.
    if (parent == p) break;
    p = parent;
  }
}
std::string Fingerprint(const Json& files) {
  if (CatalogLaunchVerificationActive()) {
    std::string bytes;
    for (const auto& f : files)
      bytes += f.at("path").get<std::string>() + "\t" + f.at("sha256").get<std::string>() + "\n";
    return PlatformSha256Hex({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
  }
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
// Content-install discs nest deeper than MAX_PATH; walk and delete without it.
fs::path Extended(const fs::path& path) {
#ifdef _WIN32
  auto name = fs::absolute(path).lexically_normal().make_preferred().native();
  if (name.starts_with(L"\\\\?\\")) return name;
  if (name.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + name.substr(2);
  return L"\\\\?\\" + name;
#else
  return path;
#endif
}
template<class Units> std::string Name(const Units& units) {
  std::u16string s;
  for (auto v : units) { if (!uint16_t(v)) break; s.push_back(char16_t(uint16_t(v))); }
  return s.empty() ? "" : Utf8(fs::path(s));
}
bool ReadPackageHeader(const fs::path& file, rex::filesystem::StfsHeader& header) {
  if (!fs::is_regular_file(file)) return false;
  std::ifstream in(file, std::ios::binary);
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (in.gcount() != sizeof(header)) return false;
  const auto magic = header.header.magic.get();
  using Type = rex::filesystem::XContentPackageType;
  return magic == Type::kCon || magic == Type::kLive || magic == Type::kPirs;
}
// Bounded by the field shared by both metadata versions; larger v1 icons are skipped.
constexpr size_t kThumbnailLimit = rex::filesystem::XContentMetadata::kThumbLengthV2;
// The package's own icon, else its title icon; only PNG data is kept.
std::vector<uint8_t> Thumbnail(const rex::filesystem::XContentMetadata& md) {
  static constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  for (const auto& [data, size] : {std::pair{md.thumbnail, uint32_t(md.thumbnail_size)},
                                   std::pair{md.title_thumbnail, uint32_t(md.title_thumbnail_size)}}) {
    if (size < sizeof(kPng) || size > kThumbnailLimit || !std::equal(std::begin(kPng), std::end(kPng), data)) continue;
    return {data, data + size};
  }
  return {};
}
// Content-addressed, so identical icons share one file; readers verify the hash.
std::string SaveThumbnail(const ProjectPaths& paths, const std::vector<uint8_t>& png) {
  const auto sha = Sha256Hex(png);
  WorkspaceLease lease(paths);
  const auto root = paths.local() / "catalog" / "thumbnails";
  const auto file = root / (sha + ".png");
  Safe(file, paths.local());
  if (const auto existing = DigestFile(file); existing && existing->sha256 == sha) return sha;
  fs::create_directories(root);
  const auto tmp = root / (sha + ".tmp"); Safe(tmp, paths.local());
  std::ofstream out(tmp, std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size())); out.close();
  if (!out) throw std::runtime_error("Could not persist the game thumbnail");
#ifdef _WIN32
  if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Could not publish the game thumbnail");
#else
  fs::rename(tmp, file);
#endif
  return sha;
}
void PackageMetadata(const fs::path& original, Json& r, std::vector<uint8_t>& thumbnail) {
  auto header = std::make_unique<rex::filesystem::StfsHeader>();
  if (!ReadPackageHeader(original, *header)) return;
  const auto& md = header->metadata;
  thumbnail = Thumbnail(md);
  r["package_title_id"] = fmt::format("{:08X}", uint32_t(md.execution_info.title_id));
  r["content_type"] = fmt::format("{:08X}", uint32_t(md.content_type.get()));
  r["publisher"] = Name(md.publisher_raw.uint);
  r["name"] = Name(md.title_name_raw.uint);
  Json names = Json::array();
  for (const auto& language : md.display_name_raw.uint) names.push_back(Name(language));
  r["display_names"] = names;
  if (r["name"] == "") for (const auto& name : names) if (name != "") { r["name"] = name; break; }
}
// Disc roots carry the dashboard art package (nxeart). Only its title name and
// icon are used: its content type and display names describe the art package,
// not the game, and a package from another title is ignored.
void DiscArt(const fs::path& root, Json& r, std::vector<uint8_t>& thumbnail) {
  if ((r["name"] != "" && !thumbnail.empty()) || r["title_id"] == "") return;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.is_symlink() || !entry.is_regular_file() || Key(Utf8(entry.path().filename())) != "nxeart") continue;
    auto header = std::make_unique<rex::filesystem::StfsHeader>();
    if (!ReadPackageHeader(entry.path(), *header)) return;
    const auto& md = header->metadata;
    if (fmt::format("{:08X}", uint32_t(md.execution_info.title_id)) != r["title_id"].get<std::string>()) return;
    if (r["name"] == "") r["name"] = Name(md.title_name_raw.uint);
    if (thumbnail.empty()) thumbnail = Thumbnail(md);
    return;
  }
}
}

std::string ImportGame(const ProjectPaths& paths, const std::vector<Target>& targets,
    const fs::path& source, const fs::path& original, const std::function<bool()>& cancelled) {
  ActionTrace trace("import.game");
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
  // Preserve the entire imported file inventory, including guest assets in
  // deep folders; the extracted source is not necessarily short on Windows.
  for (const auto& f : fs::recursive_directory_iterator(LongPath(source))) {
    stop(); Safe(f.path(), source);
    if (!f.is_regular_file()) continue;
    const auto rel = f.path().lexically_relative(LongPath(source)).generic_u8string();
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
  std::vector<uint8_t> thumbnail;
  PackageMetadata(original, record, thumbnail);
  DiscArt(source, record, thumbnail);
  if (!thumbnail.empty()) record["thumbnail_sha256"] = SaveThumbnail(paths, thumbnail);
  Target target; target.id = id; target.aot = false;
  // Discs and content revisions share a title directory, never a writable root.
  // DLC packages may live alongside them under dlc/, outside every game root.
  // The directory carries 64 bits of the identity: content-install discs nest
  // packages deep enough that the full 256-bit name exceeds MAX_PATH (error 206)
  // under an extracted package's data folder. The catalog keeps the full id.
  const auto disc = identity.disc_number ? "disc-" + std::to_string(identity.disc_number) + "-" : "";
  target.game_root = paths.inputs() / "games" /
      (identity.title_id.empty() ? "unknown" : identity.title_id) / (disc + id.substr(0, 16));
  if (const auto* recipe = Recipe(targets, record)) {
    target = *recipe;
    if (record["name"] == "") record["name"] = recipe->library.display_name;
  }
  // Content already owned by this environment needs verification, not another
  // full disc copy. Never adopt external paths, links, or a root already bound
  // to a different inventory. Changed revisions keep the isolated import path.
  if (IsWithin(source, paths.inputs() / "games")) {
    Safe(source, paths.inputs());
    const auto canonical_source = fs::weakly_canonical(source);
    const auto occupied = std::any_of(targets.begin(), targets.end(), [&](const auto& existing) {
      return existing.catalogued && fs::weakly_canonical(existing.game_root) == canonical_source &&
          existing.id != "g" + id.substr(0, 63) && existing.id != target.id;
    });
    if (!occupied) target.game_root = source;
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
    if (!record.contains("thumbnail_sha256") && old.contains("thumbnail_sha256")) record["thumbnail_sha256"] = old["thumbnail_sha256"];
  }
  record["content_root"] = Utf8(target.game_root.lexically_normal().lexically_relative(paths.inputs().lexically_normal()));
  if (!previously_complete) Save(paths, id, record); // durable interruption marker before copying
  try {
    Import(paths, target, source, cancelled);
    stop(); record["complete"] = true; record.erase("error"); Save(paths, id, record);
  } catch (const fs::filesystem_error& e) {
    // MSVC's what() contains local-code-page Windows diagnostics. JSON and the
    // desktop use UTF-8; retain the actual code and Unicode paths without letting
    // serialization replace the original failure with invalid-UTF-8 errors.
    auto message = "Erro de arquivo durante a importação (código " + std::to_string(e.code().value()) + ").";
    if (e.code().value() == 206)  // ERROR_FILENAME_EXCED_RANGE: Windows long paths are disabled
      message += " O caminho ficou longo demais para o Windows; use um ambiente em uma pasta mais curta (por exemplo C:\\x360).";
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
  CatalogPaths content_paths(paths.inputs());
  for (const auto& file : fs::directory_iterator(root)) {
    CheckVerificationCancellation();
    if (file.path().extension() != ".json") continue;
    Target t; t.id = Utf8(file.path().stem()); t.aot = false; t.catalogued = true; t.importable = false;
    t.catalog_id = t.id;
    ActionTrace target_trace("catalog.imported." + t.id);
    t.label = t.label_en = t.id;
    try {
      Safe(file.path(), paths.local());
      if (fs::file_size(file.path()) > 64 * 1024 * 1024) throw std::runtime_error("Catalog record too large");
      std::ifstream in(file.path()); const auto r = Json::parse(in);
      if (r.at("schema") != 1 || r.at("id") != t.id || !r.at("files").is_array() || r.at("files").empty() ||
          t.id != Fingerprint(r.at("files"))) throw std::runtime_error("Invalid catalog identity");
      t.game_root = paths.inputs() / Relative(r.at("content_root")); Safe(t.game_root, paths.inputs());
      std::set<fs::path> seen;
      std::unordered_set<fs::path> launch_seen;
      const bool launching = CatalogLaunchVerificationActive();
      for (const auto& f : r.at("files")) {
        CheckVerificationCancellation();
        auto rel = Relative(f.at("path")); const auto hash = f.at("sha256").get<std::string>();
        if (!HashValid(hash) || !(launching ? launch_seen.insert(rel).second : seen.insert(rel).second))
          throw std::runtime_error("Invalid catalog inventory");
        if (!content_paths.Resolve(t.game_root / rel)) Safe(t.game_root / rel, paths.inputs());
        t.inputs.push_back({t.game_root / rel, Utf8(rel.filename()), hash, rel});
      }
      t.title_id = r.at("title_id"); t.version = r.at("version"); t.media_id = r.at("media_id");
      t.original_name = r.at("original_name");
      if (r.contains("thumbnail_sha256")) {
        t.thumbnail_sha256 = r.at("thumbnail_sha256").get<std::string>();
        if (!HashValid(t.thumbnail_sha256)) throw std::runtime_error("Invalid catalog thumbnail");
        t.thumbnail = root / "thumbnails" / (t.thumbnail_sha256 + ".png");
      }
      auto name = r.at("name").get<std::string>();
      if (name.empty()) name = "Xbox 360" + (t.title_id.empty() ? " (unknown title)" : " " + t.title_id);
      t.label = t.label_en = name + (t.version.empty() ? "" : " — " + t.version);
      // Discs of one title share its name; tell them apart in the library.
      if (const auto number = r.value("disc_number", 0u), count = r.value("disc_count", 0u); number && count > 1) {
        t.label += " (Disco " + std::to_string(number) + " de " + std::to_string(count) + ")";
        t.label_en += " (Disc " + std::to_string(number) + " of " + std::to_string(count) + ")";
      }
      if (!r.value("complete", false)) t.import_error = r.value("error", "Import interrupted; select the same source to resume.");
      if (const auto* recipe = Recipe(targets, r)) {
        auto& existing = targets[size_t(recipe - targets.data())];
        if (recipe->game_root != t.game_root) existing.preparation_source = t.game_root;
        existing.catalogued = true; existing.title_id = t.title_id; existing.version = t.version;
        existing.media_id = t.media_id; existing.original_name = t.original_name; existing.import_error = t.import_error;
        existing.thumbnail = t.thumbnail; existing.thumbnail_sha256 = t.thumbnail_sha256;
        existing.catalog_id = t.catalog_id;
        existing.label = existing.label_en = t.label;
        continue;
      }
      const auto content_id = t.id;
      t.id = "g" + content_id.substr(0, 63);
      t.aot = true; t.generic_aot = true;
      t.manifest = paths.local() / "aot" / t.id / (t.id + "_manifest.toml");
      // Keep the full catalog/title identity, but leave room for SDK try_compile
      // directories in an extracted package with a long installation path.
#ifdef _WIN32
      if (HostPlatform()=="windows-x64") t.exe = paths.build() / "aot" / t.id.substr(0, 17) / (t.id + ExecutableSuffix());
      else
#endif
      t.exe = paths.build() / HostPlatform() / "aot" / t.id.substr(0,17) / (t.id+ExecutableSuffix());
      t.user_root = UserDataRoot(paths, paths.local() / "user" / t.id);
      if (!t.title_id.empty())
        t.library = {t.title_id, r.value("content_type", std::string("00004000")), content_id.substr(0, 40), name, content_id};
      if (fs::is_regular_file(t.manifest) && !t.library.title_id.empty()) {
        // Folder imports have no container content type. Reuse the generated
        // profile's source-bound routing metadata instead of inventing a second
        // registration under the folder default. Execution still checks its receipt.
        const auto setup_file = t.manifest.parent_path() / "preparation.toml";
        std::ifstream recipe(setup_file, std::ios::binary);
        if (!recipe) throw std::runtime_error("Generated AOT library metadata is missing; regenerate the project.");
        const auto setup = toml::parse(recipe, Utf8(setup_file));
        const auto library = setup["library"];
        const auto type = library["content_type"].value_or(std::string{});
        if (library["source_sha256"].value_or(std::string{}) != content_id ||
            library["title_id"].value_or(std::string{}) != t.library.title_id ||
            library["file_name"].value_or(std::string{}) != t.library.file_name ||
            type.size() != 8 || type.find_first_not_of("0123456789ABCDEF") != std::string::npos ||
            (r.contains("content_type") && type != t.library.content_type))
          throw std::runtime_error("Generated AOT library identity differs from the imported source; regenerate the project.");
        t.library.content_type = type;
      }
      t.limitations = "Preparação AOT automática disponível. Compilar não comprova compatibilidade. Inicie pela My Games original.";
      t.limitations_en = "Automatic AOT preparation available. Compilation does not prove compatibility. Launch through original My Games.";
      for (const auto& dep : r.value("imports", std::vector<std::string>{})) {
        t.limitations += "\nImport: " + dep; t.limitations_en += "\nImport: " + dep;
      }
    } catch (const VerificationCancelled&) { throw; }
      catch (const std::exception& e) { t.import_error = std::string("Catalog error; record preserved: ") + e.what(); t.inputs.clear(); }
    targets.push_back(std::move(t));
  }
}

void RemoveGame(const ProjectPaths& paths, const std::vector<Target>& catalog, const std::string& id) {
  const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& t) { return t.id == id; });
  if (found == catalog.end() || !InGameLibrary(*found))
    throw std::runtime_error("Somente jogos importados podem ser removidos da biblioteca.");
  const auto& game = *found;
  WorkspaceLease lease(paths);
  const auto record = paths.local() / "catalog" / (game.catalog_id + ".json");
  Safe(record, paths.local());
  // An unreadable record identifies no content; only the record itself goes.
  std::optional<fs::path> content;
  std::string source;
  if (fs::is_regular_file(record)) {
    Json r;
    try { std::ifstream in(record); r = Json::parse(in); } catch (const std::exception&) {}
    if (r.is_object() && r.value("id", std::string{}) == game.catalog_id && r.contains("content_root")) {
      content = paths.inputs() / Relative(r.at("content_root"));
      Safe(*content, paths.inputs());
      source = r.value("source", std::string{});
    }
  }
  // Only whole trees this environment created: no links/junctions inside.
  const auto own_tree = [](const fs::path& root) {
    for (const auto& entry : fs::recursive_directory_iterator(Extended(root))) {
      const auto type = entry.symlink_status().type();
      if (type != fs::file_type::regular && type != fs::file_type::directory)
        throw std::runtime_error("Link ou arquivo especial encontrado; nada foi apagado: " + Utf8(entry.path()));
    }
  };
  // Identify every deletion before the first write.
  const auto games = paths.inputs() / "games";
  bool delete_content = content && fs::is_directory(*content);
  if (delete_content) {
    const auto relative = content->lexically_normal().lexically_relative(games.lexically_normal());
    // games/<title>/<root>; never the shared inputs or a title directory itself.
    if (relative.empty() || *relative.begin() == ".." || std::distance(relative.begin(), relative.end()) < 2 || fs::is_symlink(*content))
      throw std::runtime_error("Conteúdo importado fora da pasta de jogos; preservado: " + Utf8(*content));
    // A folder imported in place holds the user's own files; another record may share a root.
    if (!source.empty() && IsWithin(PathFromUtf8(source), *content)) delete_content = false;
    for (const auto& other : catalog)
      if (&other != &game && other.catalogued && (IsWithin(other.game_root, *content) || IsWithin(*content, other.game_root) ||
          (!other.preparation_source.empty() && IsWithin(other.preparation_source, *content)))) delete_content = false;
    if (delete_content) own_tree(*content);
  }
  fs::path project, build;
  if (game.generic_aot) {
    project = paths.local() / "aot" / game.id;
    build = game.exe.parent_path();
    Safe(project, paths.local()); Safe(build, paths.local());
    if (fs::exists(build)) {
      std::ifstream owner_input(build / ".x360n-aot-owner"); std::string owner;
      std::getline(owner_input, owner);
      if (!owner.empty() && owner.back() == '\r') owner.pop_back();
      if (owner != game.id) throw std::runtime_error("O build AOT pertence a outro título; preservado: " + Utf8(build));
      own_tree(build);
    }
    if (fs::exists(project)) own_tree(project);
  }
  // Enumeration first: no dashboard keeps listing content that is about to go.
  for (const auto& dashboard : catalog) WithdrawLibrary(dashboard, game, catalog);
  if (!build.empty()) fs::remove_all(Extended(build));
  if (!project.empty()) fs::remove_all(Extended(project));
  if (game.generic_aot)
    for (const auto* suffix : {".incomplete", ".runtime-error", ".lock"})
      fs::remove(paths.local() / "preparation" / (game.id + suffix));
  if (delete_content) {
    fs::remove_all(Extended(*content));
    if (const auto title = content->parent_path(); fs::is_directory(title) && fs::is_empty(title)) fs::remove(title);
  }
  fs::remove(record);
}

std::vector<uint8_t> ReadThumbnail(const Target& target) {
  if (target.thumbnail_sha256.empty()) return {};
  std::error_code ec;
  if (fs::is_symlink(target.thumbnail, ec) || !fs::is_regular_file(target.thumbnail, ec)) return {};
  const auto size = fs::file_size(target.thumbnail, ec);
  if (ec || size > kThumbnailLimit) return {};
  std::vector<uint8_t> png(size);
  std::ifstream in(target.thumbnail, std::ios::binary);
  in.read(reinterpret_cast<char*>(png.data()), std::streamsize(size));
  if (size_t(in.gcount()) != size || Sha256Hex(png) != target.thumbnail_sha256) return {};
  return png;
}
}
