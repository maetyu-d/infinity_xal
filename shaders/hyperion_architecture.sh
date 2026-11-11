// Source: Hyperion flight/architecture.hpp, structureLight and its glass,
// ceramic coating, room lighting and grazing-film branches.
// Only material presentation; local metre coordinates never follow the camera.
float structureHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
// Hyperion architecture.hpp::windowInterior, adapted from model UVs to
// Infinity's existing metre-grid facades. The room is optical presentation
// behind an existing glass surface; no building or simulation is generated.
vec3 windowInterior(vec3 V,vec3 N,vec2 grid,vec3 position,vec3 accent,float identity,float working){
 vec2 du=dFdx(grid),dv=dFdy(grid);float det=du.x*dv.y-du.y*dv.x;
 if(abs(det)<1e-12)return accent*(.015+working*.09);
 vec3 px=dFdx(position)*u_materialDomain.z,py=dFdy(position)*u_materialDomain.z;
 vec3 tangent=(px*dv.y-py*du.y)/det,bitangent=(py*du.x-px*dv.x)/det;
 vec3 ray=vec3(-dot(V,normalize(tangent))/max(length(tangent),.05),-dot(V,normalize(bitangent))/max(length(bitangent),.05),-max(dot(V,N),.08)/2.8);
 vec3 safeRay=vec3(ray.x<0.?-1.:1.,ray.y<0.?-1.:1.,-1.)*max(abs(ray),vec3(.00001));
 vec3 origin=vec3(clamp(fract(grid),vec2(0.),vec2(1.))-.5,-.001);
 vec3 wall=vec3(safeRay.x>0.?.5:-.5,safeRay.y>0.?.5:-.5,-1.);
 vec3 travel=(wall-origin)/safeRay;float t=min(travel.x,min(travel.y,travel.z));vec3 hit=origin+safeRay*t;
 float back=step(travel.z,min(travel.x,travel.y)+.0001),floorFace=step(travel.y,min(travel.x,travel.z)+.0001)*step(safeRay.y,0.);
 vec3 room=mix(vec3(.20,.15,.10),accent*.25+vec3(.08,.10,.14),identity);
 room*=mix(.30,.64,back)*mix(1.,.55,floorFace);
 float details=1.-smoothstep(.08,.35,max(length(du),length(dv)));
 float desk=(1.-smoothstep(.025,.065,abs(hit.y+.24)))*(1.-smoothstep(.20,.35,abs(hit.x)))*back;
 float lamp=thread(hit.x*7.,.22)*smoothstep(.22,.42,hit.y)*back;
 room+=details*(vec3(.025,.035,.05)*desk+mix(vec3(1.,.59,.29),accent,.35)*lamp*.16);
 room*=.12+working*mix(.18,1.,step(.24,identity))*1.15;
 return mix(accent*(.015+working*.09),room,details);
}
void structureLight(out vec3 fill, out vec3 glint){
 // Regional colour belongs to incoming light. Keep a neutral component in
 // the fill so painted metal, ceramic and safety markings stay identifiable.
 fill=vec3(1.,.37,.15)*u_regionLook[1].y
     +vec3(.43,.92,.54)*u_regionLook[0].x
     +vec3(.33,.65,1.)*u_regionLook[1].x
     +vec3(1.,.38,.68)*u_regionLook[1].z;
 glint=vec3(1.,.70,.25)*u_regionLook[1].y
      +vec3(.72,1.,.45)*u_regionLook[0].x
      +vec3(.57,.80,1.)*u_regionLook[1].x
      +vec3(1.,.93,.41)*u_regionLook[1].z;
}
vec3 architecture(vec3 base,vec3 N,vec3 V,vec3 L,vec3 p,vec3 authoredPoint,float phase,float sunVisibility,vec3 position,float powered,out vec4 reflection){
 float role=u_materialDomain.y,localY=authoredPoint.y;
 bool harbour=role<-.5;
 vec3 face=normalize(cross(dFdx(authoredPoint),dFdy(authoredPoint)));
 // Remove the fixed light baked into the existing surface-mesh colours.
 // True geometry normals, sun and sky then illuminate the same triangles once.
 if(u_materialDomain.w<.5){
  vec3 authoredN=normalize(cross(dFdx(authoredPoint),dFdy(authoredPoint)));
  vec3 worldN=cross(dFdx(position),dFdy(position));
  if(dot(worldN,V)<0.)authoredN=-authoredN;
  base/=max(.55,.55+.45*max(0.,dot(authoredN,normalize(vec3(.4,1.,.3)))));
 }
 bool paving=abs(role-8.)<.1||abs(role-20.)<.1||abs(role-21.)<.1||abs(role-6.)<.1||(harbour&&abs(face.y)>.65);
 bool pad=abs(role-7.)<.1,solar=abs(role-22.)<.1;

 bool vegetation=role>16.5&&role<17.5;
 bool person=role>8.5&&role<10.5;
 bool built=!vegetation&&!person;
 float glass=built&&!pad&&!solar&&(!paving||harbour)?
  smoothstep(1.6,2.5,base.b/max(base.r,.01))*smoothstep(1.02,1.23,base.b/max(base.g,.01)):0.;
 float inlay=vegetation||person||paving||solar?0.:smoothstep(.44,.70,base.g-base.r)*smoothstep(.4,.65,base.g);
 vec3 localN=normalize(cross(dFdx(p),dFdy(p)));
 vec3 w=abs(localN);
 vec2 uv=w.z>w.x&&w.z>w.y?p.xy:w.x>w.y?p.zy:p.xz;
 vec2 grid=uv/(harbour?(paving?vec2(18.,18.):vec2(26.,20.)):vec2(3.6,3.2)),cell=fract(grid),edge=min(cell,1.-cell);
 float footprint=max(length(dFdx(p)),length(dFdy(p)));
 float fine=1.-smoothstep(.4,1.8,footprint);
 float panel=structureHash(vec3(floor(grid),phase*113.));
 float panelAA=max(fwidth(grid.x),fwidth(grid.y));
 float joint=(1.-smoothstep(.006,.018+panelAA,min(edge.x,edge.y)))*fine;
 vec2 corner=min(abs(cell-.065),abs(cell-.935));
 float bolts=(1.-smoothstep(max(0.,.009-panelAA),.009+panelAA,length(corner)))*(1.-smoothstep(.01,.06,panelAA));
 // Factory finish, panel joints and aggregate use the building's local
 // coordinates and its authored paint.
 vec2 materialUV=uv/(harbour?14.:3.6)+phase*vec2(7.1,3.7);
 vec2 materialDx=dFdx(materialUV),materialDy=dFdy(materialUV);
 vec4 finish=texture2DGrad(s_materialDetail,materialUV*.125,materialDx*.125,materialDy*.125);
 vec4 detailField=texture2DGrad(s_materialDetail,materialUV,materialDx,materialDy);
 float wear=smoothstep(.35,.75,finish.b),pores=detailField.a*detailField.g;
 float upright=1.-abs(dot(N,normalize(u_environment.xyz)));
 float runoff=smoothstep(.42,.75,finish.a)*upright*wear;
 vec3 paint=pow(max(base,vec3(.001)),vec3(1.72));
 float paintLuma=dot(paint,vec3(.25,.61,.14));
 vec3 albedo=mix(vec3(paintLuma),paint,.58)*.77+vec3(.020,.022,.025);
 albedo*=(.86+panel*.24)*(.84+finish.r*.23+detailField.r*.10*fine);
 albedo=mix(albedo,albedo*.45+vec3(.012,.018,.024),runoff*.38);
 albedo*=1.-pores*.16*fine;
 albedo*=1.-joint*.25;
 albedo+=vec3(.025,.029,.032)*bolts;
 float rough=mix(harbour?.29:.43,.088,glass),metal=mix(harbour?.66:.26,.12,glass);
 rough=clamp(rough+(detailField.a-.5)*.04+wear*.035,.074,.62);
 if(paving||pad){
  float kerb=smoothstep(.30,.50,base.r+base.g*.15);
  vec3 aggregate=mix(vec3(.025,.035,.044),vec3(.12,.135,.14),kerb);
  albedo=aggregate*(.67+detailField.r*.44+finish.b*.22);
  rough=harbour?.19:pad?.43:.73;
  metal=harbour?.41:.08;
 }
 if(solar){
  float cells=(1.-smoothstep(.025,.055+panelAA,min(edge.x,edge.y)))*fine;
  albedo=vec3(.006,.016,.045)*(1.-cells*.65);rough=.07;metal=.52;
 }

 if(vegetation){albedo=base*.23;rough=.92;metal=0.;}
 if(person){albedo=base*.48;rough=.76;metal=.05;}
 if(glass>.1){
  albedo=mix(albedo,vec3(.014,.025,.038)*(.81+panel*.35),glass);
  rough=mix(rough,.086,glass*.78);
 }
 float day=smoothstep(-.15,.18,u_surface.x);
 // Hyperion structureBump's recessed panel joints, in actual local metres.
 // Position and silhouettes stay untouched; unresolved joints fade out.
 if(built){
  float relief=(-joint*.005+(detailField.r-.5)*.002*fine-pores*.001*fine+bolts*.003)/max(u_materialDomain.z,1e-12);
  N=materialBump(N,position,relief,.28);
 }
 vec3 nx=dFdx(N),ny=dFdy(N);float variance=min(.25,max(dot(nx,nx),dot(ny,ny))*.5);
 rough=pow(min(1.,pow(rough,4.)+variance),.25);
 float fresnel=pow(1.-clamp(dot(N,V),0.,1.),4.);
 // Streamed paving has camera-relative vertices, rather than a building's
 // local footing height. Its illumination must not follow that origin.
 float footing=u_materialDomain.w>.5?1.:mix(.42,1.,smoothstep(.015,.15,localY));
 vec3 skyUp=normalize(u_environment.xyz);
 float skyFacing=clamp(dot(N,skyUp)*.5+.5,0.,1.);
 vec3 fill,glint;
 structureLight(fill,glint);
 vec3 sunColour=mix(vec3(1.05,1.02,.97),glint,.26+.27*(1.-day));
 vec3 c=brdf(albedo,N,V,L,rough,metal)*sunVisibility*sunColour;
 vec3 skyIrradiance=(vec3(.052,.057,.064)+fill*.23)*(.31+.69*day);
 vec3 groundBounce=(vec3(.035,.038,.042)+fill*.038)*(.36+.64*day);
 c+=albedo*mix(groundBounce,skyIrradiance,skyFacing)*footing;
 vec3 r=reflect(-V,N);
 float elevation=dot(r,skyUp);
 float horizon=exp(-abs(elevation-.04)/mix(.075,.30,rough));
 float reflectedSun=pow(max(dot(r,L),0.),mix(420.,18.,rough))*sunVisibility;
 // The horizon and the same stellar bearing that lights the facade both
 // appear on polished faces. Rough concrete gathers a broader reflection.
 vec3 sky=fill*(.054+.11*horizon)*(.43+.57*day)+glint*(horizon*.12+reflectedSun*.58);
 vec3 reflectance=environmentReflectance(mix(vec3(.035),albedo,metal),fresnel,rough);
 c+=sky*reflectance*(built?1.:.12);
 c*=1.-joint*.24;
 // Lit rooms recede into dark glass; they never bleach the facade coating.
 float glassEdge=joint*glass*(.10+fresnel*.65);
 c+=vec3(2.4,.80,.10)*glassEdge;
 float room=step(.37,panel),blind=smoothstep(.18,.28,cell.y)*(1.-smoothstep(.80,.86,cell.y));
 vec3 roomColour=mix(vec3(.30,.63,.70),vec3(1.,.74,.43),step(.53,panel));
 if(glass>.1){
  vec3 wallN=normalize(cross(dFdx(position),dFdy(position)));if(dot(wallN,V)<0.)wallN=-wallN;
  vec3 interior=windowInterior(V,wallN,grid,position,roomColour,panel,(.18+(1.-day)*.82)*powered);
  c=mix(c,interior+sky*(.05+fresnel*.65),glass*(1.-fresnel*.78)*fine*.84);
 }
 c+=roomColour*glass*room*blind*(.035+(1.-day)*.85)*fine*powered;
 if(vegetation)c+=base*pow(max(0.,dot(-L,V)),2.)*max(0.,-dot(N,L))*sunVisibility*.18;
 // Fixed-spectrum fixtures are part of the structure's authored colours.
 float fixture=harbour?smoothstep(.16,.42,max(base.r,max(base.g,base.b))-min(base.r,min(base.g,base.b))):inlay;
 c+=base*fixture*(harbour?.75:1.25)*powered;
 // The optical pass uses the sign to preserve the finish through the grade.
 reflection=vec4(N,built?-rough:rough);
 return c;
}
