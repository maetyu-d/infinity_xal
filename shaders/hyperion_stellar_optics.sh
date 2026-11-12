uniform vec4 u_stellar[3];
uniform mat4 u_starFromEye;
#include "hyperion_star_types.sh"
// Hyperion's source-depth test over the actual solar disc. Bearings, radius,
// temperature and eclipse area come from immutable scene descriptions.
float stellarVisibility(bool includeCloud){
 float open=0.,attenuation=0.;
 for(int i=0;i<5;i++){
  vec2 offset=vec2(0.);
  if(i==1)offset.x=1.;if(i==2)offset.x=-1.;if(i==3)offset.y=1.;if(i==4)offset.y=-1.;
  vec2 tap=u_stellar[0].xy+offset*vec2(1./u_stellar[2].x,1.)*u_stellar[0].z*.60;
  float visible=step(texture2D(s_opaqueDepth,tap).r,u_stellar[1].w*1.03+1e-10);
  float cloud=includeCloud && u_stellar[2].w>.5?texture2D(s_cloudVolume,tap).a:1.;
  open+=visible*.2;attenuation+=visible*cloud*.2;
 }
 return min(open,u_stellar[0].w)*attenuation/max(open,.0001);
}
// Replace background light behind the resolved disc. The opaque buffer
// already covers planets in front of it and any submitted photosphere mesh.
vec3 resolveStellarDisc(vec2 uv,vec3 background){
 if(u_stellar[0].w<=0.)return background;
 float radius=max(u_stellar[0].z,u_stellar[2].z*.35);
 vec2 q=(uv-u_stellar[0].xy)*vec2(u_stellar[2].x,1.);
 float aa=clamp(u_stellar[2].z/radius,.001,.8),squared=dot(q,q)/(radius*radius);
 if(squared>(1.+aa)*(1.+aa))return background;
 float depth=texture2D(s_opaqueDepth,uv).r;
 if(depth>0. && depth>u_stellar[1].w*.70)return background;
 float r=sqrt(squared),disc=1.-smoothstep(1.-aa,1.+aa,r),limb=.46+.54*sqrt(max(0.,1.-min(1.,squared)));
 vec3 photosphere=u_stellar[1].rgb*limb*1.2;
 if(u_starType[0].x>.5){
  vec2 xy=q/radius;xy.y*=u_starFromEye[3].w;
  vec3 n=mul(mat3(u_starFromEye),vec3(xy,sqrt(max(0.,1.-min(1.,squared)))));
  photosphere=typedPhotosphere(n,u_stellar[1].rgb,sqrt(max(0.,1.-min(1.,squared))),u_starType[1].w);
 }
 return mix(background,photosphere,disc*u_stellar[2].y);
}

vec3 stellarProminences(vec2 uv){
 float radius=u_stellar[0].z;
 float kind=u_starType[0].x;
 float resolved=smoothstep(9.,26.,radius/max(u_stellar[2].z,1e-8));
 if(u_stellar[0].w<=0. || resolved<=0. || kind<.5 || kind>6.5)return vec3(0.);
 vec2 offset=(uv-u_stellar[0].xy)*vec2(u_stellar[2].x,1.);
 float r=length(offset)/radius;
 if(r<.992 || r>1.53)return vec3(0.);
 if(texture2D(s_opaqueDepth,uv).r>u_stellar[1].w*1.03+1e-10)return vec3(0.);
 vec2 radial=normalize(offset);
 float altitude=r-1.;
 float phase=u_starType[1].x;
 float seconds=u_starType[1].w;
 float spin=mod(seconds*u_starType[1].y,1.)*6.283;
 float aa=max(u_stellar[2].z/radius,.002);
 vec3 local=mul(mat3(u_starFromEye),vec3(radial.x,radial.y*u_starFromEye[3].w,0.));
 float density=strataNoise(local*vec3(19.,10.,19.)+phase*13.,vec3(0.));
 float threads=strataNoise(local*62.+vec3(0.,0.,altitude*10.),vec3(0.));
 float plume=exp(-max(0.,altitude)/(.075+.24*density*density));
 vec3 corona=u_stellar[1].rgb*plume*(.011+.12*pow(density,2.5)+threads*.029);
 for(int i=0;i<4;i++){
  float index=float(i);
  float heading=phase*6.283+index*2.39996+spin;
  float latitude=.16+.38*sin(index*2.17+phase*7.);
  vec3 pole=normalize(vec3(cos(heading),latitude,sin(heading)));
  vec3 cameraPole=mul(transpose(mat3(u_starFromEye)),pole);
  vec2 projected=vec2(cameraPole.x,cameraPole.y*u_starFromEye[3].w);
  float limbVisibility=smoothstep(.62,.97,length(projected));
  vec2 axis=normalize(projected+vec2(1e-8,0.));
  float arc=atan2(radial.x*axis.y-radial.y*axis.x,dot(radial,axis));
  float width=.13+.12*fract(index*.73+phase);
  if(abs(arc)>width)continue;
  float rise=.10+.18*fract(index*.47+phase*1.7);
  float arch=pow(max(0.,1.-arc*arc/(width*width)),.74)*rise;
  arch*=.93+.07*sin(seconds*.42+index*2.3);
  float distanceToArch=abs(altitude-arch);
  float core=exp(-distanceToArch/max(.010,aa*.7));
  float hair=exp(-abs(altitude-arch*.77-.012)/max(.008,aa*.65));
  float halo=exp(-distanceToArch/.045)*.24;
  float detail=.38+.62*threads;
  corona+=u_stellar[1].rgb*(core*.87+hair*.31+halo)*limbVisibility*detail;
 }
 float limb=smoothstep(.992,1.02,r)*(1.-smoothstep(1.34,1.53,r));
 return corona*limb*resolved*u_stellar[2].y;
}
// Actual Hyperion postFragment streak and ghost, evaluated in the existing
// third-resolution bloom pass. Analytic box filtering keeps thin streaks
// stable between low-resolution pixels without a new texture or history.
vec3 stellarLens(vec2 uv){
 if(u_stellar[0].w<=0.)return vec3(0.);
 float visibility=stellarVisibility(true);if(visibility<=.0001)return vec3(0.);
 vec2 sun=uv-u_stellar[0].xy,q=sun*vec2(u_stellar[2].x,1.);
 float radius=max(u_stellar[0].z,u_stellar[2].z*.35),r=length(q)/radius;
 float aureole=exp(-r*r*.18)*.045+exp(-r*.8)*.015;
 float halfWidth=u_stellar[2].z*1.5,a=sun.y-halfWidth,b=sun.y+halfWidth;
 float integral=sign(b)*(1.-exp(-abs(b)*950.))-sign(a)*(1.-exp(-abs(a)*950.));
 float streak=integral/max(1900.*halfWidth,.0001)*exp(-abs(sun.x)*3.5);
 vec2 ghost=(uv-(.5-(u_stellar[0].xy-.5)*.38))*vec2(u_stellar[2].x,1.);
 float halo=exp(-abs(length(ghost)-.13)*190.);
 vec3 colour=u_stellar[1].rgb,flare=mix(colour,vec3(1.,.60,.22),.20)*.30;
 if(u_starType[0].x>.5)aureole*=u_starType[0].w;
 return (colour*aureole+flare*(streak*.14+halo*.03))*visibility*u_stellar[2].y;
}
