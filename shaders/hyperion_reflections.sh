SAMPLER2D(s_reflectionSurface,3);
uniform vec4 u_reflection[2]; // frustum.xy, origin sign, near; far, enable, texel.xy
float reflectionDepth(vec2 uv){
 float d=texture2D(s_opaqueDepth,uv).r;
 return u_reflection[0].w*u_reflection[1].x/(u_reflection[0].w+d*(u_reflection[1].x-u_reflection[0].w));
}
vec2 reflectionProject(vec3 p){
 return vec2(p.x/u_reflection[0].x,p.y/(u_reflection[0].y*u_reflection[0].z))/max(-p.z,1e-8)*.5+.5;
}
vec3 structureContactLight(vec2 uv,vec3 colour,float built){
 if(built<.5)return colour;
 float z=reflectionDepth(uv);
 if(z>=u_reflection[1].x*.98)return colour;
 vec2 ndc=uv*2.-1.;
 vec3 p=vec3(ndc.x*u_reflection[0].x,ndc.y*u_reflection[0].y*u_reflection[0].z,-1.)*z;
 vec3 dx=dFdx(p),dy=dFdy(p);
 vec3 crossN=cross(dx,dy);
 float normalScale=max(max(abs(crossN.x),abs(crossN.y)),max(abs(crossN.z),1e-30));
 if(normalScale<=1.01e-30)return colour;
 vec3 N=normalize(crossN/normalScale);
 if(dot(N,-p)<0.)N=-N;
 // Bounded metre-scale contact shading. No history, random kernel, or
 // screen feedback: a camera cut cannot leave an old shadow behind.
 float metresPerUnit=exp2(clamp(u_regionLook[0].z,-20.,25.)*3.321928);
 float radius=min(4.5/max(metresPerUnit,1e-20),z*.12);
 vec2 spread=vec2(radius/(z*u_reflection[0].x),radius/(z*u_reflection[0].y))*.5;
 float resolved=smoothstep(1.2,3.,length(spread/u_reflection[1].zw));
 if(resolved<.001)return colour;
 float sum=0.,weight=0.;
 for(int tap=0;tap<8;++tap){
  float angle=float(tap)*2.399963;
  float reach=.30+float(tap)*.095;
  vec2 donor=uv+vec2(cos(angle),sin(angle))*spread*reach;
  if(min(donor.x,donor.y)<.002 || max(donor.x,donor.y)>.998)continue;
  float sampleZ=reflectionDepth(donor);
  if(sampleZ>=u_reflection[1].x*.98)continue;
  vec2 xy=donor*2.-1.;
  vec3 q=vec3(xy.x*u_reflection[0].x,xy.y*u_reflection[0].y*u_reflection[0].z,-1.)*sampleZ;
  vec3 delta=q-p;
  float distance=length(delta);
  float horizon=max(0.,dot(N,delta)/max(distance,1e-12)-.095);
  float range=1.-smoothstep(radius*.32,radius,distance);
  sum+=horizon*range;weight+=1.;
 }
 float occlusion=clamp(sum/max(weight,1.)*1.65,0.,.48)*resolved;
 float lamps=1.-smoothstep(.70,1.8,dot(colour,vec3(.299,.587,.114)));
 return colour*(1.-occlusion*lamps);
}
vec3 sceneReflection(vec2 uv,vec3 original,vec4 surface){
 // Negative roughness tags built surfaces. The magnitude is the actual
 // finish roughness, and its reflection works in every regional light.
 float strength=u_reflection[1].y*(surface.w<0.?mix(.55,1.,u_regionLook[1].y):u_regionLook[1].y);
 if(strength<.001)return original;
 float mirror=length(surface.xyz),rough=abs(surface.w);
 if(mirror<.10||rough>.62)return original;
 float z=reflectionDepth(uv);
 if(z>=u_reflection[1].x*.98)return original;
 vec2 ndc=uv*2.-1.;
 vec3 p=vec3(ndc.x*u_reflection[0].x,ndc.y*u_reflection[0].y*u_reflection[0].z,-1.)*z;
 vec3 N=normalize(surface.xyz),V=normalize(-p),R=reflect(-V,N);
 float fresnel=pow(1.-max(0.,dot(N,V)),5.);
 float stride=max(z*.018,u_reflection[0].w*8.);
 vec3 origin=p+N*max(z*.0025,u_reflection[0].w*3.);
 float previous=0.;
 for(int step=0;step<28;step++){
  float t=stride*(float(step)+1.)*(1.+float(step)*.065);
  vec3 q=origin+R*t;
  if(q.z>=-u_reflection[0].w*2.)break;
  vec2 hit=reflectionProject(q);
  if(min(hit.x,hit.y)<.008||max(hit.x,hit.y)>.992)break;
  float target=reflectionDepth(hit),delta=-q.z-target;
  if(step>1&&target<u_reflection[1].x*.98&&delta>0.&&delta<max(z*.075,stride*2.)){
   float low=previous,high=t;
   for(int refine=0;refine<5;refine++){
    float mid=(low+high)*.5;vec3 test=origin+R*mid;
    float gap=-test.z-reflectionDepth(reflectionProject(test));
    if(gap>0.)high=mid;else low=mid;
   }
   vec3 hitPoint=origin+R*high;hit=reflectionProject(hitPoint);
   float edge=smoothstep(.008,.09,min(min(hit.x,hit.y),min(1.-hit.x,1.-hit.y)));
   float separation=length((hit-uv)/u_reflection[1].zw);
   float confidence=edge*smoothstep(2.,12.,separation)*(1.-smoothstep(.25,.65,rough));
   vec2 blur=u_reflection[1].zw*rough*5.;
   vec3 reflected=texture2D(s_scene,hit).rgb*.5;
   reflected+=(texture2D(s_scene,hit+blur).rgb+texture2D(s_scene,hit-blur).rgb)*.25;
   float amount=mirror*confidence*clamp(.48+fresnel*.40,0.,.88);
   return mix(original,reflected,amount*strength);
  }
  previous=t;
 }
 return original;
}
