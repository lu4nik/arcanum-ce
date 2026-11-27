// Lit25d shader (Vulkan GLSL). Edit here, then run gen.py.
#version 450
// HD floor tiles, run on the final upscaled frame (after ScaleFX): floor
// pixels whose tile has an HD version are redrawn from the atlas, lit like the
// low-res frame was (ratio of the lit pixel to the tile's own low-res colour).
layout(set = 2, binding = 0) uniform sampler2D u_up;        // upscaled lit frame
layout(set = 2, binding = 1) uniform sampler2D u_lit;       // lit frame, 1x
layout(set = 2, binding = 2) uniform usampler2D u_gbuf;     // G-buffer
layout(set = 2, binding = 3) uniform sampler2DArray u_atlas; // HD tiles
layout(std140, set = 3, binding = 0) uniform LitUniforms {
    ivec4 u_rect;     // G-buffer x, y, w, h on screen
    vec4 u_sundir;    // xyz = towards the sun, w = trace length
    vec4 u_grade;     // xyz = outdoor colour grade
    vec4 u_params;    // x = ambient, y = sun, z = time (s), w = night
    ivec4 u_iparams;  // x = origin, y = mode, z = number of lights
    vec4 u_hd;        // HD tiles: x, y = a tile's top-left (G-buffer), z = scale, w = layers
    vec4 u_lpos[64];  // x, y (G-buffer), z, radius
    vec4 u_lcol[64];  // rgb, a = flicker
};
layout(location = 0) out vec4 frag;

const uint NONE = 0xFFFFFFFFu;

uint code_at(ivec2 q) {
    if (q.x < 0 || q.y < 0 || q.x >= u_rect.z || q.y >= u_rect.w) return NONE;
    return texelFetch(u_gbuf, q, 0).r & 0xFFFFFFu;
}

// Low-res reference of the HD tile at uv: average over about one game pixel.
vec3 atlas_lowres(vec2 uv, float layer) {
    vec2 px = 1.0 / vec2(textureSize(u_atlas, 0).xy);
    vec3 c = vec3(0.0);
    for (int j = -1; j <= 2; j++)
        for (int i = -1; i <= 2; i++)
            c += texture(u_atlas, vec3(uv + vec2(float(i) - 0.5, float(j) - 0.5) * px, layer)).rgb;
    return c / 16.0;
}

void main() {
    ivec2 p3 = ivec2(gl_FragCoord.xy);
    vec4 up = texelFetch(u_up, p3, 0);
    float scale = u_hd.z;
    vec2 p1f = (vec2(p3) + 0.5) / scale;   // position in game pixels
    ivec2 p1 = ivec2(p1f);
    ivec2 q = p1 - u_rect.xy;
    uint code = code_at(q);
    if (code == NONE || ((code >> 16) & 15u) != 0u || (code & 0xFFFFu) == 0u) { frag = up; return; }
    // Keep ScaleFX's pixels next to anything that isn't HD floor.
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            uint c = code_at(q + ivec2(i, j));
            if (c == NONE || ((c >> 16) & 15u) != 0u || (c & 0xFFFFu) == 0u) { frag = up; return; }
        }
    float layer = float((code & 0xFFFFu) - 1u);

    // Which diamond of the tile lattice the pixel is in, and where inside.
    vec2 c0 = u_hd.xy + vec2(39.0, 20.0);
    vec2 d = vec2(q) + fract(p1f) - c0;
    float a = round((d.x / 40.0 + d.y / 20.0) * 0.5);
    float b = round((d.y / 20.0 - d.x / 40.0) * 0.5);
    vec2 center = c0 + a * vec2(40.0, 20.0) + b * vec2(-40.0, 20.0);
    vec2 local = vec2(q) + fract(p1f) - center + vec2(39.0, 20.0);
    vec2 uv = local / vec2(78.0, 40.0);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) { frag = up; return; }

    vec4 hd = texture(u_atlas, vec3(uv, layer));
    if (hd.a < 0.5) { frag = up; return; }
    vec3 base = max(atlas_lowres(uv, layer), vec3(0.03));
    vec3 lit = texture(u_lit, (p1f) / vec2(textureSize(u_lit, 0))).rgb;
    vec3 ratio = clamp(lit / base, 0.0, 3.0);
    frag = vec4(clamp(hd.rgb * ratio, 0.0, 1.0), 1.0);
}
