#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <algorithm>
#include <fstream>
#include "../../external/rexglue-sdk/src/codegen/decoded_binary.h"

using namespace x360n::test;

namespace {
// Synthetic implementations of the console CRT jump-buffer ABI. Register
// stores/loads are generated from their architectural fields, not game bytes.
std::vector<uint32_t> JumpBufferRoutine(bool save, bool dispatch_prefix = true) {
  std::vector<uint32_t> words;
  if (save) {
    if (dispatch_prefix) words = {0x3C808200,0x80040040,0x2C000000,0x7C0903A6,0x4C820420};
    words.insert(words.end(), {0x7C0802A6,0x7C800026});
  } else {
    words = {0x7C0802A6,0x9421FFB0,0x90010008,0x7C862378,0x2C040000,
      0x80030138,0x2C800000,0x7C671B78,0x38A00000,0x40820008,0x38C00001,
      0x408602C0,0x80670134,0x80870090,0x48000001};
  }
  const uint32_t base = save ? 3 : 7;
  for (uint32_t reg = 14; reg <= 31; ++reg)
    words.push_back(((save ? 54u : 50u) << 26) | (reg << 21) | (base << 16) | ((reg-14)*8));
  for (uint32_t reg = 13; reg <= 31; ++reg)
    words.push_back(((save ? 62u : 58u) << 26) | (reg << 21) | (base << 16) | (0x98+(reg-13)*8));
  for (uint32_t reg = 64; reg <= 127; ++reg) {
    words.push_back((save ? 0x38A00000 : 0x38600000) | (0x140+(reg-64)*16));
    words.push_back((save ? 0x10051D0B : 0x100338CB) | ((reg & 31) << 21) | ((reg & 32) >> 3));
  }
  if (save) words.insert(words.end(), {0x90030134,0x90830130,0xF8230090,
      0x38000000,0x90030138,0x38600000,0x4E800020});
  else words.insert(words.end(), {0x80A70134,0x80870130,0x7CA803A6,0xE8270090,
      0x7C8FF120,0x7CC33378,0x4E800020,0x80670004,0x80870000,0x48000001,
      0x80670000,0x80870004,0x48000001,0x80010008,0x7C0803A6,0x38210050,0x4E800020});
  return words;
}
}

TEST_CASE("CRT nonlocal jumps are recognized without title addresses",
          "[sdk][codegen][nonlocal-jumps]") {
  for (unsigned variant = 0; variant < 7; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      for (unsigned i=0;i<4;++i) bytes.at(0x1000+address-kXexBase+i)=uint8_t(word>>(24-i*8));
    };
    const auto le32 = [&](size_t offset, uint32_t word) {
      for (unsigned i=0;i<4;++i) bytes.at(offset+i)=uint8_t(word>>(i*8));
    };
    // Expand only the synthetic PE text section, still inside its mapped page.
    constexpr size_t text_section = 0x1000+0x98+224+40;
    le32(text_section+8,0x1000); le32(text_section+16,0x1000);
    if (variant == 6) { le32(text_section+8,0x2000); le32(text_section+16,0x2000); }
    auto save = JumpBufferRoutine(true, variant != 1);
    auto restore = JumpBufferRoutine(false);
    const uint32_t save_address = variant == 5 ? kXexSlots+0x100 : kXexText+0x100;
    const uint32_t restore_address = kXexText + 0x500;
    if (variant == 2) save.at(save.size()-3) ^= 4;  // Wrong jump-buffer flag slot.
    if (variant == 3) restore.at(70) ^= 4;         // Wrong VMX register/encoding.
    if (variant == 4) { le32(text_section+8,0x700); le32(text_section+16,0x700); }
    if (variant == 5) { le32(text_section-40+8,0x1000); le32(text_section-40+16,0x1000); }
    for (size_t i=0;i<save.size();++i) put(save_address+uint32_t(i)*4,save[i]);
    for (size_t i=0;i<restore.size();++i) {
      auto word=restore[i];
      if (word==0x48000001) word |= (kXexText+0x900-(restore_address+uint32_t(i)*4))&0x03FFFFFC;
      put(restore_address+uint32_t(i)*4,word);
    }
    if (variant == 6) {
      for (size_t i=0;i<save.size();++i) put(save_address+0x1000+uint32_t(i)*4,save[i]);
      for (size_t i=0;i<restore.size();++i) {
        auto word=restore[i];
        if(word==0x48000001) word|=(kXexText+0x900-(restore_address+0x1000+uint32_t(i)*4))&0x03FFFFFC;
        put(restore_address+0x1000+uint32_t(i)*4,word);
      }
    }
    put(kXexText+0x900,0x4E800020);
    put(kXexText,0x48000001 | ((save_address-kXexText)&0x03FFFFFC));
    put(kXexText+4,0x48000001 | ((restore_address-kXexText-4)&0x03FFFFFC));
    put(kXexText+8,0x4E800020);
    put(kXexPdata+4,(1u<<30)|(3u<<8));
    Workdir work("x360n_nonlocal_jump", "automatic");
    work.write("test.xex",bytes);
    std::ofstream(work.dir/"test.toml") << "project_name='jumps'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline=rex::codegen::CodegenPipeline::Create(work.dir/"test.toml");
    REQUIRE(pipeline.has_value());
    if (variant == 4 || variant == 5) {
      // Analyze/Register must not read the tail outside the declared section.
      (void)pipeline->RunAnalyze();
    } else REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK(pipeline->context().Config().setJmpAddress == 0);
    CHECK(pipeline->context().Config().longJmpAddress == 0);
    if (variant == 4 || variant == 5) {
      const auto* routine = pipeline->context().graph.getFunction(variant == 4 ? restore_address : save_address);
      CHECK((!routine || routine->authority()!=rex::codegen::FunctionAuthority::HELPER));
      continue;
    }
    if (variant == 6) {
      for (uint32_t address : {save_address+0x1000,restore_address+0x1000}) {
        const auto* routine=pipeline->context().graph.getFunction(address);
        REQUIRE(routine);
        CHECK(routine->authority()==rex::codegen::FunctionAuthority::HELPER);
      }
    }
    REQUIRE(pipeline->RunWrite().has_value());
    std::string cpp;
    for (const auto& file:std::filesystem::directory_iterator(work.dir/"out")) {
      if (file.path().extension()!=".cpp") continue;
      std::ifstream input(file.path()); cpp.append(std::istreambuf_iterator<char>(input),{});
    }
    CHECK((cpp.find("ppc_setjmp(ctx.r3.u32)")!=std::string::npos) == (variant!=2));
    CHECK((cpp.find("ppc_longjmp(ctx.r3.u32, ctx.r4.s32)")!=std::string::npos) == (variant!=3));
  }
}

TEST_CASE("Materialized code addresses discover bounded frameless callbacks",
          "[sdk][codegen][gapfill][code-address-entry]") {
  for (unsigned variant=0;variant<10;++variant) {
    CAPTURE(variant);
    auto bytes=BuildXex({},nullptr,0,Entry::kReturnZero);
    const auto put=[&](uint32_t address,uint32_t word) {
      for(unsigned i=0;i<4;++i) bytes.at(0x1000+address-kXexBase+i)=uint8_t(word>>(24-i*8));
    };
    const uint32_t target = variant==2 ? kXexText+0xFF30 : kXexText+48;
    if(variant==2) {
      for(size_t field:{size_t(0x1000+0x98+224+40+8),size_t(0x1000+0x98+224+40+16)})
        for(unsigned i=0;i<4;++i) bytes.at(field+i)=uint8_t(0x10000>>(i*8));
    }
    const uint32_t pointer=variant==8 ? kXexSlots : target;
    const bool use_ori=variant==1;
    const uint32_t high=use_ori ? pointer>>16 : (pointer+0x8000)>>16;
    put(kXexText,0x3D600000|high);                    // lis r11,high
    put(kXexText+4,variant==3 ? 0x39600000 : 0x39000001); // li r11,0 or li r8,1
    put(kXexText+8,0x80BF0010);                      // lwz r5,16(r31)
    put(kXexText+12,variant==7 ? 0x616B0123 : 0x38C00004); // clobber r11 or li r6,4
    put(kXexText+16,(use_ori ? 0x61640000 : 0x388B0000)|(pointer&0xFFFF));
    put(kXexText+20,0x4E800020);
    if(variant==4) put(kXexText+4,0x4182000C);        // branch into a different block
    put(target-24,0x81630C28); put(target-20,0x386B0008);
    put(target-16,0x816B0008); put(target-12,0x816B0014);
    put(target-8,0x7D6903A6); put(target-4,variant==6 ? 0x38600000 : 0x4E800420);
    put(target,0x81630000); put(target+4,0x816B0198);
    put(target+8,0x7D6903A6); put(target+12,0x4E800420);
    if(variant==9) {
      const uint32_t callback[]{0x81630000,0x39440004,0x81240014,0xE8840004,
          0x792607E6,0x816B0148,0xE8AA0008,0x7D6903A6,0x4E800420};
      for(unsigned i=0;i<std::size(callback);++i) put(target+i*4,callback[i]);
    }
    put(kXexPdata+4,(1u<<30)|((variant==5 ? 16u : 6u)<<8));
    Workdir work("x360n_code_address","callback"); work.write("test.xex",bytes);
    std::ofstream(work.dir/"test.toml") << "project_name='addressed'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline=rex::codegen::CodegenPipeline::Create(work.dir/"test.toml");
    REQUIRE(pipeline.has_value()); REQUIRE(pipeline->RunAnalyze().has_value());
    if(variant==9) {
      for(unsigned i=0;i<9;++i) {
        const auto* decoded=pipeline->context().decoded().get(target+i*4);
        CAPTURE(i);
        REQUIRE(decoded);
        CHECK(decoded->opcode!=rex::codegen::ppc::Opcode::kUnknown);
      }
    }
    CHECK(pipeline->context().graph.isEntryPoint(target)==(variant<3 || variant==9));
    REQUIRE(pipeline->RunWrite().has_value());
  }
}

TEST_CASE("Embedded jump-table data is not a materialized function entry",
          "[sdk][codegen][gapfill][code-address-entry][embedded-table]") {
  auto bytes=BuildXex({},nullptr,0,Entry::kReturnZero);
  const auto put=[&](uint32_t address,uint32_t word) {
    for(unsigned i=0;i<4;++i) bytes.at(0x1000+address-kXexBase+i)=uint8_t(word>>(24-i*8));
  };
  constexpr uint32_t dispatch=kXexText+64;
  const uint32_t code[]={0x28030001,0x4181003C,0x5460103A,0x3D808201,
      0x398C0060,0x7C0C002E,0x7C0903A6,0x4E800420,
      dispatch+48,dispatch+64,0x38600009,0x4E800020,
      0x2C030000,0x4182000C,0x38600001,0x4E800020,0x38600002,0x4E800020};
  for(unsigned i=0;i<std::size(code);++i) put(dispatch+i*4,code[i]);
  put(kXexText,0x48000041);put(kXexText+4,0x4E800020);
  Workdir work("x360n_code_address","embedded_table");work.write("test.xex",bytes);
  std::ofstream(work.dir/"test.toml") << "project_name='embedded'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline=rex::codegen::CodegenPipeline::Create(work.dir/"test.toml");
  REQUIRE(pipeline.has_value());REQUIRE(pipeline->RunAnalyze().has_value());
  const auto& graph=pipeline->context().graph;
  const auto* entry=graph.getFunction(dispatch);REQUIRE(entry);
  REQUIRE(entry->jumpTables().size()==1);
  CHECK(entry->jumpTables()[0].tableAddress==dispatch+32);
  CHECK(entry->jumpTables()[0].tableByteLength==8);
  CHECK_FALSE(graph.isEntryPoint(dispatch+32));
  CHECK(entry->containsAddress(dispatch+64));
  REQUIRE(pipeline->RunWrite().has_value());
}

TEST_CASE("Data references discover frameless callbacks without RTTI",
          "[sdk][codegen][gapfill][data-entry]") {
  for (bool writableName : {false,true}) {
    CAPTURE(writableName);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - i * 8));
    };
    put(kXexText + 8, 0x38630110);
    put(kXexText + 12, 0x48000074); // wrapper tails to text+128
    put(kXexText + 16, 0x38600025); put(kXexText + 20, 0x4E800020);
    put(kXexText + 128, 0x38600000); put(kXexText + 132, 0x4E800020);
    put(kXexSlots + 64, kXexText + 16); // callback table/vtable slot
    if (writableName) {
      const std::string oldName(".rdata\0\0",8), newName(".data\0\0\0",8);
      const auto name = std::search(bytes.begin(),bytes.end(),oldName.begin(),oldName.end());
      REQUIRE(name != bytes.end());
      std::copy(newName.begin(),newName.end(),name);
    }
    Workdir work("x360n_discovery", "data_callback");
    work.write("test.xex", bytes);
    std::ofstream(work.dir / "test.toml") << "project_name='callbacks'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    const auto* callback = pipeline->context().graph.getFunction(kXexText + 16);
    REQUIRE(callback);
    CHECK(callback->isSealed());
    REQUIRE(pipeline->RunWrite().has_value());
  }
}

TEST_CASE("Data entry discovery excludes metadata interiors and ordinary fallthrough",
          "[sdk][codegen][gapfill][data-entry]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  const auto put = [&](uint32_t address, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i)
      bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - i * 8));
  };
  put(kXexText + 8, 0x38600001); put(kXexText + 12, 0x38600002);
  put(kXexText + 16, 0x4E800020);
  put(kXexSlots + 64, kXexText + 4); // original pdata interior
  put(kXexSlots + 68, kXexText + 12); // preceded by ordinary addi
  put(kXexSlots + 72, kXexText + 13); // unaligned
  put(kXexSlots + 76, kXexSlots + 64); // non-executable data
  Workdir work("x360n_discovery", "invalid_data_candidates");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='callbacks'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  const auto& graph = pipeline->context().graph;
  for (uint32_t address : {kXexText+4,kXexText+12,kXexText+13,kXexSlots+64})
    CHECK_FALSE(graph.isEntryPoint(address));
  REQUIRE(pipeline->RunWrite().has_value());
}

TEST_CASE("GapFill discovers direct tail targets hidden behind adjacent wrappers",
          "[sdk][codegen][gapfill][tail-discovery]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  const auto put = [&](uint32_t offset, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i)
      bytes.at(0x11000 + offset + i) = uint8_t(word >> (24 - i * 8));
  };
  const auto branch = [&](uint32_t site, uint32_t target) {
    put(site, 0x48000000 | ((target - site) & 0x03FFFFFC));
  };
  // The gap starts with a wrapper that tails elsewhere. The following getter
  // and adjustors have no pdata or RTTI. Other wrappers reference them by b.
  put(8, 0x38630110); branch(12, 128);
  put(16, 0x3D600001); put(20, 0x616B0010);
  put(24, 0x7C63582E); put(28, 0x4E800020);
  put(32, 0x38630110); branch(36, 136);
  put(40, 0x38630110); branch(44, 144);
  put(48, 0x3C630001); put(52, 0x38630100); put(56, 0x4E800020);
  for (const auto [site, target] : {std::pair{64u,16u}, {80u,40u}, {96u,48u}, {112u,32u}}) {
    put(site, 0x38600000); branch(site + 4, target);
  }
  for (uint32_t offset : {128u,136u,144u}) {
    put(offset, 0x38600001); put(offset + 4, 0x4E800020);
  }
  Workdir work("x360n_discovery", "direct_tail_wrappers");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='tails'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  const auto result = pipeline->RunAnalyze();
  for (uint32_t offset : {16u,32u,40u,48u,128u,136u,144u}) {
    CAPTURE(offset);
    CHECK(pipeline->context().graph.isEntryPoint(kXexText + offset));
  }
  REQUIRE(result.has_value());
  REQUIRE(pipeline->RunWrite().has_value());
}

TEST_CASE("Direct tail discovery preserves data and declared metadata holes",
          "[sdk][codegen][gapfill][tail-discovery]") {
  for (unsigned protection : {0u,1u,2u}) {
    CAPTURE(protection);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - i * 8));
    };
    put(kXexText + 8, 0x38630110);
    put(kXexText + 12, 0x48000074); // b text+128, no fall-through
    put(kXexText + 16, 0x38600025); put(kXexText + 20, 0x4E800020);
    const auto target = protection ? kXexText + 16 : kXexSlots;
    put(kXexText + 64, 0x38600000);
    put(kXexText + 68, 0x48000000 | ((target - kXexText - 68) & 0x03FFFFFC));
    put(kXexText + 128, 0x38600000); put(kXexText + 132, 0x4E800020);
    if (protection == 2) put(kXexPdata + 4, (1u << 30) | (8u << 8));
    Workdir work("x360n_discovery", "protected_direct_tail");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='tails'\nfile_path='test.xex'\nout_directory_path='out'\n";
    if (protection == 1) config << "[functions]\n0x82010000 = { size = 32 }\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    const auto result = pipeline->RunAnalyze();
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(target));
    if (!protection) {
      CHECK_FALSE(result.has_value()); // Branches into data remain errors.
    } else {
      REQUIRE(result.has_value());
      const auto* owner = pipeline->context().graph.getFunction(kXexText);
      REQUIRE(owner);
      CHECK(owner->containsAddress(target));
    }
  }
}

TEST_CASE("Direct tail entries take precedence over speculative gap fallthrough",
          "[sdk][codegen][gapfill][tail-discovery]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  const auto put = [&](uint32_t offset, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i)
      bytes.at(0x11000 + offset + i) = uint8_t(word >> (24 - i * 8));
  };
  put(0, 0x48000030); // pdata root tails to text+48
  // A separate gap prefix calls a helper, then falls through into the target.
  put(32, 0x38600001); put(36, 0x4800005D); // bl text+128
  put(40, 0x38600002); put(44, 0x48000055); // bl text+128
  put(48, 0x38600003); put(52, 0x4E800020);
  put(128, 0x38600004); put(132, 0x4E800020);
  Workdir work("x360n_discovery", "referenced_gap_interior");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='tails'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  const auto* target = pipeline->context().graph.getFunction(kXexText + 48);
  REQUIRE(target);
  CHECK(target->isSealed());
  const auto* prefix = pipeline->context().graph.getFunction(kXexText + 32);
  REQUIRE(prefix);
  // Falling through is still executable behavior: preserve it as well as the
  // separate entry used by the direct caller, rather than returning early.
  CHECK(prefix->containsAddress(kXexText + 48));
  REQUIRE(pipeline->RunWrite().has_value());
}

TEST_CASE("GapFill separates adjacent virtual tail thunks without pdata or RTTI",
          "[sdk][codegen][gapfill][aot-discovery]") {
  std::vector<Placed> placed;
  auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
  const auto put = [&](uint32_t address, uint32_t word) {
    const size_t offset = 0x1000 + address - kXexBase;
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
  };
  // The first leaf has pdata. Two virtual tail thunks and a leaf have none.
  const uint32_t thunks[] = {
      0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800420,  // r11, vtable slot 52
      0x81830000, 0x818C0038, 0x7D8903A6, 0x4E800420,  // r12, vtable slot 56
      0x38600025, 0x4E800020};
  for (unsigned i = 0; i < std::size(thunks); ++i) put(kXexText + 8 + 4 * i, thunks[i]);
  Workdir work("x360n_discovery", "virtual_thunks");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  const auto& graph = pipeline->context().graph;
  const auto* first = graph.getFunction(kXexText + 8);
  const auto* second = graph.getFunction(kXexText + 24);
  const auto* leaf = graph.getFunction(kXexText + 40);
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(leaf);
  CHECK(first->blocks().size() == 1);
  CHECK(first->blocks().front().size == 16);
  CHECK(second->blocks().size() == 1);
  CHECK(second->blocks().front().size == 16);
  CHECK(leaf->blocks().front().size == 8);
  CHECK(first->jumpTables().empty());
  CHECK(second->jumpTables().empty());
  CHECK(graph.getFunction(kXexText)->authority() == rex::codegen::FunctionAuthority::PDATA);
  REQUIRE(pipeline->RunWrite().has_value());
}

TEST_CASE("GapFill does not split calls or ambiguous CTR dispatch as virtual thunks",
          "[sdk][codegen][gapfill][aot-discovery]") {
  // A bctrl must preserve fall-through. Other near-matches may be dispatch
  // inside a larger function: the new recognition must not claim a boundary.
  const uint32_t variants[][4] = {
      {0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800421},  // bctrl
      {0x81630000, 0x816B0034, 0x7D6903A6, 0x4C820420},  // conditional bcctr
      {0x81630000, 0x818B0034, 0x7D6903A6, 0x4E800420},  // mismatched load
      {0x81630000, 0x816B0034, 0x7D8903A6, 0x4E800420},  // mismatched CTR
      {0x81630004, 0x816B0034, 0x7D6903A6, 0x4E800420},  // other object member
      {0x81630000, 0x816B0035, 0x7D6903A6, 0x4E800420},  // unaligned slot
      {0x81630000, 0x816BFFFC, 0x7D6903A6, 0x4E800420},  // before vtable
      {0x83E30000, 0x83FF0034, 0x7FE903A6, 0x4E800420},  // nonvolatile r31
      {0x81630000, 0x7D6B582E, 0x7D6903A6, 0x4E800420},  // indexed load / table
  };
  for (const auto& code : variants) {
    CAPTURE(code[0], code[1], code[2], code[3]);
    std::vector<Placed> placed;
    auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      const size_t offset = 0x1000 + address - kXexBase;
      for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
    };
    for (unsigned i = 0; i < 4; ++i) put(kXexText + 8 + 4 * i, code[i]);
    put(kXexText + 24, 0x38600025);
    put(kXexText + 28, 0x4E800020);
    Workdir work("x360n_discovery", "ambiguous_ctr");
    work.write("test.xex", bytes);
    std::ofstream(work.dir / "test.toml") << "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(kXexText + 24));
  }
}

TEST_CASE("Virtual thunk discovery respects declared extents including unreachable holes",
          "[sdk][codegen][gapfill][aot-discovery]") {
  for (bool configExtent : {false, true}) {
    CAPTURE(configExtent);
    std::vector<Placed> placed;
    auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      const size_t offset = 0x1000 + address - kXexBase;
      for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
    };
    const uint32_t code[] = {0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800420,
                            0x81830000, 0x818C0038, 0x7D8903A6, 0x4E800420,
                            0x38600025, 0x4E800020};
    for (unsigned i = 0; i < std::size(code); ++i) put(kXexText + 8 + 4 * i, code[i]);
    if (!configExtent) put(kXexPdata + 4, (1u << 30) | (12u << 8));
    Workdir work("x360n_discovery", "protected_thunks");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n";
    if (configExtent) config << "[functions]\n0x82010000 = { size = 48 }\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(kXexText + 24));
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(kXexText + 40));
  }
}

TEST_CASE("Manual switch targets retain ownership across a virtual thunk boundary",
          "[sdk][codegen][gapfill][aot-discovery][manual-switch]") {
  std::vector<Placed> placed;
  auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
  const uint32_t code[] = {0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800420,
                          0x38600025, 0x4E800020};
  for (unsigned i = 0; i < std::size(code); ++i) {
    const size_t offset = 0x1000 + kXexText + 8 + 4 * i - kXexBase;
    for (unsigned j = 0; j < 4; ++j) bytes.at(offset + j) = uint8_t(code[i] >> (24 - j * 8));
  }
  Workdir work("x360n_discovery", "manual_thunk_switch");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") <<
      "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n"
      "[[switch_tables]]\naddress=0x82010014\nregister=3\nlabels=[0x82010018]\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  const auto& graph = pipeline->context().graph;
  const auto* thunk = graph.getFunction(kXexText + 8);
  REQUIRE(thunk);
  REQUIRE(thunk->jumpTables().size() == 1);
  CHECK(thunk->jumpTables().front().targets == std::vector<uint32_t>{kXexText + 24});
  CHECK(thunk->containsAddress(kXexText + 24));
  CHECK_FALSE(graph.isEntryPoint(kXexText + 24));
  CHECK(thunk->isLabel(kXexText + 24));
  REQUIRE(pipeline->RunWrite().has_value());
}
