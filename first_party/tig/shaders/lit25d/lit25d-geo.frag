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
        int dl = 0;
        int dr = 0;
        for (int i = 1; i <= 90; i++) { if (code_at(q - ivec2(i, 0)) != c) break; dl = i; }
        for (int i = 1; i <= 90; i++) { if (code_at(q + ivec2(i, 0)) != c) break; dr = i; }
        uint cl = code_at(q - ivec2(dl + 1, 0));
        uint cr = code_at(q + ivec2(dr + 1, 0));
        bool hl = cl != NONE && cls_of(cl) == 3u && abs(anchor_of(cl) - a) <= 30.0;
        bool hr = cr != NONE && cls_of(cr) == 3u && abs(anchor_of(cr) - a) <= 30.0;
        float el = hl ? (a + anchor_of(cl)) * 0.5 : a;
        float er = hr ? (a + anchor_of(cr)) * 0.5 : a;
        if (hl && !hr) er = a + (a - el);
        if (hr && !hl) el = a + (a - er);
        float t = (float(dl) + 0.5) / float(dl + dr + 1);
        a = mix(el, er, t);
    }
    frag = vec4(a, max(a - y, 0.0), float(cls), 1.0);
}
