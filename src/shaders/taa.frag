// CATHEDRA - temporal anti-aliasing: analytic camera reprojection of the
// previous frame (every camera of the film is a pure function of time), a
// variance-clipped history in YCoCg, jittered input. a = view distance (DOF).
layout(location=0) out vec4 O;
layout(set=0,binding=18) uniform sampler2D CI;
layout(set=0,binding=19) uniform sampler2D HP;
vec3 tm(vec3 c){ return c/(1.+max(c.r,max(c.g,c.b))); }
vec3 yc(vec3 c){ return vec3(c.r*.25+c.g*.5+c.b*.25,c.r*.5-c.b*.5,c.g*.5-c.r*.25-c.b*.25); }
vec3 rgb(vec3 c){ return vec3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z); }
void main(){
  vec2 R=U.yz; ivec2 ip=ivec2(gl_FragCoord.xy), mx=ivec2(R)-1;
  vec4 c=texelFetch(CI,ip,0); c.rgb=fin(c.rgb);
  vec3 m1=vec3(0),m2=vec3(0),lo=vec3(1e9),hi=vec3(-1e9); float dn=c.a;
  for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){
    vec4 s=texelFetch(CI,clamp(ip+ivec2(i,j),ivec2(0),mx),0); s.rgb=fin(s.rgb);
    vec3 y=yc(tm(s.rgb)); m1+=y; m2+=y*y; lo=min(lo,y); hi=max(hi,y); dn=min(dn,s.a);
  }
  vec3 cur=yc(tm(c.rgb)), res=cur;
  if(V.w>1.5){   // offline export: same instant, new jitter -> plain running average
    res=mix(yc(tm(fin(texelFetch(HP,ip,0).rgb))),cur,1./V.w);
  } else if(V.w>.5){
    vec3 ro,ta; float roll,fl; camGet(0,ro,ta,roll,fl);
    vec2 FC=vec2(gl_FragCoord.x,R.y-gl_FragCoord.y);
    vec3 P=ro+camRay(ro,ta,roll,fl,(2.*FC-R)/R.y)*min(dn,2e4);
    camGet(1,ro,ta,roll,fl);
    vec3 v=(P-ro)*camMat(ro,ta,roll);
    vec2 pf=(v.xy*fl/v.z*R.y+R)*.5;
    pf.y=R.y-pf.y;
    if(v.z>0.&&pf.x>0.&&pf.y>0.&&pf.x<R.x&&pf.y<R.y){
      // 5-tap Catmull-Rom history fetch keeps the image sharp
      vec2 tp=pf-.5, f=fract(tp), t1=floor(tp)+.5;
      vec2 w0=f*(-.5+f*(1.-.5*f)), w1=1.+f*f*(-2.5+1.5*f), w2=f*(.5+f*(2.-1.5*f)), w3=f*f*(-.5+.5*f), w12=w1+w2;
      vec2 a0=(t1-1.)/R, a3=(t1+2.)/R, a12=(t1+w2/w12)/R;
      vec3 h=texture(HP,vec2(a12.x,a0.y)).rgb*w12.x*w0.y+texture(HP,vec2(a0.x,a12.y)).rgb*w0.x*w12.y+texture(HP,a12).rgb*w12.x*w12.y
            +texture(HP,vec2(a3.x,a12.y)).rgb*w3.x*w12.y+texture(HP,vec2(a12.x,a3.y)).rgb*w12.x*w3.y;
      h/=w12.x*w0.y+w0.x*w12.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y;
      vec3 mu=m1/9., sg=sqrt(max(m2/9.-mu*mu,0.));
      // anti-flicker: a wider box (1.5 sigma), and a slower feedback (6 %) while the history sits
      // inside it; only a clamped (disoccluded / changed) history takes the fast 15 % path
      vec3 h0=yc(tm(fin(h))), hy=clamp(h0,max(lo,mu-1.5*sg),min(hi,mu+1.5*sg));
      res=mix(hy,cur,max(mix(.06,.15,smoothstep(0.,.04,length(hy-h0))),V.w-1.));   // (V.w-1: the warm-up after a cut)
    }
  }
  vec3 o=rgb(res);
  O=vec4(fin(o/max(1.-max(o.r,max(o.g,o.b)),1e-3)),c.a);   // NaN/Inf never enters the history (or its bloom mips)
}
