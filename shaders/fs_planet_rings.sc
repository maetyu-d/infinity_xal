$input v_pattern
#include <bgfx_shader.sh>
#include "hyperion_region_look.sh"
#include "hyperion_ring_density.sh"
uniform vec4 u_rings[4];
float ringGrainHash(vec2 p){
 vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);
}
float ringGrain(vec2 p){
 vec2 cell=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(ringGrainHash(cell),ringGrainHash(cell+vec2(1.,0.)),f.x),
            mix(ringGrainHash(cell+vec2(0.,1.)),ringGrainHash(cell+vec2(1.,1.)),f.x),f.y);
}
void main(){
 float r=length(v_pattern.xz),footprint=fwidth(r);
 float density=ringDensity(u_rings[0],r,footprint);
 if(density<.0001)discard;
 vec3 toEye=u_rings[3].xyz-v_pattern;
 float distance=length(toEye);
 vec3 L=normalize(u_rings[2].xyz),V=toEye/max(distance,.00001);
 float toLimb=clamp(-dot(v_pattern,V),0.,distance);
 vec3 limb=v_pattern+V*toLimb;
 float limbRadius=length(limb);
 // Keep the back arc behind the globe, including during a mesh LOD blend
 // when neither of the cross-fading meshes contributes opaque depth.
 float silhouette=smoothstep(.998,1.002,limbRadius);
 float along=dot(v_pattern,L);
 float closest=dot(v_pattern,v_pattern)-along*along;
 float shadowEdge=clamp(footprint*2.,.003,.3);
 float sunlight=mix(1.,smoothstep(1.-shadowEdge,1.+shadowEdge,closest),step(along,0.));
 float u=(r-u_rings[0].x)/max(.001,u_rings[0].y-u_rings[0].x);
 float grazing=max(.08,abs(V.y)),alpha=(1.-exp(-density/grazing))*u_rings[1].w*silhouette;
 float mu=dot(-V,L),forward=pow(max(0.,mu),9.),backscatter=pow(max(0.,-mu),4.);
 float shadowedSide=step(L.y*V.y,0.);
 float path=max(.10,abs(L.y));
 float transmission=exp(-density*(.24+.47*shadowedSide)/path);
 float illumination=.095+sunlight*(.92+.88*sqrt(path)+forward*1.62+backscatter*.64)*transmission;
 vec3 dust=mix(vec3(1.19,.91,.64),vec3(.73,.93,1.22),smoothstep(.34,.86,u));
 float strata=.80+.28*smoothstep(.12,.57,u)-.14*smoothstep(.71,.95,u);
 vec3 colour=u_rings[1].rgb*dust*illumination*(strata+density*1.09);
 // Local clumps modulate the fine radial bands. Their footprint is filtered
 // before orbit-scale dust turns into isolated shimmering points.
 vec2 grainPosition=v_pattern.xz*146.+u_rings[0].w*31.;
 float resolved=1.-smoothstep(.38,1.40,max(length(dFdx(grainPosition)),length(dFdy(grainPosition))));
 float particles=ringGrain(grainPosition);
 colour*=1.+(particles-.5)*.42*resolved;
 colour+=u_rings[1].rgb*sunlight*backscatter*pow(particles,5.)*resolved*.11;
 // The fog between the observer and the ring uses the ring's distance.
 // The remaining transmission still reveals the fogged sky behind it.
 float logDistance=u_rings[2].w+log2(max(.00001,distance))*.30103;
 float haze=(smoothstep(1.2,5.,logDistance)*.38+smoothstep(6.8,14.4,logDistance)*.31)*.87;
 colour=mix(colour,vec3(.044,.102,.034),haze*u_regionLook[0].x*u_regionLook[0].w*u_rings[3].w);
 gl_FragColor=vec4(colour,alpha);
}
