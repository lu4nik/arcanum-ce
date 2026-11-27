// Lit25d shader (Vulkan GLSL). Edit here, then run gen.py.
#version 450
// Full-screen triangle.
void main()
{
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
