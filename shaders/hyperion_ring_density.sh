// Optical depth of the catalogue debris field. Derivative
// filtering resolves bands without allocating a per-planet ring texture.
float ringDensity(vec4 ring,float radius,float footprint){
 float width=ring.y-ring.x;
 if(ring.z<=0.||width<=0.)return 0.;
 // Integrate pixel coverage of the annulus. A thin ring still contributes
 // where its band falls between sample centres or is foreshortened.
 float halfPixel=max(.00001,footprint*.5);
 float lo=max(ring.x,radius-halfPixel),hi=min(ring.y,radius+halfPixel);
 if(hi<=lo)return 0.;
 float coverage=(hi-lo)/(2.*halfPixel);
 float centre=.5*(lo+hi);
 float coreWidth=min(width,6.);
 float radial=centre-ring.x;
 float u=radial/coreWidth,du=footprint/coreWidth;
 float taper=smoothstep(0.,.035,u)*(1.-smoothstep(.965,1.,radial/width));
 float edge=coverage*mix(taper,1.,smoothstep(.035,.16,du));
 float gap=(1.-smoothstep(.014,.022,abs(u-(.54+ring.w*.12))))*(1.-smoothstep(.018,.058,du));
 float narrow=(1.-smoothstep(.004,.010,abs(u-(.27+ring.w*.07))))*(1.-smoothstep(.007,.024,du));
 float division=(1.-smoothstep(.002,.008,abs(u-(.81+ring.w*.07))))*(1.-smoothstep(.006,.021,du));
 float broad=.48+.27*sin(u*81.+ring.w*6.283)*(1.-smoothstep(.014,.049,du));
 float filaments=.16*sin(u*275.+ring.w*13.)*(1.-smoothstep(.003,.014,du))
                 +.08*sin(u*637.+ring.w*21.)*(1.-smoothstep(.0015,.006,du));
 float belt=exp(-abs(u-.37)*8.);
 float braids=.065*cos(u*1124.+ring.w*17.+sin(u*96.)*.7)*(1.-smoothstep(.0008,.0033,du));
 filaments+=braids;
 float zones=.65+.55*smoothstep(.17,.52,u)-.37*smoothstep(.63,.89,u)+belt*.21;
 // Broad belts carry a diffuse dust tail. A narrow detached ring still
 // retains its own radial profile, even well outside the parent globe.
 float radialFalloff=exp(-max(0.,.36*(radial-3.5)));
 float extended=mix(1.,.025+.975*radialFalloff,smoothstep(5.,11.,width));
 return ring.z*edge*(1.-gap*.98)*(1.-narrow*.86)*(1.-division*.69)*zones*max(.035,broad+filaments)*extended;
}
