#ifndef HYPERION_CABIN
#define HYPERION_CABIN

vec3 cabinWindowLight(){
 return vec3(1.,.47,.21)*u_regionLook[1].y
       +vec3(.62,1.,.43)*u_regionLook[0].x
       +vec3(.35,.74,1.)*u_regionLook[1].x
       +vec3(1.,.48,.72)*u_regionLook[1].z;
}

float cabinBox(vec2 p,vec2 halfExtent,float soft){
 vec2 edge=halfExtent-abs(p);
 return smoothstep(0.,soft,min(edge.x,edge.y));
}

float cabinWindowTransmission(vec3 p,vec3 light){
 // Trace to the approximate canopy envelope. Roof, sill and aft wall
 // stop direct stellar light. Soften the frame edges across the glass.
 vec3 inv=sign(light+vec3(.00001))/max(abs(light),vec3(.00001));
 vec3 limits=mix(vec3(-1.025,-.82,-1.462),vec3(1.025,.56,1.14),step(vec3(0.),light));
 vec3 t=max(vec3(0.),(limits-p)*inv);
 vec3 front=p+light*t.z,side=p+light*t.x;
 float aperture=0.;
 if(t.z<=min(t.x,t.y) && light.z<0.)
  aperture=cabinBox(front.xy-vec2(0.,.09),vec2(.86,.425),.085);
 else if(t.x<min(t.y,t.z))
  aperture=cabinBox(side.zy-vec2(-.13,.12),vec2(1.17,.405),.085);
 return aperture;
}

// Reflect the fixed roof light strips and the console glow into nearby
// materials. The intersections remain in hull coordinates during freelook.
vec3 cabinFixtureReflection(vec3 p,vec3 ray,float roughness){
 vec3 c=vec3(0.);
 float blur=.012+roughness*.21;
 if(ray.y>.04){
  float t=(.584-p.y)/ray.y;
  if(t>0.){
   vec3 hit=p+t*ray;
   float strip=exp(-abs(abs(hit.x)-.59)/blur)*cabinBox(vec2(0.,hit.z+.14),vec2(1.,.89),.18);
   float central=cabinBox(vec2(hit.x,hit.z+.30),vec2(.16,.19),.075+blur);
   c+=vec3(1.,.78,.50)*(strip*.83+central*.27)/(1.+roughness*2.);
  }
 }
 if(ray.y<-.06){
  float t=(-.415-p.y)/ray.y;
  if(t>0.){
   vec3 hit=p+t*ray;
   float dashboard=cabinBox(hit.xz-vec2(0.,-1.17),vec2(.39,.13),.06+blur);
   float consoles=cabinBox(vec2(abs(hit.x),hit.z)-vec2(.85,-.51),vec2(.16,.28),.075+blur);
   c+=vec3(.15,.72,.64)*(dashboard*.34+consoles*.25);
  }
 }
 return c;
}

#endif
