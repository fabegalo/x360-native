#include "../../src/content/system_update.h"
#include "../../src/content/safe_zip.h"
#include "../../src/desktop/model/import_source.h"
#include "../../src/desktop/model/catalog.h"
#include "../supervisor/scratch.h"
#include "sha256.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <rex/filesystem/devices/stfs_xbox.h>
namespace fs=std::filesystem;
using namespace x360n;
using namespace x360n::content;
using namespace x360n::desktop;
namespace {
void Write(const fs::path& p,const std::string& data) {
  fs::create_directories(p.parent_path()); std::ofstream(p,std::ios::binary)<<data;
}
std::string Read(const fs::path& p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
uint32_t Crc(const std::string& s) {
  uint32_t c=~0u;
  for (unsigned char b:s) { c^=b; for (int i=0;i<8;++i) c=(c>>1)^((0u-(c&1))&0xEDB88320); }
  return ~c;
}
void Le(std::string& s,size_t p,uint32_t v,size_t n=4) { for (size_t i=0;i<n;++i) s[p+i]=char(v>>(8*i)); }
struct Item { std::string name,data; uint32_t attr=0; uint16_t flags=0; bool deflate=false; };
std::string Zip(const std::vector<Item>& items) {
  std::string data,central;
  for (const auto& i:items) {
    auto body=i.data;
    if (i.deflate) {
      REQUIRE(i.data.size()<65536);
      body=std::string(5,'\0')+i.data;body[0]=1;
      Le(body,1,uint32_t(i.data.size()),2);Le(body,3,uint32_t(~i.data.size()),2);
    }
    std::string h(30,'\0'); Le(h,0,0x04034B50);Le(h,4,20,2);Le(h,6,i.flags,2);Le(h,8,i.deflate?8:0,2);
    Le(h,14,Crc(i.data));Le(h,18,uint32_t(body.size()));Le(h,22,uint32_t(i.data.size()));Le(h,26,uint32_t(i.name.size()),2);
    std::string c(46,'\0');Le(c,0,0x02014B50);Le(c,4,0x314,2);Le(c,6,20,2);Le(c,8,i.flags,2);
    Le(c,10,i.deflate?8:0,2);Le(c,16,Crc(i.data));Le(c,20,uint32_t(body.size()));Le(c,24,uint32_t(i.data.size()));
    Le(c,28,uint32_t(i.name.size()),2);Le(c,38,i.attr);Le(c,42,uint32_t(data.size()));
    central+=c+i.name;data+=h+i.name+body;
  }
  std::string end(22,'\0');Le(end,0,0x06054B50);Le(end,8,uint32_t(items.size()),2);Le(end,10,uint32_t(items.size()),2);
  Le(end,12,uint32_t(central.size()));Le(end,16,uint32_t(data.size()));return data+central+end;
}
struct Scratch {
  fs::path root=UniqueTempPath("system-update")/fs::path(u8"João 日本 com espaços");
  Scratch(){fs::create_directories(root);}
  ~Scratch(){std::error_code ec;fs::remove_all(root,ec);}
};
std::string Package() {
  using namespace rex::filesystem;
  StfsHeader header{};header.header.magic=XContentPackageType::kPirs;header.header.header_size=0xA000;
  header.metadata.content_type=static_cast<XContentType>(0xB0000);
  header.metadata.execution_info.title_id=0xFFFE07D1;
  auto& d=header.metadata.volume_descriptor.stfs;d.flags.bits.read_only_format=1;
  d.file_table_block_count=1;d.set_file_table_block_number(0);d.total_block_count=2;
  StfsDirectoryEntry e{};std::memcpy(e.name,"$flash_dash.xex",15);e.flags.name_length=15;
  e.directory_index=0xFFFF;e.length=4;e.set_start_block_number(1);e.set_allocated_data_blocks(1);
  std::string b(0xD000,'\0');std::memcpy(b.data(),&header,sizeof(header));
  for (size_t p:{0xA015,0xA016,0xA017,0xA02D,0xA02E,0xA02F}) b[p]=char(0xFF);
  std::memcpy(b.data()+0xB000,&e,sizeof(e));b.replace(0xC000,4,"XEX2");return b;
}
}
TEST_CASE("System Update ZIP extraction streams stored and deflated entries with Unicode paths", "[system-update]") {
  Scratch s;const auto original=s.root/"renamed.input";
  Write(original,Zip({{"$SystemUpdate/",{}},{"$SystemUpdate/file", "content",0,0,true},{"readme","bytes"}}));
  const auto hash=DigestFile(original)->sha256;
  REQUIRE(HasZipSignature(original));
  REQUIRE_NOTHROW(ExtractUpdateZip(original,s.root/"out"));
  CHECK(Read(s.root/"out/$SystemUpdate/file")=="content");
  CHECK(Read(s.root/"out/readme")=="bytes");
  CHECK_THROWS(ExtractUpdateZip(original,s.root/"out"));
  CHECK(DigestFile(original)->sha256==hash);
}
TEST_CASE("System Update rejects unsafe ZIP metadata before publication", "[system-update]") {
  Scratch s;std::vector<Item> items={{"$SystemUpdate/good","bytes"}};
  SECTION("traversal"){items.push_back({"../escape","bad"});}
  SECTION("absolute"){items.push_back({"/escape","bad"});}
  SECTION("drive or ADS"){items.push_back({"file:stream","bad"});}
  SECTION("backslash traversal"){items.push_back({"..\\escape","bad"});}
  SECTION("Windows reserved"){items.push_back({"NUL.txt","bad"});}
  SECTION("trailing dot"){items.push_back({"file.","bad"});}
  SECTION("duplicate conflict"){items.push_back({"$SystemUpdate/GOOD","bad"});}
  SECTION("file directory collision"){items={{"folder","file"},{"folder/file","bad"}};}
  SECTION("unix symlink"){items.push_back({"link","../escape",0120777u<<16});}
  SECTION("unix device"){items.push_back({"device","bad",0020777u<<16});}
  SECTION("reparse point"){items.push_back({"link","bad",0x400});}
  SECTION("encryption"){items.push_back({"encrypted","bad",0,1});}
  SECTION("embedded nul"){items.push_back({std::string("safe\0/evil",10),"bad"});}
  Write(s.root/"zip",Zip(items));
  CHECK_THROWS(ExtractUpdateZip(s.root/"zip",s.root/"out"));
  CHECK_FALSE(fs::exists(s.root/"out"));CHECK_FALSE(fs::exists(s.root/"escape"));
}
TEST_CASE("System Update refuses corrupt truncated mismatched and excessive ZIPs", "[system-update]") {
  Scratch s;auto bytes=Zip({{"file","content"}});
  SECTION("CRC"){bytes[30+4]='X';}
  SECTION("truncated"){bytes.resize(bytes.size()-12);}
  SECTION("size bound"){Le(bytes,30+4+7+24,0x7FFFFFFF);}
  SECTION("local name differs"){bytes[30]='X';}
  SECTION("local compression differs"){Le(bytes,8,8,2);}
  SECTION("local CRC differs"){Le(bytes,14,0);}
  SECTION("local length differs"){Le(bytes,22,0);}
  SECTION("too many entries"){std::vector<Item> v(4097,{"file",""});bytes=Zip(v);}
  Write(s.root/"zip",bytes);
  CHECK_THROWS(ExtractUpdateZip(s.root/"zip",s.root/"out"));
}
TEST_CASE("System Update PIRS recognition uses metadata and rejects unverified dashboard bytes", "[system-update]") {
  Scratch s;Write(s.root/"update/renamed.package",Package());
  CHECK(IsSystemUpdatePackage(s.root/"update/renamed.package"));
  CHECK(FindSystemUpdatePackage(s.root/"update")==s.root/"update/renamed.package");
  CHECK_THROWS(ImportSource(s.root/"update/renamed.package",X360N_CONTENT_EXE,s.root/"logs"));
  CHECK_FALSE(fs::exists(s.root/"logs/extraction"));
  CHECK(Read(s.root/"logs/extraction.error.log").find("Retail 17559 SHA-256")!=std::string::npos);
  Write(s.root/"update/another.package",Package());
  CHECK_THROWS(FindSystemUpdatePackage(s.root/"update"));
}
TEST_CASE("System Update cancellation and failure allow a fresh isolated retry", "[system-update]") {
  Scratch s;Write(s.root/"update",Zip({{"$SystemUpdate/renamed",Package()}}));
  const auto before=DigestFile(s.root/"update")->sha256;
  bool cancel=false;
  CHECK_THROWS(ImportSource(s.root/"update",X360N_CONTENT_EXE,s.root/"logs",[&]{return cancel;},[&]{cancel=true;}));
  CHECK_FALSE(fs::exists(s.root/"logs/extraction"));
  CHECK_THROWS(ImportSource(s.root/"update",X360N_CONTENT_EXE,s.root/"retry"));
  CHECK_FALSE(fs::exists(s.root/"retry/extraction"));CHECK(DigestFile(s.root/"update")->sha256==before);
}
TEST_CASE("System Update bounds PIRS allocation parents duplicates and block chains before SDK mounting", "[system-update]") {
  Scratch s;auto bytes=Package();std::string expected;
  SECTION("excessive allocation") {
    rex::filesystem::StfsHeader h{};std::memcpy(&h,bytes.data(),sizeof(h));
    h.metadata.volume_descriptor.stfs.total_block_count=0xFFFFFF;
    std::memcpy(bytes.data(),&h,sizeof(h));expected="allocation";
  }
  SECTION("out of range parent") {
    rex::filesystem::StfsDirectoryEntry e{};std::memcpy(&e,bytes.data()+0xB000,sizeof(e));
    e.directory_index=12;std::memcpy(bytes.data()+0xB000,&e,sizeof(e));expected="parent index";
  }
  SECTION("duplicate entry") {bytes.replace(0xB040,64,bytes.substr(0xB000,64));expected="duplicate";}
  SECTION("cyclic file chain") {bytes[0xA02D]=bytes[0xA02E]=bytes[0xA02F]=0;expected="unterminated";}
  SECTION("unsafe name") {
    rex::filesystem::StfsDirectoryEntry e{};std::memcpy(&e,bytes.data()+0xB000,sizeof(e));
    std::memcpy(e.name,"../",3);e.flags.name_length=3;
    std::memcpy(bytes.data()+0xB000,&e,sizeof(e));expected="unsafe";
  }
  Write(s.root/"package",bytes);
  CHECK_THROWS(ImportSource(s.root/"package",X360N_CONTENT_EXE,s.root/"logs"));
  CHECK(Read(s.root/"logs/extraction.error.log").find(expected)!=std::string::npos);
  CHECK_FALSE(fs::exists(s.root/"logs/extraction"));
}
TEST_CASE("real official 17559 import preserves source and refuses readiness without font bases", "[.local-system-update]") {
  const auto* value=std::getenv("X360N_SYSTEM_UPDATE");REQUIRE(value);
  const fs::path zip=PathFromUtf8(value);Scratch s;
  std::cerr<<"official import scratch: "<<Utf8(s.root)<<"\n"<<std::flush;
  const auto before=DigestFile(zip)->sha256;
  std::cerr<<"official ZIP source hashed\n"<<std::flush;
  auto source=zip;
  bool usb=true;
  SECTION("ZIP content recognition"){}
  SECTION("extracted SystemUpdate directory"){ExtractUpdateZip(zip,s.root/"usb");source=s.root/"usb/$SystemUpdate";}
  SECTION("renamed internal PIRS"){
    ExtractUpdateZip(zip,s.root/"usb");
    fs::copy_file(s.root/"usb/$SystemUpdate/su20076000_00000000",s.root/"internal.package");source=s.root/"internal.package";usb=false;
  }
  // This test needs only the production 17559 recipe, not every game's large
  // inventory or local library. Keep both source and destinations isolated.
  for (const auto* file:{"preparation.toml","dash_17559_manifest.toml"}) {
    const auto dest=s.root/"titles/dash_17559"/file;fs::create_directories(dest.parent_path());
    fs::copy_file(fs::path(X360N_SOURCE_ROOT)/"titles/dash_17559"/file,dest);
  }
  auto catalog=ReadCatalog(ProjectPaths{s.root});
  std::cerr<<"official import catalog read\n"<<std::flush;
  auto it=std::find_if(catalog.begin(),catalog.end(),[](const auto& t){return t.id=="dash_17559";});REQUIRE(it!=catalog.end());
  auto target=*it;
  for (auto& i:target.inputs) i.destination=s.root/".local/inputs/dashboards/17559"/i.destination.lexically_relative(target.game_root);
  target.game_root=s.root/".local/inputs/dashboards/17559";
  target.preparation_source.clear();
  Write(s.root/".local/user/profile/save","preserved");Write(s.root/".local/inputs/dashboards/6717/original","preserved");
  {
    ImportSource imported(source,X360N_CONTENT_EXE,s.root/"logs");REQUIRE(imported.system_update());
    std::cerr<<"official source extracted\n"<<std::flush;
    CHECK(imported.report().find(usb?"required-missing\t2":"required-missing\t3")!=std::string::npos);
    CHECK(imported.report().find("xenonjklatin.xttp")!=std::string::npos);
    CHECK(Import(ProjectPaths{s.root},target,imported.path(),{},true)==(usb?14:12));
    CHECK(Import(ProjectPaths{s.root},target,imported.path(),{},true)==0);
    const auto state=Inspect(ProjectPaths{s.root},target);CHECK_FALSE(state.inputs_ok);CHECK_FALSE(state.prepared);
    CHECK(DigestFile(target.game_root/"shrdres.xzp")->sha256=="179c5be167402297c6e137c08fefe3146f6f497964dc2bda94812596fc9aefe0");
    if (const auto* complement=std::getenv("X360N_SYSTEM_UPDATE_COMPLEMENT")) {
      CHECK(Import(ProjectPaths{s.root},target,PathFromUtf8(complement))>=size_t(usb?2:3));
      CHECK(Inspect(ProjectPaths{s.root},target).inputs_ok);
      CHECK_FALSE(Inspect(ProjectPaths{s.root},target).prepared);
    }
    Write(target.game_root/"vk.xex","incorrect");
    CHECK_THROWS(Import(ProjectPaths{s.root},target,imported.path(),{},true));
    CHECK(Read(target.game_root/"vk.xex")=="incorrect");
  }
  CHECK(DigestFile(zip)->sha256==before);
  CHECK(Read(s.root/".local/user/profile/save")=="preserved");
  CHECK(Read(s.root/".local/inputs/dashboards/6717/original")=="preserved");
  CHECK(fs::exists(s.root/"logs/system-update-17559.tsv"));
  std::cout<<"official 17559 source SHA-256 "<<before<<"\n";
}
