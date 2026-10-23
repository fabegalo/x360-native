#pragma once
#include <d3d12.h>
#include <filesystem>
#include <vector>
#include <map>
namespace x360n::native {
// Driver blobs are only hints. Exact input comparison precedes reuse; a driver
// rejection always falls back to compiling the same pipeline from its shaders.
class PipelineStore {
 public:
  void SetDirectory(const std::filesystem::path& path) { directory_ = path; }
  void SetRoot(const void* bytes, size_t size, ID3D12RootSignature* signature = nullptr);
  struct Entry { std::filesystem::path path; std::vector<char> key, blob; };
  Entry Load(const D3D12_GRAPHICS_PIPELINE_STATE_DESC&) const;
  void Save(const Entry&, ID3D12PipelineState*) const;
 private:
  std::filesystem::path directory_;
  std::map<ID3D12RootSignature*, std::vector<char>> roots_;
};
}
