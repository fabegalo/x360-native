"""Expose the actual scaled resolve address helper to a focused CPU test."""
from pathlib import Path
import sys
source=Path(sys.argv[1]).read_text()
prefix=r"""
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
using uint=uint32_t;
#define dont_flatten_xe
using std::max;
struct uint2_xe { uint x,y; };
struct uint4_xe {
 std::array<uint,4> v;
 uint4_xe(uint a=0):v{a,a,a,a}{}
 uint4_xe(uint a,uint b,uint c,uint d):v{a,b,c,d}{}
 uint4_xe operator<<(uint n) const {auto r=*this;for(auto& x:r.v)x<<=n;return r;}
 uint4_xe operator>>(uint n) const {auto r=*this;for(auto& x:r.v)x>>=n;return r;}
};
#define BIN(OP) inline uint4_xe operator OP(uint4_xe a,uint4_xe b) {for(uint c=0;c<4;++c)a.v[c]=a.v[c] OP b.v[c];return a;}
BIN(+) BIN(-) BIN(*) BIN(/) BIN(&)
#undef BIN
inline uint4_xe uint_x4_xe(uint n) {return uint4_xe(n);}
constexpr uint kXenosMsaaSamples_2X=1,kXenosMsaaSamples_4X=2;
struct XeResolveInfo {
 uint2_xe resolution_scale,edram_offset_scaled,half_pixel_offset_fill_source;
 uint edram_format_ints_log2,edram_msaa_samples,edram_base_tiles,edram_pitch_tiles;
};
"""
Path(sys.argv[2]).write_text(prefix+source,newline='\n')
