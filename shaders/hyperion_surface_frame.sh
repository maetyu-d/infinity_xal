#ifndef HYPERION_SURFACE_FRAME
#define HYPERION_SURFACE_FRAME
// Normalize before the dot/cross products. Camera-relative ship triangles
// can be many orders smaller than a render unit during strong acceleration.
vec3 surfaceUnit(vec3 v,vec3 fallback){
 float peak=max(abs(v.x),max(abs(v.y),abs(v.z)));
 return peak>1e-30?normalize(v/peak):fallback;
}
vec3 surfaceFrameNormal(vec3 position,vec3 fallback){
 vec3 dx=dFdx(position),dy=dFdy(position);
 float span=max(max(abs(dx.x),max(abs(dx.y),abs(dx.z))),
                max(abs(dy.x),max(abs(dy.y),abs(dy.z))));
 if(span<=1e-30)return fallback;
 return surfaceUnit(cross(dx/span,dy/span),fallback);
}
#endif
