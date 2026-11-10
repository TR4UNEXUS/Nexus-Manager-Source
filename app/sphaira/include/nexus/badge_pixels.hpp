// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace nexus_badge {
inline float Segment(float x,float y,float a,float b,float c,float d){
    const float dx=c-a,dy=d-b,t=std::clamp(((x-a)*dx+(y-b)*dy)/(dx*dx+dy*dy),0.f,1.f);
    return std::hypot(x-a-t*dx,y-b-t*dy);
}
inline float Tile(float x,float y){
    const float a=std::abs(x-218)-20,b=std::abs(y-218)-20;
    return std::hypot(std::max(a,0.f),std::max(b,0.f))+std::min(std::max(a,b),0.f)-10;
}
// Small high-contrast USB trident, antialiased at either HOME icon resolution.
inline bool Overlay(std::uint8_t* rgba,std::size_t bytes,unsigned size){
    if(!rgba || (size!=256 && size!=174) || bytes!=size*size*4)return false;
    for(unsigned y=size*178/256;y<size;++y)for(unsigned x=size*178/256;x<size;++x){
        auto* p=rgba+4*(y*size+x);float sum[3]{};
        for(float sy:{.25f,.75f})for(float sx:{.25f,.75f}){
            const float u=(x+sx)*256/size,v=(y+sy)*256/size;
            float c[3]{float(p[0]),float(p[1]),float(p[2])};
            const float shadow=std::clamp((4-Tile(u-1,v-2))*.1f,0.f,.45f);
            for(auto& n:c)n*=1-shadow;
            const float edge=Tile(u,v);
            if(edge<=0){
                c[0]=10*.94f+c[0]*.06f;c[1]=23*.94f+c[1]*.06f;c[2]=34*.94f+c[2]*.06f;
                if(edge>-1.3f){c[0]=33;c[1]=220;c[2]=211;}
                const bool symbol=Segment(u,v,218,202,218,235)<1.8f ||
                    Segment(u,v,218,225,206,216)<1.6f || Segment(u,v,206,216,206,207)<1.6f ||
                    Segment(u,v,218,218,230,209)<1.6f || Segment(u,v,230,209,230,203)<1.6f ||
                    std::hypot(u-206,v-205)<3.3f || (std::abs(u-230)<3.3f && std::abs(v-201)<3.3f) ||
                    (v>=195 && v<=204 && std::abs(u-218)<=(v-195)*.6f) || std::hypot(u-218,v-235)<4;
                if(symbol){c[0]=244;c[1]=253;c[2]=255;}
            }
            for(unsigned k=0;k<3;++k)sum[k]+=c[k];
        }
        for(unsigned k=0;k<3;++k)p[k]=static_cast<std::uint8_t>(sum[k]/4+.5f);
    }
    return true;
}
}
