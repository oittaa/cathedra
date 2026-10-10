// CATHEDRA - scene shader. One fullscreen pass, five procedural worlds.
// out: rgb = HDR radiance, a = view distance (TAA reprojection, DOF);
// gl_FragDepth = distance/FAR so the instanced swarm is depth-tested against it.
#extension GL_EXT_ray_query : require
layout(location=0) out vec4 O;
// TA = the gothic city (tower variants + bridges, GPU-generated meshes)
// TB = finale: Dyson swarm (refit per frame), ring habitats, rim scaffolds, debris chunks
layout(set=0,binding=8) uniform accelerationStructureEXT TA;
layout(set=0,binding=9) uniform accelerationStructureEXT TB;
layout(std430,set=0,binding=12) readonly buffer GB{ vec4 GV[]; };
layout(std430,set=0,binding=5) readonly buffer UB{ vec2 UVv[]; };
layout(set=0,binding=21) uniform sampler3D GI;   // baked city light: rgb bounce, a open sky
layout(set=0,binding=10) uniform sampler3D CL;   // cloud density + sun optical depth
layout(set=0,binding=14) uniform sampler3D PV;   // planet SDF / heat / strata / terrain
layout(set=0,binding=16) uniform sampler2D PM;   // orbit world map
float T, LT, JIT, JIT2, PXA; int SEC;
bool occA(vec3 o, vec3 d, float tm){
  rayQueryEXT q; rayQueryInitializeEXT(q,TA,gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsOpaqueEXT,255U,o,0.,d,tm);
  while(rayQueryProceedEXT(q)){}
  return rayQueryGetIntersectionTypeEXT(q,true)!=0U;
}
bool occB(vec3 o, vec3 d, float tm){
  rayQueryEXT q; rayQueryInitializeEXT(q,TB,gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsOpaqueEXT,14U,o,0.,d,tm);
  while(rayQueryProceedEXT(q)){}
  return rayQueryGetIntersectionTypeEXT(q,true)!=0U;
}
// closest mesh hit: distance, world normal (facing the ray), material, part uv, object-space position
float HT, HM, HUS; vec3 HN, HOP; vec2 HUV;
#define HITINFO(q,d) { HT=rayQueryGetIntersectionTEXT(q,true); \
  uint k=uint(rayQueryGetIntersectionInstanceCustomIndexEXT(q,true)+rayQueryGetIntersectionPrimitiveIndexEXT(q,true))*3U; \
  vec2 bc=rayQueryGetIntersectionBarycentricsEXT(q,true); float w0=1.-bc.x-bc.y; \
  vec4 a=GV[k], b=GV[k+1U], c=GV[k+2U]; mat4x3 M=rayQueryGetIntersectionObjectToWorldEXT(q,true); \
  HN=mat3(M)*(cross(b.xyz-a.xyz,c.xyz-a.xyz)/vec3(dot(M[0],M[0]),dot(M[1],M[1]),dot(M[2],M[2]))); \
  HN=dot(HN,HN)>1e-30?normalize(HN):-d; if(dot(HN,d)>0.) HN=-HN; HM=a.w; HUV=UVv[k]*w0+UVv[k+1U]*bc.x+UVv[k+2U]*bc.y; HOP=a.xyz*w0+b.xyz*bc.x+c.xyz*bc.y; \
  HUS=length(UVv[k+1U]-UVv[k])/max(length(mat3(M)*(b.xyz-a.xyz)),1e-6); }
bool traceA(vec3 o, vec3 d, float tm){
  rayQueryEXT q; rayQueryInitializeEXT(q,TA,gl_RayFlagsOpaqueEXT,255U,o,0.,d,tm);
  while(rayQueryProceedEXT(q)){}
  if(rayQueryGetIntersectionTypeEXT(q,true)==0U) return false;
  HITINFO(q,d)
  return true;
}
bool traceB(vec3 o, vec3 d, float tm){
  rayQueryEXT q; rayQueryInitializeEXT(q,TB,gl_RayFlagsOpaqueEXT,12U,o,0.,d,tm);
  while(rayQueryProceedEXT(q)){}
  if(rayQueryGetIntersectionTypeEXT(q,true)==0U) return false;
  HITINFO(q,d)
  return true;
}
vec3 hsph(float a, float b){ float z=a*2.-1., r=sqrt(1.-z*z); return vec3(r*cos(b*6.2832),r*sin(b*6.2832),z); }
vec3 cosdir(vec3 n, float a, float b){ return normalize(n+hsph(a,b)*.98); }
vec3 SUN; vec2 MBN;
vec3 stars(vec3 d){
  vec3 c=vec3(0);
  for(int i=0;i<3;i++){
    vec3 q=d*(160.+float(i)*130.), id=floor(q), f=fract(q)-.5;
    float h=h3(id+float(i)*100.);
    if(h>.975){ vec3 o=(vec3(h3(id+1.),h3(id+2.),h3(id+3.))-.5)*.6; c+=mix(vec3(1.,.7,.5),vec3(.6,.75,1.),h3(id+4.))*smoothstep(.07,0.,length(f-o))*(h-.975)*120.; }
  }
  float n=fbm(d*3.+7.,5);
  c+=mix(vec3(.15,.05,.25),vec3(.05,.15,.3),nz(d*9.))*pow(n,4.)*1.5*smoothstep(.6,0.,abs(d.y+.3*d.x));
  return c;
}
float goth(vec2 p,float w,float h){ float R=1.6*w; p.x=abs(p.x); return p.y>h? length(p-vec2(w-R,h))-R : p.x-w; }
// =====================================================================
// WORLD 0 : ASCENSIO - gothic spires rising out of a sea of clouds.
// The towers are real triangle meshes (grammar -> compute expansion -> BLAS),
// found with ray queries: primary visibility, sun shadows, AO, gold reflections.
// =====================================================================
float ALT, HI;
vec3 sky0(vec3 rd){
  float sd=max(dot(rd,SUN),0.), y=max(rd.y,0.), sh=max(dot(normalize(rd.xz+1e-5),normalize(SUN.xz)),0.);
  vec3 c=mix(mix(vec3(.38,.4,.52),vec3(.98,.52,.26),sh*sh),vec3(.05,.13,.4),pow(y,.4));
  c=mix(c,vec3(.006,.01,.03),ALT*smoothstep(-.1,.6,rd.y));
  c+=vec3(1.,.55,.28)*pow(sd,5.)*.7*(1.-ALT*.7)+vec3(1.,.75,.5)*pow(sd,64.)*1.6;
  c+=vec3(40.,30.,20.)*smoothstep(.9996,.9998,sd);
  if(rd.y>0.){
    vec2 cp=rd.xz/(rd.y+.06)*.9+vec2(T*.004,0.);
    float ci=smoothstep(.45,.85,texture(W,vec3(cp*vec2(.12,.5),.37)).r)*texture(W,vec3(cp*.6,.71)).g;
    c+=ci*smoothstep(0.,.15,rd.y)*(vec3(.5,.42,.4)+vec3(1.,.6,.3)*pow(sd,4.)*2.)*(1.-ALT)*.8;
  }
  if(rd.y<0.) c=mix(c,vec3(.42,.38,.4)*(1.-ALT*.8),clamp(-rd.y*6.,0.,1.));   // below the horizon: the lit undercast
  if(ALT>0.) c+=stars(rd)*ALT*ALT*smoothstep(0.,.3,rd.y);
  return c;
}
// the cloud sea: the baked volume (density, sun/sky optical depth, tower shadow) +
// two live erosion octaves that fade smoothly with distance (no LOD steps, no pops)
vec4 cden(vec3 p, float lod){
  vec3 tc=(p-CLO)/CLS;
  if(p.y<0.||p.y>64.) return vec4(0);
  // outside the baked domain: the analytic deck with lighting that matches the bake's statistics
  // (optical depth grows with depth below the tops); the last 8 % of the domain cross-fades, so
  // its edge never shows
  float e=smoothstep(.42-.14*HI,.5,max(abs(tc.x-.5),abs(tc.z-.5)));   // from high up: a 1 km cross-fade
  vec4 c=textureLod(CL,tc,0.);
  // the optical depth counts from each heap's own top, as in the bake: the far sea stays sunlit
  // past the baked square (from a fixed 60 m ceiling it came out a flat grey slab down the avenue)
  float hd=max((.3+.7*smoothstep(.3,.75,nz(p*.0045)))*48.-p.y,0.);
  if(e>0.) c=mix(c,vec4(cloudBase(p,lod*.5),hd*3.,hd*.8,1.),e);
  float w=1.-smoothstep(1.,4.5,lod);
  if(c.x>.001&&w>0.){
    // cauliflower detail: Worley fbm, inverted toward the tops (billows) and plain at the base (wisps)
    float h=clamp(p.y/40.,0.,1.);
    vec4 a=textureLod(W,p*.07+vec3(T*.03,0,T*.02),0.), b=textureLod(W,p*.21+vec3(T*.06,T*.02,0.),0.);
    float k=mix(a.g*.65+a.b*.35,1.-a.g*.65-a.b*.35,h)*.62+mix(b.g,1.-b.g,h)*.38;
    k*=.32*w;
    c.x=clamp((c.x/3.-k)/(1.-k),0.,1.)*3.;
  }
  return c;
}
float vis0(vec3 p){ return occA(p,SUN,1500.)?0.:1.; }
float CTD;   // sum of (cloud opacity / distance): TAA reprojection uses the opacity-weighted inverse depth
vec3 LGP; float LGI;   // lightning in the deck: position, flash
// sun: 3-octave multiple-scattering approximation with a forward lobe (silver lining) and
// powder darkening; sky: blue from above, attenuated by the baked upward optical depth (dark
// creases between the heaps), warm grey bounce from the deck below. c.w = tower shadow: it only
// takes the single-scatter octave fully - the diffuse octaves leak in from the lit cloud around
vec3 clit(vec4 c, float h, float cs, vec3 sunc, float lo){
  float ms=0., a=1., b=1., e=1., ld=c.y*.6+lo;
  for(int o=0;o<3;o++){ ms+=a*exp(-ld*.16*b)*mix(hg(cs,.7*e),hg(cs,-.25*e),.35)*mix(o>0?.22:0.,1.,c.w); a*=.5; b*=.45; e*=.5; }
  float pw=1.-exp(-c.x*2.5-ld*.08);
  return sunc*(ms*(.1+.9*pw)*4.*PI*.7*(.45+.55*smoothstep(-.1,.5,h))+.2*(1.-h)*(.3+.7*exp(-ld*.01))*c.w)
        +mix(vec3(.24,.22,.24),vec3(.3,.4,.64),h)*(.2+.8*exp(-c.z*.22))*(.6+.4*exp(-lo*.5));
}
vec4 clouds(vec3 ro, vec3 rd, float tmax, float pxs){
  float t0,t1,tf; CTD=0.;
  if(abs(rd.y)<1e-4){ t0=0.; tf=(ro.y<0.||ro.y>56.)?0.:3e5; }
  else { float a=-ro.y/rd.y, b=(56.-ro.y)/rd.y; t0=max(min(a,b),0.); tf=max(a,b); }
  t1=min(min(tf,tmax),t0+30000.);
  // the march ends at 10 km; an open ray (no tower) keeps the analytic far deck out to the horizon
  if(tmax<1e4) tf=t1;
  vec3 sc=vec3(0); float tr=1.;
  if(tf<=t0) return vec4(sc,tr);
  // geometric steps t(s)=25(e^(.008s)-1), dt=.2+.008t, started at the slab entry and jittered per
  // pixel (interleaved gradient noise, new every frame -> TAA) over a whole empty stride.
  // Every entry into the deck is bisected (5 halvings between the last empty and the first
  // dense sample) and the lattice restarts .35 step inside the found surface, so the samples ride on
  // the cloud surface itself.
  float sl=log(1.+t0/25.)*125.+JIT*2., t=t0, tp=t0, tq=t0, dt, cs=dot(rd,SUN); int rf=0; bool emp=true, fin=false;
  vec3 sunc=vec3(2.9,1.95,1.2)*(1.-ALT*.3), sh=sky0(normalize(vec3(rd.x,.01,rd.z)));
  for(int i=0;i<256;i++){
    t=(exp(sl*.008)-1.)*25.; dt=.2+t*.008;
    // a stride that would jump past the tower behind (t1) ends exactly in front of it, so the thin
    // cloud wrapped around a tower base is always integrated
    if(t>t1){ if(tq>t1-.1) break; fin=true; t=t1-.05; dt=min(dt,t-tq); }
    vec3 p=ro+rd*t;
    float lod=log2(max(t*pxs*1.5,1.));
    vec4 c=cden(p,lod);
    if(c.x>.001&&emp&&rf<4){
      float a=tp, b=t;
      for(int k=0;k<5;k++){ float m=(a+b)*.5; if(cden(ro+rd*m,lod).x>.001) b=m; else a=m; }
      t=b+(.2+b*.008)*.35; sl=log(1.+t/25.)*125.; dt=.2+t*.008;
      if(t>t1-.05){ t=t1-.05; dt=max(t1-b,.01); }   // cloud just in front of a tower's base: no sample from behind its wall
      p=ro+rd*t; c=cden(p,lod); rf++;
    }
    if(t>tmax-2.5) c.yzw=textureLod(CL,(ro+rd*(tmax-2.5)-CLO)/CLS,0.).yzw;   // within a voxel of a tower's wall the bake blends in the tower's lit inside (an orange flicker at the base): light it from just outside
    emp=c.x<=.001;
    if(!emp){
      // two short live taps toward the sun: the billows shade themselves (the bake is smooth). The taps
      // read the baked density without the erosion octaves (lod+5): the same shading at a third of the cost
      float lo=lod<3.?(cden(p+SUN*1.1,lod+5.).x*1.1+cden(p+SUN*3.4,lod+5.).x*2.3)*.3:0.;
      vec3 lc=mix(clit(c,min(p.y/44.,1.),cs,sunc,lo)+vec3(.7,.66,1.)*LGI*(exp(-dot(p-LGP,p-LGP)*8e-4)*5.+exp(-dot(p-LGP,p-LGP)*1e-4)*.4),sh,1.-exp(-t*1.7e-4*(1.-ALT*.7)));   // thinner haze seen from high up; aerial perspective is the depth cue of the sea
      float al=1.-exp(-c.x*dt*.24*(SEC==6?smoothstep(0.,40.,t):1.));   // the climb passes through the deck top: scale opacity with distance so the sunlit fog does not white out
      sc+=tr*al*lc; CTD+=tr*al/max(t,1.); tr*=1.-al;
      if(tr<.01) break;
      sl++;
    } else { tp=t; sl+=t>t1-16.?1.:2.+floor(float(i)/40.); }   // (not in the last 16 m before a tower: with long strides the thin cloud hugging its base was skipped in some frames, a flickering band)   // empty space: ever longer strides, so the march reaches the far deck (no flat-topped "wall" from the remainder)
    tq=t; if(fin) break;
  }
  if(t<tf&&tr>.01){   // beyond the marched range: the layer's mean density, analytically
    float te=t, tx=tf;
    if(rd.y<0.) te=max(t,(48.-ro.y)/rd.y); else tx=min(tf,(48.-ro.y)/max(rd.y,1e-4));
    // density ramps up over the top 25 m; soft top, no flat-topped bank on the horizon
    float a=(1.-exp(-max(tx-te,0.)*.15*(rd.y<0.?min((48.-ro.y-rd.y*tx)/25.,1.):1.)))*exp(-max(rd.y,0.)*90.);
    // the far deck carries cloud fields (5 km) and cells (1 km) on its top, mip-filtered by the
    // (grazing-stretched) footprint, so its texture runs on to the horizon without shimmer
    // Seen from above (HI) it is lit like the marched tops (no edge haze floor, low optical depth), so
    // the march's end (~3 km for grazing rays) no longer shows as a cut from white sea to brown band.
    vec2 x=(ro+rd*te).xz; float lw=log2(max(te*pxs/sqrt(max(-rd.y,.004)),1.)), f0=.55-.55*HI;
    float n=textureLod(W,vec3(x*.0002,.31),lw-5.3).g*.5+textureLod(W,vec3(x*.001,.67),lw-3.).g*.3+textureLod(W,vec3(x*.005,.5),lw-.7).g*.2;   // + 200 m cells, as the marched heaps
    sc+=tr*a*mix(clit(vec4(1.5,mix(24.,2.,HI),mix(8.,1.,HI),1.),.8+.2*HI,cs,sunc,0.)*(.5+n)*(1.-.4*HI),sh,f0+(1.-f0)*(1.-exp(-te*1.7e-4*(1.-ALT*.3))));   // lit like the deck seen edge-on
    CTD+=tr*a/max(te,1.); tr*=1.-a;
  }
  return vec4(sc,tr);
}
// leaded stained glass in window space (uv = arch coordinates / half width), band-limited:
// fp = pixel footprint in cell units. Lead lines are filtered to the footprint and the whole
// mosaic fades to its average colour before cells/leading get smaller than ~2 pixels, so the
// jittered TAA samples never flip between lead and glass.
vec3 glass(vec2 uv, float sd, float fp){
  vec2 g=uv*4.5, ip=floor(g), fq=fract(g), bi=vec2(0);
  float d1=8., d2=8., w=max(fp*.6,.03);
  for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){
    vec2 o=vec2(i,j), r=o+vec2(h2(ip+o+sd*99.),h2(ip+o+sd*99.+17.))*.8+.1-fq; float dd=dot(r,r);
    if(dd<d1){ d2=d1; d1=dd; bi=ip+o; } else d2=min(d2,dd);
  }
  float h=h2(bi+sd*37.), md=length(uv-vec2(0.,1.7)), wr=w*.34;
  if(md<.55) h=fract(sd*7.+step(.3,md)*.5+step(.12,md)*.25);
  // deep pot-metal jewel tones (sapphire, ruby, amber, emerald, violet), heavy leading,
  // and hand-made glass: streaks and seeds inside each piece (faded with the footprint)
  vec3 c=h<.45?vec3(.02,.07,.6):h<.68?vec3(.62,.02,.04):h<.8?vec3(.66,.32,.03):h<.9?vec3(.03,.36,.12):vec3(.3,.05,.5);
  float lead=smoothstep(.1-w,.1+w,sqrt(d2)-sqrt(d1))*smoothstep(.05-wr,.05+wr,abs(md-.55))*smoothstep(.05-wr,.05+wr,abs(md-.3));
  c*=(.55+.8*h2(bi+5.))*(.6+.6*smoothstep(0.,.5,sqrt(d2)-sqrt(d1)))*mix(.7+.6*nz(vec3(uv.x*7.,uv.y*30.,sd*50.)),1.,smoothstep(.02,.1,fp));
  return mix(c*lead,vec3(.16,.09,.12),smoothstep(.2,.7,fp));   // far panes read as dark glass
}
// the ashlar as a height field over course coordinates q (along the wall in m, up in
// courses): 0 = mortar bed, ~1 = the proudest face. Pillowed blocks with chipped arrises (noise on the
// bevel), their own protrusion and tilt, pitted faces, a few lost blocks; trim adds its mouldings.
// Shared by the albedo (same block ids) and the close-range relief march in world0.
vec2 Bi, Bf; float Bj, Bc, Bk; vec3 BT;
float bh(vec2 q, float m){
  q.y+=.3*sin(q.y*.53); float cr=floor(q.y); Bc=.75+.5*h2(vec2(cr,7.));   // courses of varying height
  q.x=q.x*(1.5+.6*h2(vec2(cr,3.)))+cr*.5+h2(vec2(cr,1.)); q.x+=.25*sin(q.x*1.1+cr*1.7)+.15*sin(q.x*2.3+cr*.7);   // blocks of varying length along a course
  Bi=vec2(floor(q.x),cr); Bf=fract(q);
  Bj=min(min(Bf.x,1.-Bf.x)*.55,min(Bf.y,1.-Bf.y)*Bc);
  float r=h2(Bi+21.), e=nz(vec3(q*7.,r*40.));
  return (r<.012?.35*e:smoothstep(0.,.008+.025*e,Bj-.004)*(.84+.16*r+dot(Bf-.5,vec2(h2(Bi+4.),h2(Bi+6.))-.5)*.35)*(.84+.16*nz(vec3(q*31.,3.))))
        +(m==7.?.2*sin(q.y*3.82):0.);
}
float bw(vec3 x){ return bh(vec2(dot(x.xz,BT.xz),x.y*3.3),Bk); }
// limestone / trim / copper / slate / gold albedo with weathering; returns albedo, w = metalness
vec4 mat0(vec3 p, vec3 n, float m, vec2 uv, float fp, float ao){
  vec2 tg=normalize(vec2(-n.z,n.x)+1e-5); float hc=dot(p.xz,tg);
  vec4 wn=texture(W,p*.021);
  // every stone detail is band-limited by fp, the pixel footprint on the surface (distance x
  // pixel angle / cos(incidence)), not by distance alone: grazing walls and the spire bases at the
  // cloud line no longer alias (mortar joints, per-block tints and grain fade to their mean first)
  float streak=texture(W,vec3(hc*.06,p.y*.004,.31)).r, up=max(n.y,0.), gr=mix(nz(p*5.3),.5,smoothstep(.04,.16,fp));
  if(m==1.) return vec4(vec3(1.,.72,.3)*(.8+.3*wn.b),1.);
  if(m==3.){        // copper: verdigris with brown streaks where rain runs off
    float pat=smoothstep(.35,.7,wn.g+.3*up-.25*smoothstep(.5,.8,streak));
    return vec4(mix(vec3(.38,.2,.1),vec3(.22,.48,.4),pat)*(.75+.4*gr),.3*(1.-pat));
  }
  if(m==4.){        // slate scales
    vec2 s=vec2(fract(uv.x)*7.,uv.y*9.); s.x+=.5*mod(floor(s.y),2.);
    vec2 f=fract(s)-vec2(.5,.0); float sc=smoothstep(.0,.12,1.-length(f*vec2(1.,.8)));
    return vec4(vec3(.12,.13,.16)*(.7+.5*h2(floor(s)))*(.6+.4*mix(sc,.7,smoothstep(.03,.12,fp))),.15);
  }
  // stone: ashlar in running bond at a believable scale (courses ~.3 high with varying heights,
  // blocks ~.45-.8 long; paving on flat tops); per-block tint + hue + relief (MBN tilts the shading
  // normal: recessed mortar joints and worn arrises catch the low sun), fine grain, rain streaks
  // running down from every ledge, lichen in the shelter, soot low down
  float fq=fp*3.3, ft=smoothstep(.1,.35,fq);   // footprint in course heights: joints and relief fade out
  bh(abs(n.y)>.7?p.xz*vec2(1.6,3.):vec2(hc,p.y*3.3),m);
  vec2 bi=Bi, f=Bf; float jb=Bj;
  float mort=mix(smoothstep(0.,.015+fq*.5,jb),.94,ft), hb=mix(h2(bi+floor(p.xz*.02)*17.),.5,smoothstep(.3,.9,fq)), ht=mix(h2(bi+3.),.5,smoothstep(.3,.9,fq));
  // every block is a pillowed, chipped stone: per-block bevel width, broken arrises (noise on
  // the bevel), its own tilt and pitted face; trim carries a run of horizontal mouldings (roll + hollow)
  float bw=.03+.04*h2(bi+2.)+.04*nz(p*7.);
  MBN=(1.-ft)*(vec2(smoothstep(1.-bw,1.,f.x)-smoothstep(bw,0.,f.x),smoothstep(1.-bw,1.,f.y)-smoothstep(bw,0.,f.y))*.85
      +(vec2(h2(bi+.5),h2(bi+9.5))-.5)*.12+(vec2(nz(p*13.),nz(p*13.+7.))-.5)*.35+(m==7.?vec2(0,sin(p.y*12.6)*.8):vec2(0)));
  // every tower has its own quarry: warm limestone, grey granite-like, rosy sandstone (reads at any distance)
  float tq=h2(floor(p.xz/CS)+vec2(11.,3.));
  vec3 a=(m==7.?vec3(.78,.72,.62):vec3(.68,.61,.51))*(tq<.3?vec3(1):tq<.55?vec3(.84,.9,1.02):tq<.8?vec3(1.07,.84,.76):vec3(.78,.72,.64))*(.84+.32*nz(vec3(p.xz*.02,p.y*.012)));
  a*=(.86+.24*hb)*mix(vec3(1),vec3(1.06,.97,.88),ht)*(.86+.24*gr)*(.92+.16*nz(p*23.)*smoothstep(.05,.008,fp));
  a*=mix(.72,1.,mort)*(m==7.?.86+.14*sin(p.y*12.6+1.2):1.)*mix(.88,1.,min(smoothstep(0.,.05+fq*.5,jb)+ft,1.));
  a*=mix(1.,.45,smoothstep(.42,.85,streak*(.7+.6*nz(vec3(hc*2.,p.y*.15,0.))))*(1.-up));
  // large-scale weathering: grime bands under every tier and darker, damper courses in streaks
  a*=mix(1.,.72,smoothstep(.55,.9,texture(W,vec3(hc*.015,p.y*.03,.6)).g)*(1.-up))*(.94+.06*sin(p.y*.69+tq*6.));
  float li=smoothstep(.6,.8,nz(p*.31)+.45*up+.15*(1.-ao)+.2*smoothstep(.5,.9,streak));   // soft lichen / moss on ledges and in the wet streaks
  a=mix(a,mix(vec3(.2,.24,.09),vec3(.42,.38,.16),gr),li*.5);
  a*=mix(.7,1.,smoothstep(-40.,40.,p.y));
  return vec4(a,0.);
}
vec3 SKC=vec3(.2,.25,.42), SUNC=vec3(4.4,2.9,1.8);
// cheap shading of a reflected hit (no further rays)
vec3 shadeR(vec3 p, vec3 n, float m, vec2 uv, float t){
  vec4 a=mat0(p,n,m,uv,1.,.8);
  return a.rgb*(max(dot(n,SUN),0.)*SUNC*.7+SKC*(.6+.4*n.y)+vec3(.5,.4,.3)*.3);
}
vec3 world0(vec3 ro, vec3 rd, out float dep){
  ALT=SEC==6?smoothstep(140.,420.,ro.y):0.; HI=SEC==6?smoothstep(70.,200.,ro.y):0.;
  SUN=normalize(vec3(-.9,.15,.08));
  // the deck answers the music: lightning deep in the clouds ahead, on the beat (120 BPM), more often with
  // the bells, the riser and the drums, always on the impact that opens the flight. One soft pulse per flash
  // (60 ms rise, a slow fall, gone before the next beat): no strobing, so the luma checks pass
  float bn=floor(T*2.), bu=T-bn*.5;
  LGP=vec3(ro.x+(hf(uint(bn)+33u)-.5)*500.,26.,ro.z-320.-480.*hf(uint(bn)+99u));   // at least ~320 m out: a near strike would light half the frame
  LGI=step(hf(uint(bn)),.3+.15*float(SEC==2)+float(SEC==2&&LT<.5))*float(SEC<3)*smoothstep(0.,.06,bu)*exp(-bu*8.)*smoothstep(.5,.3,bu)*8.;   // decided once per beat (time only): a flash never cuts off or pops on halfway
  float pxs=2./(U.z*1.4);
  vec3 col=sky0(rd);
  float t=1e4;
  if(traceA(ro,rd,4000.)){   // past the far edge of the city from every camera
    t=HT; vec3 p=ro+rd*t, n=HN; float m=HM; vec2 uv=HUV;
    MBN=vec2(0);
    float fp=t*pxs/max(abs(dot(HN,rd)),.2), vz=dot(rd,HN), ps=1., cv=1.;   // pixel footprint on the surface
    vec3 tg=normalize(vec3(-HN.z,0.,HN.x)+1e-5), pd=p;
    // close-range relief. The wall is a height field (bh, up to 3 cm deep): the view ray is
    // marched into it (20 steps + 4 bisections) so blocks stand proud of recessed joints and occlude
    // them at grazing angles; the normal comes from the field (bevels, chips, tilt, pitting), a short
    // march toward the sun gives the contact shadows, the depth gives the cavity occlusion. Fades out
    // with the pixel footprint (shimmer-free at distance). The low flight (section 2) skips it.
    float k=SEC!=2&&m!=2.&&m!=1.&&abs(HN.y)<.7?smoothstep(.15,.05,fp):0.;
    if(k>0.){
      float D=.03*k, sm=min(D*1.1/max(-vz,.02),D*8.), s=0., ds=sm/24., pv=1., h, de, e=max(.01,fp*.4);
      BT=tg; Bk=m;
      for(int i=0;i<24;i++){ h=D*(1.1-bw(p+rd*s))+vz*s; if(h<=0.){ s-=ds*h/(h-pv); break; } pv=h; s+=ds; }   // linear march + secant step
      pd=p+rd*min(s,sm); h=bw(pd); de=D*(1.1-h);
      vec3 B=cross(HN,tg); B*=sign(B.y);
      n=normalize(HN-D/e*((bw(pd+tg*e)-h)*tg+(bw(pd+B*e)-h)*B));
      float sz=dot(SUN,HN);
      if(sz>0.) for(int i=1;i<7;i++){ float u=D*.25*float(i); ps=min(ps,clamp((D*(1.1-bw(pd+SUN*u))-de+sz*u)/(D*.15)+1.,0.,1.)); }
      ps=mix(1.,ps,k); cv=mix(1.,.4+.6*smoothstep(.05,.75,h),k);
    }
    vec4 a=mat0(pd,HN,m,uv,fp,1.);   // geometric normal: the bump noise must not warp the coursing
    if(m!=2.){ vec3 b=(vec3(nz(pd*3.1),nz(pd*3.1+19.),nz(pd*3.1+41.))-.5)*min(abs(m-1.),1.);   // (gold: only the slow waves of the beaten leaf)
      vec3 b2=vec3(nz(p*.55),nz(p*.55+23.),nz(p*.55+47.))-.5;   // mid-scale relief: weathered, uneven wall faces
      b+=(vec3(nz(pd*17.),nz(pd*17.+5.),nz(pd*17.+9.))-.5)*1.2*k;   // chisel marks / pitting, close range only
      n=normalize(n+b*.08*smoothstep(.1,.015,fp)+b2*.14*smoothstep(.5,.07,fp)+(abs(HN.y)>.7?vec3(0):(tg*MBN.x+vec3(0,MBN.y,0))*(1.-k))); }
    // ray-traced ambient occlusion (2 cosine rays, temporally re-randomised) and sun shadow
    float ao=1.;
    if(t<900.){
      ao=0.;
      int na=SEC==2?2:SEC==6?4:6; for(int i=0;i<na;i++){ vec3 d=cosdir(n,fract(JIT+float(i)/float(na)),fract(JIT2+float(i)*.618)); ao+=occA(p+n*.04,d,10.)?0.:1./float(na); }
      ao=mix(.25+.75*ao,1.,smoothstep(600.,900.,t));   // fades out before the cut-off (no brightness step at 900 m)
    }
    float sh=(occA(p+HN*.04,SUN,2000.)?0.:1.)*ps, ndl=max(dot(n,SUN),0.), dif=ndl*sh, gl=.3+1.4*h2(Bi+8.);
    a.rgb*=mix(.5,1.,ao)*cv;
    vec3 skc=SKC*(.42+.58*n.y), bnc=vec3(.62,.5,.4)*(max(-n.y,0.)*.9+.2)*(1.-ALT);
    {  // baked light volume (sampled 1.5 cells off the surface): sky occlusion between the towers + bounce
      vec3 gq=(p+n*13.-GIO)/GIS; float gw=smoothstep(.5,.46,max(max(abs(gq.x-.5),abs(gq.y-.5)),abs(gq.z-.5)));
      vec4 G=textureLod(GI,gq,0.);
      skc*=mix(1.,.25+G.a,gw); bnc=mix(bnc,G.rgb*SUNC*(.45+.5*max(-n.y,0.)),gw);   // sky occlusion and bounce from the baked volume
    }
    vec3 rf=reflect(rd,n);
    float fre=pow(1.-max(dot(-rd,n),0.),5.);
    if(m==2.){          // stained glass: dark and reflective from outside, glowing only against the light
      // one pattern per tower face, so it does not change inside a pane or flip with sub-pixel jitter; steady glow (no beat pulse)
      vec3 g=glass(uv,h3(vec3(floor(p.xz/CS),floor(atan(HOP.z,HOP.x)*1.27+.5))),3.*HUS*t*pxs/max(abs(dot(n,rd)),.25));
      // transmitted light: the low sun pours in through the windows on the far side and lights
      // the glass from within (strongest on walls facing away from the sun), plus the sun seen through it
      float bl=.2+.4*smoothstep(.3,-.7,dot(HN,SUN))+1.4*pow(max(dot(rd,SUN),0.),6.)+.2*ALT;
      vec3 hp=floor(HOP*5.)+floor(p.xz/CS).xyx, wn=normalize(n+(vec3(h3(hp),h3(hp+7.),h3(hp+3.))-.5)*.4);
      float e=mix(900.,30.,smoothstep(.02,1.5,fp));   // each pane tilted its own way catches the low sun; far = broader, dimmer lobe
      col=g*bl*(.4+.9*h3(hp+5.))+mix(vec3(.02,.025,.035),sky0(rf),.15+.5*fre)*ao+SUNC*pow(max(dot(reflect(rd,wn),SUN),0.),e)*e*.005*sh;
      col+=g*mix(step(.75,h3(floor(HOP*.5)+floor(p.xz/CS).xyx*7.+11.)),.25,smoothstep(.5,2.,fp))*4.*smoothstep(50.,66.,p.y);   // a quarter of the windows lit from inside: the city is awake (not at the cloud line, where the deck's ragged edge in front would make them blink)
    } else if(a.w>.5){  // gold leaf: ray-traced reflection of the neighbours
      vec3 env=sky0(rf);
      if(t<1200.){ vec3 o=p+n*.05; if(traceA(o,rf,600.)) env=mix(shadeR(o+rf*HT,HN,HM,HUV,HT),env,1.-exp(-HT*.002)); }
      col=a.rgb*(env*(.35+.65*ao)+SUNC*pow(max(dot(rf,SUN),0.),60.)*sh*4.+bnc*.3);
    } else {
      col=a.rgb*(dif*SUNC+ao*(skc+bnc));
      col+=(pow(max(dot(rf,SUN),0.),a.w>.2?40.:16.)*(.08+a.w*.8))*dif*vec3(2.,1.6,1.2)*gl;
      // rim / back light: the low sun grazing the silhouettes, strongest looking into the light
      col+=a.rgb*SUNC*sh*pow(1.-abs(vz),4.)*smoothstep(-.3,.5,dot(n,SUN))*(.25+pow(max(dot(rd,SUN),0.),3.))*.7;
      col+=sky0(rf)*fre*.15*ao;
    }
    if(p.y<62.) col*=mix(.2+.8*exp(-textureLod(CL,(p-CLO)/CLS,0.).y*.06),1.,smoothstep(50.,62.,p.y));   // stone inside the deck lies in the cloud's shade (fading out above it: no hard line)
    float fg=(1.-exp(-(t*.00017+t*t*5e-8)*(1.-ALT)))*mix(1.7,.6,smoothstep(50.,260.,p.y));   // thinner aloft, denser low: rows layer by distance, the feet melt into the deck haze
    col=mix(col,sky0(normalize(vec3(rd.x,max(rd.y,.02),rd.z)))*.8,fg);
    col=mix(col,sky0(rd),smoothstep(3200.,4000.,t));   // dissolved into the plain sky before the ray's end
  }
  vec4 cl=clouds(ro,rd,t,pxs);
  col=col*cl.a+cl.rgb;
  // a flock crossing the opening in world space: heading left-forward (toward the sun side)
  // at ~7 m/s while the camera turns right, so it drifts right-to-left across the frame with
  // parallax. Each bird wheels on its own slow
  // circle and banks into it, beats its wings at its own rate with short glides; the flock slowly
  // spreads. Above the deck: the ray's cloud transmittance (all in front of them) covers them
  // In the climb a flock circles the tower 90-135 m out, at 90-210 m: the camera looks up at it, passes it, then sees it over the deck.
  vec2 tw=ro.xz-vec2(cos(LT*.3+.6),sin(LT*.3+.6))*23.;   // (the tower, from the camera's orbit around it)
  if(SEC==0||SEC==6) for(int i=0;i<16;i++){
    float f=float(i), w=T*.5+f, ba=T*.12+f*.4;
    vec3 q=(SEC==6?vec3(tw.x+cos(ba)*(90.+f*3.),150.+(fract(f*.37)-.5)*120.,tw.y+sin(ba)*(90.+f*3.))
          :vec3(-50.,92.,185.)+vec3(-6.5,.2,-8.)*T+(fract(sin(f*vec3(12.9,78.2,37.7))*43758.)-.5)*vec3(36.,10.,30.)*(1.+T*.03))
          +vec3(sin(w),.4*sin(w*1.4),cos(w))*4.-ro;
    float tb=dot(q,rd);
    if(tb>0.&&tb<t){
      vec3 v=(rd*tb-q)*(.42+.16*fract(f*.618));   // 2.4-3.1 m span, each bird its own size
      vec2 b=vec2(dot(v.xz,normalize(vec2(-rd.z,rd.x))),v.y)*rot(.35*cos(w));   // bank into the turn
      float x=b.x, ax=abs(x),
            d=max(abs(b.y-sin(T*(7.+fract(f*.37)*2.)+f*2.3)*.5*smoothstep(-.6,.2,sin(T*.9+f))*ax-.3*min(ax,.5)+.45*max(ax-.5,0.))-.11*(1.-ax/1.3),ax-1.3);   // gull wings: up to the wrist, down to the tip
      d=min(d,length(vec2(x*1.8,b.y+.05))-.16);
      vec3 bc=vec3(.03,.027,.03)+vec3(.9,.45,.2)*pow(max(dot(rd,SUN),0.),6.)*smoothstep(-.1,0.,d);   // toward the low sun the wing edges glow
      col=mix(col,mix(bc,sky0(rd),1.-exp(-tb*.001)),clamp(.5-d/(tb*pxs*.5),0.,1.)*(ro.y<64.?cl.a:1.));   // (above the deck no cloud lies in front)
    }
  }
  {  // sun shafts between the spires
    int ns=SEC<2?32:SEC==2?28:20; float tm2=min(t,900.), dt=tm2/float(ns), ts=dt*JIT, ins=0.;
    for(int i=0;i<ns;i++){
      vec3 p=ro+rd*ts;
      if(p.y>30.) ins+=vis0(p)*smoothstep(30.,50.,p.y)*exp(-max(p.y-40.,0.)*.008-ts*.0012)*dt;   // fades in above the deck: no hard edge sliding over the tower bases
      ts+=dt;
    }
    col+=ins*(vec3(1.,.62,.33)*hg(dot(rd,SUN),.6)*.0035+vec3(.4,.45,.6)*.00006)*(1.-ALT)*cl.a;
  }
  {  // morning mist: faint, soft banks drifting in a layer just above the cloud sea (50-85 m), between the towers;
     // it thins toward the layer's top and bottom, so no flat edge shows
    float ta=(50.-ro.y)/rd.y, tb=(85.-ro.y)/rd.y, m0=max(min(ta,tb),0.), m1=min(max(ta,tb),min(t,1500.));
    vec3 mm=ro+rd*(m0+m1)*.5;
    float ma=(1.-exp(-max(m1-m0,0.)*textureLod(W,vec3(mm.xz*.0008+vec2(T*.004,0),.2),0.).g*.002))*.3*(1.-ALT)*smoothstep(50.,60.,mm.y)*smoothstep(85.,75.,mm.y);
    col=mix(col,sky0(normalize(vec3(normalize(rd.xz+1e-5),.05).xzy))+SUNC*.12*pow(max(dot(rd,SUN),0.),4.),ma);   // (lit like the sky just above the horizon in the ray's direction: never the sun disc)
  }
  dep=1./(CTD+cl.a/min(t,6e4));   // opacity-weighted inverse depth: continuous, no flip between cloud and tower
  col+=vec3(1.,.6,.3)*pow(max(dot(rd,SUN),0.),300.)*2.;
  return col;
}

// =====================================================================
// WORLD 1 : the neon nave - infinite gothic hall, light shafts, wet floor
// =====================================================================
float MAT1;
float map1(vec3 p){
  vec3 q=p; q.xz=mod(q.xz,12.)-6.;
  float gx=goth(q.zy,4.8,13.), gz=goth(q.xy,4.8,13.);
  float inner=min(gx,gz);
  float d=max(max(-inner,13.-p.y),p.y-28.);
  d=max(d,1.7-length(q.xz));
  // clustered pier: fluted core + 8 shafts (radius, flute count and twist vary per pier),
  // octagonal plinth with a base moulding, astragal, carved foliage capital, chipped arrises
  vec2 cq=abs(q.xz)-6.;
  float lc=length(cq), ph=h2(floor((p.xz+6.)/12.)+vec2(5,9)), an=atan(cq.y,cq.x)+p.y*.06*step(.8,ph);
  float nf=8.+4.*floor(ph*3.), cap=smoothstep(11.6,12.2,p.y)*smoothstep(13.4,12.9,p.y);
  float col=lc-1.+.035*smoothstep(-.3,.7,cos(an*nf))*(1.-cap);
  float sr=.31+.08*fract(ph*7.);
  col=min(col,length(cq+vec2(1.05,0.))-sr);
  col=min(col,length(cq+vec2(0.,1.05))-sr);
  col=min(col,length(cq+vec2(.76,.76))-sr);
  col=min(col,max(max(max(abs(cq.x),abs(cq.y)),(abs(cq.x)+abs(cq.y))*.7071)-1.75,abs(p.y-.3)-.3));   // octagonal plinth
  col=min(col,length(vec2(lc-1.42,p.y-.78))-.17);                                                  // base torus
  col=min(col,length(vec2(max(lc-1.32,0.),p.y-11.75))-.09);                                         // astragal
  col=min(col,length(vec2(max(lc-1.45,0.),p.y-12.7))-.3-cap*.07*sin(an*14.)*sin(p.y*9.));           // foliage capital
  col+=(nz(p*7.1)-.5)*.025;                                                                          // wear / chips
  d=min(d,col);
  float rib=max(max(abs(gx-gz)*.7-.13,-(inner+.5)),13.-p.y);
  MAT1=rib<d?1.:0.;
  d=min(d,rib);
  if(p.y<d){ d=p.y; MAT1=2.; }
  return d;
}
float map1s(vec3 p){ float m=MAT1; float d=map1(p); MAT1=m; return d; }
vec3 nor1(vec3 p){ vec2 e=vec2(.003,-.003); return normalize(e.xyy*map1s(p+e.xyy)+e.yyx*map1s(p+e.yyx)+e.yxy*map1s(p+e.yxy)+e.xxx*map1s(p+e.xxx)); }
float march1(vec3 ro, vec3 rd, float tm, int n){
  float t=0.;
  for(int i=0;i<n;i++){ float d=map1(ro+rd*t); if(d<.0008*t) return t; t+=d*.9; if(t>tm) break; }
  return 1e5;
}
vec3 SUN1=normalize(vec3(.18,1.,.28)), CAM1;
float SUNI1;
vec3 CORE1;
// the oculi are rose windows of stained glass: the shafts and the light pools they throw are
// coloured per pane (6 inner + 12 outer panes, lead cames between them, a palette per window); the
// sun direction is jittered over its disc per frame, so the projected panes blur with distance (TAA)
vec3 SJ;   // the sun direction jittered across its disc for this pixel (world1 sets it)
vec3 beam(vec3 p){
  if(p.y>21.) return vec3(0);
  vec3 pp=p+SJ*(24.-p.y)/SUN1.y;
  vec2 o=mod(pp.xz,12.)-6.; float r=length(o);
  if(r>1.65) return vec3(0);   // outside the oculus: no light (and no atan, no hash)
  float a=atan(o.y,o.x)/6.2832+.5, rr=r*2.2, ns=rr<1.?6.:12.;
  float sg=floor(a*ns+floor(rr)*.5), h=h2(vec2(sg,floor(rr))+floor((pp.xz+6.)/12.)*7.);
  vec3 gc=h<.3?vec3(1.,.12,.06):h<.55?vec3(.12,.3,1.):h<.72?vec3(1.,.72,.12):h<.84?vec3(.15,.85,.3):h<.93?vec3(.7,.2,1.):vec3(1.,.95,.85);
  float ld=min(min(fract(rr),1.-fract(rr))/2.2,min(fract(a*ns+floor(rr)*.5),1.-fract(a*ns+floor(rr)*.5))/ns*6.2832*r);
  return smoothstep(1.65,1.25,r)*mix(gc,vec3(1.,.9,.8),.5)*smoothstep(.015,.04,ld)*1.4;   // pastel: light through old glass lands softer than the panes look
}
// neon colour per bay; with the riser of section 4 the tubes heat up to the core's orange, first
// near the core (S1.w is 0 in section 3)
vec3 neonc(vec3 p){
  float id=h2(floor(p.xz/12.)+vec2(3,1));
  return mix(id<.5?vec3(.15,.75,1.6):vec3(1.5,.18,.75),vec3(1.6,.5,.25),clamp(S1.w*(3.-(p.z+262.)*.02),0.,1.));
}
// neon intensity. Section 3: base + snare + a wave running down the hall.
// Section 4 (the breakdown): soft swells run down the hall and reach the core on every chord
// change, on every bar once the riser starts, on every beat in the last four seconds; dim near the
// viewer, brighter toward the core and as the riser builds
float neoni(vec3 p){
  if(SEC==3) return 1.5+5.*S0.y+3.*exp(-8.*abs(fract(p.z*.02+T*.5)-.5));
  float t=LT+(p.z+262.)*.03, y=fract(max(max(t*.25,t*.5-2.),t-8.)+.5)-.5;
  return smoothstep(20.,120.,CAM1.z-p.z)*(.4+.8*S1.w+(1.4+.6*S1.w)*exp(-12.*y*y));
}
// distance to the nearest vault rib (the neon tubes) - for the bounce light they throw
float ribD(vec3 p){ vec3 q=p; q.xz=mod(q.xz,12.)-6.; float gx=goth(q.zy,4.8,13.), gz=goth(q.xy,4.8,13.);
  return max(max(abs(gx-gz)*.7-.13,-(min(gx,gz)+.5)),13.-p.y); }
// nave stone: ashlar coursing (piers: around the shaft; walls/vault: box-mapped), per-block tint,
// recessed joints (MBN tilts the normal), grime toward the floor, water stains, mineral streaks and
// rising damp (not on the floor); floor: polished slabs
vec3 alb1(vec3 p, vec3 n, float m, float t){
  vec3 q=p; q.xz=mod(q.xz,12.)-6.; vec2 cq=abs(q.xz)-6.;
  float ft=smoothstep(20.,70.,t), lc=length(cq), fb=fbm(p*1.3,3), fp=t*2./U.z, fs=1.-smoothstep(.04,.12,fp), g=p.y+fbm(p*.4,2)*2., h=g+fb*.6*fs;
  vec2 u=m==2.?p.xz*vec2(.5,1.):lc<2.3&&p.y<13.?vec2(atan(cq.y,cq.x)*1.7,p.y*2.3):vec2(abs(n.x)>abs(n.z)?p.z:p.x,p.y)*vec2(1.1,2.3);
  // water stains run down from the capitals, fading as they go, and gather on the vault webs toward
  // the springing; pale mineral streaks between them: noise stretched along the height, its edges
  // broken by the grain of the stone, faded out where a streak would get narrower than a few pixels
  // (fp: the pixel footprint)
  float o=(min(p.y,26.-p.y)-12.)*.015, sn=nz(vec3(u.x*3.+p.x*.4,p.y*.15,p.z*.4))+fb*.35;
  vec3 s=mix(vec3(1),vec3(.55,.52,.5),smoothstep(.71,.85,sn+o)*fs*min(p.y*.1,1.))+.2*fs*smoothstep(.43,.33,sn-o);
  // rising damp: darker below a ragged tide line one to two metres up, patchy salts along it
  s*=mix(.7,1.,smoothstep(2.5-fp,2.7+fp,h))+.3*fb*smoothstep(.3+fp,0.,abs(h-2.8));
  float cr=floor(u.y); u.x+=cr*.5+h2(vec2(cr,5.))*.3;
  vec2 bi=vec2(floor(u.x),cr), f=fract(u);
  float jb=min(min(f.x,1.-f.x)*.5,min(f.y,1.-f.y)), hb=h2(bi+floor(p.xz/12.)*31.);
  MBN=(1.-ft)*vec2(smoothstep(.93,1.,f.x)-smoothstep(.07,0.,f.x),smoothstep(.93,1.,f.y)-smoothstep(.07,0.,f.y))*.5;
  vec3 a=vec3(.55,.52,.5)*(.62+.38*fb)*(.78+.4*hb)*mix(vec3(1),vec3(1.05,.98,.9),h2(bi+7.));
  a*=mix(.45,1.,mix(smoothstep(0.,.035,jb),1.,ft));
  return a*mix(.6,1.,smoothstep(0.,4.,g))*(m==2.?vec3(1):s);
}
vec3 surf1(vec3 p, vec3 n, vec3 rd, float m){
  MBN=vec2(0);
  vec3 alb=alb1(p,n,m,length(p-CAM1)), col=vec3(0);
  if(m!=2.){ vec3 tg=normalize(vec3(-n.z,0.,n.x)+1e-5); n=normalize(n+tg*MBN.x+vec3(0,MBN.y,0)); }
  float ni=neoni(p);
  if(m==1.) return neonc(p)*ni*2.+alb*.02;
  // ambient occlusion: 6 taps along the normal + one jittered cone ray (TAA integrates it)
  float ao=0.,s=1.;
  for(int i=1;i<=6;i++){ float h=float(i)*.4; ao+=(h-map1s(p+n*h))*s; s*=.65; }
  ao=clamp(1.-ao*.5,0.,1.);
  { vec3 d=cosdir(n,JIT,JIT2); float o=1., h=.3; for(int i=0;i<8;i++){ o=min(o,map1s(p+d*h)/h); h*=1.5; } ao*=.35+.65*clamp(o*1.5,0.,1.); }
  col+=alb*beam(p+n*.05)*max(dot(n,SUN1),0.)*vec3(5.,3.8,2.6)*SUNI1;
  // light bounced from the neon ribs and the lit floor
  vec3 q=p; q.xz=mod(q.xz,12.)-6.;
  float rd1=max(ribD(p+n*.6),.05);
  col+=alb*ao*(neonc(p)*ni*(.05*(.4+.6*smoothstep(8.,18.,p.y))+.5/(1.+rd1*rd1*.6))+vec3(.5,.4,.3)*SUNI1*.06*(.3+.7*n.y)+vec3(.012,.014,.02));
  // the core at the end of the hall
  vec3 L=CORE1-p; float ll=dot(L,L);
  col+=alb*vec3(1.6,.8,.5)*max(dot(n,L*inversesqrt(ll)),0.)*1800./ll*smoothstep(0.,10.,LT)*float(SEC==4)*(ao*.5+.5);
  if(m==2.){ // floor neon strips between piers
    vec2 f=abs(q.xz);
    col+=neonc(p)*ni*.8*smoothstep(.06,0.,abs(min(f.x,f.y)-5.6))*step(max(f.x,f.y),4.2);
  }
  return col;
}
vec3 world1(vec3 ro, vec3 rd, out float dep){
  CAM1=ro; SJ=normalize(SUN1+vec3(JIT-.5,0,JIT2-.5)*.006);
  SUNI1=SEC==3?1.:.6*(1.-smoothstep(0.,6.,LT))+.04;
  CORE1=vec3(6.,9.,-262.);
  float t=march1(ro,rd,260.,300);
  vec3 col=vec3(0);
  if(t<1e4){
    vec3 p=ro+rd*t; float m=MAT1; vec3 n=nor1(p);
    col=surf1(p,n,rd,m);
    if(m==2.){ // wet polished floor: one reflection bounce
      // three states: dry polished slab, damp film, standing puddles (mirror, darker floor
      // under the water, rings from drips off the vault spreading and fading); the reflected ray also
      // gathers the coloured shafts, so the light beams show up in the puddles
      vec2 tl=fract(p.xz*vec2(.5,1.));
      float rough=smoothstep(.0,.03,min(min(tl.x,1.-tl.x)*.5,min(tl.y,1.-tl.y))), wf=fbm(p*.35,3), wet=smoothstep(.35,.65,wf), pud=smoothstep(.6,.66,wf);
      vec2 g=p.xz*.7, gi=floor(g), rp=vec2(0);
      for(int k=0;k<4;k++){ vec2 c=gi+vec2(k&1,k>>1); float ph=fract(T*.45+h2(c)); vec2 dv=g-c-vec2(h2(c+1.),h2(c+2.)); float dd=length(dv), w=ph*1.3;
        rp+=dv/max(dd,1e-3)*sin((dd-w)*45.)*exp(-abs(dd-w)*14.)*(1.-ph)*step(.4,h2(c+5.)); }
      vec3 rr=reflect(rd,normalize(n+vec3(nz(p*4.)-.5,0.,nz(p*4.+9.)-.5)*mix(.04,.006,wet)*(1.-pud)+vec3(rp.x,0,rp.y)*.12*pud+(vec3(JIT,0,JIT2)-.5)*.01*(1.-pud)));
      float t2=march1(p+n*.02,rr,140.,200);
      vec3 rc=vec3(0);
      if(t2<1e4){ vec3 p2=p+rr*t2; float m2=MAT1; rc=surf1(p2,nor1(p2),rr,m2); }
      { float tm=min(t2,60.), dt=tm/40., ts=dt*JIT2; vec3 a=vec3(0);
        for(int i=0;i<40;i++){ a+=beam(p+rr*ts)*dt; ts+=dt; }
        rc+=a*vec3(1.,.78,.55)*SUNI1*.085*(.3+hg(dot(rr,SUN1),.5)*8.)*.7; }
      float fr=(.06+.7*pow(1.-max(dot(-rd,n),0.),4.))*mix(.6,1.,wet);
      col=col*mix(.6,.4,wet)*(1.-.5*pud)+rc*fr*mix(rough,1.,pud);
    }
  } else t=1e4;
  dep=min(t,1e4);
  // volumetric light shafts through the oculi + haze
  vec3 acc=vec3(0), ne=vec3(0); float tm=min(t,150.), dt=tm/200., ts=dt*JIT;
  for(int i=0;i<200;i++){
    vec3 p=ro+rd*ts;
    float dn=nz(p*.7+vec3(0,T*.4,T*.2));
    acc+=beam(p)*(.15+1.6*dn*dn)*dt;   // coloured by the glass
    if((i&3)==0) ne+=neonc(p)*neoni(p)*smoothstep(8.,18.,p.y)*(.3+dn)*dt;   // the vault's air glows with the neon of its bay (every 4th step)
    ts+=dt;
  }
  col+=ne*.06;
  // dust: one mote per half-metre cell, drifting slowly through the air, glinting where a window's beam
  // crosses it (fixed in space, about two pixels wide; a cell visited twice adds its mote once)
  vec3 pc=vec3(-9);
  for(int i=1;i<25;i++){
    vec3 dr=vec3(0,T*.03,0), ce=floor((ro+rd*float(i)*.4+dr)*2.)+.5;   // (the air settles slowly)
    if(ce==pc) continue; pc=ce;
    float hm=h3(ce+9.);   // each mote wanders on its own; most are faint, a few glint
    vec3 mp=(ce+(vec3(h3(ce),h3(ce+3.),h3(ce+7.))-.5)*.8)*.5-dr+sin(vec3(.4,.3,.5)*T+hm*30.)*.05, v=mp-ro; float al=dot(v,rd), w=al*1.6/U.z;
    if(al>0.&&al<t) col+=beam(mp)*exp(-dot(v-rd*al,v-rd*al)/(w*w))*hm*hm*hm*SUNI1*2.4;
  }
  float cs=dot(rd,SUN1);
  col+=acc*vec3(1.,.78,.55)*SUNI1*.085*(.3+hg(cs,.5)*8.);
  col=mix(col,vec3(.02,.025,.04),1.-exp(-t*.006));
  // analytic in-scattering from the core (point light in homogeneous fog)
  if(SEC==4){
    vec3 L=CORE1-ro; float b=dot(L,rd), h=length(L-rd*b)+.01;
    float g=(atan((min(t,1e4)-b)/h)+atan(b/h))/h;
    float k=smoothstep(0.,10.,LT)*(.8+.2*neoni(CORE1));   // the core swells as each wave of light reaches it
    col+=vec3(1.4,.6,.45)*(1.-exp(-g*k*.15))*.9;
    col+=vec3(1.,.8,.7)*k*12.*smoothstep(.9996,.99995,dot(rd,normalize(L)))*step(length(L),t+5.);
  }
  return col;
}

// =====================================================================
// WORLD 2 : the core - a living fractal heart inside particle rings
// =====================================================================
vec4 TRAP; float POW;
float bulb(vec3 p, int n){
  vec3 w=p; float m=dot(w,w), dz=1.; TRAP=vec4(abs(w),m);
  for(int i=0;i<n;i++){   // 9 iterations: fine surface structure (4 for the light rays)
    float r=sqrt(m), pr=pow(r,POW-1.), b=POW*acos(clamp(w.y/r,-1.,1.)), a=POW*atan(w.x,w.z);
    dz=POW*pr*dz+1.;
    w=p+pr*r*vec3(sin(b)*sin(a),cos(b),sin(b)*cos(a));
    TRAP=min(TRAP,vec4(abs(w),m));
    m=dot(w,w);
    if(m>256.) break;
  }
  return .25*log(m)*sqrt(m)/dz;
}
float LB;
float ring(vec3 p, float R, float n, float sp, float sd){
  float lb=max(abs(p.y)-.08,abs(length(p.xz)-R)-.15)-.007;   // lower bound: a ring out of reach costs nothing
  if(lb>LB) return lb;
  float seg=2.*PI/n, a=atan(p.z,p.x)+T*sp, id=floor(a/seg), ac=(id+.5)*seg-T*sp;
  float rr=R+(h2(vec2(id,sd))-.5)*.3, yy=(h2(vec2(id,sd+50.))-.5)*.16;
  return length(p-vec3(cos(ac)*rr,yy,sin(ac)*rr))-.003-.004*h2(vec2(id,sd+9.));
}
float parts(vec3 p){
  vec3 a=p; a.xy*=rot(.35); a.yz*=rot(.2);
  vec3 b=p; b.yz*=rot(1.1); b.xy*=rot(-.3);
  vec3 c=p; c.xz*=rot(.8); c.xy*=rot(1.4);
  return min(min(ring(a,1.65,220.,.25,1.),ring(b,2.05,260.,-.18,2.)),ring(c,2.5,300.,.12,3.));
}
mat3 RB; float PD;
float map2(vec3 p, out float m){
  float sc=1.+.04*S0.x;
  float d=bulb(RB*p/sc,9)*sc; LB=clamp(d,.1,.3);   // nothing can tell ring distances above this apart
  float q=parts(p); PD=q;
  m=q<d?1.:0.;
  return min(d,q);
}
vec3 world2(vec3 ro, vec3 rd, out float dep){
  POW=8.+1.2*sin(LT*.3);
  float ca=cos(T*.15),sa=sin(T*.15);
  RB=mat3(ca,0,sa, 0,1,0, -sa,0,ca);
  float t=0., m=0., gl=0., hr=0., mind=1e9, ep=.14/U.z, sc=1.+.04*S0.x; bool hit=false;   // hit threshold = ~1/3 pixel at any resolution
  // shockwave: a wispy shell of light bursts out of the heart on the drop and at every chord change (every
  // 4 s), its front torn by noise; it fades out by r = 1.8, well inside the camera's orbit, and dims as the camera closes
  // in (same brightness on screen: no flash; sv: glow gathered by the march)
  float sd=mod(LT,4.), sR=.6+sd*.7, sf=smoothstep(0.,.2,sd)*smoothstep(1.8,1.,sR)*dot(ro,ro)*.083, sv=0.;   // (starts inside: it sweeps out through the surface)
  for(int i=0;i<360;i++){
    vec3 p=ro+rd*t;
    float mm; float d=map2(p,mm), st=min(d*.9,.15), rp=length(p);
    gl+=exp(-PD*160.)*.012;
    // the heart's light streams out through the shell: lit where the shell is open in this direction from the centre
    if(rp>.9&&rp<2.4&&d>.02) hr+=smoothstep(0.,.03,bulb(RB*p*(.82/rp)/sc,4))*exp(-rp*1.6)*st;   // (not in the tiny steps along the surface: cheap)
    float sq=(rp-sR)/(.12+.1*sd);
    if(abs(sq)<3.){ sq+=(nz(p*3.+T)-.5)*2.; sv+=exp(-sq*sq)*st*pow(1.-abs(dot(rd,p/rp)),3.); }   // bright at its rim, faint face-on
    mind=min(mind,d);
    if(d<ep*t){ hit=true; m=mm; break; }
    t+=st;
    if(t>9.) break;
  }
  // background: violet haze and the core glow
  vec3 pc=-ro; float b=dot(pc,rd), h=length(pc-rd*b);
  vec3 col=vec3(.012,.006,.02)+vec3(.9,.25,.6)*.04/(h*h+.06)*(.6+.8*S0.x)+vec3(1.,.5,.2)*.012/(h*h+.01);
  col+=vec3(.3,.2,.6)*pow(fbm(rd*6.+T*.05,4),3.)*.6;
  dep=1e3;
  if(hit){
    vec3 p=ro+rd*t;
    if(m==1.) col=vec3(1.,.75,.55)*3.;
    else {
      vec2 e=vec2(ep*t,-ep*t)*1.5; float x;
      vec3 n=normalize(e.xyy*map2(p+e.xyy,x)+e.yyx*map2(p+e.yyx,x)+e.yxy*map2(p+e.yxy,x)+e.xxx*map2(p+e.xxx,x));
      vec4 tr=TRAP;   // (the orbit trap of the normal's last sample, a third of a pixel off)
      vec3 alb=mix(vec3(.12,.08,.07),vec3(.7,.45,.3),clamp(tr.y*1.2,0.,1.));
      alb=mix(alb,vec3(.05,.12,.2),clamp(tr.z*tr.z,0.,1.));
      // occlusion: orbit trap x 5 distance probes along the normal (crevices go properly dark)
      float ao=pow(clamp(tr.w*1.1,0.,1.),2.), oc=0.;
      for(int k=1;k<6;k++){ float hk=.012*float(k*k), x3; oc+=(hk-map2(p+n*hk,x3))/hk/float(k); }
      ao*=clamp(1.-oc*.45,0.,1.);
      // translucency: probe .025 inside along the view ray - thin membranes are lit from behind
      float ss=smoothstep(0.,.02,bulb(RB*(p+rd*.025)/(1.+.04*S0.x),9));
      vec3 l1=normalize(vec3(.6,.8,-.3)), l2=normalize(vec3(-.7,-.2,.6));
      float sh=1., st=.01;
      for(int i=0;i<64;i++){ float x2; float hh=map2(p+n*.002+l1*st,x2); sh=min(sh,16.*hh/st); st+=clamp(hh,.004,.15); if(sh<.01||st>3.) break; }
      sh=clamp(sh,0.,1.);
      col=alb*(vec3(2.4,1.8,1.3)*max(dot(n,l1),0.)*sh+vec3(.5,.2,.8)*max(dot(n,l2),0.)*.6+vec3(.08,.06,.1)*ao);
      col+=pow(clamp(1.+dot(rd,n),0.,1.),4.)*vec3(.9,.4,1.)*.6*ao;
      col+=ss*vec3(1.,.32,.18)*(.4+.6*S0.x)*(.3+.7*ao);   // subsurface glow of the thin walls
      // the heart: magma light from the depths of the bulb, seen through its openings, swelling with the kick and the bass
      col+=vec3(1.,.28,.07)*smoothstep(.85,.5,length(p))*(.3+1.4*S0.x+.9*S1.x);
      // veins carry pulses of light outward from the centre
      float vein=smoothstep(.012,.0,tr.x)*smoothstep(.2,.5,tr.w)*(.5+pow(.5+.5*sin(length(p)*14.-T*7.),6.)*2.);
      col+=vein*vec3(1.6,.5,.25)*(.4+3.*S1.x+2.*S0.x);
      float sq=(length(p)-sR)/.06+nz(p*3.+T)-.5;
      col+=(alb*.9+.05)*vec3(1.,.55,.25)*exp(-sq*sq)*sf;   // the shockwave lights the surface as it passes
    }
    dep=t;
  }
  col+=gl*vec3(1.,.55,.35)*(1.+S0.w*2.)+hr*vec3(1.,.3,.08)*(.6+2.4*S0.x+1.5*S1.x);
  if(!hit) col+=vec3(1.,.4,.7)*exp(-mind*40.)*.15*S0.x;   // kick haze around the surface, not over it
  col+=vec3(1.,.5,.22)*sv*sf*.4;   // the shockwave (no brighter: it must not flash the frame)
  return col;
}

float ringHit(vec3 ro, vec3 rd, mat3 F, float R, float hh, out vec3 lp){
  vec3 o=ro*F, d=rd*F;
  float a=dot(d.xz,d.xz), b=dot(o.xz,d.xz), c=dot(o.xz,o.xz)-R*R, h=b*b-a*c;
  if(h<0.) return 1e5;
  h=sqrt(h);
  float t=(-b-h)/a; lp=o+d*t;
  if(t>0.&&abs(lp.y)<hh) return t;
  t=(-b+h)/a; lp=o+d*t;
  if(t>0.&&abs(lp.y)<hh) return t;
  return 1e5;
}
mat3 rframe(float ax, float az){ mat3 m=mat3(1); m[1]=normalize(vec3(sin(az),cos(az)*cos(ax),cos(az)*sin(ax))); m[0]=normalize(cross(m[1],vec3(0,0,1))); m[2]=cross(m[0],m[1]); return m; }
vec4 seg(vec3 ro, vec3 rd, vec3 a, vec3 b){
  vec3 ba=b-a, oa=ro-a; float bb=dot(ba,ba), bd=dot(ba,rd), bo=dot(ba,oa), dd=dot(rd,oa);
  float den=bb-bd*bd, s=clamp((bo-bd*dd)/max(den,1e-6),0.,1.);
  vec3 p=a+ba*s; float t=max(dot(p-ro,rd),0.);
  return vec4(length(ro+rd*t-p),t,s,0.);
}
float glw(float d, float t, float k){ float w=max(.003,t*1.4/U.z); return exp(-d*d/(w*w))*k*.008/w+exp(-d/(w*10.))*k*.04; }
// =====================================================================
// WORLD 3 : orbit - ringed home world (baked 4K map), atmosphere, the spire
// =====================================================================
const float RA=1.035, HR=.0065, HM_=.0016;
const vec3 BR=vec3(5.5,13.,22.4)*2.5, BM=vec3(18.);
vec3 atmo(vec3 ro, vec3 rd, float tmax, vec3 sun, out vec3 tr){
  tr=vec3(1);
  vec2 a=sph(ro,rd,RA); if(a.y<0.) return vec3(0);
  float t0=max(a.x,0.), t1=min(a.y,tmax), dt=(t1-t0)/40.;
  vec3 sR=vec3(0), sM=vec3(0), em=vec3(0); float oR=0., oM=0.;
  float mu=dot(rd,sun), g=.8;
  float pR=.0597*(1.+mu*mu), pM=.1194*((1.-g*g)*(1.+mu*mu))/((2.+g*g)*pow(1.+g*g-2.*g*mu,1.5));
  for(int i=0;i<40;i++){
    vec3 p=ro+rd*(t0+dt*(float(i)+JIT)); float h=length(p)-1.;
    float dR=exp(-h/HR)*dt, dM=exp(-h/HM_)*dt; oR+=dR; oM+=dM;
    vec2 b=sph(p,sun,RA); float lt=b.y/6., lR=0., lM=0., b0=dot(p,sun), sf=b0>0.?1.:smoothstep(.97,1.025,length(p-sun*b0)); bool sh=sf<=0.;
    // night side emission: the green airglow shell (a thin limb line) and volumetric aurora
    // curtains around both magnetic poles (rays of light hanging 0.008-0.03 above the ground)
    vec3 q=p/(h+1.); q.xz*=rot(T*.01); float la=abs(asin(q.y)), lo=atan(q.z,q.x);
    float cu=smoothstep(.07,0.,abs(la-1.13-.05*sin(lo*4.+T*.2)-.02*sin(lo*13.-T*.5)))*smoothstep(.007,.011,h)*smoothstep(.032,.014,h);
    em+=(vec3(.3,1.,.4)*exp(-pow((h-.016)/.0017,2.))*.25
        +mix(vec3(.15,1.,.4),vec3(.8,.15,.6),smoothstep(.012,.026,h))*cu*(.25+.75*pow(nz(vec3(lo*70.,T*.7,h*90.)),2.))*12.)*dt*(sh?1.:.05);
    if(!sh){
      for(int j=0;j<6;j++){ float hq=max(length(p+sun*lt*(float(j)+.5))-1.,0.); lR+=exp(-hq/HR)*lt; lM+=exp(-hq/HM_)*lt; }
      // grey attenuation (no orange/green bands at the limb); a soft twilight edge reddened past the
      // terminator, and a dim multiple-scattering fill so the lit limb doesn't end in a hard line
      vec3 att=vec3(exp(-(BR.y*(oR+lR)+BM.x*1.1*(oM+lM))))*mix(vec3(1.,.42,.2),vec3(1),sf)*sf+exp(-BR.y*oR)*.04*sf;
      sR+=att*dR; sM+=att*dM;
    }
  }
  tr=exp(-(BR*oR+BM*1.1*oM));
  return 30.*sR*BR*pR+60.*sM*BM*pM+em*exp(-BR.y*oR);
}
vec3 RN=normalize(vec3(.12,1.,.18));
vec3 EL=normalize(vec3(-.25,.55,.8));
float ringDen(float r){
  return smoothstep(1.3,1.42,r)*smoothstep(2.45,2.2,r)*pow(nz(vec3(r*35.,0,0)),1.5)*(.3+.7*nz(vec3(r*170.,3,0)))*smoothstep(.015,.05,abs(r-1.88))*smoothstep(.01,.03,abs(r-2.12));
}
// light reaching p past the planet: a soft, neutral penumbra (no colour fringe)
vec3 pshadow(vec3 p, vec3 sun){
  float b=dot(p,sun); if(b>0.) return vec3(1);
  return vec3(max(smoothstep(.99,1.035,length(p-sun*b)),.02));
}
vec4 pmap(vec3 q, float lod){ return textureLod(PM,vec2(atan(q.z,q.x)/6.2832+.5,asin(clamp(q.y,-1.,1.))/PI+.5),lod); }
// the cloud layer is a shell (steps between 1 and 1+CH): tops rise with the
// coverage + a Worley octave (relief), the twilight band reddens them at the terminator, the cities
// light their bases from below, a dim moon/ring-shine silvers the tops; the ground is seen through it
const float CH=.009; vec3 MOO=normalize(vec3(.35,.62,.7));
// cloud cover (planet frame). The baked map holds the regimes (fronts, ITCZ clusters, trade
// cumulus, west-coast stratocumulus decks); zonal winds shear it by latitude. Three tropical cyclones
// are built in spiral coordinates: noise sampled on a cylinder around the log-spiral angle, so the
// feathered rain bands streak along the arms; a central dense overcast with a clear eye, the
// eyewall and CDO carry tall convective towers (CT), and an anticyclonic cirrus outflow (CI) fans out
// on top. CI also carries the jet-stream cirrus streaks of the storm belts.
float CI, CT;
float ccov(vec3 q, float ft, float mw){
  vec3 r=q; r.xz*=rot((T-100.)*.004*cos(q.y*7.));
  vec4 M=pmap(r,log2(max(ft*mw/6.2832,1.))); float cv=M.z, lw=log2(max(ft*3000.,1.));
  // trade cumulus over the subtropical sea: sub-texel speckle, generated here (mip-filtered, so it
  // greys out into a faint veil when sub-pixel instead of aliasing into blobs)
  cv=max(cv,smoothstep(.5,.85,textureLod(W,r*130.,lw+2.4).g)*smoothstep(.08,.3,abs(q.y))*smoothstep(.8,.45,abs(q.y))*smoothstep(.5,.49,M.x)*smoothstep(.35,.6,nz(r*20.))*.55);
  CI=smoothstep(.45,.8,textureLod(W,vec3(r.x*1.5,r.y*24.,r.z*1.5)+nz(r*9.)*.4,lw).g)*smoothstep(.3,.5,abs(q.y))*smoothstep(1.05,.75,abs(q.y))*smoothstep(.15,.5,cv);
  CT=smoothstep(.55,.9,cv)*textureLod(W,r*40.,lw-3.).g;
  for(int k=0;k<3;k++){
    float fk=float(k); vec3 c=normalize(vec3(cos(fk*2.4+.5),k==1?-.36:.3+fk*.04,sin(fk*2.4+.5)));
    vec3 u=normalize(cross(c,vec3(0,1,0))), v=cross(u,c); vec2 l=vec2(dot(r,u),dot(r,v)); float d=length(l)/.14;
    if(dot(r,c)>0.&&d<2.2){
      float a=atan(l.y,l.x)*sign(c.y), ld=log(d+.02), sp=a+ld*2.4-T*.12, so=-a+ld*1.1+T*.05;
      vec3 sc=vec3(cos(sp),sin(sp),ld*1.6+fk*3.);
      float bn=textureLod(W,sc*.45,0.).g*.6+textureLod(W,sc*1.4+3.,0.).g*.4;
      float ot=textureLod(W,r*90.,max(lw-6.,0.)).g, arm=smoothstep(.3,.75,.5+.5*sin(sp*2.)+(bn-.5)*1.5), eye=smoothstep(.025,.075,d+(bn-.5)*.04);
      float cdo=smoothstep(.75,.25,d+(bn-.5)*.9), ew=exp(-pow((d-.1)/.05,2.));
      cv=mix(cv,max(cdo*(.75+.35*bn+.25*ot),arm*smoothstep(1.7,.9,d)*(.45+.7*bn))*eye,smoothstep(1.7,1.1,d));
      CT=max(CT*smoothstep(.6,1.4,d),(ew*1.3+cdo*(bn*.4+ot*ot*.9))*eye);   // eyewall + overshooting tops in the CDO
      CI=max(CI,smoothstep(.45,.8,textureLod(W,vec3(cos(so),sin(so),ld*5.)*.7,0.).g)*smoothstep(.5,1.,d)*smoothstep(2.2,1.4,d)*.8);
    }
  }
  return cv+(textureLod(W,r*1.6+vec3(T*.0001,0,0),log2(max(ft*205.,1.))).g-.5)*.6*smoothstep(.05,.5,cv)
           +(textureLod(W,r*55.+.3,lw+1.2).g-.5)*.55*smoothstep(.1,.6,cv)   // mid-scale texture on the decks
           +(nz(r*420.+vec3(T*.02,0,0))-.5)*.3*smoothstep(1.,.3,ft*420.);
}
// cloud shell, 28 jittered steps: low/mid cloud up to a top height that grows with the cover (thin
// decks, tall towers), a thin cirrus veil near the shell top. Light: one tap toward the sun at the height
// the sun ray has climbed half-way to the top - towers shade the deck beside them, shadows lengthen to
// the terminator - and depth below the local top darkens the bases (bright tops, grey flanks)
vec3 cshell(vec3 ro, vec3 rd, float tp, vec3 sun, vec3 col){
  vec2 s=sph(ro,rd,1.+CH); if(s.y<0.) return col;
  float t0=max(s.x,0.), dt=(min(s.y,tp)-t0)/28., t=t0+dt*JIT, tr=1., mw=float(textureSize(PM,0).x); vec3 ac=vec3(0), sq=sun; sq.xz*=rot(T*.01);
  for(int i=0;i<28;i++){
    vec3 p=ro+rd*t, n=normalize(p), q=n; q.xz*=rot(T*.01);
    float hq=(length(p)-1.)/CH, ft=t*2./(1.6*U.z);
    float cv=ccov(q,ft,mw), top=cv*(.5+.4*cv)+CT*.45, dl=top-hq*.85-.1;
    float de=smoothstep(0.,.1,dl)*smoothstep(0.,.1,hq), dc=CI*smoothstep(.72,.8,hq)*smoothstep(1.,.92,hq)*.2;
    if(de+dc>.001){
      float mu=dot(n,sun)+hq*.025, nt=smoothstep(.06,-.14,mu), hm=hq+(1.-hq)*.5;
      float c2=ccov(normalize(q+sq*CH*(1.-hq)*.5/max(mu,.06)),ft,mw), sh=exp(-max(c2*(.5+.4*c2)+CT*.45-hm*.85-.1,0.)*6.);
      vec3 L=mix(vec3(1.,.38,.15),vec3(1.,.97,.93),smoothstep(0.,.3,mu))*(max(mu,0.)+.025*smoothstep(-.06,.04,mu))*2.3*pshadow(p,sun)*(sh*exp(-max(dl,0.)*3.5)+.1)
            +vec3(.3,.45,.8)*.12*(.3+.7*hq)*smoothstep(-.1,.2,mu)
            +(vec3(1.,.52,.22)*pmap(q,log2(max(ft*mw/6.2832,1.))).y*exp(-hq*2.)*2.5+vec3(.45,.55,.8)*.2*(.2+.8*hq)*max(dot(n,MOO)+.5,0.))*nt;
      float a=1.-exp(-(de+dc)*dt*800.);
      ac+=tr*a*L; tr*=1.-a;
      if(tr<.01) break;
    }
    t+=dt;
  }
  return col*tr+ac;
}
// ring station (local units: habitat torus radius 1): the torus, six spokes and the hub spin; the
// despun mast and its four solar wings hold still. SM: the habitat's distance (windows), SN: the wings'
float SM, SN;
float stn(vec3 p){
  vec3 q=p; q.xz*=rot(T*.15);
  float r=length(q.xz), b=mod(atan(q.z,q.x),1.0472)-.5236;
  vec3 w=abs(vec3(p.x,abs(p.y)-.78,abs(p.z)-.37))-vec3(.004,.11,.29);
  SM=length(vec2(r-1.,q.y))-.075; SN=max(w.x,max(w.y,w.z));
  return min(min(min(SM,SN),max(length(p.xz)-.018,abs(p.y)-.92)),min(max(length(vec2(r*sin(b),q.y))-.02,r-1.),length(vec2(r,max(abs(q.y)-.2,0.)))-.15));
}
vec3 FLS=vec3(0);
float ringHitS(vec3 ro, vec3 rd, mat3 F, float R, float hh, out vec3 lp);   // (the finale's habitat rings, below)
vec3 ringCol0(vec3 lp, vec3 rd, mat3 F, float R, float hh, float sp, float t);
vec3 world3(vec3 ro, vec3 rd, out float dep){
  vec3 sun=normalize(SEC==8?vec3(-1.,.1,.05):vec3(-.75,.14,.65));   // section 7: a growing lit crescent and terminator; section 8: the eclipse
  float sd=max(dot(rd,sun),0.);
  vec3 col=stars(rd)*(1.-smoothstep(.97,1.,sd));
  col+=vec3(30.,26.,22.)*smoothstep(.99985,.9999,sd)+vec3(1.,.85,.7)*(pow(sd,600.)*4.+pow(sd,40.)*.25+pow(sd,6.)*.04);
  {  // the home star already wears a Dyson swarm: four tilted families of four nested collector rings,
     // some still being built (a gap), panels glinting as they turn
    vec3 su=normalize(cross(sun,vec3(0,1,0))), sv=cross(su,sun);
    vec2 a=vec2(dot(rd,su),dot(rd,sv))/max(dot(rd,sun),.1);
    for(int k=0;k<8;k++){   // hair-thin rings of countless collectors in the star's light, glints running along them
      float fk=float(k), rk=.04+.017*fk;
      vec2 b=a*rot(fk*.3+T*.004);
      b.y/=cos(.75+.12*sin(fk*2.));
      float an=atan(b.y,b.x), d=abs(length(b)-rk)*U.z;   // (about pixels from the ring)
      float gl=(exp(-d*d)*.8+exp(-d*.15)*.08)*(.5+3.*step(.97,h2(vec2(floor(an*700.+T*(.3+fk*.07)),fk))))*step(h2(vec2(fk,7.))*.4,fract(an/6.2832+fk*.37));
      if(b.y<0.||length(a)>.0175) col+=vec3(1.,.8,.55)*gl*(b.y>0.?1.:.5);   // the far halves show their sunlit faces
    }
  }
  vec2 ps=sph(ro,rd,1.);
  float tp=ps.x>0.?ps.x:1e5;
  float tR=-dot(ro,RN)/dot(rd,RN); vec4 rc=vec4(0);
  if(tR>0.){
    vec3 p=ro+rd*tR; float r=length(p);
    if(r>1.3&&r<2.45){
      // ringlets: sharp bands of random opacity, faded to their mean below the radial pixel footprint
      float fw=hg(dot(rd,sun),.6)*6., rl=mix(.5+h2(vec2(floor(r*260.),5.)),1.,smoothstep(.0012,.0045,tR*1.25/U.z/max(abs(dot(rd,RN)),.05)));
      rc=vec4(vec3(.95,.82,.66)*(.12+fw)*pshadow(p,sun)*.9,min(ringDen(r)*.95*rl,1.));
    }
  }
  dep=1e4;
  if(tp<1e4){
    vec3 p=ro+rd*tp, n=p;
    vec3 q=n; q.xz*=rot(T*.01);
    float foot=tp*2./(1.6*U.z), mw=float(textureSize(PM,0).x), lod=log2(max(foot*mw/6.2832,1.));
    vec4 M=pmap(q,max(lod-.7,0.));
    float e=M.x, land=smoothstep(.498,.506,e), lat=asin(clamp(q.y,-1.,1.));
    // relief from the elevation map
    vec3 tu=normalize(cross(vec3(0,1,0),q)), tv=cross(q,tu); float dd=max(foot*1.5,3./mw);
    float ex=pmap(normalize(q+tu*dd),lod).x, ey=pmap(normalize(q+tv*dd),lod).x;
    vec3 nb=normalize(q-(tu*(ex-e)+tv*(ey-e))/dd*.06*land); nb.xz*=rot(-T*.01);
    float ice=smoothstep(1.12,1.25,abs(lat)+(e-.5)*.8+(nz(q*160.)-.5)*.12);   // ragged pack-ice edge
    // ocean: depth colour, zonal currents/eddies, turquoise shelves, tropical reefs, coastal silt
    float shf=smoothstep(.47,.5,e)*(1.-land), tro=smoothstep(.5,.1,abs(lat));
    vec3 alb=mix(vec3(.002,.01,.03),vec3(.006,.03,.07),smoothstep(.34,.48,e))*(.65+.7*nz(vec3(q.x*9.,q.y*30.,q.z*9.)+nz(q*5.)*2.));
    alb=mix(alb,vec3(.012,.09,.1),shf);
    alb=mix(alb,vec3(.06,.25,.22),shf*tro*smoothstep(.55,.72,nz(q*320.)));
    alb=mix(alb,vec3(.045,.06,.03),smoothstep(.49,.5,e)*(1.-land)*nz(q*110.));
    // phytoplankton blooms: teal-green filaments wound into mesoscale eddies (warped noise), on the
    // shelves and in the cool mid/high-latitude seas
    vec3 ew=q*34.+vec3(nz(q*7.),nz(q*7.+5.),nz(q*7.+9.))*4.;
    alb=mix(alb,mix(vec3(.01,.06,.055),vec3(.03,.1,.07),nz(ew*3.)),smoothstep(.5,.8,nz(ew))*max(shf,smoothstep(.55,.8,abs(lat))*smoothstep(1.15,.95,abs(lat)))*(1.-land)*.8);
    // land: ochre/red deserts, savanna -> forest by moisture, tropical rainforest, tundra
    vec3 la=mix(mix(vec3(.4,.24,.11),vec3(.42,.36,.22),nz(q*45.)),vec3(.07,.12,.04),smoothstep(.3,.6,M.w));
    la=mix(la,vec3(.025,.06,.025),smoothstep(.62,.85,M.w)*tro);
    la=mix(la,vec3(.17,.16,.12),smoothstep(.8,1.05,abs(lat)));
    la=mix(la,vec3(.24,.22,.2),smoothstep(.58,.68,e));
    la=mix(la,vec3(.85,.88,.9),smoothstep(.7,.76,e));
    alb=mix(alb,la,land);
    alb=mix(alb,vec3(.85,.9,.95),ice);
    float mu=dot(n,sun), dif=max(dot(nb,sun),0.);
    vec3 sl=vec3(1.,.92,.85)*exp(-BR*.0055/max(mu,.06));   // sunlight reddens through the air toward the terminator
    vec3 sq=sun; sq.xz*=rot(T*.01);
    float cs=clamp(ccov(normalize(q+sq*.008/max(mu,.15)),foot,mw)-.25,0.,1.);   // cloud shadows (the moving, spiralling cover)
    // ring shadow on the surface
    float trn=-dot(p,RN)/dot(sun,RN), rs=trn>0.?1.-.8*ringDen(length(p+sun*trn)):1.;
    col=alb*dif*sl*3.2*(1.-cs*.55)*rs;
    // sea surface: wind-roughness patches (calm slicks mirror-sharp, rough seas a wide glitter)
    float wv=nz(q*900.+T*.3), rg=smoothstep(.3,.7,nz(q*16.+vec3(T*.02,0,0))*.55+nz(vec3(q.x*70.,q.y*260.,q.z*70.))*.45);   // + streaky wind slicks: a mottled glint
    float night=smoothstep(.06,-.14,mu);
    // sun glint, and on the night side the moon's glint and moonlit relief (the map's normals)
    // sunglint: Beckmann lobe on the half vector (slope variance from the wind patches) with Fresnel:
    // the broad, textured glint patch of orbital photos; plus the Fresnel sky reflection toward the limb
    vec3 nw=normalize(n+(vec3(wv,nz(q*900.+17.),nz(q*870.+31.))-.5)*mix(.01,.05,rg)), hv=normalize(sun-rd);
    float m2=mix(.003,.035,rg), nh=max(dot(nw,hv),.01), nm=max(dot(nw,normalize(MOO-rd)),.01), fre=.02+.98*pow(1.-max(dot(-rd,nw),0.),5.);
    col+=(1.-land)*(1.-ice)*(exp(-(1.-nh*nh)/(nh*nh*m2))/(m2*nh*nh*nh*nh)*fre*.9/max(dot(-rd,n),.3)*sl*rs*(1.-cs*.8)*smoothstep(0.,.25,mu)
         +fre*vec3(.12,.25,.55)*smoothstep(-.1,.3,mu)*.6
         +night*vec3(.6,.7,1.)*exp(-(1.-nm*nm)/(nm*nm*m2))/(m2*nm*nm*nm*nm)*fre*.1);   // the moon's glint: the same smooth lobe, dimmer
    col+=night*alb*vec3(.45,.55,.8)*max(dot(nb,MOO),0.)*.12;
    // city lights: sodium, warm-white and cool LED districts; denser along coasts and low valleys
    vec3 city=mix(vec3(1.,.45,.12),mix(vec3(1.,.75,.45),vec3(.75,.85,1.),step(.6,h3(floor(q*250.)+3.))),step(.45,h3(floor(q*120.))))
             *M.y*(.55+1.3*smoothstep(.025,0.,e-.5)*land);
    // fine street grids inside the city glow (fade out when sub-pixel)
    // sub-texel sparkle of individual lights inside the (supersampled) street network, faded when sub-pixel
    float fp=foot*9000.;
    city*=mix(.4+1.4*step(.55,h3(floor(q*9000.))),1.,smoothstep(.4,1.2,fp));
    // not soft blobs: a city is a cluster of separate towns and districts (16 km cells), its glow falls off
    // sharply; faded to the mean where the cells get smaller than a pixel
    city*=M.y*mix(smoothstep(.5,.75,nz(q*400.))*2.4+.1,.7,smoothstep(.5,1.5,foot*400.))*1.6;
    // ring-shine on the night side: the lit rings show oceans, coasts and cloud tops in silver-blue
    vec3 rshine=vec3(.55,.62,.8)*(.04+.1*smoothstep(-.2,.6,dot(n,RN)*sign(dot(sun,RN))*-1.+.3));
    // night sea: fishing fleets (clusters of bright boat lights off the coasts) and a faint moonlit current texture
    float sea=(1.-land)*(1.-ice), fc=smoothstep(.62,.8,nz(q*40.+vec3(0,T*.01,0)))*smoothstep(.4,.49,e);
    city+=sea*(vec3(.85,.95,1.)*step(.985,h3(floor(q*4000.)))*fc*mix(1.,.3,smoothstep(.4,1.2,fp))*3.);
    col+=night*(city*1.6*(1.+.5*S1.x)+alb*1.5*rshine+sea*vec3(.012,.02,.035)*nz(vec3(q.x*9.,q.y*30.,q.z*9.)+nz(q*5.)*2.));
    // thunderstorms: flashes glow softly from inside the thick night-side cloud,
    // a few cells at a time, each one a short burst of 2-3 flickers
    vec3 g3=q*55., sc3=floor(g3); float hs=h3(sc3), ph=T*.21+hs*9.;
    float fe=step(.93,h3(sc3+floor(ph)*vec3(1,3,5))), pf=fract(ph)*3.;
    float fl=exp(-pf*16.)+.55*exp(-abs(pf-.09)*40.)+.35*exp(-abs(pf-.21)*35.)+.08*exp(-pf*3.);
    vec3 dc=g3-sc3-.5-(vec3(h3(sc3+3.),h3(sc3+5.),h3(sc3+8.))-.5)*.5;
    FLS=night*vec3(.55,.62,1.)*fe*fl*exp(-dot(dc,dc)*8.)*smoothstep(.3,.8,M.z)*2.2;   // added above the cloud shell
    dep=tp;
  }
  col=cshell(ro,rd,tp,sun,col)+FLS*.2;
  if(rc.a>0.&&tR<tp){ col=mix(col,rc.rgb,rc.a); dep=min(dep,tR); }
  {  // an orbital ring habitat in the ring plane, built like the finale's rings (plated hull, rows of windows,
     // lamp rows, the running blue strip), lit by the sun past the planet; tethered by six elevators
    mat3 F; F[1]=RN; F[0]=normalize(cross(RN,vec3(0,0,1))); F[2]=cross(F[0],F[1]);
    vec3 lp; float to=ringHitS(ro,rd,F,1.16,.016,lp);
    if(to<min(tp,tR>0.&&rc.a>.5?tR:1e5)){
      vec3 p=F*lp, nl=normalize(F*vec3(lp.x,0,lp.z));
      col=ringCol0(lp,rd,F,1.16,.016,0.,to)+vec3(.25,.25,.28)*max(dot(nl*sign(dot(-rd,nl)),sun),0.)*2.*pshadow(p,sun);
      dep=to;
    }
    for(int k=0;k<6;k++){
      float a=float(k)*1.0472+T*.01; vec3 dk=F*vec3(cos(a),0,sin(a));
      vec4 sg=seg(ro,rd,dk,dk*1.16);
      if(sg.y<tp) col+=vec3(.5,.8,1.)*glw(sg.x,sg.y,.6+S0.x)*.5;
    }
  }
  vec3 tr; vec3 at=atmo(ro,rd,tp,sun,tr);
  if(tp<1e4) at*=mix(.12,1.,exp(-dot(-rd,normalize(ro+rd*tp))*5.));   // thinner haze looking straight down, full at the limb
  col=col*dot(tr,vec3(.3,.4,.3))+at;   // neutral extinction: no red fringe on the limb
  vec3 e1=EL, e2=EL*3.6, ed=e2-e1; float el=length(ed); ed/=el;
  vec3 w0=ro-e1; float bb=dot(rd,ed), d0=dot(rd,w0), e0=dot(ed,w0), den=1.-bb*bb;
  float tc=clamp((bb*e0-d0)/den,0.,1e5);
  float se=clamp(e0+tc*bb,0.,el);
  vec3 pr=ro+rd*(dot(e1+ed*se-ro,rd)), pe=e1+ed*se;
  float dd=length(pr-pe), tl=dot(pe-ro,rd);
  if(tl>0.&&tl<tp){
    float pulse=exp(-10.*abs(fract(se*.6-T*.4)-.5))*(1.+2.*S0.x);
    col+=vec3(.4,.8,1.)*(exp(-dd*900.)*1.5+exp(-dd*120.)*.15)*(1.+3.*pulse)*smoothstep(el,el-.1,se);
    col+=vec3(1.,.7,.4)*exp(-length(e2-ro-rd*dot(e2-ro,rd))*200.)*2.;
  }
  if(SEC==7){   // a ring station co-orbiting with the camera (drifts left to right across the limb), the sun behind it; edges antialiased by the TAA jitter
    float a=T*.0335-3.175, t=1e5, s;
    vec3 c=vec3(cos(a)*1.47,.4,sin(a)*1.47), n=vec3(0); mat3 F=rframe(.72,-.5);
    vec3 o=(ro-c)*F/.03, d=rd*F, p; vec2 h=sph(o,d,1.25);
    if(h.y>0.){
      float m=max(h.x,0.);
      for(int i=0;i<80&&m<h.y;i++){ p=o+d*m; s=stn(p); if(s<m*.0005) break; m+=s; }
      if(m<h.y){
        t=m;
        vec3 q=p; q.xz*=rot(T*.15);
        for(int i=0;i<4;i++){ vec3 e=vec3((i+3)>>1&1,i>>1&1,i&1)*.008-.004; n+=e*stn(p+e); }
        n=F*normalize(n);
        // hull in painted segments, wings of dark cells with the sun showing through the gaps between them;
        // light: the sun (behind), earthshine from the planet, a warm rim where the hull grazes the sun
        float dif=max(dot(n,sun),0.), bk=pow(max(dot(rd,sun),0.),8.), fr=1.-abs(dot(n,rd));
        vec2 g=abs(fract(vec2(p.y*14.,p.z*10.))-.5);
        bool wg=SN<.02;
        col=(wg?vec3(.015,.025,.06):vec3(.3-.12*step(.97,cos(atan(q.z,q.x)*24.))))*(dif*vec3(4.4,3.6,3.)+.03+vec3(.1,.18,.4)*max(dot(n,-normalize(c)),0.))
           +pow(max(dot(reflect(rd,n),sun),0.),24.)+vec3(.25,.45,1.)*fr*fr*fr*.4+vec3(1.,.75,.5)*bk*(fr*fr*fr*fr*2.+(wg?.1+.7*step(.45,max(g.x,g.y)):0.))
           +vec3(1.,.7,.4)*step(SM,.01)*step(abs(length(q.xz)-1.),.016)*mix(step(fract(atan(q.z,q.x)*12.),.6)*step(.3,h2(vec2(floor(atan(q.z,q.x)*12.),step(0.,q.y)))),.4,smoothstep(.01,.04,t/U.z))*smoothstep(.1,-.2,dif)*2.;
        dep=t*.06;
      }
    }
    for(int i=0;i<4;i++){   // navigation lights: red and green on the habitat rim (they turn with it), white strobes on the mast ends with the kick
      float fi=float(i); vec3 l=i<2?vec3(0,fi*1.88-.94,0):vec3(cos(T*.15),0,sin(T*.15))*(5.45-fi*2.18);
      vec4 sg=seg(o,d,l,l);
      float w=sg.y*2./U.z;
      if(sg.y<t) col+=(i<2?vec3(pow(S0.x,4.)*6.):i<3?vec3(3.,.15,.06):vec3(.15,3.,.3))*exp(-sg.x*sg.x/(w*w));
    }
    // a shuttle leaves the hub 3 s in and burns away retrograde, dropping toward the planet: a blue-white
    // engine light with a trail as long as its last second of flight, fading behind it
    float u=max(LT-3.,0.), u0=max(u-1.,0.); vec3 sv=normalize(vec3(sin(a),-.1,-cos(a))-c*.3)*F;
    vec4 sg=seg(o,d,sv*(u*.3+u*u*.04),sv*(u0*.3+u0*u0*.04));
    float w=sg.y*2./U.z*(1.+sg.z*2.);
    if(sg.y<t&&u>0.) col+=vec3(.5,.75,1.)*exp(-sg.x*sg.x/(w*w))*(1.-sg.z)*(1.-sg.z)*4.;
  }
  if(SEC==8){   // the star's corona past the limb: streamers (noise of the direction around the planet only, so they
    // run outward, each its own length), brightest where the hidden sun sits closest to the limb; at the limb a
    // thin red ring, the sunsets all round the planet seen through its air. As the riser builds to the cut, the
    // diamond ring: the edge of the sun breaks out at the limb point nearest it. All of it lies behind the rings
    vec3 pc=-normalize(ro), q=normalize(rd-pc*dot(rd,pc));
    float pr=asin(1./length(ro)), x=acos(clamp(dot(rd,pc),-1.,1.))-pr, s=nz(q*5.+T*.02)*.6+nz(q*17.)*.4,
          g=1.-dot(rd,normalize(pc*cos(pr)+normalize(sun-pc*dot(sun,pc))*sin(pr)));
    if(x>0.) col+=(1.-rc.a)*((1.+3.*dot(q,sun))*(vec3(1.,.75,.5)*exp(-x*18.)*(.36+.48*s)+vec3(1.,.85,.7)*exp(-x*mix(9.,3.,nz(q*7.+9.)))*s*s*.4+vec3(1.,.22,.04)*exp(-x*400.)*(3.+5.*s))*(1.+.3*S1.x)
                                   +vec3(1.,.92,.8)*S1.w*S1.w*exp(-g*1e5)*300.);
    col+=vec3(1.,.9,.75)*S1.w*S1.w*exp(-g*800.)*4.;   // its glare in the lens, over everything
  }
  return col;
}

// =====================================================================
// WORLD 4 : SOL - a Kardashev II civilisation englobing its star
// =====================================================================
const float RS=2.3, PBX=.82;
float BP;
float starVis(vec3 p){
  float v=0.;
  for(int i=0;i<4;i++){ vec3 tg=hsph(fract(JIT+float(i)*.25),fract(JIT2+float(i)*.37))*.95, d=tg-p; float l=length(d); v+=occB(p,d/l,l-1.)?0.:.25; }
  // swarm shadows: discrete collectors on orbits around the star (cells in the direction from the
  // star, each band of latitude orbiting at its own rate, closer bands faster); each throws a soft
  // umbra + wide penumbra (the star is a big disc from here) that sweeps across planet and rings
  vec3 a=normalize(p); float sw=0.; 
  for(int k=0;k<2;k++){ vec3 b=a; float fk=float(k); b.xz*=rot(T*(.03+.02*fk)*cos(a.y*3.)+fk); vec3 g=b*(9.+6.*fk), c=floor(g);
    float hc=h3(c+fk*17.); vec3 dc=g-c-.5-(vec3(h3(c+3.),h3(c+5.),h3(c+8.))-.5)*.5;
    sw+=step(.55,hc)*smoothstep(.42,.12,length(dc))*(.6+.25*fk); }
  return v*(1.-min(sw,.85)*smoothstep(1.4,2.,length(p)));
}
vec3 ringCol0(vec3 lp, vec3 rd, mat3 F, float R, float hh, float sp, float t){
  float an=atan(lp.z,lp.x)+T*sp, u=an*R, v=lp.y/hh, cs=dot(rd*F,vec3(lp.x,0,lp.z)/R);
  vec3 nl=vec3(lp.x,0,lp.z)/R, q=lp; q.xz*=rot(-T*sp); q+=R*1.7;   // surface point in the ring's own (rotating) frame
  bool inner=cs>0.&&SEC>8;   // (the planet's ring habitat in section 8 is shaded as hull all round)
  float px=t*1.25/U.z, pa=px/max(abs(cs),.12), l0=log2(max(sqrt(px*pa)*128.,1.));   // texture LOD from the mean of the footprint axes: sharp across the oblique band
  // habitat sections: dividing walls (integer count around the ring, so no seam)
  float ns=floor(R*6.28/.55), dw=abs(fract(an/6.2832*ns+.5)-.5)*6.2832*R/ns;
  float wall=(1.-smoothstep(.002,.002+pa,dw))*(1.-smoothstep(.02,.08,pa));
  if(abs(v)>.86){   // rim walls: ribbed plating, seams, a lamp row, the blue edge strip
    float rb=smoothstep(.4,.5,abs(fract(u*30.)-.5))*(1.-smoothstep(.01,.03,px*30.));
    float lm=step(.6,h2(vec2(floor(u*15.),R)))*smoothstep(.3,.1,length(vec2(fract(u*15.)-.5,(abs(v)-.93)*6.)))*(1.-smoothstep(.02,.06,px*15.));
    return vec3(.07,.07,.08)*(.6+.6*h2(vec2(floor(u*15.),R+1.)))*(inner?3.:1.)*(1.+rb)+vec3(1.,.75,.45)*lm*.8
          +vec3(.4,.7,1.)*smoothstep(.97,.99,abs(v))*.6+vec3(.5,.5,.55)*wall*.3;
  }
  if(inner){   // the inhabited band, seen from afar: sampled on the true 3D surface (no stretching),
               // three explicitly filtered octaves; land use, water, clouds, cities, section walls
    // continents ~1-2 units across (the band is .2 wide): coherent land masses and oceans that
    // read at orbital distance, coastline + terrain detail in two finer filtered octaves
    vec4 a=textureLod(W,q*.9,l0-.15),b=textureLod(W,q*4.5+.3,l0+2.17),d=textureLod(W,q*18.+.6,l0+4.17);
    float f2=1.-smoothstep(.3,1.,pa*4.5), f3=1.-smoothstep(.3,1.,pa*18.);
    float e=a.g*.7+(b.r-.5)*.22*f2+(d.g-.5)*.1*f3+.15, ew=.004+pa*1.2;
    vec3 q2=q+vec3(0,.006,0);   // terrain relief: the slope across the band, lit from one side of it
    float sl=clamp(1.+(e-textureLod(W,q2*.9,l0-.15).g*.7-(textureLod(W,q2*4.5+.3,l0+2.17).r-.5)*.22*f2-(textureLod(W,q2*18.+.6,l0+4.17).g-.5)*.1*f3-.15)*6.,.5,1.4);
    float wat=smoothstep(.5+ew,.5-ew,e), m=a.b+(b.g-.5)*.45*f2;
    float fh=mix(h2(floor(vec2(u,lp.y)*260.)+R),.5,smoothstep(.2,.8,pa*260.));       // field patchwork
    vec3 al=mix(vec3(.07,.11,.04),vec3(.13,.13,.07),fh*.6+.2);                         // farmland, olive to dry
    al=mix(al,vec3(.035,.07,.03)*(.8+.4*d.r),smoothstep(.5,.6,m));                   // forest
    al=mix(al,vec3(.3,.24,.16)*(.8+.4*b.b),smoothstep(.36,.26,m));                   // desert
    al=mix(al,vec3(.17,.15,.13)*(.8+.4*d.g),smoothstep(.72,.8,e));                   // bare rock on the high ground
    al=mix(al,vec3(.7,.72,.75),smoothstep(.83,.88,e+(d.r-.5)*.06*f3));                  // snow on the peaks
    al=mix(al,vec3(.36,.32,.24),smoothstep(.53,.505,e));                              // beaches
    float city=smoothstep(.6,.72,b.b+.15*d.b*f3)*(1.-wat);
    al=mix(al,vec3(.2,.2,.19),city*.8);
    al*=sl;
    al=mix(al,mix(vec3(.006,.02,.05),vec3(.04,.12,.13),smoothstep(.44,.5,e)),wat);       // deep water, turquoise shallows
    float L=.12+.88*starVis(F*lp*.999);
    vec3 col=al*35.2/(R*R)*L, n=-F*nl, Ls=-normalize(F*lp);
    float ex=mix(400.,30.,smoothstep(0.,.6,pa*30.));
    col+=wat*pow(max(dot(reflect(rd,n),Ls),0.),ex)*ex*.012*L*vec3(1.,.9,.7);           // sun glint
    col+=city*(1.-L)*vec3(1.,.6,.3)*mix(step(.7,h2(floor(vec2(u,lp.y)*500.)+R))*2.,.6,smoothstep(.2,.8,pa*500.));   // lights in the shade
    float cl=smoothstep(.5,.78,textureLod(W,q*vec3(.8,3.,.8)+vec3(T*.003,0,.5),l0+1.).a*.75+b.a*.25*f2);
    col=mix(col,mix(vec3(.55,.6,.7),vec3(1.),cl)*L*1.3,cl*.8);                           // weather: thin veils blue-grey, thick decks white
    col=mix(col,vec3(.2,.2,.22)*L,wall*.8);
    return mix(col,vec3(.3,.45,.8)*L,1.-exp(-.05/(abs(cs)+.02)));                         // the band's own air, thicker the more obliquely we look through it
  }
  // outer hull: plated in panels of varying tone, ribbed every 1/8 unit, sheen from the swarm glow
  // behind the camera (grazing view), warm rim where the side walls catch the star; all filtered
  vec2 g=vec2(u*40.,v*4.), pg=vec2(u*8.,v*2.+.5*floor(u*8.)), pf=abs(fract(pg)-.5), wf=abs(fract(g)-.5);
  float fa=smoothstep(.1,.6,px*40.), rb=smoothstep(.46-px*8.,.5,max(pf.x,pf.y*.25))*(1.-smoothstep(.1,.5,px*8.));
  float li=step(.5,h2(floor(g)+R))*smoothstep(.32,.26,wf.x)*smoothstep(.1,.06,wf.y);   // rows of windows, half of them lit
  li=mix(li,.05,fa);
  float fr=1.-abs(dot(rd*F,nl));
  vec3 c=vec3(.045,.047,.055)*mix(.5+h2(floor(pg))*.9,.95,smoothstep(.05,.3,px*8.))*(1.+rb*1.5)
        +vec3(.35,.3,.4)*pow(fr,3.)*.25+vec3(1.,.62,.32)*smoothstep(.6,.86,abs(v))*.12
        +vec3(1.,.58,.25)*li*1.3;
  c*=1.-.35*smoothstep(.3,.5,abs(fract(u*120.)-.5))*(1.-smoothstep(.01,.03,px*120.))*step(.5,h2(floor(pg)+3.));   // radiator fins
  c+=vec3(.2,.21,.24)*wall;
  c+=vec3(.3,.8,1.4)*smoothstep(.04+px*3.,.0,abs(abs(v)-.45))*.5*(.6+.4*sin(u*3.-T*4.));
  return c;
}
// the habitat rings are solid bands with depth: outer hull cylinder (R+RW), inner (inhabited)
// cylinder R and the two side walls (annuli at y=+-hh). The closest of the three is the hit, so the
// band is opaque from every side.
float RW, RTY;
float ringHitS(vec3 ro, vec3 rd, mat3 F, float R, float hh, out vec3 lp){
  vec3 o=ro*F, d=rd*F; RW=hh*.6; RTY=0.;
  float a=dot(d.xz,d.xz), b=dot(o.xz,d.xz), c=dot(o.xz,o.xz), tb=1e5, h;
  h=b*b-a*(c-(R+RW)*(R+RW));
  if(h>0.){ float t=(-b-sqrt(h))/a; vec3 q=o+d*t; if(t>0.&&abs(q.y)<hh){ tb=t; lp=q; RTY=1.; } }
  h=b*b-a*(c-R*R);
  if(h>0.){ float t=(-b+sqrt(h))/a; vec3 q=o+d*t; if(t>0.&&t<tb&&abs(q.y)<hh){ tb=t; lp=q; RTY=0.; } }
  for(int k=0;k<2;k++){ float t=((k==0?hh:-hh)-o.y)/d.y; vec3 q=o+d*t; float r=length(q.xz);
    if(t>0.&&t<tb&&r>R&&r<R+RW){ tb=t; lp=q; RTY=2.; } }
  return tb;
}
vec3 ringCol(vec3 lp, vec3 rd, mat3 F, float R, float hh, float sp, float t){
  vec3 Ls=-normalize(F*lp), nw=RTY>1.?F[1]*sign(lp.y):F*normalize(vec3(lp.x,0,lp.z));
  // star light on the hull and walls, and a backlit silhouette rim when the star is behind the band
  vec3 ex=vec3(1.,.78,.55)*(max(dot(nw,Ls),0.)*35.2/(R*R)*.06+pow(1.-abs(dot(rd,nw)),4.)*max(dot(rd,Ls),0.)*.9)*starVis(F*lp*.999+nw*.002);
  if(RTY>1.) lp.y=sign(lp.y)*hh*(.88+.1*fract((length(lp.xz)-R)/RW*3.));   // side wall: shaded as rim plating
  return ringCol0(lp,rd,F,R,hh,sp,t)+(RTY>0.?ex:vec3(0));
}
float jet(vec3 p){
  vec3 q=p; q.xy*=rot(.18);
  float y=abs(q.y); if(y<.9) return 0.;
  float r=length(q.xz), w=.04+.07*(y-.9);
  float an=atan(q.z,q.x)+y*2.2-T*1.3*sign(q.y);
  return exp(-r*r/(w*w))*(.55+.45*sin(an*3.))*smoothstep(4.6,1.8,y)*(.4+1.2*nz(vec3(r*30.,y*8.-T*5.,an*2.)));
}
float loops(vec3 p){
  float g=0.;
  for(int i=0;i<7;i++){
    float fi=float(i);
    vec3 nn=normalize(vec3(sin(fi*2.4+.5),sin(fi*1.7)*.6,cos(fi*2.4+.5)));
    vec3 tg=normalize(cross(nn,vec3(.3,1,.2))), q=p-nn*1.02;
    float pl=dot(q,cross(nn,tg)), rr=length(vec2(dot(q,nn),dot(q,tg)))-.22-.04*fi;
    g+=exp(-(rr*rr+pl*pl)*900.)*step(-.02,dot(q,nn));
  }
  return g;
}
// planet: baked volume (q relative to the planet centre) + the analytic mining ring
vec4 pvol(vec3 q){ return textureLod(PV,q/(2.*PBX)+.5,0.); }
float mapP(vec3 q, out float m){
  float d=dot(q,q)>PBX*PBX*.9?length(q)-PR-.04:pvol(q).x; m=0.;
  vec3 tq=q; tq.yz*=rot(.42); tq.xz*=rot(T*.05);
  float rg=length(vec2(length(tq.xz)-1.05,tq.y))-.012;
  if(rg<d){ d=rg; m=1.; }
  return d;
}
vec3 bbody(float k){ return mix(vec3(1.,.12,.02),mix(vec3(1.,.5,.12),vec3(1.,.9,.7),smoothstep(.6,1.,k)),smoothstep(.2,.7,k))*k*k; }
vec3 world4(vec3 ro, vec3 rd, out float dep){
  BP=.2+.14*smoothstep(146.,174.,T);
  vec3 col=stars(rd)*.8;
  col+=vec3(.5,.25,.6)*pow(fbm(rd*2.5+3.,5),3.)*.25+vec3(.2,.3,.5)*pow(fbm(rd*4.-1.,4),4.)*.3;
  float tn=1e5; vec3 sc=vec3(0);
  vec2 st=sph(ro,rd,1.);
  if(st.x>0.){
    tn=st.x; vec3 n=normalize(ro+rd*st.x); float mu=max(dot(n,-rd),0.);
    vec3 nq=n; nq.xz*=rot(T*.02);
    float gr=texture(W,nq*1.3+vec3(0,0,T*.004)).g, sp=smoothstep(.62,.72,fbm(nq*2.5+5.,4));
    sc=vec3(1.,.55,.25)*(2.+6.*gr*gr)*(.25+.75*pow(mu,.5))*(1.-.9*sp);   // bright enough to blaze, dim enough that the granulation and spots show
    sc+=vec3(1.,.75,.5)*smoothstep(.55,.75,texture(W,nq*4.).b)*(1.-mu)*6.;   // faculae near the limb
    sc+=vec3(1.,.25,.12)*pow(1.-mu,6.)*14.;                                  // chromosphere rim
  }
  mat3 F1=rframe(.32,.1), F2=rframe(-.45,-.2);
  vec3 lp; float tr=ringHitS(ro,rd,F1,5.2,.1,lp);
  if(tr<tn){ tn=tr; sc=ringCol(lp,rd,F1,5.2,.1,.03,tr); }
  tr=ringHitS(ro,rd,F2,7.6,.15,lp);
  if(tr<tn){ tn=tr; sc=ringCol(lp,rd,F2,7.6,.15,-.02,tr); }
  // --- rim scaffolds and the debris chunks (ray-traced meshes)
  float tb=1e5;
  if(traceB(ro,rd,min(tn,40.))){
    tb=HT; vec3 p=ro+rd*tb, n=HN, Ls=-normalize(p); float irr=12./dot(p,p);
    float dif=max(dot(n,Ls),0.)*(.15+.85*starVis(p+n*.001));
    vec3 cg=PP+RAX*.25, lc=cg-p; float lcd=dot(lc,lc);
    vec3 glow=vec3(1.,.38,.1)*max(dot(n,lc*inversesqrt(lcd)),0.)*.05/lcd;
    if(HM==5.){   // basalt chunk: sun-lit, ray-traced shadows, hot cracks that cool on the way out
      float cr=smoothstep(.035,0.,abs(fbm(HOP*3.5+11.,3)-.5))*smoothstep(.3,.6,nz(HOP*6.)), ht=smoothstep(.9,.1,length(p-PP)-PR);
      vec3 al=vec3(.085,.077,.07)*(.55+.9*nz(HOP*13.))*(.8+.4*nz(HOP*41.));
      sc=al*(dif*irr+.012)+glow*.7+vec3(.5,.47,.44)*pow(max(dot(reflect(rd,n),Ls),0.),14.)*dif*irr*.06+bbody(.5+.35*ht)*cr*ht*.8;
      sc+=vec3(1.,.62,.32)*pow(1.-abs(dot(n,rd)),4.)*max(dot(rd,Ls),0.)*irr*.3;   // backlit by the star: hot rim, not a black square
    } else {
      sc=vec3(.3,.31,.34)*(dif*irr+.05)+glow*2.+vec3(1.,.8,.6)*pow(max(dot(reflect(rd,n),Ls),0.),30.)*irr*dif*.5;
      sc+=vec3(1.,.15,.05)*step(.97,fract(HOP.y*40.+HOP.x*13.))*step(.5,fract(T*.7+HOP.z*30.))*4.;
    }
    tn=tb;
  }
  // --- the planet being disassembled: baked SDF volume
  vec2 pr2=sph(ro-PP,rd,1.07);
  if(pr2.y>0.&&pr2.x<tn){
    float t=max(pr2.x,0.), m=0.; bool h=false;
    for(int i=0;i<240;i++){
      vec3 q=ro+rd*t-PP;
      float d=mapP(q,m);
      if(d<.00035*t){ h=true; break; }
      t+=d*.72;
      if(t>pr2.y||t>tn) break;
    }
    if(h&&t<tn){
      tn=t; vec3 p=ro+rd*t, q=p-PP; float x;
      vec2 e=vec2(.0022,-.0022);
      vec3 n=normalize(e.xyy*mapP(q+e.xyy,x)+e.yyx*mapP(q+e.yyx,x)+e.yxy*mapP(q+e.yxy,x)+e.xxx*mapP(q+e.xxx,x));
      vec3 Ls=-normalize(p); float irr=12./dot(p,p), r=length(q);
      vec3 nq=q/r;
      float ndl=max(dot(n,Ls),0.), sh=0.;
      if(ndl>0.){
        sh=.1+.9*starVis(p+n*.002);
        float s2=1., ts=.006;
        for(int i=0;i<16;i++){ float hh=pvol(q+n*.002+Ls*ts).x; s2=min(s2,10.*hh/ts); ts+=max(hh,.006); if(s2<.02||ts>.4) break; }
        sh*=clamp(s2,0.,1.);
      }
      // light of the exposed core, scattered up the walls of the pit
      vec3 cg=RAX*.24, lc=cg-q; float lcd=dot(lc,lc);
      float cv=1., tq=.01; vec3 ld=lc*inversesqrt(lcd);
      for(int i=0;i<12;i++){ float hh=pvol(q+n*.003+ld*tq).x; cv=min(cv,8.*hh/tq); tq+=max(hh,.01); if(cv<.02||tq*tq>lcd) break; }
      vec3 corel=vec3(1.,.36,.09)*(max(dot(n,ld),0.)*clamp(cv,0.,1.)+.25*smoothstep(PR-.02,.3,r))*.06/lcd*(1.+.3*S1.x);
      if(m==1.){   // the mass-driver ring: a steel tube in segments with a row of small lights, payloads running along it
        vec3 tq=q; tq.yz*=rot(.42); tq.xz*=rot(T*.05); float u=atan(tq.z,tq.x)/6.2832;
        sc=vec3(.25,.26,.3)*ndl*sh*irr*(1.-.6*step(.9,fract(u*120.)))+vec3(1.,.75,.45)*step(.75,fract(u*240.))*step(abs(tq.y),.005)*.8
          +vec3(.4,.8,1.4)*exp(-abs(fract(u*3.-T*.2)-.5)*120.)*4.+corel*.3;
      } else {
        vec4 P=pvol(q);
        float th=acos(clamp(dot(nq,RAX),-1.,1.)), dep_=PR-r;
        float ph=atan(dot(nq,normalize(cross(RAX,vec3(0,1,0)))),dot(nq,cross(RAX,normalize(cross(RAX,vec3(0,1,0))))));
        vec3 alb;
        if(r<.265){        // the core: convecting, crusting plasma
          float c1=fbm(q*14.+vec3(0,T*.25,0)+fbm(q*6.-T*.08,3)*2.,4), crust=smoothstep(.55,.72,fbm(q*34.+T*.15,3));
          sc=bbody(.75+.55*c1)*(2.2-1.6*crust)+corel*.2;
        } else {
          float ex=smoothstep(.012,.04,dep_);            // exposed interior vs crust
          float sb=P.z*38., band=floor(sb);
          vec3 sa=h2(vec2(band,3.))<.25?vec3(.5,.36,.2):h2(vec2(band,3.))<.5?vec3(.36,.16,.08):h2(vec2(band,3.))<.75?vec3(.3,.29,.27):vec3(.6,.52,.4);
          sa*=(.75+.4*nz(q*90.))*mix(1.,.45,smoothstep(.5,.3,r));
          sa*=mix(.85,1.15,smoothstep(.2,.8,dot(n,nq)));   // dusty benches, darker risers
          float ice=smoothstep(.78,.9,abs(nq.y)+.1*(P.w-.5))+smoothstep(.43,.38,P.w)*.8;
          vec3 ca=mix(vec3(.1,.085,.075),vec3(.24,.2,.16),smoothstep(.45,.65,P.w))*(.7+.6*nz(q*60.));
          ca=mix(ca,vec3(.55,.6,.66),clamp(ice,0.,1.)*.85);
          float scar=smoothstep(.7,.9,nz(vec3(ph*18.,th*3.,1.)))*smoothstep(1.35,.7,th);
          ca=mix(ca,vec3(.36,.3,.24),scar*.6);
          alb=mix(ca,sa,ex);
          float fill=.03+.03*max(dot(n,normalize(ro-p)),0.);
          sc=alb*(ndl*sh*irr*1.7+fill*vec3(1.,.8,.6))+alb*corel*3.;
          float hot=P.y*(.75+.25*sin(T*1.7+q.x*40.));
          sc+=bbody(clamp(hot,0.,1.))*2.2*smoothstep(.15,.5,hot);
          sc+=bbody(.9)*smoothstep(.36,.27,r)*.6*(.5+.5*nz(q*50.+T));
          // the deeper the cut, the hotter the rock: dull red benches, glowing seams between strata
          float kd=smoothstep(.62,.3,r)*ex, fs=fract(sb);
          sc+=bbody(.35+.45*kd)*kd*(.5+.6*nz(q*24.+T*.1));
          sc+=bbody(.8)*smoothstep(.88,.97,fs)*smoothstep(1.,.97,fs)*smoothstep(.66,.42,r)*ex*2.*(.4+.6*nz(q*70.))*smoothstep(.35,.65,nz(q*9.+3.));
          // industry on the night side: lights along roads and around the rim
          float nt=smoothstep(.1,-.2,dot(nq,Ls));
          vec3 g=nq*190.; float lt=step(.9,h3(floor(g)))*smoothstep(.35,.1,length(fract(g)-.5));
          lt*=smoothstep(1.5,.75,th)*(1.-ex)+scar*.6;
          sc+=nt*vec3(1.,.62,.3)*lt*2.*(1.-smoothstep(.5,2.,t*1.25/U.z*190.));
        }
      }
    }
  }
  // tethers from the rim to the swarm
  for(int i=0;i<5;i++){
    float a=float(i)*1.2566+.4; vec3 u=normalize(cross(RAX,vec3(0,1,0))), v=cross(RAX,u);
    vec3 d=normalize(RAX*cos(.8)+(u*cos(a)+v*sin(a))*sin(.8)), a0=PP+d*.76, a1=a0+normalize(d-PP*.3)*1.6;
    vec4 s=seg(ro,rd,a0,a1);
    if(s.y<tn) col+=vec3(.5,.8,1.2)*glw(s.x,s.y,.6+.6*exp(-12.*abs(fract(s.z*2.-T*.5)-.5)))*.6;
  }
  // --- the lattice shell (front and back crossings). Engineered megastructure: 40 latitude bands,
  // each split into panels of roughly constant width (no polar pinching) and varied length; a
  // triangular truss (band chords, panel posts, alternating diagonals); deliberate service slots
  // instead of random holes; panels have thickness (their sun-lit edges glow along the seams) and
  // emissive conduits along the band seams; the inner faces mirror the star.
  vec2 sh=sph(ro,rd,RS);
  vec3 gacc=vec3(0); float gtr=1.;
  for(int k=0;k<2;k++){
    float t=k==0?sh.x:sh.y;
    if(t<=0.||t>=tn) continue;
    vec3 p=ro+rd*t, n=p/RS;
    vec3 nr=n; nr.xz*=rot(T*.01);
    float la=asin(clamp(nr.y,-1.,1.)), bv=(la/PI+.5)*40., bi=floor(bv), bf=fract(bv);
    float hb=h2(vec2(bi,3.)), cl=cos((bi+.5)/40.*PI-PI*.5);
    float ns=max(floor(cl*80.*(.7+.6*hb)),4.), uq=(atan(nr.z,nr.x)/6.2832+.5)*ns+hb*7.;
    float pid=floor(uq), pf=fract(uq), hc=h2(vec2(bi,pid)), hp=h2(vec2(pid,bi+.5));
    float lat=abs(n.y);
    float pxb=t*1.25/U.z*40./(PI*RS), pxu=pxb*ns/(80.*cl+1e-3);   // pixel footprint in band / panel units
    bool slot=mod(bi,7.)==3.&&fract(pid*.25)<.5;                  // service slots: light shines through in rows
    bool frame=lat<BP+.38-.12*hc;
    bool pan=lat<BP-.06*hc&&!slot;
    bool outer=k==0;
    float dv=min(bf,1.-bf), de=min(min(pf,1.-pf)*80.*cl/ns,dv);   // distance to the panel edge / band seam (band units)
    // hierarchy: sectors of 5 bands x 6 panels carry the structural seams (lit edges, conduits);
    // single panel joints are only thin dark lines, so it reads as a structure, not as tiles
    float s5=min(fract(bv*.2),1.-fract(bv*.2))*5., u6=min(fract(uq/6.),1.-fract(uq/6.))*6.*80.*cl/ns;
    float hs=h2(vec2(floor(bv*.2),floor(uq/6.))), sfa=1.-smoothstep(.2,.6,pxb);
    float ed=(1.-smoothstep(.04,.04+pxb*1.5,min(s5,u6)))*sfa, jn=(1.-smoothstep(.012,.012+pxb*1.5,de))*sfa;
    if(pan){
      tn=t;
      float sv=starVis(p*(outer?.995:.998)), lit=.15+.85*sv;
      if(outer){   // the back: graphite / titanium plates of varied tone, radiator ribs, env sheen
        vec3 rf=reflect(rd,n), env=stars(rf)*.5+vec3(.06,.05,.09)+vec3(1.,.55,.3)*.08*max(dot(rf,-normalize(p)),0.);
        float rib=1.-.35*smoothstep(.3,.5,abs(fract(bf*7.)-.5))*(1.-smoothstep(.1,.35,pxb*7.));
        float fre=.06+.94*pow(1.-abs(dot(rd,n)),5.);
        sc=vec3(.075,.078,.088)*(.45+.9*hs+.25*hp)*rib*(.7+.5*texture(W,nr*6.).g)*(1.-.6*jn)+env*fre*(.5+.5*hs);
        sc+=vec3(1.,.72,.42)*ed*(.35+1.2*sv)*.8;                    // lit plate edges (thickness)
        sc+=vec3(.25,.6,1.)*(1.-smoothstep(.012,.012+pxb*1.5,s5))*(1.-smoothstep(.15,.5,pxb))*1.5*(.4+.6*exp(-6.*abs(fract(uq/ns*3.-T*.15+hb)-.5)));   // conduits along the band seams
        sc+=vec3(1.,.6,.3)*step(.93,hp)*smoothstep(.25,.05,length(vec2(pf-.5,bf-.5)))*smoothstep(.3,.05,pxb)*1.5;   // hatch lights
      } else {     // the sunward faces, seen through the gaps: photovoltaic, gold foil and mirrors
        float mt=fract(hp*7.3);
        vec3 al=mt<.55?vec3(.06,.09,.2):mt<.82?vec3(.95,.62,.25)*.75:vec3(.5,.52,.56);
        al*=.75+.5*fract(hp*31.);
        vec3 L=-normalize(p), rf=reflect(rd,-n);
        float ca=sqrt(max(1.-1./dot(p,p),0.)), sp=smoothstep(ca-.02,ca+.01,dot(rf,L));   // the star disc mirrored
        al=mix(al,vec3(.06,.09,.2),step(.5,hs)*.7);   // whole sectors of collector, others mixed
        sc=al*1.6*lit*(1.-.5*ed)*(1.-.5*jn)+(mt<.55?vec3(.15,.2,.35):mt<.82?vec3(1.,.7,.35):vec3(1.,.95,.9))*sp*(mt>.82?6.:mt<.55?1.:3.)*sv;
      }
      if(abs(lat-BP+.06*hc)<.02) sc+=vec3(.7,.85,1.)*step(.93,h2(vec2(bi,pid)+floor(T*12.)))*10.;   // welding at the construction front
      break;
    }
    if(frame){
      // triangular truss: band chords, panel posts, one diagonal per bay (alternating), filtered
      float gw=.04, dg=abs((pf-(mod(pid+bi,2.)<1.?bf:1.-bf))*.7071);
      float m1=1.-smoothstep(gw,gw+pxb*1.5,min(bf,1.-bf)), m2=1.-smoothstep(gw,gw+pxu*1.5,min(pf,1.-pf)), m3=1.-smoothstep(gw*.7,gw*.7+pxu,dg);
      float cov=max(max(m1,m2),m3*step(.25,hc));
      cov=mix(cov,.22,smoothstep(.15,.6,max(pxb,pxu)));
      vec3 gc=outer?vec3(.05,.052,.06)+vec3(.4,.45,.55)*pow(1.-abs(dot(rd,n)),3.)*.15+vec3(1.,.7,.4)*.25*starVis(p*.995):vec3(1.,.7,.4)*(.5+.9*hc)*(.15+.85*starVis(p*.998));
      gc+=vec3(1.,.8,.55)*step(.8,hc)*smoothstep(.15,.0,length(vec2(min(pf,1.-pf),min(bf,1.-bf))))*(.5+.5*sin(T*2.+hc*40.))*3.*smoothstep(.3,.1,pxb);   // node lamps
      if(abs(lat-BP-.38+.12*hc)<.015) gc+=vec3(.6,.8,1.)*step(.9,h2(vec2(bi,pid)+floor(T*14.)))*8.;
      gacc+=gtr*cov*gc; gtr*=1.-cov;
    }
  }
  if(tn<1e5) col=sc;
  col=col*gtr+gacc;
  vec3 oc=-ro; float b=dot(oc,rd), hh=length(oc-rd*b);
  if(tn>b){   // halo only when nothing in front of the star occludes it
    float an=atan(dot(rd,cross(normalize(oc),vec3(0,1,0))),rd.y);
    float cr=(.5+.5*nz(vec3(an*6.,T*.2,0)))*.35/pow(max(hh,1.),5.)+.12/pow(max(hh,1.),2.5);
    col+=vec3(1.,.55,.28)*cr*step(1.,hh)*.8;
  }
  // polar jets (inside a bounding cylinder) and magnetic loops (inside r < 1.35): short bounded marches
  {
    vec3 qo=ro, qd=rd; qo.xy*=rot(.18); qd.xy*=rot(.18);
    float a=dot(qd.xz,qd.xz), b=dot(qo.xz,qd.xz), c=dot(qo.xz,qo.xz)-.81, h=b*b-a*c;
    vec2 jb=sph(ro,rd,4.8);
    if(h>0.&&jb.y>0.){
      h=sqrt(h); float t0=max(max(jb.x,(-b-h)/a),0.), t1=min(min(jb.y,(-b+h)/a),tn);
      if(t1>t0){ float dt=(t1-t0)/32., t=t0+dt*JIT; vec3 acc=vec3(0);
        for(int i=0;i<32;i++){ vec3 p=ro+rd*t; acc+=vec3(1.,.42,.5)*jet(p)*(1.5+2.*smoothstep(2.,1.,abs(p.y)))*dt; t+=dt; }
        col+=acc*2.5*(1.+.5*S1.x); }
    }
    // corona: radial streamers (noise of the direction only, so it streaks outward), fading
    // with height; prominences: the magnetic loops, frayed into filaments
    jb=sph(ro,rd,2.4);
    if(jb.y>0.&&jb.x<tn){ float t0=max(jb.x,0.), t1=min(jb.y,tn), dt=(t1-t0)/56., t=t0+dt*JIT; vec3 acc=vec3(0);   // stop at the first occluder
      // volumetric corona: helmet streamers (broad, bulb-shaped at the base, narrowing to rays),
      // fine polar plumes, a K-corona glow, and a coronal mass ejection every 9 s - a frayed, expanding
      // shell sector that leaves the star along a random direction; 56 jittered steps
      vec3 ce=normalize(vec3(h2(vec2(floor(T/9.),1.))-.5,h2(vec2(floor(T/9.),2.))-.5,h2(vec2(floor(T/9.),3.))-.5)); float ca=fract(T/9.), cr=1.+ca*1.6;
      for(int i=0;i<56;i++){
        vec3 p=ro+rd*t; float r=length(p); vec3 a=p/r; a.xz*=rot(T*.01);
        float sm=texture(W,a*vec3(1.2,3.6,1.2)+vec3(0,T*.002,0)).g;
        float hl=texture(W,a*vec3(.6,1.2,.6)+.37).b, pl=texture(W,a*vec3(6.,2.,6.)+vec3(0,-T*.01,0)).g;
        float de=pow(sm,4.)*exp(-(r-1.)*3.6)*2.4+pow(hl,3.)*exp(-(r-1.)*2.2)*smoothstep(1.,1.25,r+hl*.3)*.9+pow(pl,6.)*smoothstep(.6,.9,abs(a.y))*exp(-(r-1.)*2.)*3.+exp(-(r-1.)*9.)*.15;
        de+=exp(-pow((r-cr)/(.05+ca*.08),2.))*smoothstep(.82,.96,dot(a,ce))*(.4+1.2*nz(p*14.-ce*T))*(1.-ca)*5.;
        if(r<1.65) acc+=vec3(1.,.4,.14)*loops(p)*(.4+1.2*nz(p*38.+vec3(0,T*.6,0)))*3.*dt;   // the loops end below r=1.48
        acc+=vec3(1.,.82,.62)*de*dt;
        t+=dt; }
      col+=acc*2.5*(1.+.5*S1.x); }
  }
  // heat haze / glow above the rift
  {
    vec2 hb=sph(ro-PP,rd,PR+.12);
    if(hb.y>0.){ float t0=max(hb.x,0.), t1=min(hb.y,tn), dt=(t1-t0)/12., t=t0+dt*JIT, g=0.;
      for(int i=0;i<12;i++){ vec3 q=ro+rd*t-PP; float a=max(dot(normalize(q),RAX)-.8,0.); g+=a*smoothstep(PR+.12,PR,length(q))*nz(q*30.-vec3(0,T*.8,0))*dt; t+=dt; }
      col+=vec3(1.,.35,.1)*g*6.; }
  }
  vec3 nd[8];
  for(int i=0;i<8;i++){ float a=float(i)*PI/4.+T*.03; nd[i]=vec3(cos(a)*3.3,sin(a*2.)*.15,sin(a)*3.3); nd[i].yz*=rot(.12); }
  for(int i=0;i<8;i++){
    vec4 s=seg(ro,rd,nd[i],nd[(i+3)%8]);
    float pu=exp(-12.*abs(fract(s.z*3.-T*.7+float(i)*.3)-.5));
    if(s.y<tn) col+=vec3(.35,.7,1.4)*glw(s.x,s.y,1.+2.*pu+2.*S0.x);
    if(i%2==0){
      s=seg(ro,rd,nd[i],normalize(nd[i])*1.05);
      if(s.y<tn) col+=vec3(1.,.4,.9)*glw(s.x,s.y,1.2+1.5*S0.w);
    }
    s=seg(ro,rd,nd[i],nd[i]);
    if(s.y<tn) col+=vec3(1.,.85,.6)*glw(s.x,s.y,6.);
  }
  dep=min(tn,FAR);
  return col;
}

void main(){
  T=U.x; SEC=min(int(T/16.),10); LT=T-float(SEC)*16.;
  vec2 R=U.yz, FC=vec2(gl_FragCoord.x,R.y-gl_FragCoord.y)+V.yz, uv=(2.*FC-R)/R.y;
  float fr=float((int(T*60.)+int(V.w)*7)%64);
  JIT=fract(52.9829189*fract(dot(gl_FragCoord.xy+fr*vec2(5.588,5.588),vec2(.06711056,.00583715))));
  JIT2=fract(JIT*1.618034+fr*.1373+h2(gl_FragCoord.xy)*.5);
  vec3 ro,ta; float roll,fl; camGet(0,ro,ta,roll,fl);
  vec3 rd=camRay(ro,ta,roll,fl,uv);
  vec3 col; float dep;
  if(SEC<=2||SEC==6) col=world0(ro,rd,dep);
  else if(SEC<=4) col=world1(ro,rd,dep);
  else if(SEC==5) col=world2(ro,rd,dep);
  else if(SEC<=8) col=world3(ro,rd,dep);
  else col=world4(ro,rd,dep);
  // NaN/Inf guard: one bad sample would otherwise live on in the TAA history and spread
  // through the bloom mips as large black blocks
  // (anything above fp16's 65504 would become Inf in the RGBA16F target)
  if(!(dep<FAR)) dep=FAR;
  O=vec4(fin(col),dep);
  gl_FragDepth=min(dep/FAR,1.);
}
