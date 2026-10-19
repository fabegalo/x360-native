#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <span>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xex_module.h>
#include "synthetic_xex.h"
#include "../supervisor/scratch.h"
using rex::X_STATUS;
namespace {
using Key = std::array<uint8_t, 16>;
// The SDK owns its wrapping-key constants. The test reads that pinned source
// instead of copying key bytes into the project's test data or diagnostics.
std::array<Key, 4> ReadSdkKeys() {
 std::ifstream file(std::filesystem::path(X360N_SOURCE_ROOT)/"external/rexglue-sdk/src/system/xex_module.cpp");
 REQUIRE(file.good());
 const std::string source((std::istreambuf_iterator<char>(file)),{});
 const std::array<const char*,2> names{"xe_xex2_retail_key","xe_xex2_devkit_key"};
 std::array<Key,4> keys{};
 for(size_t i=0;i<names.size();++i) {
  const auto start=source.find(std::string(names[i])+"[16]");
  REQUIRE(start!=std::string::npos);
  const auto open=source.find('{',start), close=source.find('}',open);
  REQUIRE(close!=std::string::npos);
  const auto body=source.substr(open,close-open);
  const std::regex octet("0x([0-9A-Fa-f]{2})"); size_t index=0;
  for(auto it=std::sregex_iterator(body.begin(),body.end(),octet);it!=std::sregex_iterator();++it) {
   REQUIRE(index<16); keys[i][index++]=uint8_t(std::stoul((*it)[1].str(),nullptr,16));
  }
  REQUIRE(index==16);
 }
 for (unsigned i=0;i<16;++i) { keys[2][i]=uint8_t(0x41+i); keys[3][i]=uint8_t(0x71+i); }
 return keys;
}

struct LocalKeys {
 std::filesystem::path file = UniqueTempPath("synthetic-legacy-keys.txt");
 std::string saved = rex::cvar::GetFlagByName("xex_keys_file");
 LocalKeys(const std::array<Key,4>& keys) {
  std::ofstream out(file);
  const char* digits="0123456789abcdef";
  for(unsigned i=2;i<4;++i) {
   out << (i==2?"xex1-retail ":"xex1-devkit ");
   for(auto b:keys[i]) out << digits[b>>4] << digits[b&15];
   out << '\n';
  }
  out.close();
  REQUIRE(rex::cvar::SetFlagByName("xex_keys_file",file.string()));
 }
 ~LocalKeys() { rex::cvar::SetFlagByName("xex_keys_file",saved); std::filesystem::remove(file); }
};

std::vector<uint8_t> Encrypt(const Key& key, std::span<const uint8_t> bytes);
namespace {
using Bytes = std::vector<uint8_t>;
void Put32(Bytes& b, size_t at, uint32_t value) {
 for(unsigned i=0;i<4;++i) b.at(at+i)=uint8_t(value>>(24-i*8));
}
void Put16(Bytes& b, size_t at, uint16_t value) { b.at(at)=uint8_t(value>>8);b.at(at+1)=uint8_t(value); }
Bytes Sha1(std::span<const uint8_t> bytes) {
 BCRYPT_ALG_HANDLE alg{};
 REQUIRE(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA1_ALGORITHM,nullptr,0)==0);
 Bytes hash(20);
 const auto result=BCryptHash(alg,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),hash.data(),ULONG(hash.size()));
 BCryptCloseAlgorithmProvider(alg,0); REQUIRE(result==0); return hash;
}
Bytes ResourceImage(const Key& master, unsigned compression, bool encrypted=true, uint32_t flags=10) {
 constexpr uint32_t address=0x91000000;
 Bytes header(0x1000), image(0x2000);
 for(size_t i=0;i<0x1000;++i) image[i]=uint8_t(i*7+3);
 std::memcpy(image.data(),"XUIZ",4);
 Put32(header,0,0x58455832);Put32(header,4,flags);Put32(header,8,0x1000);
 Put32(header,0x10,0x100);Put32(header,0x14,2);
 Put32(header,0x18,0x3FF);Put32(header,0x1C,0x40);
 Put32(header,0x20,0x2FF);Put32(header,0x24,0x80);
 Put32(header,0x80,20);std::memcpy(header.data()+0x84,"skin",4);
 Put32(header,0x8C,address);Put32(header,0x90,64);
 Put32(header,0x100,0x184+48);Put32(header,0x104,uint32_t(image.size()));
 Put32(header,0x20C,0x10000000);Put32(header,0x210,address);
 Put32(header,0x280,2);Put32(header,0x284,0x12);Put32(header,0x29C,0x13);
 Bytes link(image.begin()+0x1000,image.end());link.insert(link.end(),header.begin()+0x29C,header.begin()+0x2B4);
 const auto last=Sha1(link);std::copy(last.begin(),last.end(),header.begin()+0x288);
 link.assign(image.begin(),image.begin()+0x1000);link.insert(link.end(),header.begin()+0x284,header.begin()+0x29C);
 const auto first=Sha1(link);std::copy(first.begin(),first.end(),header.begin()+0x214);
 Key session{};for(unsigned i=0;i<16;++i)session[i]=uint8_t(i+0x20);
 const auto wrapped=Encrypt(master,session);std::copy(wrapped.begin(),wrapped.end(),header.begin()+0x250);
 Put16(header,0x44,encrypted?1:0);Put16(header,0x46,uint16_t(compression));
 Bytes body;
 if(compression==0) { Put32(header,0x40,8);body=image; }
 else if(compression==1) {
  Put32(header,0x40,16);Put32(header,0x48,0x1000);Put32(header,0x4C,0x1000);
  body.assign(image.begin(),image.begin()+0x1000);
 } else {
  Put32(header,0x40,0x24);Put32(header,0x48,0x8000);
  const auto bits=(3u<<28)|(uint32_t(image.size())<<4);
  Bytes stored{uint8_t(bits>>16),uint8_t(bits>>24),uint8_t(bits),uint8_t(bits>>8)};
  for(unsigned i=0;i<3;++i)stored.insert(stored.end(),{1,0,0,0});
  stored.insert(stored.end(),image.begin(),image.end());
  body.resize(24);body.push_back(uint8_t(stored.size()>>8));body.push_back(uint8_t(stored.size()));
  body.insert(body.end(),stored.begin(),stored.end());body.insert(body.end(),{0,0});
  Put32(header,0x4C,uint32_t(body.size()));const auto digest=Sha1(body);
  std::copy(digest.begin(),digest.end(),header.begin()+0x50);
 }
 body.resize((body.size()+15)&~size_t(15));if(encrypted)body=Encrypt(session,body);
 header.insert(header.end(),body.begin(),body.end());return header;
}
}
TEST_CASE("XEX resource-only modules accept authenticated encrypted payloads and reject executable metadata", "[sdk][xex-resource]") {
 using namespace x360n::test;
 const auto keys=ReadSdkKeys(); LocalKeys local(keys); Workdir work("x360n_xex_resource","authenticated");
 rex::Runtime rt(work.dir);rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
 REQUIRE(rt.Setup(std::move(config))==X_STATUS_SUCCESS);
 for(unsigned compression=0;compression<3;++compression) {
  for(unsigned key=0;key<keys.size();++key) {
   INFO("compression="<<compression<<" key="<<key);
   auto bytes=ResourceImage(keys[key],compression);
   rex::runtime::XexModule module(rt.function_dispatcher(),rt.kernel_state());
   REQUIRE(module.Load("skin.xex","game:\\skin.xex",bytes.data(),bytes.size()));
   REQUIRE(module.LoadContinue());
   CHECK(std::memcmp(rt.memory()->TranslateVirtual(0x91000000),"XUIZ",4)==0);
   REQUIRE(module.Unload());
  }
 }
 for(unsigned invalid=0;invalid<6;++invalid) {
  auto bytes=ResourceImage(keys[0],2);
  if(invalid==0)bytes[0x214]^=1; // Root page digest.
  if(invalid==1)bytes[0x288]^=1; // Chained page digest.
  if(invalid==2)Put32(bytes,0x284,0x11); // Code section.
  if(invalid==3)Put32(bytes,0x8C,0x90FFFFFF); // Resource outside image.
  if(invalid==4) {Put32(bytes,0x14,3);Put32(bytes,0x28,0x10100);Put32(bytes,0x2C,0x91000000);} // Entrypoint.
  if(invalid==5)Put32(bytes,4,0x8A); // Unsupported executable class.
  rex::runtime::XexModule module(rt.function_dispatcher(),rt.kernel_state());
  CHECK_FALSE(module.Load("invalid.xex","game:\\invalid.xex",bytes.data(),bytes.size()));
  REQUIRE(module.Unload());
 }
}
std::vector<uint8_t> Encrypt(const Key& key, std::span<const uint8_t> bytes) {
 BCRYPT_ALG_HANDLE algorithm=nullptr;
 REQUIRE(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0)==0);
 REQUIRE(BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),sizeof(BCRYPT_CHAIN_MODE_CBC),0)==0);
 BCRYPT_KEY_HANDLE handle=nullptr;
 REQUIRE(BCryptGenerateSymmetricKey(algorithm,&handle,nullptr,0,const_cast<PUCHAR>(key.data()),ULONG(key.size()),0)==0);
 std::array<uint8_t,16> iv{};
 std::vector<uint8_t> output(bytes.size()); ULONG written=0;
 REQUIRE(BCryptEncrypt(handle,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),nullptr,iv.data(),ULONG(iv.size()),output.data(),ULONG(output.size()),&written,0)==0);
 REQUIRE(written==bytes.size());
 BCryptDestroyKey(handle); BCryptCloseAlgorithmProvider(algorithm,0);
 return output;
}
}
TEST_CASE("XEX loader accepts legacy and current wrapping keys without losing other allocations", "[sdk][xex-key]") {
 using namespace x360n::test;
 const auto keys=ReadSdkKeys(); LocalKeys local(keys);
 Workdir work("x360n_xex_key","wrapping");
 rex::Runtime rt(work.dir);
 rex::RuntimeConfig config{}; config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
 REQUIRE(rt.Setup(std::move(config))==X_STATUS_SUCCESS);
 auto* heap=rt.memory()->LookupHeap(kXexBase);
 constexpr auto rw=rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite;
 constexpr uint32_t neighbor=kXexBase-4*kXexPage;
 REQUIRE(heap->AllocFixed(neighbor,kXexPage,kXexPage,rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,rw));
 *rt.memory()->TranslateVirtual<uint32_t*>(neighbor)=0x13579BDF;
 for(unsigned choice=0;choice<keys.size();++choice) {
  INFO("wrapping key index " << choice);
  std::vector<Placed> placed;
  auto bytes=BuildXex({},&placed,0,Entry::kReturnZero);
  Key session{}; for(unsigned i=0;i<16;++i) session[i]=uint8_t(0xF0+i);
  const auto wrapped=Encrypt(keys[choice],session);
  std::memcpy(bytes.data()+0x800+0x150,wrapped.data(),wrapped.size());
  bytes[0x44]=0;bytes[0x45]=1; // XEX_ENCRYPTION_NORMAL
  const auto encrypted=Encrypt(session,std::span<const uint8_t>(bytes).subspan(0x1000));
  std::memcpy(bytes.data()+0x1000,encrypted.data(),encrypted.size());
  rex::runtime::XexModule module(rt.function_dispatcher(),rt.kernel_state());
  REQUIRE(module.Load("synthetic.xex","game:\\synthetic.xex",bytes.data(),bytes.size()));
  REQUIRE(module.LoadContinue());
  CHECK(module.is_dev_kit()==(choice==1||choice==3));
  CHECK(module.base_address()==kXexBase);
  CHECK(rex::memory::load_and_swap<uint32_t>(rt.memory()->TranslateVirtual(kXexText))==0x38600000);
  CHECK(*rt.memory()->TranslateVirtual<uint32_t*>(neighbor)==0x13579BDF);
  REQUIRE(module.Unload());
 }
 REQUIRE(heap->Release(neighbor));
}

TEST_CASE("Legacy encrypted resources reject missing or malformed local key material", "[sdk][xex-key]") {
 using namespace x360n::test;
 const auto keys=ReadSdkKeys(); LocalKeys local(keys);
 SECTION("missing file") { std::filesystem::remove(local.file); }
 SECTION("missing legacy retail") { std::ofstream(local.file) << "xex2-devkit 00000000000000000000000000000000\n"; }
 SECTION("malformed hex") { std::ofstream(local.file) << "xex1-retail invalid\n"; }
 SECTION("duplicate name") {
  std::ofstream(local.file) << "xex1-retail 00000000000000000000000000000000\nxex1-retail 00000000000000000000000000000000\n";
 }
 Workdir work("x360n_xex_key","invalid-local");
 rex::Runtime rt(work.dir); rex::RuntimeConfig config{}; config.tool_mode=true; config.kernel_init=rex::kernel::InitializeKernel;
 REQUIRE(rt.Setup(std::move(config))==X_STATUS_SUCCESS);
 auto bytes=ResourceImage(keys[2],2);
 rex::runtime::XexModule module(rt.function_dispatcher(),rt.kernel_state());
 CHECK_FALSE(module.Load("skin.xex","game:\\skin.xex",bytes.data(),bytes.size()));
 REQUIRE(module.Unload());
}
