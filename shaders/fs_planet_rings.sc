$input v_pattern
#include <bgfx_shader.sh>
#include "hyperion_ring_density.sh"
uniform vec4 u_rings[4];
void main(){
 float r=length(v_pattern.xz),footprint=fwidth(r);
 float density=ringDensity(u_rings[0],r,footprint);
 if(density<.0001)discard;
 vec3 L=normalize(u_rings[2].xyz),V=normalize(u_rings[3].xyz-v_pattern);
 float sunlight=1.;float along=dot(v_pattern,L);
 float closest=dot(v_pattern,v_pattern)-along*along;
 float shadowEdge=max(.003,footprint*2.);
 if(along<0.)sunlight=smoothstep(1.-shadowEdge,1.+shadowEdge,closest);
 float u=(r-u_rings[0].x)/max(.001,u_rings[0].y-u_rings[0].x);
 float grazing=max(.08,abs(V.y)),alpha=(1.-exp(-density/grazing))*u_rings[1].w;
 float mu=dot(-V,L),forward=pow(max(0.,mu),9.),backscatter=pow(max(0.,-mu),4.);
 float shadowedSide=step(L.y*V.y,0.);
 float path=max(.10,abs(L.y));
 float transmission=exp(-density*(.24+.47*shadowedSide)/path);
 float illumination=.13+sunlight*(.96+.79*sqrt(path)+forward*1.35+backscatter*.63)*transmission;
 vec3 dust=mix(vec3(1.12,.87,.65),vec3(.76,.91,1.16),smoothstep(.41,.88,u));
 vec3 colour=u_rings[1].rgb*dust*illumination*(.82+density*1.05);
 gl_FragColor=vec4(colour,alpha);
}
