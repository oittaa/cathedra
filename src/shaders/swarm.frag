layout(location=0) out vec4 O;
layout(location=0) in vec2 vq; layout(location=1) flat in vec3 vc; layout(location=2) flat in float vt; layout(location=3) flat in float va;
void main(){
  vec3 c=vc; float a=va;
  if(vt<.25){   // resolved panel: a satellite - a body and two cell-gridded wings on a boom (big on screen; small ones stay plain quads)
    vec2 q=abs(vq); float w=max(length(fwidth(vq)),1e-4), d=min(min(max(max(.26-q.x,q.x-.98),q.y-.8),max(q.x-.16,q.y-.42)),q.y-.05),
          e=smoothstep(-.12,0.,d)*(1.-smoothstep(.1,.3,w));   // shape distance; its lit edge
    c*=mix(1.-.3*step(.88,fract(vq.x*5.))-.2*step(.85,fract(vq.y*3.)),.8,smoothstep(.06,.15,w))*(1.+.6*e);   // cells (their mean when small), a frame that catches the light
    c+=vec3(1.,.72,.4)*vt*12.*e;   // against the star (vt: backlight) the edges glow
    a*=mix(clamp(.5-d/w,0.,1.),1.,smoothstep(.15,.4,w));
  } else if(vt>.75){ float r=dot(vq,vq); if(r>1.) discard; a*=1.-r; }
  if(!(a>0.)) discard;
  O=vec4(fin(c),min(a,1.));   // NaN/Inf guard
}
