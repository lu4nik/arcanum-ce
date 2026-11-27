// Local experiment: 2.5D per-pixel lighting pass (see tig/lit25d.h).
//
// Runs one fragment shader in a private OpenGL 4.1 core context on a hidden
// window (same approach as scalefx_gl.c) and reads the lit frame back.

#include "tig/lit25d.h"

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

static int lit_mode;
static TigLit25dProvider lit_provider;

int tig_lit25d_mode(void)
{
    return lit_mode;
}

void tig_lit25d_set_mode(int mode)
{
    lit_mode = mode;
}

void tig_lit25d_set_provider(TigLit25dProvider provider)
{
    lit_provider = provider;
}

#if defined(__APPLE__) && TARGET_OS_OSX

#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>

#include <string.h>

static const char* lit_vs
    = "#version 410 core\n"
      "in vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0); }\n";

// Pass 1: per-pixel geometry. Walls are drawn one tile-wide piece at a time
// and every piece has a single base point, which turns their shadows into
// stairs; here the base is interpolated across each piece from its
// neighbours, so a diagonal wall gets a continuous base line.
// Output: r = ground screen y of the pixel's base, g = height, b = class.
static const char* lit_geo_fs
    = "#version 410 core\n"
      "uniform usampler2D u_gbuf;\n"
      "uniform ivec4 u_rect;\n"
      "uniform int u_origin;\n"
      "out vec4 frag;\n"
      "const uint NONE = 0xFFFFFFFFu;\n"
      "const float ROOF_H = 110.0;\n"
      "uint code_at(ivec2 q) {\n"
      "    if (q.x < 0 || q.y < 0 || q.x >= u_rect.z || q.y >= u_rect.w) return NONE;\n"
      "    return texelFetch(u_gbuf, q, 0).r & 0xFFFFFFu;\n"
      "}\n"
      "uint cls_of(uint c) { return (c >> 16) & 15u; }\n"
      "float anchor_of(uint c) {\n"
      "    int s = (int(c & 0xFFFFu) + u_origin) & 0xFFFF;\n"
      "    if (s >= 32768) s -= 65536;\n"
      "    return float(s);\n"
      "}\n"
      "void main() {\n"
      "    ivec2 q = ivec2(gl_FragCoord.xy);\n"
      "    uint c = code_at(q);\n"
      "    uint cls = cls_of(c);\n"
      "    float y = float(q.y);\n"
      "    if (c == NONE || cls == 0u) { frag = vec4(y, 0.0, 0.0, 1.0); return; }\n"
      "    if (cls == 4u) { frag = vec4(y + ROOF_H, ROOF_H, 4.0, 1.0); return; }\n"
      "    float a = anchor_of(c);\n"
      "    if (cls == 3u) {\n"
      "        int dl = 0;\n"
      "        int dr = 0;\n"
      "        for (int i = 1; i <= 90; i++) { if (code_at(q - ivec2(i, 0)) != c) break; dl = i; }\n"
      "        for (int i = 1; i <= 90; i++) { if (code_at(q + ivec2(i, 0)) != c) break; dr = i; }\n"
      "        uint cl = code_at(q - ivec2(dl + 1, 0));\n"
      "        uint cr = code_at(q + ivec2(dr + 1, 0));\n"
      "        bool hl = cl != NONE && cls_of(cl) == 3u && abs(anchor_of(cl) - a) <= 30.0;\n"
      "        bool hr = cr != NONE && cls_of(cr) == 3u && abs(anchor_of(cr) - a) <= 30.0;\n"
      "        float el = hl ? (a + anchor_of(cl)) * 0.5 : a;\n"
      "        float er = hr ? (a + anchor_of(cr)) * 0.5 : a;\n"
      "        if (hl && !hr) er = a + (a - el);\n"
      "        if (hr && !hl) el = a + (a - er);\n"
      "        float t = (float(dl) + 0.5) / float(dl + dr + 1);\n"
      "        a = mix(el, er, t);\n"
      "    }\n"
      "    frag = vec4(a, max(a - y, 0.0), float(cls), 1.0);\n"
      "}\n";

// Pixel coordinates: texel row r of every texture is screen row r (the frame
// is uploaded top row first and read back the same way), so gl_FragCoord.y
// counts rows from the top of the screen.
static const char* lit_fs
    = "#version 410 core\n"
      "uniform sampler2D u_color;\n"
      "uniform usampler2D u_gbuf;\n"
      "uniform sampler2D u_geo;\n"
      "uniform ivec4 u_rect;\n" // G-buffer x, y, w, h on screen
      "uniform int u_origin;\n"
      "uniform float u_ambient;\n"
      "uniform float u_sun;\n"
      "uniform vec4 u_sundir;\n" // xyz = towards the sun, w = trace length
      "uniform int u_mode;\n"
      "uniform int u_num_lights;\n"
      "uniform vec4 u_lpos[64];\n" // x, y (window), z, radius
      "uniform vec4 u_lcol[64];\n"
      "out vec4 frag;\n"
      "\n"
      "const uint NONE = 0xFFFFFFFFu;\n"
      "const float ROOF_H = 110.0;\n"
      "const float THICK = 14.0;\n"
      "\n"
      "uint code_at(ivec2 q) {\n"
      "    if (q.x < 0 || q.y < 0 || q.x >= u_rect.z || q.y >= u_rect.w) return NONE;\n"
      "    return texelFetch(u_gbuf, q, 0).r & 0xFFFFFFu;\n"
      "}\n"
      "uint cls_of(uint c) { return (c >> 16) & 15u; }\n"
      "\n"
      // World: x = screen x, y = 2 * ground screen y (iso 2:1), z = height.
      "vec3 world_at(ivec2 q) {\n"
      "    vec4 g = texelFetch(u_geo, q, 0);\n"
      "    return vec3(float(q.x), g.r * 2.0, g.g);\n"
      "}\n"
      // Interleaved gradient noise: per-pixel jitter for soft shadows.
      "float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }\n"
      "ivec2 project(vec3 w) { return ivec2(floor(vec2(w.x, w.y * 0.5 - w.z) + 0.5)); }\n"
      "\n"
      // 1 = lit, 0 = fully blocked. `j` (0..1) shifts the samples so
      // neighbouring pixels sample different points.
      "float trace(vec3 from, vec3 to, uint self_code, int steps, float j) {\n"
      "    float occ = 0.0;\n"
      // Things hanging on the receiver's own plane (banners, paintings on a
      // wall) must not shade it; ground points keep contact shadows.
      "    bool raised = from.z > 0.5;\n"
      "    for (int i = 1; i < steps; i++) {\n"
      "        float t = (float(i) + j - 0.5) / float(steps);\n"
      "        vec3 s = mix(from, to, t);\n"
      "        ivec2 sp = project(s);\n"
      "        uint c = code_at(sp);\n"
      "        if (c == NONE || c == self_code) continue;\n"
      "        uint cls = cls_of(c);\n"
      "        if (cls == 0u || cls == 4u) continue;\n"
      "        vec3 o = world_at(sp);\n"
      "        cls = cls_of(c);\n"
      "        if (raised && abs(o.y - from.y) < (cls == 1u ? 46.0 : 16.0)) continue;\n"
      "        float dy = abs(o.y - s.y);\n"
      "        float th = cls == 3u ? THICK * 1.6 : THICK;\n"
      "        occ = max(occ, 1.0 - smoothstep(th * 0.4, th, dy));\n"
      "        if (occ >= 1.0) break;\n"
      "    }\n"
      "    return 1.0 - occ;\n"
      "}\n"
      "\n"
      "float luma(ivec2 p) { return dot(texelFetch(u_color, p, 0).rgb, vec3(0.299, 0.587, 0.114)); }\n"
      "\n"
      "void main() {\n"
      "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "    vec3 src = texelFetch(u_color, p, 0).rgb;\n"
      "    ivec2 q = p - u_rect.xy;\n"
      "    uint code = code_at(q);\n"
      "    if (code == NONE) { frag = vec4(src, 1.0); return; }\n"
      "    uint cls = cls_of(code);\n"
      "    bool indoor = ((code >> 20) & 1u) != 0u;\n"
      "    vec3 w = world_at(q);\n"
      "    float jn = ign(gl_FragCoord.xy);\n"
      "\n"
      "    if (u_mode == 2) {\n"
      "        vec3 tint = cls == 0u ? vec3(0.1, 0.15, 0.4) : cls == 1u ? vec3(1.0, 0.8, 0.3)\n"
      "            : cls == 2u ? vec3(1.0, 0.3, 0.3) : cls == 3u ? vec3(0.3, 1.0, 0.4) : vec3(0.7, 0.4, 1.0);\n"
      "        float hh = clamp(w.z / 160.0, 0.0, 1.0);\n"
      "        frag = vec4(mix(src * 0.25, tint * (0.25 + 0.75 * hh), 0.8), 1.0);\n"
      "        return;\n"
      "    }\n"
      "\n"
      // Normals. Bumps come from the art's own luminance; sprites get a rounded
      // profile from the distance to their left/right silhouette.
      // Wider, averaged differences: big shapes (cobbles, bricks) give relief,
      // thin lines (wood grain, plank gaps) mostly cancel out.
      "    float gx = 0.5 * (luma(p + ivec2(2, 0)) - luma(p - ivec2(2, 0)))\n"
      "        + 0.25 * (luma(p + ivec2(2, 1)) - luma(p - ivec2(2, 1)) + luma(p + ivec2(2, -1)) - luma(p - ivec2(2, -1)));\n"
      "    float gy = 0.5 * (luma(p + ivec2(0, 2)) - luma(p - ivec2(0, 2)))\n"
      "        + 0.25 * (luma(p + ivec2(1, 2)) - luma(p - ivec2(1, 2)) + luma(p + ivec2(-1, 2)) - luma(p - ivec2(-1, 2)));\n"
      "    gx = clamp(gx * 0.5, -0.12, 0.12);\n"
      "    gy = clamp(gy * 0.5, -0.12, 0.12);\n"
      "    vec3 n;\n"
      "    if (cls == 0u) {\n"
      "        n = normalize(vec3(-gx * 1.6, -gy * 1.6, 1.0));\n"
      "    } else if (cls == 4u) {\n"
      "        n = normalize(vec3(-gx, 0.5, 0.85));\n"
      "    } else if (cls == 3u) {\n"
      "        n = normalize(vec3(-gx * 1.5, 0.85, 0.45 + gy * 1.5));\n"
      "    } else {\n"
      "        int dl = 0;\n"
      "        int dr = 0;\n"
      "        for (int i = 1; i <= 20; i++) { if (code_at(q - ivec2(i, 0)) != code) break; dl = i; }\n"
      "        for (int i = 1; i <= 20; i++) { if (code_at(q + ivec2(i, 0)) != code) break; dr = i; }\n"
      "        float nx = float(dl - dr) / float(dl + dr + 2);\n"
      "        n = normalize(vec3(nx * 1.2 - gx, sqrt(max(1.0 - nx * nx, 0.0)) * 0.8, 0.45 + gy * 1.5));\n"
      "    }\n"
      "\n"
      "    vec3 acc = vec3(0.0);\n"
      "    float shade = 0.0;\n"
      "    for (int i = 0; i < u_num_lights; i++) {\n"
      "        vec4 lp = u_lpos[i];\n"
      "        vec3 lw = vec3(lp.x, lp.y * 2.0, lp.z);\n"
      "        vec3 d = lw - w;\n"
      "        float dist = length(d);\n"
      "        if (dist >= lp.w) continue;\n"
      "        float att = 1.0 - dist / lp.w;\n"
      "        att *= att;\n"
      "        float ndl = max(dot(n, d / max(dist, 1.0)), 0.0) * 0.85 + 0.15;\n"
      // Two rays to points around the lamp give a soft penumbra; stop short
      // of the light itself, it usually sits inside its lamp.
      "        vec3 side = normalize(vec3(-d.y, d.x, 0.0) + 1e-4) * 7.0;\n"
      "        float sh = 0.5 * (trace(w, mix(w, lw + side * (jn - 0.5) * 2.0, 0.9), code, 16, jn)\n"
      "            + trace(w, mix(w, lw - side * (jn - 0.5) * 2.0, 0.9), code, 16, fract(jn + 0.5)));\n"
      "        acc += u_lcol[i].rgb * att * ndl * sh;\n"
      // The game already lit this spot (coarsely), so a shadow from this lamp
      // also takes some of that away, or it would barely show.
      "        float strength = dot(u_lcol[i].rgb, vec3(0.333));\n"
      "        shade = max(shade, (1.0 - sh) * att * strength);\n"
      "    }\n"
      "\n"
      // The game's own coarse light is still in the frame, so this adds the
      // detail on top. Highlights are compressed so lamps glow instead of
      // blowing out to flat white.
      "    vec3 m = acc * (0.7 / max(u_ambient, 0.35));\n"
      "    m = m / (1.0 + m * 0.7);\n"
      "    vec3 outc = src * (1.0 + m * 1.5) * (1.0 - 0.45 * min(shade * 1.6, 1.0));\n"
      "\n"
      "    if (u_sun > 0.0 && cls != 4u && !indoor) {\n"
      "        vec3 sd = normalize(u_sundir.xyz);\n"
      // Four rays inside a small cone, rotated per pixel: soft edges.
      "        vec3 t1 = normalize(cross(sd, vec3(0.0, 0.0, 1.0)));\n"
      "        vec3 t2 = cross(sd, t1);\n"
      "        float sh = 0.0;\n"
      "        for (int k = 0; k < 4; k++) {\n"
      "            float ang = (float(k) + jn) * 1.5707963;\n"
      "            vec3 dir = normalize(sd + (t1 * cos(ang) + t2 * sin(ang)) * 0.07);\n"
      "            sh += trace(w, w + dir * u_sundir.w, code, 28, fract(jn + float(k) * 0.25));\n"
      "        }\n"
      "        sh *= 0.25;\n"
      "        float facing = cls == 0u ? 1.0 : 0.8 + 0.4 * max(dot(n, sd), 0.0);\n"
      "        outc *= mix(1.0, facing * mix(0.6, 1.0, sh), u_sun);\n"
      "    }\n"
      "\n"
      "    frag = vec4(min(outc, vec3(1.0)), 1.0);\n"
      "}\n";

static SDL_Window* lit_window;
static SDL_GLContext lit_context;
static bool lit_ready;
static bool lit_failed;
static GLuint lit_program;
static GLuint lit_geo_program;
static GLuint lit_geo_tex;
static GLuint lit_geo_fbo;
static GLint g_gbuf, g_rect, g_origin, u_geo;
static GLuint lit_vao;
static GLuint lit_vbo;
static GLuint lit_color_tex;
static GLuint lit_gbuf_tex;
static GLuint lit_out_tex;
static GLuint lit_fbo;
static int lit_w;
static int lit_h;
static int lit_gw;
static int lit_gh;
static GLint u_sundir;
static GLint u_color, u_gbuf, u_rect, u_origin, u_ambient, u_sun, u_mode, u_num_lights, u_lpos, u_lcol;
static TigLit25dFrame lit_frame;

static GLuint lit_compile(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        SDL_Log("Lit25d: shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint lit_link(const char* fs_src)
{
    GLuint vs = lit_compile(GL_VERTEX_SHADER, lit_vs);
    GLuint fs = lit_compile(GL_FRAGMENT_SHADER, fs_src);
    GLuint program = 0;
    if (vs != 0 && fs != 0) {
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "a_pos");
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[2048];
            glGetProgramInfoLog(program, sizeof(log), NULL, log);
            SDL_Log("Lit25d: link failed: %s", log);
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vs != 0) glDeleteShader(vs);
    if (fs != 0) glDeleteShader(fs);
    return program;
}

static bool lit_init(void)
{
    if (lit_ready) {
        return true;
    }
    if (lit_failed) {
        return false;
    }
    lit_failed = true;

    SDL_Window* prev_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext prev_context = SDL_GL_GetCurrentContext();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    lit_window = SDL_CreateWindow("Lit25d", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (lit_window != NULL) {
        lit_context = SDL_GL_CreateContext(lit_window);
    }
    SDL_GL_ResetAttributes();

    if (lit_window == NULL || lit_context == NULL) {
        SDL_Log("Lit25d: no OpenGL 4.1 context: %s", SDL_GetError());
        if (prev_context != NULL) {
            SDL_GL_MakeCurrent(prev_window, prev_context);
        }
        return false;
    }
    SDL_GL_MakeCurrent(lit_window, lit_context);

    lit_program = lit_link(lit_fs);
    lit_geo_program = lit_link(lit_geo_fs);
    bool ok = lit_program != 0 && lit_geo_program != 0;

    if (ok) {
        u_color = glGetUniformLocation(lit_program, "u_color");
        u_gbuf = glGetUniformLocation(lit_program, "u_gbuf");
        u_rect = glGetUniformLocation(lit_program, "u_rect");
        u_origin = glGetUniformLocation(lit_program, "u_origin");
        u_ambient = glGetUniformLocation(lit_program, "u_ambient");
        u_sun = glGetUniformLocation(lit_program, "u_sun");
        u_mode = glGetUniformLocation(lit_program, "u_mode");
        u_num_lights = glGetUniformLocation(lit_program, "u_num_lights");
        u_lpos = glGetUniformLocation(lit_program, "u_lpos");
        u_lcol = glGetUniformLocation(lit_program, "u_lcol");
        u_geo = glGetUniformLocation(lit_program, "u_geo");
        u_sundir = glGetUniformLocation(lit_program, "u_sundir");
        g_gbuf = glGetUniformLocation(lit_geo_program, "u_gbuf");
        g_rect = glGetUniformLocation(lit_geo_program, "u_rect");
        g_origin = glGetUniformLocation(lit_geo_program, "u_origin");

        static const float quad[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
        glGenVertexArrays(1, &lit_vao);
        glBindVertexArray(lit_vao);
        glGenBuffers(1, &lit_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, lit_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
    }

    if (prev_context != NULL) {
        SDL_GL_MakeCurrent(prev_window, prev_context);
    }
    if (!ok) {
        return false;
    }

    lit_failed = false;
    lit_ready = true;
    SDL_Log("Lit25d: ready (%s)", (const char*)glGetString(GL_RENDERER));
    return true;
}

static GLuint lit_make_texture(GLenum internal_format, GLenum format, GLenum type, int w, int h)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, w, h, 0, format, type, NULL);
    return tex;
}

bool tig_lit25d_apply(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    if (lit_mode == 0 || lit_provider == NULL) {
        return false;
    }

    memset(&lit_frame, 0, sizeof(lit_frame));
    if (!lit_provider(&lit_frame) || lit_frame.gbuf == NULL || lit_frame.width <= 0 || lit_frame.height <= 0) {
        return false;
    }

    if (!lit_init()) {
        lit_mode = 0;
        return false;
    }

    SDL_Window* prev_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext prev_context = SDL_GL_GetCurrentContext();
    SDL_GL_MakeCurrent(lit_window, lit_context);

    if (w != lit_w || h != lit_h) {
        if (lit_color_tex != 0) glDeleteTextures(1, &lit_color_tex);
        if (lit_out_tex != 0) glDeleteTextures(1, &lit_out_tex);
        if (lit_fbo != 0) glDeleteFramebuffers(1, &lit_fbo);
        lit_color_tex = lit_make_texture(GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, w, h);
        lit_out_tex = lit_make_texture(GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, w, h);
        glGenFramebuffers(1, &lit_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, lit_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lit_out_tex, 0);
        lit_w = w;
        lit_h = h;
    }
    if (lit_frame.width != lit_gw || lit_frame.height != lit_gh) {
        if (lit_gbuf_tex != 0) glDeleteTextures(1, &lit_gbuf_tex);
        lit_gbuf_tex = lit_make_texture(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, lit_frame.width, lit_frame.height);
        if (lit_geo_tex != 0) glDeleteTextures(1, &lit_geo_tex);
        if (lit_geo_fbo != 0) glDeleteFramebuffers(1, &lit_geo_fbo);
        lit_geo_tex = lit_make_texture(GL_RGBA32F, GL_RGBA, GL_FLOAT, lit_frame.width, lit_frame.height);
        glGenFramebuffers(1, &lit_geo_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, lit_geo_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lit_geo_tex, 0);
        lit_gw = lit_frame.width;
        lit_gh = lit_frame.height;
    }

    glBindTexture(GL_TEXTURE_2D, lit_color_tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, src_pitch_px);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, src);
    glBindTexture(GL_TEXTURE_2D, lit_gbuf_tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, lit_frame.gbuf_pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, lit_frame.width, lit_frame.height, GL_RED_INTEGER, GL_UNSIGNED_INT, lit_frame.gbuf);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(lit_vao);

    // Pass 1: geometry.
    glBindFramebuffer(GL_FRAMEBUFFER, lit_geo_fbo);
    glViewport(0, 0, lit_frame.width, lit_frame.height);
    glUseProgram(lit_geo_program);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, lit_gbuf_tex);
    glUniform1i(g_gbuf, 1);
    glUniform4i(g_rect, lit_frame.x, lit_frame.y, lit_frame.width, lit_frame.height);
    glUniform1i(g_origin, lit_frame.origin_y);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Pass 2: lighting.
    glBindFramebuffer(GL_FRAMEBUFFER, lit_fbo);
    glViewport(0, 0, w, h);
    glUseProgram(lit_program);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lit_geo_tex);
    glUniform1i(u_geo, 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lit_color_tex);
    glUniform1i(u_color, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, lit_gbuf_tex);
    glUniform1i(u_gbuf, 1);
    glActiveTexture(GL_TEXTURE0);

    glUniform4i(u_rect, lit_frame.x, lit_frame.y, lit_frame.width, lit_frame.height);
    glUniform1i(u_origin, lit_frame.origin_y);
    glUniform1f(u_ambient, lit_frame.ambient);
    glUniform1f(u_sun, lit_frame.sun);
    glUniform4f(u_sundir, lit_frame.sun_dir[0], lit_frame.sun_dir[1], lit_frame.sun_dir[2], lit_frame.sun_reach);
    glUniform1i(u_mode, lit_mode);

    float lpos[TIG_LIT25D_MAX_LIGHTS * 4];
    float lcol[TIG_LIT25D_MAX_LIGHTS * 4];
    int n = lit_frame.num_lights;
    if (n > TIG_LIT25D_MAX_LIGHTS) n = TIG_LIT25D_MAX_LIGHTS;
    for (int i = 0; i < n; i++) {
        const TigLit25dLight* l = &lit_frame.lights[i];
        lpos[i * 4 + 0] = l->x;
        lpos[i * 4 + 1] = l->y;
        lpos[i * 4 + 2] = l->z;
        lpos[i * 4 + 3] = l->radius;
        lcol[i * 4 + 0] = l->r;
        lcol[i * 4 + 1] = l->g;
        lcol[i * 4 + 2] = l->b;
        lcol[i * 4 + 3] = 1.0f;
    }
    glUniform1i(u_num_lights, n);
    if (n > 0) {
        glUniform4fv(u_lpos, n, lpos);
        glUniform4fv(u_lcol, n, lcol);
    }

    glBindVertexArray(lit_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glPixelStorei(GL_PACK_ROW_LENGTH, dst_pitch_px);
    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, dst);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    bool ok = true;
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        SDL_Log("Lit25d: GL error 0x%x", err);
        ok = false;
    }

    if (prev_context != NULL) {
        SDL_GL_MakeCurrent(prev_window, prev_context);
    }
    return ok;
}

#else

bool tig_lit25d_apply(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    (void)src;
    (void)src_pitch_px;
    (void)w;
    (void)h;
    (void)dst;
    (void)dst_pitch_px;
    return false;
}

#endif
