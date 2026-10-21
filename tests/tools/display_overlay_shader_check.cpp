#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  std::ifstream input(argv[1]);
  std::string source{std::istreambuf_iterator<char>(input), {}};
  if (source.empty()) return 2;
  Microsoft::WRL::ComPtr<ID3DBlob> shader, error;
  const auto result = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
      "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
      0, &shader, &error);
  if (FAILED(result)) {
    if (error) std::cerr.write(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
    return 1;
  }
  if (argc == 3) {
    std::ofstream out(argv[2], std::ios::binary);
    out << "// Compiled from display_overlay.hlsl with D3DCompile cs_5_1 /O3.\n"
           "const unsigned char display_overlay_cs[] = {\n";
    auto* data = static_cast<const unsigned char*>(shader->GetBufferPointer());
    for (size_t i = 0; i < shader->GetBufferSize(); ++i) {
      if (!(i % 16)) out << "  ";
      out << "0x" << std::hex << std::setw(2) << std::setfill('0') << unsigned(data[i]) << ',';
      out << ((i % 16 == 15) ? '\n' : ' ');
    }
    out << "\n};\n";
    if (!out) return 1;
  }
  std::cout << "PASS: display overlay HLSL compiled, " << shader->GetBufferSize() << " bytes\n";
}
