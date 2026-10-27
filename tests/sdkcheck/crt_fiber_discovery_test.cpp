#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include "crt_fiber_fixture.h"
#include <fstream>

using namespace x360n::test;
TEST_CASE("Complete CRT fiber lifecycle selects host continuations without title hints",
          "[sdk][codegen][crt-fiber-discovery]") {
  for(unsigned variant=0;variant<9;++variant) {
    CAPTURE(variant);
    auto bytes=BuildXex({},nullptr,0,Entry::kReturnZero);
    const auto put=[&](uint32_t at,uint32_t word) {
      for(unsigned i=0;i<4;++i) bytes.at(0x1000+at-kXexBase+i)=uint8_t(word>>(24-i*8));
    };
    constexpr size_t text=0x1000+0x98+224+40;
    for(size_t field:{text+8,text+16})
      for(unsigned i=0;i<4;++i) bytes.at(field+i)=uint8_t(0x1000>>(i*8));
    const uint32_t family=kXexText+(variant==1?0x180:0x100), context=kXexText+0x600;
    const uint32_t bootstrap=kXexText+0xB00, dummy=kXexText+0xC00;
    const uint32_t offsets[]{0,0x90,0xE0,0x1C0};
    auto routines=crt_fiber_fixture::Lifecycle();
    for(unsigned routine=0;routine<4;++routine) {
      auto words=routines[routine];
      for(unsigned i=0;i<words.size();++i) {
        const uint32_t pc=family+offsets[routine]+i*4;
        auto word=words[i];
        if(word==0x48000001 || word==0x48000000)
          word|=(dummy-pc)&0x03FFFFFC;
        if(word==0x3D600000) word|=(bootstrap+0x8000)>>16;
        if(word==0x396B0000) word|=bootstrap&0xFFFF;
        if(routine==3 && i==9) word=0x48000001|((family+0x90-pc)&0x03FFFFFC);
        if(variant==2 && routine==2 && i==49) word^=8; // Wrong saved SP slot.
        if(variant==3 && routine==1 && i==9) word^=4;  // Wrong KTHREAD member.
        if(variant==5 && routine==3 && i==9) word+=4;  // Unlinked lifecycle.
        put(pc,word);
      }
    }
    put(family+0x220,0x48000000|((context-family-0x220)&0x03FFFFFC));
    auto switcher=crt_fiber_fixture::SwitchContext();
    REQUIRE(switcher.size()==259);
    for(unsigned i=0;i<switcher.size();++i) {
      auto word=switcher[i];
      if(word==0x48000000) word|=(dummy-context-i*4)&0x03FFFFFC;
      if(variant==4 && i==12) word^=8; // One nonvolatile GPR slot differs.
      if(variant==6 && i==70) word^=4; // One VMX register differs.
      put(context+i*4,word);
    }
    if(variant==7) { // Declared section ends inside the complete switch body.
      for(size_t field:{text+8,text+16})
        for(unsigned i=0;i<4;++i) bytes.at(field+i)=uint8_t(0x900>>(i*8));
    }
    put(bootstrap,0x7FE3FB78);put(bootstrap+4,0x48000001|((dummy-bootstrap-4)&0x03FFFFFC));
    put(bootstrap+8,0x3860000E);put(bootstrap+12,0x48000001|((dummy-bootstrap-12)&0x03FFFFFC));
    put(dummy,0x4E800020);
    Workdir work("x360n_crt_fiber","discovery");work.write("test.xex",bytes);
    {
      std::ofstream config(work.dir/"test.toml");
      config << "project_name='fibers'\nfile_path='test.xex'\nout_directory_path='out'\n";
      if(variant==8) {
        config << "[rexcrt]\n";
        const char* names[]{"ConvertThreadToFiber","ConvertFiberToThread","CreateFiber","DeleteFiber","SwitchToFiber"};
        const uint32_t entries[]{family,family+0x90,family+0xE0,family+0x1C0,family+0x220};
        for(unsigned i=0;i<5;++i) config << names[i] << "=" << entries[i] << '\n';
      }
    }
    auto pipeline=rex::codegen::CodegenPipeline::Create(work.dir/"test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK(pipeline->context().Config().rexcrtFunctions.size()==(variant==8?5:0));
    CHECK(pipeline->context().analysisState().crtFiberHooks.size()==(variant<2?6:0));
    REQUIRE(pipeline->RunWrite().has_value());
    std::string output;
    for(const auto& file:std::filesystem::directory_iterator(work.dir/"out")) {
      if(file.path().extension()!=".cpp" && file.path().extension()!=".h") continue;
      std::ifstream in(file.path());output.append(std::istreambuf_iterator<char>(in),{});
    }
    for(const char* name:{"ConvertThreadToFiber","ConvertFiberToThread","CreateFiber","DeleteFiber","SwitchToFiber"})
      CHECK((output.find(std::string("rexcrt_")+name)!=std::string::npos)==(variant<2 || variant==8));
  }
}
