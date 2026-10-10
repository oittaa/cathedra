#version 460
// CATHEDRA - shared prefix, prepended to every shader stage.
// U = time, width, height, render scale; S0 = kick snare hat arp; S1 = bass lead impact riser
// V = previous frame time, jitter x/y (pixels), history valid
layout(push_constant) uniform PC{ vec4 U,S0,S1,V; };
layout(set=0,binding=0) uniform sampler3D N;   // 64^3 value noise
layout(set=0,binding=3) uniform sampler3D W;   // 128^3 Perlin-Worley / Worley fBm, mipmapped
#ifdef WR
#extension GL_EXT_ray_query : require
layout(set=0,binding=8) uniform accelerationStructureEXT TA;   // the city: bakes tower shadows into the clouds
layout(std430,set=0,binding=6) buffer SB{ vec4 D[]; };
#else
layout(std430,set=0,binding=6) readonly buffer SB{ vec4 D[]; };
#endif
#define PI 3.14159265
#define FAR 4000.
uint hu(uint x){ x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }
// finite + fp16-safe: NaN/Inf -> 0 (bit test, survives fast-math), else clamp to [0,6e4]
vec3 fin(vec3 c){ return any(equal(floatBitsToUint(c)&uvec3(0x7f800000U),uvec3(0x7f800000U)))?vec3(0):clamp(c,0.,6e4); }
float hf(uint x){ return float(hu(x))*(1./4294967295.); }
float h2(vec2 p){ ivec2 i=ivec2(floor(p)); return float(hu(uint(i.x)*1973U+hu(uint(i.y)+9277U)))*(1./4294967295.); }
float h3(vec3 p){ ivec3 i=ivec3(floor(p)); return float(hu(uint(i.x)+hu(uint(i.y)*7U+hu(uint(i.z)*13U))))*(1./4294967295.); }
float nz(vec3 p){ return texture(N,p*.015625).r; }
float fbm(vec3 p,int o){ float a=.5,s=0.; for(int i=0;i<o;i++){ s+=a*nz(p); p=p*2.03+vec3(17.1,3.7,9.3); a*=.5; } return s; }
mat2 rot(float a){ float c=cos(a),s=sin(a); return mat2(c,s,-s,c); }
// gentle low-frequency drift on the big impacts only (no per-beat jolts)
vec3 shake(float T, float a){ return (vec3(nz(vec3(T*1.7,1,2)),nz(vec3(3,T*1.7,7)),nz(vec3(5,8,T*1.7)))-.5)*a*.3; }
mat3 camMat(vec3 ro, vec3 ta, float roll){
  vec3 w=normalize(ta-ro), u=normalize(cross(w,vec3(sin(roll),cos(roll),0.))), v=cross(u,w);
  return mat3(u,v,w);
}
vec3 camRay(vec3 ro, vec3 ta, float roll, float fl, vec2 uv){ return normalize(camMat(ro,ta,roll)*vec3(uv,fl)); }
float hg(float c,float g){ return (1.-g*g)/(4.*PI*pow(1.+g*g-2.*g*c,1.5)); }
vec2 sph(vec3 ro, vec3 rd, float r){ float b=dot(ro,rd), c=dot(ro,ro)-r*r, h=b*b-c; if(h<0.) return vec2(-1.); h=sqrt(h); return vec2(-b-h,-b+h); }

// ---- the spire city of the opening: at most one tower per 60x60 cell, clustered
// (smooth density field), jittered, varied heights; mirrored on the CPU for the BVH
const float CS=60.;
float vn2(vec2 p){ vec2 i=floor(p), f=p-i; f*=f*(3.-2.*f);
  return mix(mix(h2(i),h2(i+vec2(1,0)),f.x),mix(h2(i+vec2(0,1)),h2(i+1.),f.x),f.y); }
vec4 pcell(vec2 c){
  if((c.x==1.&&c.y==-12.)||(c.x==1.&&c.y==-7.)||(c.x==-2.&&c.y==-8.))   // hero towers of the camera moves
    return vec4((c.x+.5)*CS+(h2(c+vec2(7,1))-.5)*10.,(c.y+.5)*CS,100.+h2(c+vec2(3,9))*170.,8.+5.*h2(c+vec2(5,2)));
  float cl=vn2(c*.23);
  bool ex=h2(c)<.2+.65*cl&&(abs(c.x+.5)>1.||c.y<-59.)&&abs(c.x+.5)<20.&&c.y<-6.&&c.y>-100.;
  return vec4((c.x+.5)*CS+(h2(c+vec2(7,1))-.5)*34.,(c.y+.5)*CS+(h2(c+vec2(13,4))-.5)*34.,ex?110.+250.*pow(h2(c+vec2(3,9)),1.5)*(.55+.7*cl):-1.,7.+6.*h2(c+vec2(5,2)));
}

// ---- the cloud sea of the opening: static, baked into one RGBA16F volume
// (density, sun optical depth, sky optical depth, ray-traced tower shadow), analytic outside it
const vec3 CLO=vec3(-2400.,0.,-4300.), CLS=vec3(4800.,64.,4800.), GIO=vec3(-2400.,-40.,-4300.), GIS=vec3(4800.,560.,4800.);
float cloudBase(vec3 p, float lod){
  float h=p.y/48.;
  vec4 w=textureLod(W,p*.0024,lod);
  float lf=w.g*.625+w.b*.25+w.a*.125, b=(w.r-(lf-1.))/(2.-lf);
  float top=.3+.7*smoothstep(.3,.75,nz(p*.0045)), pr=smoothstep(0.,.1,h)*smoothstep(top,top*.4,h);
  b=clamp((b*pr-.48)/.52,0.,1.);
  b=mix(b,pr*.5,smoothstep(2.,5.,lod));   // far field: the layer's mean density, so the sea reaches the horizon
  return max(b,smoothstep(.2,.05,h)*(.55+.45*w.g))*3.;   // closed undercast: no dark holes below
}

// ---- the Kardashev system (star frame, star radius 1 at the origin) ----
const vec3 PP=vec3(6.2,.5,3.4);   // the planet being taken apart
const float PR=.7;
const vec3 RAX=vec3(-.6312,.6941,-.3461);   // axis of the excavation rift: 48 deg off the sun line, so low sun rakes the terraces

// every camera of the film, as a pure function of time (shared by the scene,
// the instanced swarm and the temporal AA reprojection)
void camera(float T, out vec3 ro, out vec3 ta, out float roll, out float fl){
  int sc=min(int(T/16.),10); float l=T-float(sc)*16.;
  roll=0.; fl=1.8; vec3 sh=vec3(0);
  if(sc==0){
    float k=smoothstep(0.,11.,l);
    ro=vec3(-30.+l*1.5,44.+20.*k,260.-l*8.); ta=ro+vec3(-1.5+l*.07,.1-.17*k,-1.);
  } else if(sc==1){
    float k=smoothstep(0.,16.,l); vec4 P=pcell(vec2(1,-7));
    ro=mix(vec3(14.,66.,90.),vec3(P.x-P.w*2.1-7.,78.,P.y+P.w*1.2),k);
    ta=ro+vec3(.06+.75*smoothstep(6.,16.,l),.08+.22*smoothstep(8.,16.,l),-1.); roll=-.08*k;
  } else if(sc==2){
    ro=vec3(26.*sin(l*.35),52.+6.*sin(l*.6),-400.-l*40.); ta=ro+vec3(.2*cos(l*.35),-.04,-1.); roll=.28*sin(l*.35); fl=1.4;
    sh=shake(T,.05*S1.z);
  } else if(sc==3){
    ro=vec3(6.+sin(l*.2)*1.2,3.5+l*.25,10.-l*4.5);
    ta=ro+vec3(sin(l*.2+1.)*.2,.05+.35*smoothstep(4.,12.,l)-.25*smoothstep(12.,16.,l),-1.);
    roll=.05*sin(l*.3); fl=1.5; sh=shake(T,.04*S1.z);
  } else if(sc==4){
    ro=vec3(6.,2.2+l*.08,-120.-l*3.); ta=ro+vec3(.03*sin(l*.4),.04,-1.); sh=shake(T,.04*S1.z);
  } else if(sc==5){
    float a=l*.22+1., r=3.3-l*.08;
    ro=vec3(sin(a)*r,.5*sin(l*.25)+.2,cos(a)*r); ta=vec3(0); roll=.1*sin(l*.2); fl=1.6;
    sh=shake(T,.05*S1.z);
  } else if(sc==6){
    vec4 P=pcell(vec2(1,-12)); float a=l*.3+.6, y=4.+l*l*1.45, rr=P.w*1.75+5.;
    ro=vec3(P.x+cos(a)*rr,y,P.y+sin(a)*rr);
    float lk=smoothstep(10.5,15.5,l);
    ta=ro+vec3(-cos(a)*(.35+lk*.6)-sin(a)*.5,mix(.9,-.55,lk),-sin(a)*(.35+lk*.6)+cos(a)*.5); fl=1.2;
    sh=shake(T,.05*S1.z);
  } else if(sc<9){
    vec3 sun=normalize(vec3(-.75,.14,.65)); roll=.08; fl=1.7;
    if(sc==7){ float ph=radians(18.+l*2.2); ro=vec3(cos(ph),.18,sin(ph))*1.85; ta=ro+normalize(mix(-normalize(ro),sun,.55)); }
    else { float ph=radians(4.-l*.8); ro=vec3(cos(ph),.03+l*.004,sin(ph))*(2.6+l*.05); ta=vec3(0.,.15,0.); fl=1.5; }
    sh=shake(T,.02*S1.z);
  } else if(sc==9){   // close on the sunlit rift, then pull back and turn to the star
    float k=smoothstep(2.5,16.,l);
    ro=mix(vec3(5.34,1.626,1.989)+vec3(.04,.025,-.05)*l,vec3(2.598,1.732,3.378),k);
    ta=mix(PP+RAX*.22,vec3(0.,-.35,0.),smoothstep(3.,14.,l));
    roll=.1-.16*k; fl=1.55; sh=shake(T,.03*S1.z);
  } else {
    float e=pow(l/16.,1.8);
    vec3 d=normalize(vec3(.6,.4+.5*e,.78)); d.xz*=rot(.25*l/16.);
    ro=d*(4.6+24.*e);
    ta=vec3(0.,-.35-3.2*e,0.)+vec3(-1.2,-.9,0.)*smoothstep(4.,16.,l);
    roll=-.06+.05*e; fl=1.55+.15*e; sh=shake(T,.03*S1.z);
  }
  ta+=sh;
}
// camera of this frame (k=0) or the previous one (k=1), written by camera.comp
void camGet(int k, out vec3 ro, out vec3 ta, out float roll, out float fl){
  int L=D.length()-4+2*k; vec4 a=D[L], b=D[L+1]; ro=a.xyz; roll=a.w; ta=b.xyz; fl=b.w;
}
// swarm element i at time T: position, facing, debris dust parameter.
// E.y = kind: 0 photovoltaic, .1 gold foil, .2 mirror ring panel, .3 free statite, 1 rift dust
vec3 swarmP(vec4 A, vec4 E, float T, out vec3 nr, out float s){
  vec3 p; s=0.;
  if(E.y<.5){
    float an=A.w+T*E.z;
    p=vec3(cos(an)*A.x,E.y<.25?(floor(E.w)-4.)*E.x*1.45:0.,sin(an)*A.x);
    p.y+=sin(an*(2.+floor(A.y*2.+1.4))+A.z*4.+T*.04)*A.x*.03;   // the rings ripple into ribbons   // ring lanes side by side across the orbit plane
    p.yz*=rot(A.y); p.xz*=rot(A.z);
    // coherent tilt waves along each ring: glints travel along the band instead of sparkling
    vec3 tl=vec3(sin(an*3.+A.z*5.),sin(an*2.+A.y*7.+T*.2),cos(an*5.+A.x*3.))*(E.y>.15?.45:.22)+(fract(fract(E.w)*vec3(13.1,71.7,157.3))-.5)*.16;   // mirrors (E.y=.2) steer hardest: many faces catch the star toward the viewer
    nr=normalize(-normalize(p)+tl);
  } else {
    s=fract(A.x+T*E.z);
    vec3 dir=normalize(PP+RAX*PR);
    p=mix(PP+RAX*PR*.9,dir*1.15,pow(s,.8));
    p.xz*=rot(-s*1.4);
    vec3 side=normalize(cross(dir,vec3(0,1,0)));
    p+=(side*cos(A.y+s*30.)+vec3(0,1,0)*sin(A.y+s*30.))*(.09*(1.-s)+.02)*A.z;
    nr=-normalize(p);
  }
  return p;
}
