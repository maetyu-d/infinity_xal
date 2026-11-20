$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
uniform vec4 u_glazing; // pixel size, texture Y orientation, and glazing view
SAMPLER2D(s_scene,0);
SAMPLER2D(s_opaqueDepth,2);
#include "hyperion_region_look.sh"
#include "hyperion_noise.sh"
#include "hyperion_surface_frame.sh"
#include "hyperion_cabin.sh"
vec2 canopyProject(vec3 p){
 vec4 clip=mul(u_viewProj,vec4(p,1.));
 return .5+.5*(clip.xy/max(.001,clip.w))*vec2(1.,u_glazing.z);
}
void main(){
 vec3 p=v_position,V=normalize(u_hyperion[2].xyz-p);
 vec3 N=surfaceFrameNormal(p,V);
 bool curved=u_glazing.w>.5 && u_glazing.w<1.5 && stationBridgeInterior(p);
 if(curved && p.y>u_stationWindows[2].y)N=stationBridgeArchNormal(p);
 if(dot(N,V)<0.)N=-N;
 float facing=clamp(dot(N,V),0.,1.);
 float fresnel=.023+.54*pow(1.-facing,4.);
 vec3 ray=reflect(-V,N);
 vec3 exteriorLight=normalize(u_hyperion[3].xyz+vec3(1e-12));
 if(u_glazing.w>1.5){
  // Exterior panes are depth-tested in the world and transmit the scene beneath them.
  float sunGlint=pow(max(0.,dot(ray,exteriorLight)),68.);
  float skySheen=pow(.52+.48*abs(ray.y),2.);
  vec3 clearGlass=v_color0.rgb*vec3(.16,.28,.36);
  vec3 reflection=v_color0.rgb*(.25+fresnel*.90)*skySheen+vec3(.99,.92,.81)*sunGlint*1.8;
  gl_FragColor=vec4(clearGlass+reflection,clamp(.13+fresnel*.55+sunGlint*.22,0.,.59));
  return;
 }
 bool tunnel=u_glazing.w>.5;
 bool cargo=p.z>3.72;
 vec3 reflection;
 if(curved){
  float sunGlint=pow(max(0.,dot(ray,exteriorLight)),58.);
  reflection=stationBridgeRailReflection(p,ray,.14)+vec3(.97,.88,.70)*sunGlint*.6;
 }else if(tunnel){
  // Sample the lounge side of the pane so both faces resolve the same room's fixtures.
  vec4 window=u_stationWindows[1];
  vec3 inside=p-vec3(0.,0.,sign(window.z-window.w)*.003);
  int room=stationAnnexSlot(inside);
  float sunGlint=pow(max(0.,dot(ray,exteriorLight)),58.);
  reflection=stationFixtureReflection(inside,ray,.17,room)*.70+vec3(.97,.88,.70)*sunGlint*.6;
 }else{
  reflection=cargo?deckFixtureReflection(p,ray,.095):cabinFixtureReflection(p,ray,.14);
 }
 float reflectedEnergy=max(reflection.r,max(reflection.g,reflection.b));
 float grime=strataNoise(v_pattern*vec3(23.,47.,23.)+4.,vec3(0.));
 float wipe=strataNoise(v_pattern*vec3(5.,77.,5.)+vec3(17.,3.,9.),vec3(0.));
 float specks=smoothstep(.54,.81,grime*.66+wipe*.34);
 float forwardLight=pow(max(0.,dot(-V,exteriorLight)),44.);
 vec3 environment=cabinWindowLight();
 float film=(.023+specks*(cargo?.028:.055)+forwardLight*.064)*(.52+.48*(1.-facing));
 float reflectOpacity=fresnel*(.19+reflectedEnergy*.77);
 if(tunnel){
  film*=.64;
  reflectOpacity+=reflectedEnergy*.12;
 }
 if(curved){
  float crown=smoothstep(u_stationWindows[2].y,u_stationWindows[2].z,p.y);
  film+=.018*crown*(.48+.52*pow(1.-facing,2.));
 }
 vec3 tint=mix(v_color0.rgb*.19,environment*.57,forwardLight*.67+specks*.17);
 vec3 spec=clamp(reflection*.87+v_color0.rgb*.23,vec3(0.),vec3(1.));

 // A parallel laminate shifts the transmitted ray sideways. Project the
 // shift through the actual pane and resolve the exterior before the HUD.
 vec2 uv=canopyProject(p);
 vec3 transmitted=refract(-V,N,1./1.49);
 vec3 shear=transmitted - V*dot(transmitted,V);
 vec2 offset=canopyProject(p+shear*(tunnel?.012+specks*.004:.022+specks*.012))-uv;
 offset=clamp(offset,-u_glazing.xy*7.,u_glazing.xy*7.);
 float border=smoothstep(.0,.04,min(min(uv.x,uv.y),min(1.-uv.x,1.-uv.y)));
 float resolved=smoothstep(.42,1.35,length(offset/u_glazing.xy));
 float grazing=smoothstep(.035,.23,facing);
 float transmittedWeight=(.06+.22*pow(1.-facing,1.4)+.04*specks)*border*resolved*grazing;
 vec2 sampleUV=clamp(uv+offset*border,vec2(.001),vec2(.999));
 if(tunnel){
  // Lens shear can transmit a nearby surface or the sky, but should not
  // drag one across the other as a station beam moves past the window.
  float depth=texture2D(s_opaqueDepth,uv).r;
  float displaced=texture2D(s_opaqueDepth,sampleUV).r;
  float separation=abs(depth-displaced)/max(max(depth,displaced),1e-12);
  transmittedWeight*=1.-smoothstep(.08,.28,separation);
 }
 vec3 glassImage=texture2D(s_scene,sampleUV).rgb;
 float alpha=clamp(film+reflectOpacity+transmittedWeight,0.,.66);
 vec3 colour=(film*tint+reflectOpacity*spec+transmittedWeight*glassImage)/
             max(.001,film+reflectOpacity+transmittedWeight);
 gl_FragColor=vec4(colour,alpha);
}
