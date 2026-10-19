#include "../../src/desktop/model/catalog.h"
#include "sha256.h"
#include "digest_batch.h"
#include "../../src/desktop/model/import_source.h"
#include "session_profile.h"
#include "preparation.h"
#include "../supervisor/scratch.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

using namespace x360n;
using namespace x360n::desktop;
namespace fs = std::filesystem;
namespace {
void Write(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s;
}
std::string Xex(unsigned version = 1, unsigned title = 0x12345678) {
  std::string s(64, '\0');
  const auto be = [&](size_t p, unsigned n) { for (int i=0; i<4; ++i) s[p+i]=char(n>>(24-8*i)); };
  s.replace(0, 4, "XEX2"); be(4,1); be(8,56); be(20,1); be(24,0x40006); be(28,32);
  be(32,0xAABBCCDD); be(36,version); be(44,title); return s;
}
// Minimal PIRS header: Title ID in the execution info, UTF-16BE title name.
std::string ArtPackage(unsigned title, const std::u16string& name) {
  std::string s(0x971A, '\0'); s.replace(0, 4, "PIRS");
  for (int i=0; i<4; ++i) s[0x360+i]=char(title>>(24-8*i));
  for (size_t i=0; i<name.size(); ++i) { s[0x1691+2*i]=char(name[i]>>8); s[0x1692+2*i]=char(name[i]&0xFF); }
  return s;
}
struct Fixture {
  fs::path root = UniqueTempPath("universal_library");
  ProjectPaths paths{root};
  fs::path source = root / "source";
  Fixture() { Write(source/"default.xex", Xex()); Write(source/"assets/data", "asset"); Write(root/".local/user/save", "keep"); }
  void Infrastructure() {
    for (const auto& file : {"titles/common/generic/CMakeLists.txt", "titles/common/generic/main.cpp",
         "titles/common/generic/modules.cmake", "external/rexglue-sdk/resources/templates/init/rexglue_cmake.inja"}) {
      auto dest=root/file; fs::create_directories(dest.parent_path()); fs::copy_file(fs::path(X360N_SOURCE_ROOT)/file,dest);
    }
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
}
TEST_CASE("universal library persists unsupported titles and exact duplicates", "[universal]") {
  Fixture f;
  const auto id = ImportGame(f.paths, {}, f.source, f.source);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size()==1);
  CHECK(games[0].id==id); CHECK(games[0].catalogued); CHECK(games[0].generic_aot);
  CHECK(games[0].title_id=="12345678"); CHECK(games[0].version=="0.0.0.1");
  CHECK(Inspect(f.paths,games[0]).inputs_ok); CHECK_FALSE(Inspect(f.paths,games[0]).prepared);
  CHECK(ImportGame(f.paths,games,f.source,f.source)==id);
  CHECK(ReadCatalog(f.paths).size()==1);
  CHECK(fs::file_size(f.root/".local/user/save")==4);
  SECTION("different version") { Write(f.source/"default.xex",Xex(2)); }
  SECTION("different title") { Write(f.source/"default.xex",Xex(1,0x87654321)); }
  SECTION("same executable different content") { Write(f.source/"assets/data","different assets"); }
  CHECK(ImportGame(f.paths,games,f.source,f.source)!=id);
  CHECK(ReadCatalog(f.paths).size()==2);
}
TEST_CASE("folder imports name discs from their own title art package", "[universal][disc-name]") {
  Fixture f; std::string expected;
  SECTION("matching title, any case") { Write(f.source/"NXEART", ArtPackage(0x12345678, u"Disc Game")); expected = "Disc Game"; }
  SECTION("art package of another title is ignored") { Write(f.source/"nxeart", ArtPackage(0x87654321, u"Other Game")); }
  SECTION("art that is not a package is ignored") { Write(f.source/"nxeart", "art"); }
  SECTION("nested art package is not the disc root") { Write(f.source/"media/nxeart", ArtPackage(0x12345678, u"Nested")); }
  const auto id = ImportGame(f.paths, {}, f.source, f.source);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  CHECK(games[0].label.starts_with(expected.empty() ? "Xbox 360 12345678" : expected));
  if (expected.empty()) return;
  // Records written before the name was read regain it on reimport of the same content.
  const auto record_file = fs::directory_iterator(f.paths.local()/"catalog")->path();
  auto record = nlohmann::json::parse(std::ifstream(record_file));
  CHECK(record["name"] == expected);
  record["name"] = ""; Write(record_file, record.dump());
  CHECK(ReadCatalog(f.paths)[0].label.starts_with("Xbox 360 12345678"));
  CHECK(ImportGame(f.paths, ReadCatalog(f.paths), f.source, f.source) == id);
  games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  CHECK(games[0].label.starts_with(expected));
  CHECK(Inspect(f.paths, games[0]).inputs_ok);
}
TEST_CASE("launch inventory hashing preserves portable identities and rejects tampering", "[launch-preflight]") {
  Fixture f;
  const auto id = ImportGame(f.paths, {}, f.source, f.source);
  const auto ordinary = ReadCatalog(f.paths);
  REQUIRE(ordinary.size() == 1);
  {
    DigestBatch batch;
    CatalogLaunchVerification scope;
    const auto launched = ReadCatalog(f.paths);
    REQUIRE(launched.size() == 1);
    CHECK(launched[0].id == id);
    CHECK(launched[0].game_root == ordinary[0].game_root);
    CHECK(launched[0].import_error == ordinary[0].import_error);
    REQUIRE(launched[0].inputs.size() == ordinary[0].inputs.size());
    for (size_t i = 0; i < launched[0].inputs.size(); ++i) {
      CHECK(launched[0].inputs[i].destination == ordinary[0].inputs[i].destination);
      CHECK(launched[0].inputs[i].sha256 == ordinary[0].inputs[i].sha256);
    }
  }
  auto file = fs::directory_iterator(f.paths.local() / "catalog")->path();
  auto record = nlohmann::json::parse(std::ifstream(file));
  std::string expected;
  SECTION("changed hash") {
    record["files"][0]["sha256"] = std::string(64, '0');
    expected = "Invalid catalog identity";
  }
  SECTION("duplicate paths with a valid inventory fingerprint") {
    record["files"].push_back(record["files"][0]);
    std::string bytes;
    for (const auto& input : record["files"])
      bytes += input.at("path").get<std::string>() + "\t" + input.at("sha256").get<std::string>() + "\n";
    const auto identity = Sha256Hex({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    record["id"] = identity;
    const auto renamed = file.parent_path() / (identity + ".json");
    fs::rename(file, renamed); file = renamed;
    expected = "Invalid catalog inventory";
  }
  Write(file, record.dump());
  DigestBatch batch;
  CatalogLaunchVerification scope;
  const auto corrupted = ReadCatalog(f.paths);
  REQUIRE(corrupted.size() == 1);
  CHECK_FALSE(corrupted[0].import_error.empty());
  CHECK(corrupted[0].import_error.find(expected) != std::string::npos);
  CHECK(corrupted[0].inputs.empty());
}
TEST_CASE("folder catalog retains source-bound generated library routing metadata", "[library-content-type]") {
  Fixture f; f.Infrastructure();
  ImportGame(f.paths, {}, f.source, f.source);
  auto game = ReadCatalog(f.paths).front();
  GenerateAotProject(f.paths, game);
  const auto file = game.manifest.parent_path() / "preparation.toml";
  auto setup = toml::parse_file(file.string());
  setup["library"].as_table()->insert_or_assign("content_type", "000D0000");
  bool valid = true;
  SECTION("folder default cannot advertise a second content type") {}
  SECTION("metadata from another source is refused") {
    valid = false;
    setup["library"].as_table()->insert_or_assign("source_sha256", std::string(64, 'a'));
  }
  SECTION("explicit container type conflicts require regeneration") {
    valid = false;
    for (const auto& entry : fs::directory_iterator(f.paths.local() / "catalog")) {
      auto record = nlohmann::json::parse(std::ifstream(entry.path()));
      record["content_type"] = "00004000";
      Write(entry.path(), record.dump());
    }
  }
  { std::ofstream out(file); out << setup; }
  const auto catalog = ReadCatalog(f.paths);
  REQUIRE(catalog.size() == 1);
  if (valid) {
    CHECK(catalog[0].import_error.empty());
    CHECK(catalog[0].library.content_type == "000D0000");
    CHECK_FALSE(catalog[0].inputs.empty());
    const auto profile = LoadSessionProfile(f.paths, game.manifest, false);
    CHECK(PathUtf8(profile.library_relative_xex).find("000D0000") != std::string::npos);
  } else {
    CHECK_FALSE(catalog[0].import_error.empty());
    CHECK(catalog[0].inputs.empty());
  }
  CHECK(fs::file_size(f.root / ".local/user/save") == 4);
}
TEST_CASE("universal library isolates discs and leaves sibling DLC intact", "[universal]") {
  Fixture f;
  auto first = Xex(); first[50] = 1; first[51] = 2;
  Write(f.source/"default.xex", first);
  const auto first_id = ImportGame(f.paths, {}, f.source, f.source);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  const auto first_root = games[0].game_root;
  CHECK(first_root.parent_path() == f.paths.inputs()/"games/12345678");
  CHECK(Utf8(first_root.filename()).starts_with("disc-1-"));
  Write(first_root.parent_path()/"dlc/package", "preserve DLC");
  first[50] = 2; Write(f.source/"default.xex", first);
  CHECK(ImportGame(f.paths, games, f.source, f.source) != first_id);
  games = ReadCatalog(f.paths); REQUIRE(games.size() == 2);
  const auto second = std::find_if(games.begin(), games.end(), [&](const auto& game) { return game.game_root != first_root; });
  REQUIRE(second != games.end());
  CHECK(Utf8(second->game_root.filename()).starts_with("disc-2-"));
  CHECK(second->game_root.parent_path() == first_root.parent_path());
  CHECK(DigestFile(first_root/"default.xex")->sha256 != DigestFile(second->game_root/"default.xex")->sha256);
  CHECK(fs::file_size(first_root.parent_path()/"dlc/package") == 12);
  CHECK(Inspect(f.paths, *second).inputs_ok);
}
TEST_CASE("universal library resumes catalogs in arbitrary existing input locations", "[universal]") {
  Fixture f; const auto id = ImportGame(f.paths, {}, f.source, f.source);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  const auto record_file = *fs::directory_iterator(f.paths.local()/"catalog");
  std::ifstream in(record_file.path()); auto record = nlohmann::json::parse(in); in.close();
  const auto legacy = f.paths.inputs()/"library"/record.at("id").get<std::string>();
  fs::create_directories(legacy.parent_path()); fs::rename(games[0].game_root, legacy);
  record["content_root"] = Utf8(legacy.lexically_relative(f.paths.inputs()));
  Write(record_file.path(), record.dump());
  games = ReadCatalog(f.paths);
  CHECK(ImportGame(f.paths, games, f.source, f.source) == id);
  CHECK(ReadCatalog(f.paths)[0].game_root == legacy);
  CHECK(Inspect(f.paths, ReadCatalog(f.paths)[0]).inputs_ok);
}
TEST_CASE("universal library adopts verified local games without duplicating discs", "[universal][local-inputs]") {
  Fixture f;
  const auto source = f.paths.inputs()/"games/existing/disc-1";
  fs::create_directories(source.parent_path()); fs::rename(f.source, source);
  const auto before = fs::last_write_time(source/"default.xex");
  const auto id = ImportGame(f.paths, {}, source, source);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  CHECK(games[0].game_root == source);
  CHECK(Inspect(f.paths, games[0]).inputs_ok);
  CHECK(fs::last_write_time(source/"default.xex") == before);
  CHECK_FALSE(fs::exists(f.paths.inputs()/"games/12345678"));
  CHECK(ImportGame(f.paths, games, source, source) == id);
  Write(source/"assets/data", "changed revision");
  CHECK(ImportGame(f.paths, games, source, source) != id);
  games = ReadCatalog(f.paths); REQUIRE(games.size() == 2);
  const auto other = std::find_if(games.begin(), games.end(), [&](const auto& game) { return game.id != id; });
  REQUIRE(other != games.end());
  CHECK(other->game_root != source);
  CHECK(Inspect(f.paths, *other).inputs_ok);
  CHECK_FALSE(Inspect(f.paths, *std::find_if(games.begin(), games.end(), [&](const auto& game) { return game.id == id; })).inputs_ok);
}
TEST_CASE("universal library rejects malformed games before publication", "[universal]") {
  Fixture f;
  SECTION("truncated XEX") { Write(f.source/"default.xex","XEX2bad"); }
  SECTION("dashboard is not a game") { Write(f.source/"default.xex",Xex(1,0xFFFE07D1)); }
  SECTION("missing entrypoint") { fs::remove(f.source/"default.xex"); }
  CHECK_THROWS(ImportGame(f.paths,{},f.source,f.source));
  CHECK(ReadCatalog(f.paths).empty());
}
TEST_CASE("universal library recovers interrupted import and isolates corrupt records", "[universal]") {
  Fixture f;
  bool reached_copy = false;
  CHECK_THROWS(ImportGame(f.paths,{},f.source,f.source,[&] {
    if (fs::is_directory(f.paths.local()/"catalog")) { reached_copy=true; return true; } return false;
  }));
  REQUIRE(reached_copy); auto games=ReadCatalog(f.paths); REQUIRE(games.size()==1);
  CHECK_FALSE(Inspect(f.paths,games[0]).inputs_ok); CHECK_FALSE(games[0].import_error.empty());
  ImportGame(f.paths,games,f.source,f.source);
  games=ReadCatalog(f.paths); REQUIRE(games.size()==1); CHECK(Inspect(f.paths,games[0]).inputs_ok);
  Write(f.paths.local()/"catalog/broken.json","invalid");
  games=ReadCatalog(f.paths); REQUIRE(games.size()==2);
  CHECK(std::count_if(games.begin(),games.end(),[](const auto& t){return !t.import_error.empty();})==1);
}
TEST_CASE("universal library never overwrites changed installed content", "[universal]") {
  Fixture f; ImportGame(f.paths,{},f.source,f.source); auto games=ReadCatalog(f.paths);
  Write(games[0].game_root/"default.xex","preserve me");
  CHECK_THROWS(ImportGame(f.paths,games,f.source,f.source));
  CHECK(fs::file_size(games[0].game_root/"default.xex")==11);
  CHECK_FALSE(Inspect(f.paths,ReadCatalog(f.paths)[0]).inputs_ok);
}
TEST_CASE("universal library persists filesystem failures as readable UTF8 diagnostics", "[universal]") {
  Fixture f;
  // An ordinary file blocks the destination directory and produces an OS error.
  Write(f.paths.inputs() / "games", "preserve this obstruction");
  const auto before = DigestFile(f.paths.inputs() / "games")->sha256;
  std::string diagnostic;
  try { ImportGame(f.paths, {}, f.source, f.source); }
  catch (const std::runtime_error& e) { diagnostic = e.what(); }
  REQUIRE(diagnostic.find("Erro de arquivo") != std::string::npos);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  CHECK(games[0].import_error == diagnostic);
  CHECK(DigestFile(f.paths.inputs() / "games")->sha256 == before);
  fs::rename(f.paths.inputs() / "games", f.paths.inputs() / "preserved");
  CHECK_NOTHROW(ImportGame(f.paths, games, f.source, f.source));
  games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  CHECK(games[0].import_error.empty()); CHECK(Inspect(f.paths, games[0]).inputs_ok);
}
TEST_CASE("universal library does not infer AOT from title id or executable alone", "[universal]") {
  Fixture f; Target recipe; recipe.id="supported"; recipe.kind="game";
  recipe.game_root=f.paths.inputs()/"supported";
  recipe.inputs={{recipe.game_root/"default.xex","default.xex",DigestFile(f.source/"default.xex")->sha256,{}},
    {recipe.game_root/"assets/data","data",std::string(64,'0'),"assets/data"}};
  auto id=ImportGame(f.paths,{recipe},f.source,f.source); CHECK(id!="supported");
  CHECK(ReadCatalog(f.paths)[0].generic_aot);
}
TEST_CASE("universal library discovers a newly installed AOT recipe without moving originals", "[universal]") {
  Fixture f; fs::remove(f.source/"assets/data"); ImportGame(f.paths,{},f.source,f.source);
  const auto hash=DigestFile(f.source/"default.xex")->sha256;
  Write(f.root/"titles/new_game/preparation.toml", "label='Game'\nlimitations='Test'\n[[files]]\npath='default.xex'\nsource_name='default.xex'\nsha256='"+hash+"'\n");
  Write(f.root/"titles/new_game/new_game_manifest.toml", "[project]\ngame_root='../../.local/inputs/new_game'\n");
  auto games=ReadCatalog(f.paths); REQUIRE(games.size()==1);
  CHECK(games[0].id=="new_game"); CHECK(games[0].aot); CHECK(games[0].catalogued);
  CHECK_FALSE(games[0].preparation_source.empty()); CHECK(Inspect(f.paths,games[0]).inputs_ok);
  CHECK_FALSE(Inspect(f.paths,games[0]).prepared);
  Import(f.paths,games[0],games[0].preparation_source);
  CHECK(fs::exists(games[0].game_root/"default.xex"));
  CHECK(fs::exists(games[0].preparation_source/"default.xex"));
}
TEST_CASE("universal library imports available real content into a persistent environment", "[.universal-real]") {
  const char* root=std::getenv("X360N_LIBRARY_ROOT"); const char* original=std::getenv("X360N_LIBRARY_SOURCE");
  const char* extractor=std::getenv("X360N_TEST_EXTRACTOR");
  REQUIRE(root); REQUIRE(original); REQUIRE(extractor);
  ProjectPaths paths{PathFromUtf8(root)}; const auto source=PathFromUtf8(original);
  auto targets=ReadCatalog(paths);
  ImportSource content(source,PathFromUtf8(extractor),paths.local()/"logs"/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  auto id=ImportGame(paths,targets,content.path(),source);
  targets=ReadCatalog(paths);
  auto game=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.id==id;});
  REQUIRE(game!=targets.end()); CHECK(game->catalogued); CHECK(Inspect(paths,*game).inputs_ok);
  std::ofstream(paths.local()/"last-import.txt") << id << '\n' << game->label << '\n' << game->title_id << '\n' << game->version << '\n';
  if (std::getenv("X360N_GENERATE_AOT")) {
    REQUIRE(game->generic_aot); GenerateAotProject(paths, *game);
    REQUIRE(fs::is_regular_file(game->manifest));
  }
}

TEST_CASE("automatic AOT generates a stable project without a manual recipe", "[universal][generic-aot]") {
  Fixture f;
  f.Infrastructure();
  ImportGame(f.paths,{},f.source,f.source); const auto t=ReadCatalog(f.paths).front();
  REQUIRE(t.generic_aot); REQUIRE(t.aot);
  CHECK(t.id.size() == 64);
  CHECK(t.exe.parent_path() == f.paths.build()/"aot"/t.id.substr(0,17));
  const auto user_root = t.user_root;
  GenerateAotProject(f.paths,t);
  std::ifstream preset_input(t.manifest.parent_path()/"CMakePresets.json");
  nlohmann::json release_preset; preset_input >> release_preset;
  CHECK(release_preset["configurePresets"][0]["cacheVariables"]["CMAKE_BUILD_TYPE"] == "Release");
  auto profile=LoadSessionProfile(f.paths,t.manifest,false);
  CHECK(profile.name==t.id);
  // Session paths are canonicalized; Windows TEMP may use an 8.3 alias.
  CHECK(fs::equivalent(profile.xex,t.game_root/"default.xex"));
  CHECK_FALSE(profile.library_relative_xex.empty());
  CHECK(profile.library_relative_xex.filename()=="default.xex");
  CHECK(Inspect(f.paths,t).interrupted); CHECK_FALSE(Inspect(f.paths,t).prepared);
  const auto integration=t.manifest.parent_path()/"generated/x360n_rexglue.cmake";
  std::ifstream in(integration); const std::string text{std::istreambuf_iterator<char>(in),{}};
  CHECK(text.find("${CMAKE_CURRENT_SOURCE_DIR}")!=std::string::npos);
  CHECK(text.find("{{")==std::string::npos);
  CHECK(text.find("include(\"${X360N_GENERATED_ROOT}/sources.cmake\")")!=std::string::npos);
  CHECK(text.find("${CMAKE_CURRENT_SOURCE_DIR}/generated/default")==std::string::npos);
  std::ifstream host_input(t.manifest.parent_path()/"main.cpp");
  const std::string host{std::istreambuf_iterator<char>(host_input),{}};
  CHECK(host.find("#include \"" + t.id + "_init.h\"") != std::string::npos);
  CHECK(host.find("generated/default/") == std::string::npos);
  CHECK(fs::weakly_canonical(profile.exe)==fs::weakly_canonical(t.exe));
  CHECK(fs::weakly_canonical(profile.user_root)==fs::weakly_canonical(user_root));
  const auto stamp=fs::last_write_time(t.manifest); const auto hash=DigestFile(integration)->sha256;
  GenerateAotProject(f.paths,t);
  CHECK(fs::last_write_time(t.manifest)==stamp); CHECK(DigestFile(integration)->sha256==hash);
  Write(t.manifest.parent_path()/"generated/rexglue.cmake","SDK-owned regenerated glue");
  GenerateAotProject(f.paths,t);
  CHECK(DigestFile(integration)->sha256==hash);
  auto collision = t;
  collision.id = t.id.substr(0,17) + std::string(47,'f');
  REQUIRE(collision.id != t.id);
  collision.manifest = f.paths.local()/"aot"/collision.id/(collision.id+"_manifest.toml");
  collision.exe = t.exe.parent_path()/(collision.id+".exe");
  CHECK_THROWS(GenerateAotProject(f.paths,collision));
  CHECK_FALSE(fs::exists(collision.manifest));
  CHECK(DigestFile(integration)->sha256==hash);
  const auto orphan = f.paths.build()/"aot/orphan/unregistered.exe";
  Write(orphan,"uncertified native fixture");
  CHECK(CheckPreparedLaunch(f.paths,orphan));
  auto dashboard=f.root/"titles/dashboard/dashboard_manifest.toml";
  Write(dashboard,"[project]\nname='dashboard'\ngame_root='../../.local/inputs/dash'\n[entrypoint]\nfile_path='../../.local/inputs/dash/dash.xex'\n[x360n]\nnative_exe='../../.local/build/dash.exe'\nuser_data_root='../../.local/user/dash'\nxex_sha256='"+std::string(64,'0')+"'\nlaunch_library=true\n");
  CHECK(LoadSessionProfile(f.paths,dashboard,false).launch_targets.empty());
  std::string recursive;
  { std::ifstream manifest_input(t.manifest); recursive.assign(std::istreambuf_iterator<char>(manifest_input),{}); }
  const auto section = recursive.find("[x360n]"); REQUIRE(section!=std::string::npos);
  recursive.insert(section+std::string("[x360n]").size(),"\nlaunch_library = true\n");
  Write(t.manifest,recursive);
  CHECK(LoadSessionProfile(f.paths,dashboard,false).launch_targets.empty());
  GenerateAotProject(f.paths,t);
  const auto repaired_stamp = fs::last_write_time(t.manifest);
  Write(t.manifest.parent_path()/"preparation.toml","broken optional project");
  CHECK(LoadSessionProfile(f.paths,dashboard,false).launch_targets.empty());
  GenerateAotProject(f.paths,t); // repair interrupted project without touching content
  CHECK(fs::last_write_time(t.manifest)==repaired_stamp);
  Write(t.game_root/"default.xex",Xex(9));
  CHECK_THROWS(GenerateAotProject(f.paths,t));
  CHECK(fs::last_write_time(t.manifest)==repaired_stamp);
}
TEST_CASE("automatic AOT discovers verified XEX dependencies by content and preserves their identity", "[universal][generic-aot]") {
  Fixture f; f.Infrastructure();
  auto module = [](unsigned entry) {
    std::string s(512, '\0');
    auto be = [&](size_t p, unsigned n) { for (int i=0; i<4; ++i) s[p+i]=char(n>>(24-8*i)); };
    s.replace(0,4,"XEX2"); be(4,9); be(8,512); be(16,64); be(20,2);
    be(24,0x10100); be(28,entry); be(32,0x10201); be(36,0x83000000);
    be(68,0x10000); return s;
  };
  Write(f.source/"nested/HostLoader.dll", module(0x83001000));
  Write(f.source/"game.exe.xex", module(0x83002000));
  std::string resource(600,'\0');
  const auto resource32=[&](size_t p,unsigned n) { for(int i=0;i<4;++i) resource[p+i]=char(n>>(24-8*i)); };
  resource.replace(0,4,"XEX2"); resource32(4,8); resource32(8,600); resource32(16,64); resource32(20,3);
  resource32(24,0x10201); resource32(28,0x83000000);
  resource32(32,0x3FF); resource32(36,528); resource32(528,8); resource[535]=1;
  resource32(40,0x2FF); resource32(44,560); resource32(560,20); resource[564]='s';
  resource32(572,0x83000000); resource32(576,4);
  resource32(68,0x10000); resource32(64+0x110,0x83000000);
  resource32(64+0x180,1); resource32(64+0x184,0x13);
  Write(f.source/"resource.dll",resource);
  REQUIRE(IdentifyContent({},f.source/"resource.dll").front().resource_only);
  for (const auto offset : {533,455,572}) {
    auto invalid=resource;
    invalid[offset]=offset==533 ? 1 : offset==455 ? 0x11 : char(0x84);
    Write(f.source/"unproven-resource.dll",invalid);
    CHECK_FALSE(IdentifyContent({},f.source/"unproven-resource.dll").front().resource_only);
  }
  fs::remove(f.source/"unproven-resource.dll");
  auto no_entry=module(0);
  Write(f.source/"zero_entry.dll",no_entry);
  CHECK_FALSE(IdentifyContent({},f.source/"zero_entry.dll").front().resource_only);
  fs::remove(f.source/"zero_entry.dll");
  Write(f.source/"ordinary.dll", "ordinary native/data asset");
  SECTION("zero entry point does not suppress an executable DLL") {
    Write(f.source/"zero_entry.dll",no_entry);
    ImportGame(f.paths,{},f.source,f.source); const auto t=ReadCatalog(f.paths).front();
    GenerateAotProject(f.paths,t);
    auto manifest=toml::parse_file(t.manifest.string());
    REQUIRE(manifest["modules"].as_array());
    CHECK(manifest["modules"].as_array()->size()==3);
  }
  SECTION("ambiguous native targets fail before project publication") {
    Write(f.source/"other/HostLoader.dll", module(0x83003000));
    ImportGame(f.paths,{},f.source,f.source); const auto t=ReadCatalog(f.paths).front();
    CHECK_THROWS_WITH(GenerateAotProject(f.paths,t), "Ambiguous AOT module target: other/hostloader.dll");
    CHECK_FALSE(fs::exists(t.manifest));
  }
  SECTION("malformed XEX dependency fails before project publication") {
    Write(f.source/"broken.dll", "XEX2broken");
    ImportGame(f.paths,{},f.source,f.source); const auto t=ReadCatalog(f.paths).front();
    CHECK_THROWS_WITH(GenerateAotProject(f.paths,t), "Unidentified AOT dependency: broken.dll");
    CHECK_FALSE(fs::exists(t.manifest));
  }
  SECTION("original identity and exact-hash analysis survive deterministic regeneration") {
  ImportGame(f.paths,{},f.source,f.source); const auto t=ReadCatalog(f.paths).front();
  const auto original=DigestFile(t.game_root/"nested/HostLoader.dll")->sha256;
  Write(f.root/"titles/common/module_analysis.toml", "[modules.'"+original+"']\nfunctions=1\n");
  CHECK_THROWS(GenerateAotProject(f.paths,t)); CHECK_FALSE(fs::exists(t.manifest));
  Write(f.root/"titles/common/module_analysis.toml", "[modules.'"+std::string(64,'0')+"'.functions]\n0x83009900={}\n[modules.'"+original+"'.functions]\n0x83001000={}\n");
  std::ifstream record_file(fs::directory_iterator(f.paths.local()/"catalog")->path());
  const auto record=nlohmann::json::parse(record_file);
  auto dependency=std::find_if(record["modules"].begin(),record["modules"].end(),[](const auto& m) { return m["path"]=="nested/HostLoader.dll"; });
  REQUIRE(dependency!=record["modules"].end());
  CHECK((*dependency)["sha256"]==original);
  CHECK((*dependency)["entry_point"]==0x83001000);
  CHECK((*dependency)["image_base"]==0x83000000);
  GenerateAotProject(f.paths,t);
  auto manifest=toml::parse_file(t.manifest.string());
  auto modules=manifest["modules"].as_array(); REQUIRE(modules); REQUIRE(modules->size()==2);
  CHECK((*modules)[0].as_table()->at("guest_path").value_or(std::string{})=="game.exe.xex");
  CHECK((*modules)[1].as_table()->at("guest_path").value_or(std::string{})=="nested/HostLoader.dll");
  CHECK(fs::equivalent(PathFromUtf8((*modules)[1].as_table()->at("file_path").value_or("")),t.game_root/"nested/HostLoader.dll"));
  CHECK((*modules)[1].as_table()->at("functions").as_table()->contains("0x83001000"));
  CHECK_FALSE((*modules)[0].as_table()->contains("functions"));
  CHECK_FALSE((*modules)[1].as_table()->at("functions").as_table()->contains("0x83009900"));
  auto setup=toml::parse_file((t.manifest.parent_path()/"preparation.toml").string());
  REQUIRE(setup["files"].as_array()->size()==3);
  CHECK((*setup["files"].as_array())[2].as_table()->at("sha256").value_or("")==original);
  CHECK(DigestFile(t.game_root/"nested/HostLoader.dll")->sha256==original);
  const auto config_hash=DigestFile(t.manifest)->sha256;
  GenerateAotProject(f.paths,t); CHECK(DigestFile(t.manifest)->sha256==config_hash);
  Write(t.game_root/"nested/HostLoader.dll",module(0x83004000));
  CHECK_THROWS(GenerateAotProject(f.paths,t));
  CHECK(DigestFile(t.manifest)->sha256==config_hash);
  }
}
TEST_CASE("unchanged AOT generation does not invalidate preparation and preserves earlier failures", "[generic-aot]") {
  Fixture f; f.Infrastructure();
  ImportGame(f.paths, {}, f.source, f.source);
  const auto game = ReadCatalog(f.paths).front();
  GenerateAotProject(f.paths, game);
  const auto marker = PreparationMarker(f.paths, game);
  REQUIRE(fs::exists(marker));
  fs::remove(marker); // fixture's simulated completed preparation
  const auto manifest_time = fs::last_write_time(game.manifest);
  GenerateAotProject(f.paths, game);
  CHECK_FALSE(fs::exists(marker));
  CHECK(fs::last_write_time(game.manifest) == manifest_time);
  CHECK_FALSE(Inspect(f.paths, game).prepared); // no receipt/exe is implicitly approved
  std::ofstream(f.root / "titles/common/generic/modules.cmake", std::ios::app) << "\n# changed build integration\n";
  GenerateAotProject(f.paths, game);
  CHECK(fs::exists(marker));
  Write(marker, "previous interrupted preparation");
  GenerateAotProject(f.paths, game);
  std::ifstream in(marker); std::string contents{std::istreambuf_iterator<char>(in), {}};
  CHECK(contents == "previous interrupted preparation");
}
TEST_CASE("local AOT regenerates an existing imported title without copying content", "[.local-existing-aot]") {
  const char* root = std::getenv("X360N_AOT_ROOT"), *id = std::getenv("X360N_AOT_TITLE");
  REQUIRE(root); REQUIRE(id);
  ProjectPaths paths{PathFromUtf8(root)};
  const auto games = ReadCatalog(paths);
  const auto game = std::find_if(games.begin(),games.end(),[&](const auto& t){return t.id==id;});
  REQUIRE(game!=games.end()); REQUIRE(game->generic_aot);
  fs::path catalog;
  for (const auto& entry : fs::directory_iterator(paths.local()/"catalog")) {
    if (entry.path().extension() != ".json") continue;
    std::ifstream in(entry.path()); auto record = nlohmann::json::parse(in);
    if (fs::equivalent(paths.inputs()/PathFromUtf8(record.at("content_root").get<std::string>()), game->game_root)) catalog=entry.path();
  }
  REQUIRE_FALSE(catalog.empty());
  const auto before = DigestFile(catalog); REQUIRE(before);
  const bool prepared_before = Inspect(paths, *game).prepared;
  const auto started = std::chrono::steady_clock::now();
  GenerateAotProject(paths,*game);
  std::cout << "project generation ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()
            << " prepared-before=" << prepared_before << '\n';
  if (prepared_before) {
    CHECK_FALSE(fs::exists(PreparationMarker(paths, *game)));
    CHECK(Inspect(paths, *game).prepared);
  }
  const auto profile = LoadSessionProfile(paths,game->manifest,false);
  CHECK(profile.name==game->id);
  CHECK(fs::weakly_canonical(profile.exe)==fs::weakly_canonical(game->exe));
  CHECK(fs::equivalent(profile.xex,game->game_root/"default.xex"));
  CHECK(fs::weakly_canonical(profile.user_root)==fs::weakly_canonical(game->user_root));
  CHECK(DigestFile(catalog)->sha256==before->sha256);
  CHECK(fs::is_regular_file(game->exe.parent_path()/".x360n-aot-owner"));
}

TEST_CASE("local catalog snapshot records actual preparation state", "[.local-catalog-snapshot]") {
  const auto* root = std::getenv("X360N_LIBRARY_ROOT");
  const auto* output = std::getenv("X360N_LIBRARY_SNAPSHOT");
  REQUIRE(root); REQUIRE(output);
  ProjectPaths paths{PathFromUtf8(root)};
  nlohmann::json snapshot = nlohmann::json::array();
  for (const auto& target : ReadCatalog(paths)) {
    const auto state = Inspect(paths, target);
    snapshot.push_back({{"id", target.id}, {"label", target.label}, {"kind", target.kind},
        {"root", Utf8(target.game_root)}, {"manifest", Utf8(target.manifest)},
        {"exe", Utf8(target.exe)}, {"catalogued", target.catalogued},
        {"generic_aot", target.generic_aot}, {"title_id", target.title_id},
        {"inputs_ok", state.inputs_ok}, {"prepared", state.prepared},
        {"interrupted", state.interrupted}, {"issues", state.issues}});
  }
  Write(PathFromUtf8(output), snapshot.dump(2) + "\n");
  CHECK(fs::is_regular_file(PathFromUtf8(output)));
}
