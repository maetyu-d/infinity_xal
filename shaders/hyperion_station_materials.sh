// Station finishes use metre-scale coordinates so the surface detail stays
// fixed to the geometry during walking, freelook and docking.
struct StationSurface {
 vec3 albedo;
 float height;
 float roughness;
 float metallic;
};

vec2 stationSurfacePlane(vec3 p,vec3 normal){
 vec3 n=abs(normal);
 if(n.y>=max(n.x,n.z))return p.xz;
 if(n.x>n.z)return p.zy;
 return p.xy;
}

float stationDetailVisibility(vec2 cellsPerPixel){
 return 1.-smoothstep(.23,.85,max(cellsPerPixel.x,cellsPerPixel.y));
}

float stationSurfaceHash(vec2 cell){
 vec3 p=fract(vec3(cell.xyx)*.1031);
 p+=dot(p,p.yzx+33.33);
 return fract((p.x+p.y)*p.z);
}

StationSurface stationSurface(vec3 base,float finish,vec3 p,vec3 normal){
 vec2 uv=stationSurfacePlane(p,normal);
 vec2 width=fwidth(uv);
 float coating=strataNoise(p*vec3(5.4,7.8,5.4),vec3(17.,11.,29.))-.5;
 float fine=strataNoise(p*195.,vec3(12.,3.,27.))-.5;
 StationSurface material;
 material.albedo=base*(1.+coating*.18+fine*.07);
 material.height=fine*.00013;
 material.roughness=.51;
 material.metallic=0.;

 // These indices match CabinMaterial. Displays use the common cabin shader.
 if(finish==8.){
  // Staggered perforations with rolled rims and dark acoustic backing.
  vec2 tile=uv/vec2(.036,.031);
  tile.x+=mod(floor(tile.y),2.)*.5;
  float radius=length(fract(tile)-.5);
  float visible=stationDetailVisibility(width/vec2(.036,.031));
  float blur=clamp(length(width)*19.,.022,.17);
  float hole=(1.-smoothstep(.155-blur,.155+blur,radius))*visible;
  float rim=(1.-smoothstep(.20,.27,radius))*visible-hole*.87;
  material.albedo*=1.-hole*.78+rim*.15;
  material.height+=.00054*rim-.0016*hole;
  material.roughness=.73+hole*.16;
 }else if(finish==9.){
  // Opposing rows of yarn form a herringbone runner. Fade the fibres before
  // they reach pixel size while the larger diagonal ribs remain readable.
  vec2 grid=uv/vec2(.067,.034);
  float bias=mod(floor(grid.x),2.)*2.-1.;
  float rib=sin((grid.y+bias*(fract(grid.x)-.5)*1.55)*6.2831853);
  float outline=abs(fract(grid.x+.5)-.5);
  float joint=(1.-smoothstep(.014,.084,outline))*stationDetailVisibility(width/.067);
  float nap=strataNoise(vec3(uv*vec2(139.,218.),.17),vec3(13.,2.,6.))-.5;
  rib*=stationDetailVisibility(width/vec2(.067,.034));
  material.albedo*=1.+rib*.23-joint*.16+nap*.31+coating*.19;
  material.height+=rib*.00076+nap*.0007-joint*.00061;
  material.roughness=.88+nap*.11;
 }else if(finish==5.){
  // Diagonal twill under the finer raised warp and weft threads.
  float twill=sin((uv.x-uv.y*.49)*358.);
  twill*=stationDetailVisibility(width*57.);
  vec2 threads=sin(uv*vec2(1227.,1068.));
  float yarn=dot(threads,vec2(.61,.39))*stationDetailVisibility(width*vec2(195.,170.));
  material.albedo*=1.+twill*.14+yarn*.15+coating*.24;
  material.height+=twill*.00040+yarn*.00040;
  material.roughness=.76+yarn*.16;
 }else if(finish==10.){
  // Cast mineral composite with varied stone fragments and fine aggregate.
  vec2 grid=uv*49.,cell=floor(grid),fraction=fract(grid)-.5;
  float type=stationSurfaceHash(cell);
  vec2 shift=vec2(stationSurfaceHash(cell+vec2(19.,73.)),stationSurfaceHash(cell+vec2(53.,4.)))-.5;
  vec2 chip=(fraction-shift*.31)*vec2(1.+type*.51,1.19-type*.37);
  float outline=abs(chip.x)*.84+abs(chip.y)*.71+length(chip)*.48;
  float size=.082+type*.192;
  float blur=clamp(length(width)*24.,.014,.19);
  float crystal=(1.-smoothstep(size-blur,size+blur,outline))*stationDetailVisibility(width*49.);
  vec3 delta=mix(base*-.58,vec3(.29,.22,.15)+base*.18,smoothstep(.52,.81,type));
  material.albedo+=delta*crystal;
  material.height+=crystal*(type-.4)*.00068;
  material.roughness=.43+crystal*(.21-type*.24)+coating*.14;
 }else if(finish==6.){
  // Raised, alternating lozenges and the worn crests of pressed tread.
  vec2 grid=uv/vec2(.086,.073);
  vec2 cell=floor(grid),chip=fract(grid)-.5;
  chip.x*=mod(cell.x+cell.y,2.)*2.-1.;
  vec2 axis=vec2(.707107,.707107);
  vec2 stud=vec2(dot(chip,axis),dot(chip,vec2(-axis.y,axis.x)));
  float grip=cabinBox(stud,vec2(.355,.107),.065);
  grip*=stationDetailVisibility(width/vec2(.086,.073));
  float scuff=smoothstep(-.23,.32,coating+fine*.33);
  float seam=cabinSeamGrid(uv*vec2(.58,.82));
  material.albedo*=1.+grip*(.37+scuff*.43)-seam*.33+scuff*.13;
  material.height+=grip*.0027-seam*.0012;
  material.roughness=.77-grip*(.16+scuff*.20);
  material.metallic=.32+grip*.28;
 }else if(finish==2. || finish==3.){
  float grain=strataNoise(vec3(uv*vec2(5.7,258.),.29),vec3(3.,32.,2.))-.5;
  float scratch=smoothstep(.57,.75,strataNoise(vec3(uv*vec2(15.5,475.),.04),vec3(6.,17.,7.)));
  material.albedo*=1.+grain*.63+scratch*.36+coating*.21;
  material.height+=grain*.00049+scratch*.00030;
  material.roughness=.32+grain*.19+scratch*.12;
  material.metallic=.74;
  if(finish==3.){
   float oxide=smoothstep(.03,.40,coating+fine*.25);
   material.albedo=mix(material.albedo,base*vec3(.46,.63,.58),oxide*.56);
   material.roughness+=oxide*.28;
  }
 }else if(finish==7.){
  float glaze=strataNoise(p*vec3(12.1,17.8,9.7),vec3(23.,19.,4.))-.5;
  float pores=smoothstep(.17,.34,fine+glaze*.16);
  float flow=sin((uv.y+glaze*.018)*123.)*stationDetailVisibility(width*20.);
  material.albedo*=1.+glaze*.28+flow*.07-pores*.27;
  material.height+=glaze*.00043-pores*.00038;
  material.roughness=.26+glaze*.16+pores*.27;
 }else if(finish==11. || finish==0.){
  float chipped=smoothstep(.20,.37,coating+fine*.48);
  float seam=finish==11.?cabinSeamGrid(uv*vec2(.89,1.14)):0.;
  float gasket=exp(-abs(p.y+1.09)*9.);
  material.albedo=mix(material.albedo,base*.58,chipped*.42+gasket*.16);
  material.albedo*=1.-seam*.39;
  material.height+=fine*.00027-seam*.00135;
  material.roughness=.44+chipped*.22+seam*.20;
 }else{
  material.roughness=.77;
 }
 return material;
}
