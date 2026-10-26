// Executes the shader address helper against an independent physical-tile oracle.
#include "scaled_resolve_address_math.h"
#include <iostream>
#include <rex/graphics/pipeline/render_target/scaled_resolve.h>
uint32_t Reference(const XeResolveInfo& i, unsigned x, unsigned y, unsigned s) {
  uint64_t hx=uint64_t(x)+i.edram_offset_scaled.x;
  uint64_t hy=uint64_t(std::max(y,i.half_pixel_offset_fill_source.y))+i.edram_offset_scaled.y;
  uint64_t sx=i.resolution_scale.x, sy=i.resolution_scale.y;
  uint64_t px=((hx/sx)*(i.edram_msaa_samples==2?2:1)+(s>>1))*sx+hx%sx;
  uint64_t py=((hy/sy)*(i.edram_msaa_samples?2:1)+(s&1))*sy+hy%sy;
  uint64_t tw=(80*sx)>>i.edram_format_ints_log2, th=16*sy;
  uint64_t tile=(i.edram_base_tiles+(py/th)*i.edram_pitch_tiles+px/tw)%2048;
  return uint32_t((tile*1280*sx*sy+((py%th)*tw+px%tw)*(1u<<i.edram_format_ints_log2))*4);
}
int main() {
  uint64_t checks=0;
  using namespace rex::graphics;
  reg::RB_COPY_DEST_INFO dest{};
  dest.copy_dest_format=xenos::ColorFormat::k_5_6_5;
  auto enabled=[&](bool scaled,bool depth,unsigned source,xenos::MsaaSamples samples) {
    return UseScaledRgb565Resolve(scaled,depth,source,samples,dest);
  };
  bool guards=enabled(true,false,0,xenos::MsaaSamples::k2X) &&
      enabled(true,false,7,xenos::MsaaSamples::k4X) &&
      !enabled(false,false,0,xenos::MsaaSamples::k2X) &&
      !enabled(true,true,0,xenos::MsaaSamples::k2X) &&
      !enabled(true,false,0,xenos::MsaaSamples::k1X) &&
      !enabled(true,false,1,xenos::MsaaSamples::k2X) &&
      !enabled(true,false,4,xenos::MsaaSamples::k2X) &&
      !enabled(true,false,5,xenos::MsaaSamples::k2X);
  dest.copy_dest_format=xenos::ColorFormat::k_8_8;
  guards=guards && !enabled(true,false,0,xenos::MsaaSamples::k2X);
  dest.copy_dest_format=xenos::ColorFormat::k_5_6_5;
  dest.copy_dest_number=xenos::SurfaceNumberFormat::kUnsignedInteger;
  guards=guards && !enabled(true,false,0,xenos::MsaaSamples::k2X);
  ++checks;if(!guards)return 1;
  for(unsigned scale=1;scale<=7;++scale)
    for(unsigned x=0;x<65536;++x) {
      auto q=XeResolveScaleQuotient4({x,x+1,x+2,x+3},scale);
      for(unsigned c=0;c<4;++c) {++checks;if(q.v[c]!=(x+c)/scale) return 1;}
      ++checks;if(XeResolveScaleQuotient(x,scale)!=x/scale) return 1;
    }
  const unsigned xs[]={0,1,2,3,7,8,19,20,21,39,40,41,79,80,81,1023};
  const unsigned ys[]={0,1,3,7,8,9,15,16,17,31,127,1023};
  for(unsigned sx=1;sx<=4;++sx) for(unsigned sy=1;sy<=4;++sy)
  for(unsigned wide=0;wide<2;++wide) for(unsigned msaa=0;msaa<3;++msaa)
  for(unsigned sample=0;sample<(1u<<msaa);++sample)
  for(unsigned pitch:{1u,2u,7u,1023u}) for(unsigned base:{0u,2047u})
  for(bool offset:{false,true}) {
    XeResolveInfo i{}; i.resolution_scale={sx,sy};i.edram_format_ints_log2=wide;
    i.edram_msaa_samples=msaa;i.edram_pitch_tiles=pitch;i.edram_base_tiles=base;
    i.edram_offset_scaled={offset?8*sx:0,offset?8*sy:0};
    i.half_pixel_offset_fill_source={sx/2,sy/2};
    for(auto x:xs) for(auto y:ys) {
      auto a=XeResolveColorCopySource4SampleAddressesBytes(i,{x*sx+sx-1,y*sy+sy-1},sample);
      for(unsigned c=0;c<4;++c) {
        ++checks;auto ref=Reference(i,x*sx+sx-1+c,y*sy+sy-1,sample);
        if(a.v[c]!=ref) {std::cerr<<"address mismatch "<<sx<<','<<sy<<' '<<wide<<' '<<msaa<<' '<<sample<<' '<<x<<','<<y<<' '<<a.v[c]<<' '<<ref<<'\n';return 1;}
      }
    }
  }
  std::cout<<checks<<" exact shader address and quotient checks passed\n";
}
