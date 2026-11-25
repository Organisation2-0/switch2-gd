// SPDX-License-Identifier: GPL-3.0-or-later
// Switch 2 backend: OpenGL ES 2 on the GPU (OunceVK) through EGL on the default NWindow.
// SDL's video subsystem is not used. Quads are collected and drawn in batches.
#ifdef __SWITCH__
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "gfx_backend.h"

#define MAX_QUADS 4096 // 4 vertices each, must stay addressable with 16-bit indices

struct GfxTex { GLuint id; };

typedef struct { float x, y, u, v; Uint8 r, g, b, a; } Vertex;

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLSurface s_surf = EGL_NO_SURFACE;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static GLuint s_prog, s_vbo, s_ibo, s_white;
static Vertex s_vtx[MAX_QUADS * 4];
static int s_nquads, s_additive;
static GLuint s_tex;
static int s_w, s_h;       // logical size (drawing coordinates)
static EGLint s_sw, s_sh;  // surface size in pixels

static const char *const k_vs =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_col;\n"
    "uniform vec2 u_size;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "}\n";

// highp where available: mediump is too coarse to address single texels of 2048 px sheets
static const char *const k_fs =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() { gl_FragColor = texture2D(u_tex, v_uv) * v_col; }\n";

static int fail(int code, const char *what)
{
    SDL_SetError("%s (EGL error 0x%x)", what, (unsigned)eglGetError());
    fprintf(stderr, "gl: %s\n", SDL_GetError());
    return code;
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "gl: shader: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static int init_egl(void)
{
    s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, NULL, NULL)) return fail(-1, "eglInitialize failed");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return fail(-1, "eglBindAPI failed");
    static const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(s_dpy, cfg_attr, &cfg, 1, &n) || n == 0) return fail(-2, "no EGL config");
    nwindowSetDimensions(nwindowGetDefault(), (u32)s_w, (u32)s_h);
    s_surf = eglCreateWindowSurface(s_dpy, cfg, nwindowGetDefault(), NULL);
    if (s_surf == EGL_NO_SURFACE) return fail(-2, "eglCreateWindowSurface failed");
    static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    s_ctx = eglCreateContext(s_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (s_ctx == EGL_NO_CONTEXT) return fail(-3, "eglCreateContext failed");
    if (!eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) return fail(-3, "eglMakeCurrent failed");
    eglSwapInterval(s_dpy, 1);
    s_sw = s_w;
    s_sh = s_h;
    eglQuerySurface(s_dpy, s_surf, EGL_WIDTH, &s_sw);
    eglQuerySurface(s_dpy, s_surf, EGL_HEIGHT, &s_sh);
    return 0;
}

static int init_gl(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, k_fs);
    if (!vs || !fs) return fail(-3, "shader compile failed");
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glBindAttribLocation(s_prog, 0, "a_pos");
    glBindAttribLocation(s_prog, 1, "a_uv");
    glBindAttribLocation(s_prog, 2, "a_col");
    glLinkProgram(s_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(s_prog, GL_LINK_STATUS, &linked);
    if (!linked) return fail(-3, "shader link failed");
    glUseProgram(s_prog);
    glUniform2f(glGetUniformLocation(s_prog, "u_size"), (float)s_w, (float)s_h);
    glUniform1i(glGetUniformLocation(s_prog, "u_tex"), 0);

    static GLushort idx[MAX_QUADS * 6];
    for (int q = 0; q < MAX_QUADS; q++) {
        static const int corner[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; i++) idx[q * 6 + i] = (GLushort)(q * 4 + corner[i]);
    }
    glGenBuffers(1, &s_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    for (int i = 0; i < 3; i++) glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, r));

    static const unsigned char white[4] = {255, 255, 255, 255};
    glGenTextures(1, &s_white);
    glBindTexture(GL_TEXTURE_2D, s_white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); // flipped sprites have reversed winding
    glEnable(GL_BLEND);
    fprintf(stderr, "gl: %s, %s (%dx%d)\n", (const char *)glGetString(GL_RENDERER),
            (const char *)glGetString(GL_VERSION), (int)s_sw, (int)s_sh);
    return 0;
}

int gfxb_init(Gfx *g, int w, int h, int fullscreen)
{
    (void)g;
    (void)fullscreen;
    if (SDL_Init(SDL_INIT_EVENTS) != 0) return -1;
    s_w = w;
    s_h = h;
    int rc = init_egl();
    if (rc == 0) rc = init_gl();
    return rc;
}

void gfxb_quit(Gfx *g)
{
    (void)g;
    if (s_dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s_ctx != EGL_NO_CONTEXT) eglDestroyContext(s_dpy, s_ctx); // frees all GL objects
    if (s_surf != EGL_NO_SURFACE) eglDestroySurface(s_dpy, s_surf);
    eglTerminate(s_dpy);
    s_dpy = EGL_NO_DISPLAY;
    s_surf = EGL_NO_SURFACE;
    s_ctx = EGL_NO_CONTEXT;
    s_nquads = 0;
}

static void flush(void)
{
    if (!s_nquads) return;
    glBindTexture(GL_TEXTURE_2D, s_tex);
    if (s_additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
    else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s_nquads * 4 * sizeof(Vertex)), s_vtx, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, s_nquads * 6, GL_UNSIGNED_SHORT, 0);
    s_nquads = 0;
}

GfxTex *gfxb_tex_create(const unsigned char *rgba, int w, int h)
{
    GfxTex *t = malloc(sizeof *t);
    if (!t) return NULL;
    flush();
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        SDL_SetError("texture %dx%d: GL error 0x%x", w, h, (unsigned)err);
        fprintf(stderr, "gl: %s\n", SDL_GetError());
        glDeleteTextures(1, &t->id);
        free(t);
        return NULL;
    }
    return t;
}

void gfxb_tex_destroy(GfxTex *t)
{
    flush(); // queued quads may still use it
    if (s_ctx != EGL_NO_CONTEXT) glDeleteTextures(1, &t->id);
    free(t);
}

void gfxb_quad(GfxTex *tex, const SDL_FPoint pos[4], const SDL_FPoint uv[4], const SDL_Color col[4],
               int additive)
{
    GLuint id = tex ? tex->id : s_white;
    if (s_nquads == MAX_QUADS || (s_nquads && (id != s_tex || additive != s_additive))) flush();
    s_tex = id;
    s_additive = additive;
    Vertex *v = &s_vtx[s_nquads++ * 4];
    for (int i = 0; i < 4; i++) {
        v[i] = (Vertex){pos[i].x, pos[i].y, uv[i].x, uv[i].y, col[i].r, col[i].g, col[i].b, col[i].a};
    }
}

void gfxb_clip(const SDL_Rect *r)
{
    flush();
    if (!r) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST); // GL counts rows from the bottom
    glScissor(r->x * s_sw / s_w, (s_h - r->y - r->h) * s_sh / s_h, r->w * s_sw / s_w, r->h * s_sh / s_h);
}

void gfxb_begin_frame(void)
{
    flush();
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, s_sw, s_sh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gfxb_present(void)
{
    flush();
    static int swap_failed;
    if (!eglSwapBuffers(s_dpy, s_surf) && !swap_failed) {
        swap_failed = 1;
        fprintf(stderr, "gl: eglSwapBuffers failed (0x%x)\n", (unsigned)eglGetError());
    }
    // frame rate in the log, to judge the speed on hardware
    static Uint64 since;
    static int frames;
    Uint64 now = SDL_GetPerformanceCounter();
    if (!since) since = now;
    if (++frames == 600) {
        fprintf(stderr, "gl: %.1f fps\n",
                frames * (double)SDL_GetPerformanceFrequency() / (double)(now - since));
        since = now;
        frames = 0;
    }
}

void gfxb_output_size(int *w, int *h)
{
    *w = s_sw;
    *h = s_sh;
}

int gfxb_read_pixels(unsigned char *rgba, int w, int h)
{
    flush();
    unsigned char *row = malloc((size_t)w * 4);
    if (!row) return -1;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int y = 0; y < h / 2; y++) { // GL returns the bottom row first
        unsigned char *a = rgba + (size_t)y * w * 4, *b = rgba + (size_t)(h - 1 - y) * w * 4;
        memcpy(row, a, (size_t)w * 4);
        memcpy(a, b, (size_t)w * 4);
        memcpy(b, row, (size_t)w * 4);
    }
    free(row);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}
#endif
