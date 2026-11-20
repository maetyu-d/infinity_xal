#ifndef HYPERION_CORONA
#define HYPERION_CORONA
// An optically thin magnetic envelope in the star's local frame. The
// photosphere, eclipses and temperature still come from the actual star.
vec3 stellarCoronaEnvelope(vec3 local,float altitude,float aa){
 float phase=u_starType[1].x;
 vec3 pole=normalize(vec3(.31+phase*.37,.87,-.29));
 float latitude=dot(local,pole);
 vec3 q=local*(7.+altitude*2.3)+vec3(phase*23.,7.,13.);
 float broad=strataNoise(q,vec3(0.));
 float curl=strataNoise(q*2.17+vec3(5.,broad*1.7,11.),vec3(0.));
 float fibre=strataNoise(q*5.3+curl*.8,vec3(0.));
 float sheet=pow(1.-abs(curl*2.-1.),6.);
 float equator=exp(-latitude*latitude*4.1);
 float stream=exp(-altitude/(.13+.43*broad*broad));
 float polar=pow(abs(latitude),6.)*exp(-altitude/.23);
 float inner=exp(-altitude/max(.022,aa))*(.26+fibre*.54);
 float corona=(sheet*(.10+.21*equator)+.016)*stream+polar*.021+inner*.24;
 float rim=1.-smoothstep(1.25,1.70,altitude);
 vec3 cool=mix(u_stellar[1].rgb,vec3(.52,.68,.92),.17);
 return mix(u_stellar[1].rgb,cool,smoothstep(.10,.72,altitude))*corona*rim;
}
#endif
