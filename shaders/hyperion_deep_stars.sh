#ifndef HYPERION_DEEP_STARS
#define HYPERION_DEEP_STARS
vec3 deepStarHash(vec2 cell,float seed){
 vec3 p=fract(vec3(cell.x,cell.y,cell.x)*vec3(.1031,.1030,.0973)+seed);
 p+=dot(p,p.yxz+33.33);
 return fract((p.xxy+p.yzz)*p.zyx);
}
vec3 deepStarPlane(vec3 ray,float phase,float cells,float seed){
 vec2 plane=ray.xy/max(abs(ray.z),.15);
 float field=1.-smoothstep(.76,1.12,max(abs(plane.x),abs(plane.y)));
 if(field<.001)return vec3(0.);
 vec2 p=plane*cells,base=floor(p);
 float footprint=max(length(dFdx(p)),length(dFdy(p)));
 float variance=max(.008,footprint*footprint*.38);
 vec3 sum=vec3(0.);
 float face=seed+step(0.,ray.z)*19.+phase*113.;
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
  vec2 cell=base+vec2(float(x),float(y));
  vec3 key=deepStarHash(cell,face);
  if(key.z<.88)continue;
  vec2 centre=cell+.12+key.xy*.76;
  vec2 offset=p-centre;
  float pointSpread=exp(-dot(offset,offset)/max(.0001,2.*variance));
  // Integrated subpixel flux, with a fixed stellar magnitude. Camera
  // rotation changes pixel coverage without randomly blinking the stars.
  float energy=(.00002+pow(key.z,42.)*.016)/variance;
  vec3 colour=mix(vec3(1.,.62,.32),vec3(.56,.76,1.),key.x);
  colour=mix(colour,vec3(1.,.98,.91),key.y*.72);
  sum+=colour*pointSpread*energy;
 }
 return sum*field;
}
vec3 deepStars(vec3 direction,float phase){
 // Overlapping celestial projections have no cube-face seam and no
 // preferred up. Their fade depends on a fixed sky direction, never time.
 vec3 c=vec3(0.);
 c+=deepStarPlane(direction,phase,39.,.17);
 c+=deepStarPlane(direction.yzx,phase,39.,3.81);
 c+=deepStarPlane(direction.zxy,phase,39.,7.23);
 c+=deepStarPlane(direction,phase,109.,11.43)*.18;
 c+=deepStarPlane(direction.yzx,phase,109.,13.17)*.18;
 c+=deepStarPlane(direction.zxy,phase,109.,17.63)*.18;
 return c;
}
#endif
