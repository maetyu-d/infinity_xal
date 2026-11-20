// A surface-fixed optical cloud deck for temperate, pressure-bearing worlds.
// Uses globe coordinates rather than a screen plane, including when rolled.
float orbitalCloudDensity(vec3 n,float phase,float seconds){
 vec3 drift=vec3(seconds*.000012,0.,-seconds*.000009);
 vec3 q=n*8.8+vec3(phase*31.,11.,23.)+drift;
 float weather=strataNoise(q,vec3(0.));
 float curl=strataNoise(q*2.37+vec3(4.,weather*1.7,13.),vec3(0.));
 float cells=strataNoise(q*6.1+curl*.9,vec3(0.));
 float cirrus=strataNoise(q*vec3(6.7,2.1,4.3)+vec3(curl*2.,7.,weather),vec3(0.));
 float belt=sin(n.y*15.+weather*4.2+n.z*2.1)*.055;
 float mass=weather*.54+curl*.30+cells*.16+belt;
 float front=smoothstep(.46,.65,mass);
 float erosion=smoothstep(.23,.68,cells*.64+cirrus*.36);
 float wisps=smoothstep(.49,.63,weather*.58+cirrus*.42)*smoothstep(.54,.74,cirrus)*.16;
 return clamp(front*(.50+.50*erosion)+wisps,0.,1.);
}

vec3 orbitalCloudLight(vec3 ground,vec3 n,vec3 radial,vec3 V,vec3 L,
                       vec3 localLight,float phase,float seconds,float pressure,float temperature,float altitudeBlend){
 float temperate=smoothstep(165.,240.,temperature)*(1.-smoothstep(375.,510.,temperature));
 float air=smoothstep(.08,.65,pressure)*temperate*altitudeBlend;
 if(air<.001)return ground;
 float density=orbitalCloudDensity(n,phase,seconds);
 float coverage=(1.-exp(-density*2.7))*air;
 vec3 tangent=localLight-n*dot(n,localLight);
 float shadow=orbitalCloudDensity(normalize(n+tangent*.022),phase,seconds)*air;
 float day=smoothstep(-.13,.18,dot(radial,L));
 ground*=exp(-shadow*day*1.05);
 // The upper bank's density gradient and a short sunward optical march
 // separate sunlit towers from their shaded bases. All samples remain
 // attached to the globe, including the terminator and an inverted camera.
 vec3 billow=materialBump(n,n,density*.011,.52);
 float billowSun=max(0.,dot(billow,localLight));
 float optical=0.;
 for(int step=1;step<=3;++step){
  float reach=float(step);
  float blocker=orbitalCloudDensity(normalize(n+tangent*reach*.010),phase,seconds);
  optical+=max(0.,blocker-density*.51-reach*.075);
 }
 float selfShadow=exp(-optical*3.5);
 float upper=orbitalCloudDensity(normalize(n-tangent*.006),phase,seconds);
 float relief=clamp(.48+(density-upper)*6.2,.16,1.12);
 float silver=pow(1.-clamp(dot(radial,V),0.,1.),3.)*day;
 float banks=pow(clamp(coverage,0.,1.),.52);
 float powder=(1.-exp(-coverage*4.8))*(1.-banks*.58);
 vec3 ambient=vec3(.028,.043,.070)*(.46+density*.54);
 vec3 direct=vec3(.82,.88,.96)*(.21+billowSun*.75)*relief*selfShadow;
 vec3 cloud=mix(vec3(.003,.006,.012),ambient+direct,day);
 cloud+=vec3(1.,.66,.34)*pow(1.-abs(dot(radial,L)),8.)*day*.12;
 cloud+=vec3(.42,.61,.82)*silver*(.18+powder*.43);
 cloud+=vec3(.24,.31,.40)*powder*day*.18;
 return mix(ground,cloud,coverage*.94);
}
