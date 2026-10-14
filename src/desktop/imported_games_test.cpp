#include "catalog.h"
#include "sha256.h"
#include "import_source.h"
#include "session_profile.h"
#include "../../tests/supervisor/scratch.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <algorithm>
#include <cstdlib>

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
struct Fixture {
  fs::path root = UniqueTempPath("universal_library");
  ProjectPaths paths{root};
  fs::path source = root / "source";
  Fixture() { Write(source/"default.xex", Xex()); Write(source/"assets/data", "asset"); Write(root/".local/user/save", "keep"); }
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
  Write(f.paths.inputs() / "library", "preserve this obstruction");
  const auto before = DigestFile(f.paths.inputs() / "library")->sha256;
  std::string diagnostic;
  try { ImportGame(f.paths, {}, f.source, f.source); }
  catch (const std::runtime_error& e) { diagnostic = e.what(); }
  REQUIRE(diagnostic.find("Erro de arquivo") != std::string::npos);
  auto games = ReadCatalog(f.paths); REQUIRE(games.size() == 1);
  CHECK(games[0].import_error == diagnostic);
  CHECK(DigestFile(f.paths.inputs() / "library")->sha256 == before);
  fs::rename(f.paths.inputs() / "library", f.paths.inputs() / "preserved");
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
  for (const auto& file : {"titles/common/generic/CMakeLists.txt", "titles/common/generic/main.cpp",
       "external/rexglue-sdk/resources/templates/init/rexglue_cmake.inja"}) {
    auto dest=f.root/file; fs::create_directories(dest.parent_path()); fs::copy_file(fs::path(X360N_SOURCE_ROOT)/file,dest);
  }
  ImportGame(f.paths,{},f.source,f.source); const auto t=ReadCatalog(f.paths).front();
  REQUIRE(t.generic_aot); REQUIRE(t.aot);
  GenerateAotProject(f.paths,t);
  auto profile=LoadSessionProfile(f.paths,t.manifest,false);
  CHECK(profile.name==t.id);
  // Session paths are canonicalized; Windows TEMP may use an 8.3 alias.
  CHECK(fs::equivalent(profile.xex,t.game_root/"default.xex"));
  CHECK(profile.library_relative_xex=="default.xex");
  CHECK(Inspect(f.paths,t).interrupted); CHECK_FALSE(Inspect(f.paths,t).prepared);
  const auto integration=t.manifest.parent_path()/"generated/rexglue.cmake";
  std::ifstream in(integration); const std::string text{std::istreambuf_iterator<char>(in),{}};
  CHECK(text.find("${CMAKE_CURRENT_SOURCE_DIR}")!=std::string::npos);
  CHECK(text.find("{{")==std::string::npos);
  const auto stamp=fs::last_write_time(t.manifest); const auto hash=DigestFile(integration)->sha256;
  GenerateAotProject(f.paths,t);
  CHECK(fs::last_write_time(t.manifest)==stamp); CHECK(DigestFile(integration)->sha256==hash);
  auto dashboard=f.root/"titles/dashboard/dashboard_manifest.toml";
  Write(dashboard,"[project]\nname='dashboard'\ngame_root='../../.local/inputs/dash'\n[entrypoint]\nfile_path='../../.local/inputs/dash/dash.xex'\n[x360n]\nnative_exe='../../.local/build/dash.exe'\nuser_data_root='../../.local/user/dash'\nxex_sha256='"+std::string(64,'0')+"'\nlaunch_library=true\n");
  CHECK(LoadSessionProfile(f.paths,dashboard,false).launch_targets.empty());
  Write(t.manifest.parent_path()/"preparation.toml","broken optional project");
  CHECK(LoadSessionProfile(f.paths,dashboard,false).launch_targets.empty());
  GenerateAotProject(f.paths,t); // repair interrupted project without touching content
  CHECK(fs::last_write_time(t.manifest)==stamp);
  Write(t.game_root/"default.xex",Xex(9));
  CHECK_THROWS(GenerateAotProject(f.paths,t));
  CHECK(fs::last_write_time(t.manifest)==stamp);
}
