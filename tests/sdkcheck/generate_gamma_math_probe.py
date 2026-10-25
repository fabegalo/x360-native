"""Expose the actual scalar shader functions to the CPU regression executable."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
functions = []
for signature in ("uint XePWLGammaToLinearCode(", "float XeResolveAveragePWLGamma("):
    first = source.index(signature)
    brace = source.index("{", first)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[first:end])
pixel = (Path(sys.argv[1]).parent / "pixel_formats.xesli").read_text()
first = pixel.index("float4_xe XeUnpackR16EdramX4(")
end = pixel.index("}", first) + 1
functions.append(pixel[first:end])
Path(sys.argv[2]).write_text("// Extracted from the production shader; do not edit.\n"
                            "using uint = unsigned;\nusing std::min;\n" + r'''
using uint4_xe = std::array<uint32_t, 4>;
struct int4_xe {
  std::array<int32_t, 4> v;
  explicit int4_xe(uint4_xe u) { for (unsigned i=0;i<4;++i) v[i]=int32_t(u[i]); }
  int4_xe operator<<(unsigned n) const {
    auto out=*this; for(auto& x:out.v) x=int32_t(uint32_t(x)<<n); return out;
  }
  int4_xe operator>>(unsigned n) const {
    auto out=*this; for(auto& x:out.v) x>>=n; return out;
  }
};
struct float4_xe {
  std::array<float, 4> v;
  explicit float4_xe(int4_xe x) { for(unsigned i=0;i<4;++i) v[i]=float(x.v[i]); }
  float4_xe operator*(float scale) const {
    auto out=*this; for(auto& x:out.v) x*=scale; return out;
  }
};
''' +
                            "\n".join(functions) + "\n", newline="\n")
