$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
#include "hyperion_region_look.sh"
#include "hyperion_noise.sh"
#include "hyperion_cabin.sh"
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
 float alpha=clamp(film+reflectOpacity,0.,.56);
 gl_FragColor=vec4(mix(tint,spec,reflectOpacity/max(.001,film+reflectOpacity)),alpha);
}
