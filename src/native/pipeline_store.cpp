#include "pipeline_store.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <xxhash.h>
#include <fstream>
#include <cstdio>
#include <cstring>
namespace x360n::native {
namespace {
constexpr uint64_t kMagic = 0x31534F504E303633; // 360NPSO1; native Windows ABI.
constexpr uint64_t kLimit = 64ull << 20;
struct Header { uint64_t magic, key_bytes, blob_bytes; };
void Append(std::vector<char>& out, const void* bytes, size_t size) {
  if (size) out.insert(out.end(), static_cast<const char*>(bytes), static_cast<const char*>(bytes)+size);
}
}
void PipelineStore::SetRoot(const void* bytes, size_t size) { root_.clear(); Append(root_, bytes, size); }
PipelineStore::Entry PipelineStore::Load(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd) const {
  Entry e;
  // Native currently has no input layout or stream output. Do not cache pointer
  // structures until their contents participate in the serialized identity.
  if (directory_.empty() || root_.empty() || pd.InputLayout.NumElements ||
      pd.StreamOutput.NumEntries || pd.StreamOutput.NumStrides) return e;
  auto flat = pd;
  flat.pRootSignature = nullptr;
  flat.VS.pShaderBytecode = flat.PS.pShaderBytecode = flat.GS.pShaderBytecode = nullptr;
  flat.HS.pShaderBytecode = flat.DS.pShaderBytecode = nullptr;
  flat.InputLayout.pInputElementDescs = nullptr;
  flat.StreamOutput.pSODeclaration = nullptr; flat.StreamOutput.pBufferStrides = nullptr;
  flat.CachedPSO = {};
  Append(e.key, &flat, sizeof(flat)); Append(e.key, root_.data(), root_.size());
  for (auto stage : {pd.VS, pd.PS, pd.GS, pd.HS, pd.DS}) Append(e.key, stage.pShaderBytecode, stage.BytecodeLength);
  auto hash = XXH3_128bits(e.key.data(), e.key.size());
  char name[40]; std::snprintf(name, sizeof(name), "%016llx%016llx.pso", hash.high64, hash.low64);
  e.path = directory_ / name;
  std::ifstream in(e.path, std::ios::binary);
  Header h{};
  if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)) || h.magic != kMagic ||
      h.key_bytes != e.key.size() || !h.blob_bytes || h.blob_bytes > kLimit) return e;
  std::vector<char> key(h.key_bytes);
  if (!in.read(key.data(), key.size()) || key != e.key) return e;
  e.blob.resize(h.blob_bytes);
  if (!in.read(e.blob.data(), e.blob.size()) || in.peek() != std::char_traits<char>::eof()) e.blob.clear();
  return e;
}
void PipelineStore::Save(const Entry& e, ID3D12PipelineState* pso) const {
  if (e.path.empty() || e.key.size() > kLimit) return;
  Microsoft::WRL::ComPtr<ID3DBlob> blob;
  if (FAILED(pso->GetCachedBlob(&blob)) || !blob->GetBufferSize() || blob->GetBufferSize() > kLimit) return;
  std::error_code ec; std::filesystem::create_directories(directory_, ec); if (ec) return;
  auto temporary = e.path; temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  Header h{kMagic, e.key.size(), blob->GetBufferSize()};
  std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&h), sizeof(h)); out.write(e.key.data(), e.key.size());
  out.write(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
  out.close();
  if (out) MoveFileExW(temporary.c_str(), e.path.c_str(), MOVEFILE_REPLACE_EXISTING);
  std::filesystem::remove(temporary, ec);
}
}
