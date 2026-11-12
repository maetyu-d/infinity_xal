$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
uniform vec4 u_glazing; // pixel size and texture Y orientation
SAMPLER2D(s_scene,0);
#include "hyperion_region_look.sh"
#include "hyperion_noise.sh"
#include "hyperion_cabin.sh"
vec2 canopyProject(vec3 p){
 vec4 clip=mul(u_viewProj,vec4(p,1.));
 return .5+.5*(clip.xy/max(.001,clip.w))*vec2(1.,u_glazing.z);
}
void main(){
 vec3 p=v_position,V=normalize(u_hyperion[2].xyz-p);
 vec3 N=normalize(cross(dFdx(p),dFdy(p)));
 if(dot(N,V)<0.)N=-N;
 float facing=clamp(dot(N,V),0.,1.);
 float fresnel=.023+.78*pow(1.-facing,4.);
 vec3 reflection=cabinFixtureReflection(p,reflect(-V,N),.065);
 float reflectedEnergy=max(reflection.r,max(reflection.g,reflection.b));
 float grime=strataNoise(v_pattern*vec3(23.,47.,23.)+4.,vec3(0.));
 float wipe=strataNoise(v_pattern*vec3(5.,77.,5.)+vec3(17.,3.,9.),vec3(0.));
 float specks=smoothstep(.54,.81,grime*.66+wipe*.34);
 float forwardLight=pow(max(0.,dot(-V,normalize(u_hyperion[3].xyz+vec3(1e-12)))),44.);
 vec3 environment=cabinWindowLight();
 float film=(.023+specks*.055+forwardLight*.064)*(.52+.48*(1.-facing));
 float reflectOpacity=fresnel*(.19+reflectedEnergy*.77);
 vec3 tint=mix(v_color0.rgb*.19,environment*.57,forwardLight*.67+specks*.17);
 vec3 spec=clamp(reflection*.87+v_color0.rgb*.23,vec3(0.),vec3(1.));

 // A parallel laminate shifts the transmitted ray sideways. Project the
 // shift through the actual pane and resolve the exterior before the HUD.
 vec2 uv=canopyProject(p);
 vec3 transmitted=refract(-V,N,1./1.49);
 vec3 shear=transmitted - V*dot(transmitted,V);
 vec2 offset=canopyProject(p+shear*(.022+specks*.012))-uv;
 offset=clamp(offset,-u_glazing.xy*7.,u_glazing.xy*7.);
 float border=smoothstep(.0,.04,min(min(uv.x,uv.y),min(1.-uv.x,1.-uv.y)));
 float resolved=smoothstep(.42,1.35,length(offset/u_glazing.xy));
 float transmittedWeight=(.06+.25*pow(1.-facing,1.4)+.05*specks)*border*resolved;
 if(transmittedWeight<.001){
  float opacity=film+reflectOpacity;
  gl_FragColor=vec4((film*tint+reflectOpacity*spec)/max(.001,opacity),min(.66,opacity));
  return;
 }
 vec2 sampleUV=uv+offset*border;
 vec3 glassImage=texture2D(s_scene,clamp(sampleUV,vec2(.001),vec2(.999))).rgb;
 float alpha=clamp(film+reflectOpacity+transmittedWeight,0.,.66);
 vec3 colour=(film*tint+reflectOpacity*spec+transmittedWeight*glassImage)/
             max(.001,film+reflectOpacity+transmittedWeight);
 gl_FragColor=vec4(colour,alpha);
}
