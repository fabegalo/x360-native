// Maintenance only: the pinned SDK's existing DXBC checksum implementation.
#include <cstring>
#include <fstream>
#include <vector>
#include "DXBCChecksum.h"
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
  if (!in || in.tellg() < 32) return 3;
  std::vector<unsigned char> data(static_cast<size_t>(in.tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(data.data()), data.size());
  if (!in || std::memcmp(data.data(), "DXBC", 4)) return 4;
  in.close();
  unsigned hash[4];
  CalculateDXBCChecksum(data.data(), unsigned(data.size()), hash);
  std::memcpy(data.data() + 4, hash, sizeof(hash));
  std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), data.size());
  return out ? 0 : 5;
}
