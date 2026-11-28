// Lit25d shader (Vulkan GLSL). Edit here, then run gen.py.
#version 450
layout(set = 2, binding = 0) uniform sampler2D u_color;
layout(set = 2, binding = 1) uniform usampler2D u_gbuf;
layout(set = 2, binding = 2) uniform sampler2D u_geo;
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
const float THICK = 14.0;

uint code_at(ivec2 q) {
    if (q.x < 0 || q.y < 0 || q.x >= u_rect.z || q.y >= u_rect.w) return NONE;
    return texelFetch(u_gbuf, q, 0).r & 0xFFFFFFu;
}
uint cls_of(uint c) { return (c >> 16) & 15u; }

vec3 world_at(ivec2 q) {
    vec4 g = texelFetch(u_geo, q, 0);
    return vec3(float(q.x), g.r * 2.0, g.g);
}
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
ivec2 project(vec3 w) { return ivec2(floor(vec2(w.x, w.y * 0.5 - w.z) + 0.5)); }

// Class of the pixel being lit; walls don't shade themselves through their
// neighbouring pieces.
uint g_rcls;
float trace(vec3 from, vec3 to, uint self_code, int steps, float j) {
    float occ = 0.0;
    bool raised = from.z > 0.5;
    for (int i = 1; i < steps; i++) {
        float t = (float(i) + j - 0.5) / float(steps);
        vec3 s = mix(from, to, t);
        ivec2 sp = project(s);
        uint c = code_at(sp);
        if (c == NONE || c == self_code) continue;
        uint cls = cls_of(c);
        if (cls == 0u || cls == 4u || cls == 15u) continue;
        vec3 o = world_at(sp);
        cls = cls_of(c);
        if (g_rcls == 3u && cls == 3u && length(o.xy - from.xy) < 90.0) continue;
        if (raised && abs(o.y - from.y) < (cls == 1u ? 46.0 : 16.0)) continue;
        float dy = abs(o.y - s.y);
        float th = cls == 3u ? THICK * 1.6 : THICK;
        occ = max(occ, 1.0 - smoothstep(th * 0.4, th, dy));
        if (occ >= 1.0) break;
    }
    return 1.0 - occ;
}

float solid_at(vec3 s, uint self_code) {
    ivec2 sp = project(s);
    uint c = code_at(sp);
    if (c == NONE || c == self_code) return 0.0;
    uint cls = cls_of(c);
    if (cls == 0u || cls == 4u || cls == 15u) return 0.0;
    if (g_rcls == 3u && cls == 3u) return 0.0;
    vec3 o = world_at(sp);
    return 1.0 - smoothstep(THICK * 0.6, THICK * 1.4, abs(o.y - s.y));
}
float ambient_occlusion(vec3 w, uint self_code, float j) {
    float occ = 0.0;
    for (int k = 0; k < 8; k++) {
        float a = (float(k) + j) * 0.785398;
        vec2 d = vec2(cos(a), sin(a));
        occ += solid_at(w + vec3(d * 14.0, 6.0), self_code);
        occ += 0.6 * solid_at(w + vec3(d * 30.0, 14.0), self_code);
    }
    return clamp(occ / 12.8, 0.0, 1.0);
}

float luma(ivec2 p) { return dot(texelFetch(u_color, p, 0).rgb, vec3(0.299, 0.587, 0.114)); }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 src = texelFetch(u_color, p, 0).rgb;
    ivec2 q = p - u_rect.xy;
    uint code = code_at(q);
    if (code == NONE) { frag = vec4(src, 1.0); return; }
    // Text and dialog boxes the game draws into the world layer.
    if (cls_of(code) == 15u) { frag = vec4(src, 1.0); return; }
    uint cls = cls_of(code);
    g_rcls = cls;
    bool indoor = ((code >> 20) & 1u) != 0u;
    vec3 w = world_at(q);
    float jn = ign(gl_FragCoord.xy);

    float gx = 0.5 * (luma(p + ivec2(2, 0)) - luma(p - ivec2(2, 0)))
        + 0.25 * (luma(p + ivec2(2, 1)) - luma(p - ivec2(2, 1)) + luma(p + ivec2(2, -1)) - luma(p - ivec2(2, -1)));
    float gy = 0.5 * (luma(p + ivec2(0, 2)) - luma(p - ivec2(0, 2)))
        + 0.25 * (luma(p + ivec2(1, 2)) - luma(p - ivec2(1, 2)) + luma(p + ivec2(-1, 2)) - luma(p - ivec2(-1, 2)));
    gx = clamp(gx * 0.5, -0.12, 0.12);
    gy = clamp(gy * 0.5, -0.12, 0.12);
    vec3 n;
    if (cls == 0u) {
        n = normalize(vec3(-gx * 1.6, -gy * 1.6, 1.0));
    } else if (cls == 4u) {
        n = normalize(vec3(-gx, 0.5, 0.85));
    } else if (cls == 3u) {
        n = normalize(vec3(-gx * 1.5, 0.85, 0.45 + gy * 1.5));
    } else {
        int dl = 0;
        int dr = 0;
        for (int i = 1; i <= 20; i++) { if (code_at(q - ivec2(i, 0)) != code) break; dl = i; }
        for (int i = 1; i <= 20; i++) { if (code_at(q + ivec2(i, 0)) != code) break; dr = i; }
        float nx = float(dl - dr) / float(dl + dr + 2);
        n = normalize(vec3(nx * 1.2 - gx, sqrt(max(1.0 - nx * nx, 0.0)) * 0.8, 0.45 + gy * 1.5));
        // Characters with baked normals (top byte, see game/lit25d.c):
        // screen-space x right, y up, z towards the viewer.
        uint hi = texelFetch(u_gbuf, q, 0).r >> 24;
        uint na = hi >> 4;
        uint nb = hi & 15u;
        if (cls == 2u && na >= 1u && na <= 14u && nb >= 1u && nb <= 14u) {
            float sx = (float(na) - 7.5) / 6.5;
            float sy = (float(nb) - 7.5) / 6.5;
            float sz = sqrt(max(1.0 - sx * sx - sy * sy, 0.05));
            // Screen up is (0, -1, 2)/sqrt5 in the world, the view (0, 2, 1)/sqrt5.
            n = normalize(vec3(sx, (-sy + 2.0 * sz) * 0.4472, (2.0 * sy + sz) * 0.4472));
        }
    }

    if (u_mode == 2) {
        vec3 tint = cls == 0u ? vec3(0.1, 0.15, 0.4) : cls == 1u ? vec3(1.0, 0.8, 0.3)
            : cls == 2u ? vec3(1.0, 0.3, 0.3) : cls == 3u ? vec3(0.3, 1.0, 0.4) : vec3(0.7, 0.4, 1.0);
        float hh = clamp(w.z / 160.0, 0.0, 1.0);
        frag = vec4(mix(src * 0.25, tint * (0.25 + 0.75 * hh), 0.8), 1.0);
        if (cls == 2u) frag = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 acc = vec3(0.0);
    vec3 glow = vec3(0.0);
    float shade = 0.0;
    for (int i = 0; i < u_num_lights; i++) {
        vec4 lp = u_lpos[i];
        vec3 lw = vec3(lp.x, lp.y * 2.0, lp.z);
        vec3 d = lw - w;
        // A wall is lit on its front face only: lamps behind it don't count.
        if (cls == 3u && d.y < -6.0) continue;
        // Roofs cover lamps rather than catch their light.
        if (cls == 4u) continue;
        float dist = length(d);
        if (dist >= lp.w) continue;
        float att = 1.0 - dist / lp.w;
        att *= att;
        float fl = fract(u_lcol[i].a);
        float fi = float(i);
        att *= 1.0 + fl * (0.6 * sin(u_time * 6.1 + fi * 1.7) + 0.4 * sin(u_time * 13.7 + fi * 3.9));
        float ndl = max(dot(n, d / max(dist, 1.0)), 0.0) * 0.85 + 0.15;
        vec3 side = normalize(vec3(-d.y, d.x, 0.0) + 1e-4) * 7.0;
        float sh = 0.5 * (trace(w, mix(w, lw + side * (jn - 0.5) * 2.0, 0.9), code, 16, jn)
            + trace(w, mix(w, lw - side * (jn - 0.5) * 2.0, 0.9), code, 16, fract(jn + 0.5)));
        acc += u_lcol[i].rgb * att * ndl * sh;
        float strength = dot(u_lcol[i].rgb, vec3(0.333));
        shade = max(shade, (1.0 - sh) * att * strength);
        if (cls == 0u && !indoor && u_night > 0.0) {
            vec2 ls = vec2(lp.x, lp.y);
            vec2 dd = vec2(q) - ls;
            float below = smoothstep(-4.0, 10.0, dd.y);
            float streak = exp(-dd.x * dd.x / 60.0 - dd.y * dd.y / 3200.0) * below;
            glow += u_lcol[i].rgb * streak * sh * 0.16 * u_night;
        }
    }
    for (int i = 0; i < u_num_lights; i++) {
        // Only lamps with a visible flame (alpha >= 1) glow in the air; the
        // game also has invisible light sources for mood.
        if (u_lcol[i].a < 1.0) continue;
        vec4 lp = u_lpos[i];
        vec2 flame = vec2(lp.x, lp.y - lp.z);
        vec2 dd = vec2(q) - flame;
        float r2 = dot(dd, dd);
        glow += u_lcol[i].rgb * (exp(-r2 / 260.0) * 0.35 + exp(-r2 / 2400.0) * 0.12) * (0.35 + 0.65 * u_night);
    }

    vec3 m = acc * (0.7 / max(u_ambient, 0.35));
    m = m / (1.0 + m * 0.7);
    vec3 outc = src * (1.0 + m * 1.5) * (1.0 - 0.45 * min(shade * 1.6, 1.0));

    if (u_sun > 0.0 && cls != 4u && !indoor) {
        vec3 sd = normalize(u_sundir.xyz);
        vec3 t1 = normalize(cross(sd, vec3(0.0, 0.0, 1.0)));
        vec3 t2 = cross(sd, t1);
        float sh = 0.0;
        for (int k = 0; k < 4; k++) {
            float ang = (float(k) + jn) * 1.5707963;
            vec3 dir = normalize(sd + (t1 * cos(ang) + t2 * sin(ang)) * 0.07);
            sh += trace(w, w + dir * u_sundir.w, code, 28, fract(jn + float(k) * 0.25));
        }
        sh *= 0.25;
        float facing = cls == 0u ? 1.0 : 0.8 + 0.4 * max(dot(n, sd), 0.0);
        // Characters get real modelling from the sun: lit side, shaded side.
        // Characters get real modelling: the sun as a key (often from behind,
        // so a rim) and a soft fill from the viewer's side opposite to it.
        if (cls == 2u) {
            vec3 fill = normalize(vec3(-sd.x, 1.0, 0.9));
            facing = 0.55 + 0.5 * max(dot(n, sd), 0.0) + 0.38 * max(dot(n, fill), 0.0);
        }
        outc *= mix(1.0, facing * mix(0.6, 1.0, sh), u_sun);
    }

    float ao = cls == 4u ? 0.0 : ambient_occlusion(w, code, jn);
    outc *= 1.0 - 0.38 * ao;
    if (!indoor) outc *= u_grade.xyz;
    outc += glow;
    frag = vec4(min(outc, vec3(1.0)), 1.0);
}
