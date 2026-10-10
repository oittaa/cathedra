// CATHEDRA - one instanced quad per swarm element; position, extent and
// colour come from the per-frame compute pass (va = 0: culled -> off-screen)
layout(location=0) out vec2 vq; layout(location=1) flat out vec3 vc; layout(location=2) flat out float vt; layout(location=3) flat out float va;
layout(std430,set=0,binding=7) readonly buffer VB{ vec4 Q[]; };
void main(){
  uint b=uint(gl_InstanceIndex)*3U;
  vec4 a=Q[b], c=Q[b+1U], d=Q[b+2U];
  if(a.w<=0.){ gl_Position=vec4(2,2,2,1); return; }
  float fl, roll; vec3 ro, ta;
  camGet(0,ro,ta,roll,fl);
  vq=vec2(gl_VertexIndex&1,gl_VertexIndex>>1)*2.-1.;
  vec2 bt=unpackHalf2x16(floatBitsToUint(d.w));
  vc=vec3(unpackHalf2x16(floatBitsToUint(c.w)),bt.x); vt=bt.y; va=a.w;
  vec3 P=a.xyz+c.xyz*vq.x+d.xyz*vq.y, v=(P-ro)*camMat(ro,ta,roll);
  gl_Position=vec4(v.x*fl*U.z/U.y-2.*V.y/U.y*v.z, -v.y*fl+2.*V.z/U.z*v.z, length(P-ro)/FAR*v.z, v.z);
}
