#include <catch2/catch_test_macros.hpp>
#include "import_source.h"
#include "catalog.h"
#include "sha256.h"
#include "../../tests/supervisor/scratch.h"
#include <rex/filesystem/devices/stfs_xbox.h>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <sstream>
namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
void Write(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << bytes;
}
void LE(std::string& b, size_t p, uint32_t v) { for (int i=0;i<4;++i) b[p+i]=char(v>>(8*i)); }
std::string Iso() {
  std::string b(35*2048, '\0'); b.replace(32*2048,20,"MICROSOFT*XBOX*MEDIA",20);
  LE(b,32*2048+20,33); LE(b,32*2048+24,2048);
  LE(b,33*2048+4,34); LE(b,33*2048+8,4); b[33*2048+12]=char(0x80); b[33*2048+13]=11;
  b.replace(33*2048+14,11,"default.xex"); b.replace(34*2048,4,"game"); return b;
}
std::string Stfs(rex::filesystem::XContentPackageType magic) {
  using namespace rex::filesystem;
  StfsHeader header{}; header.header.magic = magic; header.header.header_size = 0xA000;
  auto& desc=header.metadata.volume_descriptor.stfs; desc.flags.bits.read_only_format=1;
  desc.file_table_block_count=1; desc.set_file_table_block_number(0); desc.total_block_count=2;
  StfsDirectoryEntry entry{}; std::memcpy(entry.name,"default.xex",11); entry.flags.name_length=11;
  entry.directory_index=0xFFFF; entry.length=4; entry.set_start_block_number(1); entry.set_allocated_data_blocks(1);
  std::string b(0xD000,'\0'); std::memcpy(b.data(),&header,sizeof(header));
  // Header, one hash block, directory block 0, data block 1.
  b[0xA000+21]=b[0xA000+22]=b[0xA000+23]=char(0xFF);
  b[0xA000+24+21]=b[0xA000+24+22]=b[0xA000+24+23]=char(0xFF);
  std::memcpy(b.data()+0xB000,&entry,sizeof(entry)); b.replace(0xC000,4,"game"); return b;
}
struct Scratch {
  fs::path root=UniqueTempPath("import_source") / fs::path(u8"João 日本");
  Scratch() { fs::remove_all(root); fs::create_directories(root); }
  ~Scratch() { std::error_code ec; fs::remove_all(root,ec); }
};
}
TEST_CASE("desktop extracts ISO and extensionless CON LIVE PIRS before verified import", "[desktop][extraction]") {
  Scratch s; std::string bytes; std::string name="original";
  SECTION("ISO") { bytes=Iso(); name+=".ISO"; }
  SECTION("CON") { bytes=Stfs(rex::filesystem::XContentPackageType::kCon); }
  SECTION("LIVE") { bytes=Stfs(rex::filesystem::XContentPackageType::kLive); }
  SECTION("PIRS") { bytes=Stfs(rex::filesystem::XContentPackageType::kPirs); }
  const auto original=s.root/name; Write(original,bytes); const auto before=DigestFile(original)->sha256;
  Target t; t.id="sample"; t.game_root=s.root/".local/inputs/game";
  t.inputs={{t.game_root/"default.xex","default.xex",Sha256Hex({reinterpret_cast<const uint8_t*>("game"),4}),{}}};
  Write(s.root/".local/user/save","preserved");
  bool progress=false; fs::path staging;
  {
    ImportSource source(original,X360N_CONTENT_EXE,s.root/"logs",{},[&]{progress=true;});
    REQUIRE(source.extracted()); staging=source.path(); CHECK(progress);
    CHECK(source.identity_path()==source.path()/"default.xex");
    REQUIRE(IdentifyContent({t},source.identity_path()).front().recognized);
    CHECK(Import(ProjectPaths{s.root},t,source.path())==1);
    CHECK(Inspect(ProjectPaths{s.root},t).inputs_ok);
    CHECK_FALSE(Inspect(ProjectPaths{s.root},t).prepared);
  }
  CHECK_FALSE(fs::exists(staging)); CHECK(fs::exists(s.root/"logs/extraction.log"));
  CHECK(DigestFile(original)->sha256==before);
  CHECK(fs::file_size(s.root/".local/user/save")==9);
}
TEST_CASE("desktop extraction failure and interruption never publish content", "[desktop][extraction]") {
  Scratch s; auto original=s.root/"game.iso"; Write(original,Iso());
  SECTION("truncated ISO") { Write(original,"invalid"); CHECK_THROWS(ImportSource(original,X360N_CONTENT_EXE,s.root/"logs")); }
  SECTION("truncated STFS") { original=s.root/"package"; Write(original,"LIVEbad"); CHECK_THROWS(ImportSource(original,X360N_CONTENT_EXE,s.root/"logs")); }
  SECTION("missing helper") { CHECK_THROWS(ImportSource(original,s.root/"missing.exe",s.root/"logs")); }
  SECTION("cancel while extracting") {
    bool cancel=false;
    CHECK_THROWS(ImportSource(original,X360N_CONTENT_EXE,s.root/"logs",[&]{return cancel;},[&]{cancel=true;}));
  }
  SECTION("interrupted staging is not reused") {
    Write(s.root/"logs/extraction/content/default.xex","partial");
    CHECK_THROWS(ImportSource(original,X360N_CONTENT_EXE,s.root/"logs"));
    CHECK(fs::file_size(s.root/"logs/extraction/content/default.xex")==7);
    ImportSource retry(original,X360N_CONTENT_EXE,s.root/"retry"); CHECK(retry.extracted());
  }
  CHECK(fs::exists(original)); CHECK_FALSE(fs::exists(s.root/".local/inputs"));
}
TEST_CASE("desktop keeps extracted files and directories as direct import sources", "[desktop][extraction]") {
  Scratch s; Write(s.root/"default.xex","XEX2");
  ImportSource file(s.root/"default.xex",{},s.root/"logs"); CHECK_FALSE(file.extracted());
  ImportSource directory(s.root,{},s.root/"logs"); CHECK(directory.path()==s.root); CHECK_FALSE(directory.extracted());
}
TEST_CASE("desktop refuses incomplete extracted inventories before copying", "[desktop][extraction]") {
  Scratch s; Write(s.root/"game.iso",Iso());
  ImportSource source(s.root/"game.iso",X360N_CONTENT_EXE,s.root/"logs");
  Target t; t.id="game"; t.game_root=s.root/".local/inputs/game";
  t.inputs={{t.game_root/"default.xex","default.xex",DigestFile(source.path()/"default.xex")->sha256,{}},
            {t.game_root/"missing.bin","missing.bin",std::string(64,'0'),{}}};
  CHECK_THROWS(Import(ProjectPaths{s.root},t,source.path()));
  CHECK_FALSE(fs::exists(t.game_root));
}
TEST_CASE("desktop imports the user's real container into an isolated environment", "[.local-extraction]") {
  const auto* original=std::getenv("X360N_TEST_CONTAINER");
  const auto* repository=std::getenv("X360N_TEST_SOURCE");
  const auto* id=std::getenv("X360N_TEST_TITLE");
  REQUIRE(original); REQUIRE(repository); REQUIRE(id);
  Scratch s; const auto input=PathFromUtf8(original); const auto before=DigestFile(input)->sha256;
  auto catalog=ReadCatalog(ProjectPaths{PathFromUtf8(repository)});
  ImportSource source(input,X360N_CONTENT_EXE,s.root/"logs"); REQUIRE(source.extracted());
  auto identities=IdentifyContent(catalog,source.identity_path());
  auto match=std::find_if(identities.begin(),identities.end(),[&](const auto& i){return i.target_id==id;});
  REQUIRE(match!=identities.end());
  auto target=*std::find_if(catalog.begin(),catalog.end(),[&](const auto& t){return t.id==id;});
  for(auto& i:target.inputs) i.destination=s.root/".local/inputs/game"/i.destination.lexically_relative(target.game_root);
  target.game_root=s.root/".local/inputs/game";
  Write(s.root/".local/user/save","preserved");
  CHECK(Import(ProjectPaths{s.root},target,source.path())==target.inputs.size());
  CHECK(Inspect(ProjectPaths{s.root},target).inputs_ok);
  CHECK(DigestFile(input)->sha256==before); CHECK(fs::file_size(s.root/".local/user/save")==9);
}
TEST_CASE("desktop extracts real containers against their established file inventory", "[.local-container-inventory]") {
  const auto* original=std::getenv("X360N_TEST_CONTAINER");
  const auto* inventory=std::getenv("X360N_TEST_INVENTORY");
  REQUIRE(original); REQUIRE(inventory); Scratch s;
  const auto input=PathFromUtf8(original); const auto before=DigestFile(input)->sha256;
  ImportSource source(input,X360N_CONTENT_EXE,s.root/"logs"); REQUIRE(source.extracted());
  auto read=[](const fs::path& path) { std::ifstream f(path,std::ios::binary); REQUIRE(f.good());
    std::vector<std::string> lines; for(std::string line;std::getline(f,line);) {
      if(!line.empty() && line.back()=='\r') line.pop_back(); lines.push_back(line);
    } std::sort(lines.begin(),lines.end()); return lines; };
  auto expected=read(PathFromUtf8(inventory)); auto actual=read(s.root/"logs/extraction/content.sha256");
  CHECK(actual==expected); CHECK(actual.size()>100); CHECK(DigestFile(input)->sha256==before);
}
