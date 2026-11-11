uniform vec4 u_spaceGlass; // amount, phase, seconds, reserved
uniform mat4 u_spaceGlassRotation;
#include "hyperion_space_glass.sh"
vec3 refractSpace(vec2 uv,vec3 original){
 if(u_spaceGlass.x<.001||texture2D(s_opaqueDepth,uv).r>0.)return original;
 vec2 ndc=uv*2.-1.;
 vec3 ray=normalize(vec3(ndc.x*u_reflection[0].x,ndc.y*u_reflection[0].y*u_reflection[0].z,-1.));
 vec3 d=normalize(mul(u_spaceGlassRotation,vec4(ray,0.)).xyz);
 vec2 fold=spaceGlassFold(d,u_spaceGlass.y,u_spaceGlass.z);
 vec2 slope=spaceGlassSlope(fold);
 float boundary=smoothstep(.0,.06,min(min(uv.x,uv.y),min(1.-uv.x,1.-uv.y)));
 float edge=exp(-abs(sin(fold.x*2.25+.13*sin(fold.y*3.7)))*4.);
 vec2 bend=slope*(.003+.013*edge)*u_spaceGlass.x*boundary;
 vec2 through=clamp(uv+bend,vec2(.001),vec2(.999));
 vec2 inside=clamp(uv-bend*.62,vec2(.001),vec2(.999));
 float clearA=step(texture2D(s_opaqueDepth,through).r,0.);
 float clearB=step(texture2D(s_opaqueDepth,inside).r,0.);
 vec3 refracted=texture2D(s_scene,through).rgb;
 vec2 fringe=clamp(through+bend*.25,vec2(.001),vec2(.999));
 if(texture2D(s_opaqueDepth,fringe).r<=0.)
  refracted.b=mix(refracted.b,texture2D(s_scene,fringe).b,edge*.60);
 vec3 c=mix(original,refracted,(.30+.41*edge)*clearA*boundary);
 c=mix(c,texture2D(s_scene,inside).rgb,(.07+.20*edge)*clearB*boundary);
 return c;
}
