// Calls the same import/project/registration model as the desktop worker.
// Diagnostic only, excluded from ALL; never launches or emulates guest code.
#include "catalog.h"
#include "paths.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace x360n;
using namespace x360n::desktop;
int main(int argc,char** argv) {
  if(argc!=5) return 2;
  try {
    const ProjectPaths paths{std::filesystem::absolute(PathFromUtf8(argv[2]))};
    const auto output=std::filesystem::absolute(PathFromUtf8(argv[4]));
    if(std::filesystem::exists(output)) throw std::runtime_error("Evidence output already exists");
    auto catalog=ReadCatalog(paths);
    const std::string mode=argv[1];
    std::string id;
    if(mode=="import" || mode=="generate") {
      const auto source=std::filesystem::absolute(PathFromUtf8(argv[3]));
      id=ImportGame(paths,catalog,source,source);
      catalog=ReadCatalog(paths);
    } else if(mode=="register") id=argv[3];
    else return 2;
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& target){return target.id==id;});
    if(found==catalog.end()) throw std::runtime_error("Imported target missing from official catalog");
    if(mode=="generate") GenerateAotProject(paths,*found);
    if(mode=="register" && !CompleteLibraryPreparation(paths,*found,catalog)) throw std::runtime_error("Original library registration failed");
    nlohmann::json result{{"id",id},{"mode",mode},{"generic_aot",found->generic_aot},
      {"manifest",PathUtf8(found->manifest)},{"exe",PathUtf8(found->exe)},{"files",found->inputs.size()}};
    std::filesystem::create_directories(output.parent_path());
    std::ofstream(output,std::ios::binary)<<result.dump(2)<<'\n';
    std::cout<<result.dump()<<'\n';
    return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
