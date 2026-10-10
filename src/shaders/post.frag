// CATHEDRA - post (on the TAA-resolved image): gentle background-only DOF,
// mip bloom (no lens ghosts, no chromatic aberration), ACES filmic tonemap, grading, grain, letterbox and SDF titles.
layout(location=0) out vec4 O;
layout(set=0,binding=2) uniform sampler2D C;
layout(set=0,binding=1) uniform sampler2D X;

vec3 aces(vec3 x){ return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.); }
vec2 R;
float txt(float line, vec2 ctr, float h, vec2 o){
  vec2 p=(vec2(gl_FragCoord.x,R.y-gl_FragCoord.y)+o*R.y-ctr*R)/(h*R.y);
  p=vec2(p.x*.046875+.5,p.y+.5);
  if(p.x<0.||p.x>1.||p.y<0.||p.y>1.) return 0.;
  // four taps half a texel off ([1 2 1] each way): the glyph bitmap's pixel staircase does not show magnified
  vec2 q=vec2(p.x,(line+p.y)/8.), e=.5/vec2(2048,768);
  return (texture(X,q+e).r+texture(X,q-e).r+texture(X,q+vec2(e.x,-e.y)).r+texture(X,q-vec2(e.x,-e.y)).r)*.25;
}
vec2 ink(float s, float a){ float w=fwidth(s)*.7+1e-4; return vec2(smoothstep(.5-w,.5+w,s),smoothstep(.3,.5,s)*.45)*a; }   // tight, light halo (no muddy glow on bright skies)
float win(float t, float a, float b, float f){ return smoothstep(a,a+f,t)*(1.-smoothstep(b-f,b,t)); }

void main(){
  R=U.yz;
  vec2 uv=gl_FragCoord.xy/R;
  float T=U.x; int sec=min(int(T/16.),10); float lt=T-float(sec)*16.;
  // ---- depth of field: background only, small aperture - the subject and
  // anything in front of the focus plane always stay sharp
  float ap=sec==5?.0028:sec==3||sec==4?.0018:sec>=9?.0012:sec>=7?.001:.0014;
  if(sec<3||sec==6) ap=0.;   // the cloud worlds stay crisp edge to edge
  float fl=4.+log2(U.w*U.z/1080.);
  float f=min(min(textureLod(C,vec2(.5),fl).a,textureLod(C,vec2(.47,.52),fl).a),textureLod(C,vec2(.53,.5),fl).a);
  if(sec==3) f=max(f,8.);
  float L0=max(log2(U.w),0.);
  vec4 c0=textureLod(C,uv,L0);
  float coc0=clamp((1.-f/max(c0.a,1e-3))*ap*30.,0.,1.);
  vec3 acc=c0.rgb; float ws=1.;
  float maxr=ap*R.y;
  if(coc0>.05) for(int i=1;i<32;i++){
    float r=sqrt(float(i)/32.), a=float(i)*2.39996;
    vec4 s=textureLod(C,uv+vec2(cos(a),sin(a))*r*maxr/R,L0);
    float coc=clamp((1.-f/max(s.a,1e-3))*ap*30.,0.,1.);
    float w=smoothstep(r-.15,r,min(coc,coc0));
    acc+=s.rgb*w; ws+=w;
  }
  vec3 col=acc/ws;
  col=fin(col);
  // ---- bloom from the mip chain
  float L=log2(U.w*U.z/1080.);
  vec3 bl=textureLod(C,uv,2.+L).rgb*.25+textureLod(C,uv,3.+L).rgb*.25+textureLod(C,uv,4.+L).rgb*.22+textureLod(C,uv,5.+L).rgb*.18+textureLod(C,uv,6.+L).rgb*.15+textureLod(C,uv,7.+L).rgb*.12;
  bl=fin(bl);
  // finale: geometry well in front of the focus (the ring bands crossing the star) takes little
  // of the star's bloom, so an occluding band stays solid instead of glowing like glass
  if(sec>=9) bl*=mix(.3,1.,smoothstep(.45,.85,c0.a/f));
  col+=max(bl-.6,0.)*(.12+.08*S0.x)+bl*.02;
  // ---- exposure, flashes, fades
  float ex=1.;
  // no full-screen white flashes over the cloud worlds (sections 0-2 and 6); elsewhere only a soft, warm impact lift
  float fw=sec<=2||sec==6?0.:1.;
  ex*=.8*smoothstep(0.,4.,T)*(1.-smoothstep(166.,175.,T)*.94)*(1.+S1.w*.15*fw)*(1.-.12*win(T,151.,160.,4.));   // the eye adapts as the camera turns to the star
  col*=ex;
  col+=vec3(1.,.9,.8)*S1.z*S1.z*.25*fw*smoothstep(2.5,1.5,lt);   // only on the hit at a section cut, not on the bells mid-shot
  // ---- grade per world
  vec3 gr=sec<=2||sec==6?vec3(1.04,1.,.94):sec<=4?vec3(.95,1.,1.08):sec==5?vec3(1.08,.95,1.04):sec>=9?vec3(1.03,.99,.98):vec3(.96,1.,1.07);
  col=aces(col*gr*.9);
  col=pow(col,vec3(1./2.2));
  col=mix(col,col*col*(3.-2.*col),.25);
  // vignette: a smooth ramp over the outer part of the frame
  col*=mix(.76,1.,smoothstep(-.1,.7,16.*uv.x*uv.y*(1.-uv.x)*(1.-uv.y)));
  // ---- titles (distance-field text)
  vec2 t=vec2(0);
  vec2 ti=ink(txt(0.,vec2(.5,.54),.22+.015*smoothstep(4.,14.,T),vec2(0)),win(T,4.,13.5,2.)*smoothstep(0.,.08,.2+(T-4.)*.25-uv.x));   // revealed left to right
  t+=ti;
  t+=ink(txt(1.,vec2(.5,.42),.05,vec2(0)),win(T,6.,13.5,1.5));
  t+=ink(txt(2.,vec2(.5,.16),.035,vec2(0)),win(T,17.,22.,1.));
  t+=ink(txt(3.,vec2(.5,.16),.035,vec2(0)),win(T,49.,54.,1.));
  t+=ink(txt(4.,vec2(.5,.16),.035,vec2(0)),win(T,81.,86.,1.));
  t+=ink(txt(5.,vec2(.5,.16),.035,vec2(0)),win(T,113.,118.,1.));
  t+=ink(txt(7.,vec2(.5,.16),.035,vec2(0)),win(T,145.,150.,1.));
  t+=ink(txt(1.,vec2(.5,.27),.032,vec2(0)),win(T,164.,175.5,2.));
  t+=ink(txt(6.,vec2(.5,.225),.032,vec2(0)),win(T,164.,175.5,1.2));   // fully in by t=165.2
  // the section titles (odd sections, 1-6 s in) sit on a soft dark plate, so they read even over the bright cloud sea
  col*=1.-.45*float(sec&1)*win(lt,1.,6.,1.)*exp(-pow((uv.x-.5)*8.,2.)-pow((uv.y-.84)*30.,2.));
  col*=1.-t.y*.8;
  col=col*(1.-t.x)+vec3(1.,.93,.82)*t.x;
  col+=ti.x*vec3(1.,.8,.5)*.3*exp(-pow((uv.x+uv.y*.3-(T-5.)*.15)*6.,2.));   // a slow glint crosses the title once
  // end card: the title in bevelled gold. The glyph distance field is the height field of the
  // extruded letters: bevel normals from its slope, the Dyson star as key light with a slow sweep across
  // the bevels, the scene above mirrored in the faces (screen-space), a soft shadow cast onto the plate
  float ea=win(T,162.,175.5,2.);
  if(ea>0.){
    vec2 c2=vec2(.5,.36);
    col*=1.-.55*ea*smoothstep(.3,.55,txt(0.,c2,.13,vec2(-.004,-.007)));
    float s=txt(0.,c2,.13,vec2(0)), hb=smoothstep(.5,.7,s), cv=ink(s,ea).x;
    vec3 n=normalize(vec3(-dFdx(hb),dFdy(hb),.25)), L=normalize(vec3(.35*sin(T*.35),.65,.6)), H=normalize(L+vec3(0,0,1));
    vec3 env=aces(textureLod(C,vec2(uv.x+n.x*.15,1.5-uv.y+n.y*.1),3.).rgb);
    vec3 m=vec3(1.,.78,.45)*(.22+.6*max(dot(n,L),0.)+env*.9)+vec3(1.,.92,.75)*(pow(max(dot(n,H),0.),40.)*1.4
          +exp(-pow((uv.x-.5-.6*sin(T*.45))*9.,2.))*(1.-n.z)*2.);
    col=mix(col,min(m,1.),cv);
  }
  // ---- grain + letterbox 2.35:1
  col+=(fract(sin(dot(gl_FragCoord.xy+fract(T)*400.,vec2(12.9898,78.233)))*43758.5453)-.5)*.018;
  float bar=max(0.,(1.-(R.x/2.2)/R.y)*.5);
  col*=smoothstep(bar,bar+.002,uv.y)*smoothstep(bar,bar+.002,1.-uv.y);
  O=vec4(col,1.);
}
