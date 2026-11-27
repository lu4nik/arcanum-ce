// Local tweak: ScaleFX 3x upscale on the GPU via OpenGL.
//
// Runs the five-pass ScaleFX shader chain by Sp00kyFox (libretro glsl-shaders,
// MIT license; files loaded at runtime from HighRes/shaders/) in a private
// OpenGL 4.1 core context on a hidden window, and reads the 3x result back into
// memory. The game keeps presenting through the SDL renderer as before.

#include "tig/scalefx_gl.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// macOS only: iOS has no desktop OpenGL.
#if defined(__APPLE__) && TARGET_OS_OSX

#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SFX_PASSES 5

typedef struct SfxPass {
    GLuint program;
    GLuint fbo;
    GLuint tex;
    int width;
    int height;
    GLint u_mvp;
    GLint u_texture;
    GLint u_texture_size;
    GLint u_input_size;
    GLint u_output_size;
    GLint u_frame_count;
    GLint u_frame_direction;
    GLint u_prev_tex[8];
    GLint u_prev_tex_size[8];
    GLint u_prev_input_size[8];
    GLint a_vertex;
    GLint a_texcoord;
    GLint a_color;
} SfxPass;

static const int sfx_scale[SFX_PASSES] = { 1, 1, 1, 1, 3 };
static const GLenum sfx_format[SFX_PASSES] = { GL_RGBA32F, GL_RGBA32F, GL_RGBA8, GL_RGBA8, GL_RGBA8 };

static SDL_Window* sfx_window;
static SDL_GLContext sfx_context;
static bool sfx_ready;
static bool sfx_failed;
static SfxPass sfx_passes[SFX_PASSES];
static GLuint sfx_src_tex;
static int sfx_w;
static int sfx_h;
static GLuint sfx_vao;
static GLuint sfx_vbo;

static char* read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)size + 1);
    if (buf != NULL) {
        size_t n = fread(buf, 1, (size_t)size, f);
        buf[n] = '\0';
    }
    fclose(f);
    return buf;
}

static GLuint compile(GLenum type, const char* body, const char* stage)
{
    // Drop the shader's own "#version" line; GLSL 4.10 core keeps the
    // libretro COMPAT_* macros on their __VERSION__ >= 130 branch.
    const char* rest = body;
    if (strncmp(rest, "#version", 8) == 0) {
        const char* nl = strchr(rest, '\n');
        rest = nl != NULL ? nl + 1 : "";
    }

    char header[64];
    snprintf(header, sizeof(header), "#version 410\n#define %s\n", stage);
    const char* sources[2] = { header, rest };

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 2, sources, NULL);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("ScaleFX: %s shader compile failed: %s", stage, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool load_pass(SfxPass* pass, const char* path)
{
    char* src = read_file(path);
    if (src == NULL) {
        SDL_Log("ScaleFX: cannot read %s", path);
        return false;
    }

    GLuint vs = compile(GL_VERTEX_SHADER, src, "VERTEX");
    GLuint fs = compile(GL_FRAGMENT_SHADER, src, "FRAGMENT");
    free(src);
    if (vs == 0 || fs == 0) {
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindFragDataLocation(program, 0, "FragColor");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        SDL_Log("ScaleFX: link failed for %s: %s", path, log);
        glDeleteProgram(program);
        return false;
    }

    pass->program = program;
    pass->u_mvp = glGetUniformLocation(program, "MVPMatrix");
    pass->u_texture = glGetUniformLocation(program, "Texture");
    pass->u_texture_size = glGetUniformLocation(program, "TextureSize");
    pass->u_input_size = glGetUniformLocation(program, "InputSize");
    pass->u_output_size = glGetUniformLocation(program, "OutputSize");
    pass->u_frame_count = glGetUniformLocation(program, "FrameCount");
    pass->u_frame_direction = glGetUniformLocation(program, "FrameDirection");
    for (int k = 0; k < 8; k++) {
        char name[64];
        snprintf(name, sizeof(name), "PassPrev%dTexture", k);
        pass->u_prev_tex[k] = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "PassPrev%dTextureSize", k);
        pass->u_prev_tex_size[k] = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "PassPrev%dInputSize", k);
        pass->u_prev_input_size[k] = glGetUniformLocation(program, name);
    }
    pass->a_vertex = glGetAttribLocation(program, "VertexCoord");
    pass->a_texcoord = glGetAttribLocation(program, "TexCoord");
    pass->a_color = glGetAttribLocation(program, "COLOR");
    return true;
}

static GLuint make_texture(GLenum internal_format, int w, int h)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLenum type = internal_format == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_FLOAT;
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, w, h, 0, GL_RGBA, type, NULL);
    return tex;
}

static void free_targets(void)
{
    for (int i = 0; i < SFX_PASSES; i++) {
        if (sfx_passes[i].fbo != 0) {
            glDeleteFramebuffers(1, &sfx_passes[i].fbo);
            sfx_passes[i].fbo = 0;
        }
        if (sfx_passes[i].tex != 0) {
            glDeleteTextures(1, &sfx_passes[i].tex);
            sfx_passes[i].tex = 0;
        }
    }
    if (sfx_src_tex != 0) {
        glDeleteTextures(1, &sfx_src_tex);
        sfx_src_tex = 0;
    }
    sfx_w = 0;
    sfx_h = 0;
}

static bool make_targets(int w, int h)
{
    free_targets();
    sfx_src_tex = make_texture(GL_RGBA8, w, h);
    int cw = w;
    int ch = h;
    for (int i = 0; i < SFX_PASSES; i++) {
        SfxPass* pass = &sfx_passes[i];
        pass->width = cw * sfx_scale[i];
        pass->height = ch * sfx_scale[i];
        pass->tex = make_texture(sfx_format[i], pass->width, pass->height);
        glGenFramebuffers(1, &pass->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, pass->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pass->tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("ScaleFX: framebuffer %d incomplete", i);
            return false;
        }
        cw = pass->width;
        ch = pass->height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    sfx_w = w;
    sfx_h = h;
    return true;
}

bool tig_scalefx_init(const char* shader_dir)
{
    if (sfx_ready) {
        return true;
    }
    if (sfx_failed) {
        return false;
    }
    sfx_failed = true;

    SDL_Window* prev_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext prev_context = SDL_GL_GetCurrentContext();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    sfx_window = SDL_CreateWindow("ScaleFX", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (sfx_window != NULL) {
        sfx_context = SDL_GL_CreateContext(sfx_window);
    }
    SDL_GL_ResetAttributes();

    if (sfx_window == NULL || sfx_context == NULL) {
        SDL_Log("ScaleFX: no OpenGL 4.1 context: %s", SDL_GetError());
        if (prev_context != NULL) {
            SDL_GL_MakeCurrent(prev_window, prev_context);
        }
        return false;
    }

    SDL_GL_MakeCurrent(sfx_window, sfx_context);

    bool ok = true;
    for (int i = 0; i < SFX_PASSES && ok; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/scalefx-pass%d.glsl", shader_dir, i);
        ok = load_pass(&sfx_passes[i], path);
    }

    if (ok) {
        // Full-screen quad: VertexCoord (x, y, 0, 1), TexCoord (x, y, 0, 0),
        // COLOR (1, 1, 1, 1), as a triangle strip.
        static const float quad[4][12] = {
            { 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1 },
            { 1, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1 },
            { 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1 },
            { 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1 },
        };
        glGenVertexArrays(1, &sfx_vao);
        glBindVertexArray(sfx_vao);
        glGenBuffers(1, &sfx_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, sfx_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    }

    if (prev_context != NULL) {
        SDL_GL_MakeCurrent(prev_window, prev_context);
    }

    if (!ok) {
        return false;
    }

    sfx_failed = false;
    sfx_ready = true;
    SDL_Log("ScaleFX: ready (%s)", (const char*)glGetString(GL_RENDERER));
    return true;
}

bool tig_scalefx_run(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    if (!sfx_ready) {
        return false;
    }

    SDL_Window* prev_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext prev_context = SDL_GL_GetCurrentContext();
    SDL_GL_MakeCurrent(sfx_window, sfx_context);

    bool ok = true;
    if (w != sfx_w || h != sfx_h) {
        ok = make_targets(w, h);
    }

    if (ok) {
        glBindTexture(GL_TEXTURE_2D, sfx_src_tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, src_pitch_px);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, src);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        static const float mvp[16] = {
            2, 0, 0, 0,
            0, 2, 0, 0,
            0, 0, 1, 0,
            -1, -1, 0, 1,
        };

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(sfx_vao);
        glBindBuffer(GL_ARRAY_BUFFER, sfx_vbo);

        for (int i = 0; i < SFX_PASSES; i++) {
            SfxPass* pass = &sfx_passes[i];
            GLuint in_tex = i == 0 ? sfx_src_tex : sfx_passes[i - 1].tex;
            int in_w = i == 0 ? w : sfx_passes[i - 1].width;
            int in_h = i == 0 ? h : sfx_passes[i - 1].height;

            glBindFramebuffer(GL_FRAMEBUFFER, pass->fbo);
            glViewport(0, 0, pass->width, pass->height);
            glUseProgram(pass->program);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, in_tex);
            if (pass->u_texture >= 0) glUniform1i(pass->u_texture, 0);
            if (pass->u_mvp >= 0) glUniformMatrix4fv(pass->u_mvp, 1, GL_FALSE, mvp);
            if (pass->u_texture_size >= 0) glUniform2f(pass->u_texture_size, (float)in_w, (float)in_h);
            if (pass->u_input_size >= 0) glUniform2f(pass->u_input_size, (float)in_w, (float)in_h);
            if (pass->u_output_size >= 0) glUniform2f(pass->u_output_size, (float)pass->width, (float)pass->height);
            if (pass->u_frame_count >= 0) glUniform1i(pass->u_frame_count, 0);
            if (pass->u_frame_direction >= 0) glUniform1i(pass->u_frame_direction, 1);

            // PassPrevK = output of pass (i - K); pass -1 is the original frame.
            int unit = 1;
            for (int k = 1; k < 8; k++) {
                if (pass->u_prev_tex[k] < 0 && pass->u_prev_tex_size[k] < 0) {
                    continue;
                }
                int j = i - k;
                if (j < -1) {
                    continue;
                }
                GLuint t = j == -1 ? sfx_src_tex : sfx_passes[j].tex;
                int tw = j == -1 ? w : sfx_passes[j].width;
                int th = j == -1 ? h : sfx_passes[j].height;
                if (pass->u_prev_tex[k] >= 0) {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    glBindTexture(GL_TEXTURE_2D, t);
                    glUniform1i(pass->u_prev_tex[k], unit);
                    unit++;
                }
                if (pass->u_prev_tex_size[k] >= 0) glUniform2f(pass->u_prev_tex_size[k], (float)tw, (float)th);
                if (pass->u_prev_input_size[k] >= 0) glUniform2f(pass->u_prev_input_size[k], (float)tw, (float)th);
            }
            glActiveTexture(GL_TEXTURE0);

            const GLsizei stride = 12 * sizeof(float);
            if (pass->a_vertex >= 0) {
                glEnableVertexAttribArray((GLuint)pass->a_vertex);
                glVertexAttribPointer((GLuint)pass->a_vertex, 4, GL_FLOAT, GL_FALSE, stride, (const void*)0);
            }
            if (pass->a_texcoord >= 0) {
                glEnableVertexAttribArray((GLuint)pass->a_texcoord);
                glVertexAttribPointer((GLuint)pass->a_texcoord, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(4 * sizeof(float)));
            }
            if (pass->a_color >= 0) {
                glEnableVertexAttribArray((GLuint)pass->a_color);
                glVertexAttribPointer((GLuint)pass->a_color, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(8 * sizeof(float)));
            }

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        SfxPass* last = &sfx_passes[SFX_PASSES - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, last->fbo);
        glPixelStorei(GL_PACK_ROW_LENGTH, dst_pitch_px);
        glReadPixels(0, 0, last->width, last->height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, dst);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        static int logged;
        if (!logged) {
            logged = 1;
            SDL_Log("ScaleFX: first frame %dx%d -> %dx%d", w, h, last->width, last->height);
        }

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            SDL_Log("ScaleFX: GL error 0x%x", err);
            ok = false;
        }
    }

    if (prev_context != NULL) {
        SDL_GL_MakeCurrent(prev_window, prev_context);
    }

    return ok;
}

void tig_scalefx_exit(void)
{
    if (!sfx_ready && sfx_context == NULL) {
        return;
    }
    if (sfx_context != NULL) {
        SDL_GL_MakeCurrent(sfx_window, sfx_context);
        free_targets();
        for (int i = 0; i < SFX_PASSES; i++) {
            if (sfx_passes[i].program != 0) {
                glDeleteProgram(sfx_passes[i].program);
                sfx_passes[i].program = 0;
            }
        }
        if (sfx_vbo != 0) glDeleteBuffers(1, &sfx_vbo);
        if (sfx_vao != 0) glDeleteVertexArrays(1, &sfx_vao);
        sfx_vbo = 0;
        sfx_vao = 0;
        SDL_GL_DestroyContext(sfx_context);
        sfx_context = NULL;
    }
    if (sfx_window != NULL) {
        SDL_DestroyWindow(sfx_window);
        sfx_window = NULL;
    }
    sfx_ready = false;
}

bool tig_scalefx_available(void)
{
    return true;
}

#else

bool tig_scalefx_init(const char* shader_dir)
{
    (void)shader_dir;
    return false;
}

bool tig_scalefx_run(const uint32_t* src, int src_pitch_px, int w, int h, uint32_t* dst, int dst_pitch_px)
{
    (void)src;
    (void)src_pitch_px;
    (void)w;
    (void)h;
    (void)dst;
    (void)dst_pitch_px;
    return false;
}

void tig_scalefx_exit(void)
{
}

bool tig_scalefx_available(void)
{
    return false;
}

#endif
