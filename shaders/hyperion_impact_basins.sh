#ifndef HYPERION_IMPACT_BASINS
#define HYPERION_IMPACT_BASINS
float basinHash(float seed){
 vec3 p=fract(vec3(seed)*vec3(.1031,.1030,.0973));
 p+=dot(p,p.yzx+33.33);
 return fract((p.x+p.y)*p.z);
}
vec4 impactBasins(vec3 n,vec3 light,float phase,out float coverage){
 float height=0.,rims=0.,ejecta=0.,visibility=1.;
 coverage=0.;
 float footprint=max(length(dFdx(n)),length(dFdy(n)));
 for(int index=0;index<18;++index){
  float seed=float(index)*7.31+phase*113.;
  vec3 centre=normalize(vec3(basinHash(seed+1.),basinHash(seed+9.),basinHash(seed+23.))-.5);
  float size=index<4?.13+basinHash(seed+37.)*.12:.025+basinHash(seed+37.)*.085;
  float resolved=1.-smoothstep(.16,.60,footprint/size);
  vec3 offset=(n-centre)/size;
  float r=length(offset);
  if(r>2.1 || resolved<.001)continue;
  float aa=max(.014,footprint/size);
  float floorMask=1.-smoothstep(.12,.98,r*r);
  float rim=exp(-pow((r-1.035)/(.095+aa),2.));
  float terrace=exp(-pow((r-.77)/(.065+aa),2.))*.14;
  float central=exp(-r*r/((index<4?.13:.065)+aa))*.24;
  height+=size*(-floorMask*.045+(rim+terrace+central)*.012)*resolved;
  coverage=max(coverage,(1.-smoothstep(.70,1.36,r))*resolved);
  rims+=rim*resolved;
  vec3 rayDirection=normalize(n-centre*dot(n,centre)+vec3(1e-8));
  float rayPhase=dot(rayDirection,vec3(17.3,29.1,11.7))+seed;
  float rays=mix(.226,pow(.5+.5*sin(rayPhase),6.),1.-smoothstep(.6,2.,fwidth(rayPhase)));
  float skirt=exp(-max(0.,r-1.)*3.5)*smoothstep(.9,1.12,r);
  ejecta+=skirt*(.36+rays*.64)*resolved;
  vec3 tangent=light-centre*dot(light,centre);
  float horizontal=max(length(tangent),.001);
  vec3 towardSun=tangent/horizontal;
  float along=dot(offset,towardSun);
  float across2=max(0.,dot(offset,offset)-along*along);
  float toRim=max(.035,sqrt(max(.0,1.-across2))-along);
  float requiredSlope=(.012+floorMask*.045)/toRim;
  float lit=smoothstep(requiredSlope-.035,requiredSlope+.035,dot(n,light)/horizontal);
  visibility=min(visibility,mix(1.,.26+.74*lit,floorMask*resolved));
 }
 return vec4(height,min(1.,rims),min(1.,ejecta),visibility);
}
#endif
