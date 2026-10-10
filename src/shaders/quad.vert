void main(){ gl_Position=vec4(vec2(gl_VertexIndex&1,gl_VertexIndex>>1)*4.-1.,0.,1.); }
