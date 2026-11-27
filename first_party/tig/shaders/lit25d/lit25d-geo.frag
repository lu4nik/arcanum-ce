// Lit25d shader (Vulkan GLSL). Edit here, then run gen.py.
#version 450
layout(set = 2, binding = 0) uniform usampler2D u_gbuf;
layout(std140, set = 3, binding = 0) uniform LitUniforms {
    ivec4 u_rect;     // G-buffer x, y, w, h on screen
    vec4 u_sundir;    // xyz = towards the sun, w = trace length
    vec4 u_grade;     // xyz = outdoor colour grade
    vec4 u_params;    // x = ambient, y = sun, z = time (s), w = night
    ivec4 u_iparams;  // x = origin, y = mode, z = number of lights
    vec4 u_lpos[64];  // x, y (G-buffer), z, radius
    vec4 u_lcol[64];  // rgb, a = flicker
};
#define u_origin u_iparams.x
#define u_mode u_iparams.y
#define u_num_lights u_iparams.z
#define u_ambient u_params.x
#define u_sun u_params.y
#define u_time u_params.z
#define u_night u_params.w

layout(location = 0) out vec4 frag;
const uint NONE = 0xFFFFFFFFu;
const float ROOF_H = 110.0;
uint code_at(ivec2 q) {
    if (q.x < 0 || q.y < 0 || q.x >= u_rect.z || q.y >= u_rect.w) return NONE;
    return texelFetch(u_gbuf, q, 0).r & 0xFFFFFFu;
}
uint cls_of(uint c) { return (c >> 16) & 15u; }
float anchor_of(uint c) {
    int s = (int(c & 0xFFFFu) + u_origin) & 0xFFFF;
    if (s >= 32768) s -= 65536;
    return float(s);
}
void main() {
    ivec2 q = ivec2(gl_FragCoord.xy);
    uint c = code_at(q);
    uint cls = cls_of(c);
    float y = float(q.y);
    if (c == NONE || cls == 0u) { frag = vec4(y, 0.0, 0.0, 1.0); return; }
    if (cls == 4u) { frag = vec4(y + ROOF_H, ROOF_H, 4.0, 1.0); return; }
    float a = anchor_of(c);
    if (cls == 3u) {
        // Where this column of the wall meets the floor: walk down past the
        // wall (and anything standing in front of it) to the first floor
        // pixel. Continuous along the wall, unlike the per-piece anchors;
        // kept near the piece's own base in case furniture hides the foot.
        float base = a;
        for (int i = 1; i <= 200; i += 2) {
            uint cc = code_at(q + ivec2(0, i));
            if (cc == NONE) break;
            if (cls_of(cc) == 0u) { base = float(q.y + i); break; }
        }
        a = clamp(base, a - 26.0, a + 26.0);
    }
    frag = vec4(a, max(a - y, 0.0), float(cls), 1.0);
}
