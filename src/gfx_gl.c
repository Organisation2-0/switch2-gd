// SPDX-License-Identifier: GPL-3.0-or-later
// Platform backend: OpenGL ES 2 on the Switch 2 GPU (OunceVK, through EGL on the default NWindow,
// without SDL's video system) and the same code on the PC with OpenGL 2.1 in an SDL window.
// Quads are collected and drawn in batches into a picture of their own; shader effects turn that
// picture into a second one; presenting copies it to the screen (with black bars if the screen has
// another shape).
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx_backend.h"

#ifdef __SWITCH__
#include <switch.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#else
#include <SDL_opengl.h>

// the functions used, fetched from the driver when the window is there
#define GL_FUNCTIONS(F)                                                                                                \
    F(void, ActiveTexture, (GLenum)) F(void, AttachShader, (GLuint, GLuint))                                           \
    F(void, BindAttribLocation, (GLuint, GLuint, const char *)) F(void, BindBuffer, (GLenum, GLuint))                  \
    F(void, BindFramebuffer, (GLenum, GLuint)) F(void, BindTexture, (GLenum, GLuint))                                  \
    F(void, BlendFunc, (GLenum, GLenum)) F(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                  \
    F(void, BufferData, (GLenum, ptrdiff_t, const void *, GLenum)) F(GLenum, CheckFramebufferStatus, (GLenum))         \
    F(void, Clear, (GLbitfield)) F(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                             \
    F(void, CompileShader, (GLuint)) F(GLuint, CreateProgram, (void)) F(GLuint, CreateShader, (GLenum))                \
    F(void, DeleteFramebuffers, (GLsizei, const GLuint *)) F(void, DeleteShader, (GLuint))                             \
    F(void, DeleteTextures, (GLsizei, const GLuint *)) F(void, Disable, (GLenum))                                      \
    F(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) F(void, Enable, (GLenum))                           \
    F(void, EnableVertexAttribArray, (GLuint)) F(void, Finish, (void))                                                 \
    F(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                             \
    F(void, GenBuffers, (GLsizei, GLuint *)) F(void, GenFramebuffers, (GLsizei, GLuint *))                             \
    F(void, GenTextures, (GLsizei, GLuint *)) F(GLenum, GetError, (void))                                              \
    F(void, GetProgramiv, (GLuint, GLenum, GLint *)) F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *))   \
    F(void, GetShaderiv, (GLuint, GLenum, GLint *)) F(const GLubyte *, GetString, (GLenum))                            \
    F(GLint, GetUniformLocation, (GLuint, const char *)) F(void, LinkProgram, (GLuint))                                \
    F(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))                                      \
    F(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                                                 \
    F(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *))                                       \
    F(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))                 \
    F(void, TexParameteri, (GLenum, GLenum, GLint)) F(void, Uniform1i, (GLint, GLint))                                 \
    F(void, Uniform2f, (GLint, GLfloat, GLfloat)) F(void, Uniform4fv, (GLint, GLsizei, const GLfloat *))               \
    F(void, UseProgram, (GLuint)) F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    F(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#define DECLARE(ret, name, args) static ret (APIENTRY *p_gl##name) args;
GL_FUNCTIONS(DECLARE)
#undef DECLARE

#define glActiveTexture p_glActiveTexture
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glBindBuffer p_glBindBuffer
#define glBindFramebuffer p_glBindFramebuffer
#define glBindTexture p_glBindTexture
#define glBlendFunc p_glBlendFunc
#define glBlendFuncSeparate p_glBlendFuncSeparate
#define glBufferData p_glBufferData
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glCompileShader p_glCompileShader
#define glCreateProgram p_glCreateProgram
#define glCreateShader p_glCreateShader
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glDeleteShader p_glDeleteShader
#define glDeleteTextures p_glDeleteTextures
#define glDisable p_glDisable
#define glDrawElements p_glDrawElements
#define glEnable p_glEnable
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glFinish p_glFinish
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glGenBuffers p_glGenBuffers
#define glGenFramebuffers p_glGenFramebuffers
#define glGenTextures p_glGenTextures
#define glGetError p_glGetError
#define glGetProgramiv p_glGetProgramiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glGetShaderiv p_glGetShaderiv
#define glGetString p_glGetString
#define glGetUniformLocation p_glGetUniformLocation
#define glLinkProgram p_glLinkProgram
#define glReadPixels p_glReadPixels
#define glScissor p_glScissor
#define glShaderSource p_glShaderSource
#define glTexImage2D p_glTexImage2D
#define glTexParameteri p_glTexParameteri
#define glUniform1i p_glUniform1i
#define glUniform2f p_glUniform2f
#define glUniform4fv p_glUniform4fv
#define glUseProgram p_glUseProgram
#define glVertexAttribPointer p_glVertexAttribPointer
#define glViewport p_glViewport
#endif

#define MAX_QUADS 4096 // 4 vertices each, must stay addressable with 16-bit indices

struct GfxTex { GLuint id; };

typedef struct { float x, y, u, v; Uint8 r, g, b, a; } Vertex;

static GLuint s_prog, s_fx, s_vbo, s_ibo, s_white;
static GLint s_fx_values;
static Vertex s_vtx[MAX_QUADS * 4];
static int s_nquads, s_blend;
static GLuint s_tex;
static int s_w, s_h;                    // logical size (drawing coordinates)
static int s_pw, s_ph;                  // size of the picture in pixels
static int s_vx, s_vy, s_sw, s_sh;      // where it lies on the screen, and the screen's size
static GLuint s_target[3], s_picture[3];   // two to draw into in turn, one for single layers
static int s_now;                       // the picture being drawn into
static int s_clipped;
static SDL_Rect s_clip;

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
#define PRECISION                      \
    "#ifdef GL_ES\n"                   \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" \
    "precision highp float;\n"         \
    "#else\n"                          \
    "precision mediump float;\n"       \
    "#endif\n"                         \
    "#endif\n"

static const char *const k_fs =
    PRECISION
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() { gl_FragColor = texture2D(u_tex, v_uv) * v_col; }\n";

// The shader effects of the level, all in one pass. Positions are 0..1 from the lower left corner
// of the picture; the meaning of u[] is that of GfxEffects (gfx.h). First the place to read from is
// bent, then the colour found there is changed.
static const char *const k_effects =
    PRECISION
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u[28];\n"
    "varying vec2 v_uv;\n"
    "float random(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 4.1414))) * 437.5854); }\n"
    "float noise(vec2 p) {\n"                                      // smooth between whole numbers
    "    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(random(i), random(i + vec2(1.0, 0.0)), f.x),\n"
    "               mix(random(i + vec2(0.0, 1.0)), random(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "float ramp(float a, float b, float x) {\n"                    // 0 at a, 1 at b; b may lie below a
    "    float d = b - a; if (abs(d) < 0.00001) d = 0.00001;\n"
    "    float k = clamp((x - a) / d, 0.0, 1.0); return k * k * (3.0 - 2.0 * k);\n"
    "}\n"
    "float ring(vec4 e, float x) { return clamp(ramp(e.x, e.y, x) - ramp(e.z, e.w, x), 0.0, 1.0); }\n"
    "vec4 pick(vec2 p) {\n"                                        // the picture, in blocks if pixelated
    "    vec2 size = u[14].zw;\n"
    "    if (size.x <= 0.0) return texture2D(u_tex, p);\n"
    "    if (u[15].x > 0.5) return texture2D(u_tex, (floor(p / size) + 0.5) * size);\n"
    "    vec2 q = p / size - 0.5, i = floor(q), f = q - i;\n"
    "    vec2 a = (i + 0.5) * size, b = a + size;\n"
    "    return mix(mix(texture2D(u_tex, a), texture2D(u_tex, vec2(b.x, a.y)), f.x),\n"
    "               mix(texture2D(u_tex, vec2(a.x, b.y)), texture2D(u_tex, b), f.x), f.y);\n"
    "}\n"
    "vec4 apart(vec4 c, vec2 p, vec2 q) {\n"                      // red from one place, blue from another
    "    vec4 r = pick(p), b = pick(q);\n"
    "    return vec4(r.r, c.g, b.b, max(c.a, max(r.a, b.a)));\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = v_uv;\n"
    "    vec2 tall = vec2(1.0, 1.0 / u[0].y);\n"                   // to widths and back
    "    vec2 back = vec2(1.0, u[0].y);\n"
    "    if (u[7].x != 0.0) {\n"                                   // shock wave
    "        vec2 d = (uv - u[6].xy) * tall; float r = max(length(d), 0.0001);\n"
    "        float k = u[7].y != 0.0 ? clamp(1.0 - r * u[7].y, 0.0, 1.0) : 1.0;\n"
    "        if (u[9].x > 0.5) r = u[7].z - max(r, u[7].w);\n"
    "        k *= ring(u[8], r);\n"
    "        float from = abs(r) < 0.0001 ? 0.0001 : r;\n"
    "        uv += k * (d / from) * sin(r * u[6].w - u[6].z) * u[7].x * back;\n"
    "    }\n"
    "    if (u[11].z != 0.0) {\n"                                  // shock line
    "        float r = u[10].y > 0.5 ? (uv.y - u[10].x) * tall.y : uv.x - u[10].x;\n"
    "        float k = u[11].w == 0.0 ? 1.0 : clamp(1.0 - abs(r) * u[11].w, 0.0, 1.0);\n"
    "        if (u[10].w > 0.5) r = abs(r);\n"
    "        if (u[10].z > 0.5) r = 1.0 - r;\n"
    "        k *= ring(u[12], r);\n"
    "        uv += k * sin(r * u[11].y - u[11].x) * u[11].z * back;\n"
    "    }\n"
    "    if (u[2].w > 0.0) {\n"                                    // bulge
    "        vec2 d = (uv - u[2].xy) * tall; float r = length(d);\n"
    "        if (r < u[2].z && r > 0.00001) {\n"
    "            float k = r / u[2].z; k = k * k * k;\n"
    "            vec2 q = u[2].xy + d / r * tan(min(r * u[2].w, 1.5)) * u[3].x * back;\n"
    "            uv = mix(q, uv, k);\n"
    "        }\n"
    "    }\n"
    "    if (u[3].y > 0.0) {\n"                                    // pinch
    "        vec2 d = (uv - u[4].xy) * tall; float r = length(d);\n"
    "        if (r < u[3].y && r > 0.00001) {\n"
    "            float k = r / u[3].y; k = k * k;\n"
    "            vec2 reach = vec2(atan(r * u[4].z), atan(r * u[4].w)) * u[5].xy;\n"
    "            uv = mix(u[4].xy + d / r * reach * back, uv, k);\n"
    "        }\n"
    "    }\n"
    "    if (u[13].w > 0.0 && uv.y >= u[13].x && uv.y < u[13].y) uv.x += u[13].z;\n"   // glitch
    "    if (u[1].x != 0.0) {\n"                                   // split screen: halves from both edges
    "        uv.x = fract(uv.x < 0.5 ? uv.x * u[1].x : (uv.x - 1.0) * u[1].x + 1.0);\n"
    "        uv.y = fract(uv.y < 0.5 ? uv.y * u[1].y : (uv.y - 1.0) * u[1].y + 1.0);\n"
    "    }\n"
    "    vec4 c = pick(uv);\n"
    "    if (u[16].x != 0.0) {\n"                                  // line glitch with red and blue apart
    "        float cell = v_uv.y * u[16].w;\n"
    "        uv.x += (noise(vec2(u[16].z, cell)) - noise(vec2(0.0, cell))) * u[16].y;\n"
    "        c = apart(pick(uv), uv - vec2(u[16].x, 0.0), uv + vec2(u[16].x, 0.0));\n"
    "        if (u[17].x > 0.0)\n"
    "            c.rgb -= smoothstep(u[17].x - 0.01, u[17].x + 0.01, sin(v_uv.y * u[17].z)) * u[17].y * c.a;\n"
    "    }\n"
    "    if (u[13].w > 0.0) {\n"                                   // glitch: one colour from elsewhere
    "        vec4 other = pick(uv + u[14].xy);\n"
    "        if (u[13].w < 1.5) c.r = other.r; else if (u[13].w < 2.5) c.g = other.g; else c.b = other.b;\n"
    "        c.a = max(c.a, other.a);\n"
    "    }\n"
    "    if (u[15].z != 0.0 || u[15].w != 0.0) {\n"                // chromatic
    "        c = apart(c, uv + u[15].zw, uv - u[15].zw);\n"
    "    }\n"
    "    if (u[18].z != 0.0) {\n"                                  // radial blur, 10 samples
    "        vec2 stride = (u[18].xy - uv) * u[18].z, p = uv;\n"
    "        float weight = 1.0 + 4.5 * u[18].w;\n"
    "        c *= weight;\n"
    "        for (int i = 1; i < 10; i++) { weight -= u[18].w; p += stride; c += pick(p) * weight; }\n"
    "        c *= 0.1;\n"
    "    } else if (u[19].x != 0.0 || u[19].y != 0.0) {\n"         // motion blur, 9 samples
    "        for (int i = 1; i < 9; i++) {\n"
    "            float n = float(i);\n"
    "            if (u[19].w > 0.5) n = i < 5 ? n : 4.0 - n;\n"     // 1..4 to both sides
    "            c += pick(uv + u[19].xy * (n / 9.0)) * (1.0 - abs(n) * u[19].z);\n"
    "        }\n"
    "        c /= 9.0 - u[19].z * (u[19].w > 0.5 ? 20.0 : 36.0);\n"
    "    }\n"
    "    if (u[20].x > 0.0) {\n"                                   // grayscale
    "        float grey = u[20].y > 0.5 ? dot(c.rgb, vec3(0.299, 0.587, 0.114)) : (c.r + c.g + c.b) / 3.0;\n"
    "        c.rgb = c.rgb * (1.0 - u[20].x) + grey * u[20].x * u[21].rgb;\n"
    "    }\n"
    "    if (u[20].z > 0.0) {\n"                                   // sepia
    "        vec3 s = vec3(dot(c.rgb, vec3(0.393, 0.769, 0.189)), dot(c.rgb, vec3(0.349, 0.686, 0.168)),\n"
    "                      dot(c.rgb, vec3(0.272, 0.534, 0.131)));\n"
    "        c.rgb = mix(c.rgb, s, u[20].z);\n"
    "    }\n"
    "    c.rgb = c.rgb * (1.0 - u[22].rgb) + (c.a - c.rgb) * u[22].rgb;\n"   // invert
    "    if (u[23].z > 0.0) {\n"                                   // hue shift
    "        vec3 w = vec3(0.57735);\n"
    "        c.rgb = c.rgb * u[23].x + cross(w, c.rgb) * u[23].y + w * dot(w, c.rgb) * (1.0 - u[23].x);\n"
    "    }\n"
    "    c.rgb = c.rgb * (1.0 + u[24].rgb) + u[25].rgb * c.a;\n"   // colour change
    "    if (u[27].w > 0.0) {\n"                                   // lens circle
    "        float r = length((v_uv - u[26].xy) * tall);\n"
    "        float k = u[27].w * (u[26].z >= u[26].w ? step(u[26].w, r) : ramp(u[26].z, u[26].w, r));\n"
    "        c.rgb = u[15].y > 0.5 ? c.rgb + u[27].rgb * k * c.a : mix(c.rgb, u[27].rgb * c.a, k);\n"
    "    }\n"
    "    gl_FragColor = c;\n"
    "}\n";

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

static GLuint program(const char *fragment)
{
    GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_col");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked) return 0;
    glUseProgram(p);
    glUniform2f(glGetUniformLocation(p, "u_size"), (float)s_w, (float)s_h);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 0);
    return p;
}

static int init_gl(void)
{
    s_prog = program(k_fs);
    if (!s_prog) { SDL_SetError("shaders failed"); return -3; }
    s_fx = program(k_effects);   // without it the level's shader effects are left out
    if (s_fx) s_fx_values = glGetUniformLocation(s_fx, "u");
    else fprintf(stderr, "gl: the effect shader does not work here\n");
    glUseProgram(s_prog);

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
    fprintf(stderr, "gl: %s, %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    return 0;
}

static void screen_size(Gfx *g, int *w, int *h);

// Fits the picture into the screen and makes pictures of that size.
static int fit_screen(Gfx *g)
{
    int sw = s_w, sh = s_h;
    screen_size(g, &sw, &sh);
    if (sw < 1 || sh < 1) return 0;
    int pw = sw, ph = sw * s_h / s_w;
    if (ph > sh) { ph = sh; pw = sh * s_w / s_h; }
    s_sw = sw;
    s_sh = sh;
    s_vx = (sw - pw) / 2;
    s_vy = (sh - ph) / 2;
    if (pw == s_pw && ph == s_ph) return 0;
    if (s_target[0]) {
        glDeleteFramebuffers(3, s_target);
        glDeleteTextures(3, s_picture);
    }
    s_pw = pw;
    s_ph = ph;
    glGenTextures(3, s_picture);
    glGenFramebuffers(3, s_target);
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, s_picture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, s_target[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_picture[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_SetError("no render target of %dx%d", pw, ph);
            return -3;
        }
    }
    return 0;
}

static void draw_queued(void)
{
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s_nquads * 4 * sizeof(Vertex)), s_vtx, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, s_nquads * 6, GL_UNSIGNED_SHORT, 0);
    s_nquads = 0;
}

static void flush(void)
{
    if (!s_nquads) return;
    glBindTexture(GL_TEXTURE_2D, s_tex);
    // colours arrive premultiplied; 1 adds light, 2 multiplies, 3 inverts
    if (s_blend == 1) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
    else if (s_blend == 2) glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    else if (s_blend == 3) glBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
    else glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw_queued();
}

// A whole picture over the whole target, upright (GL counts rows from the bottom).
static void whole_picture(GLuint picture)
{
    const float w = (float)s_w, h = (float)s_h;
    const float xy[4][2] = {{0, 0}, {w, 0}, {w, h}, {0, h}}, uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int i = 0; i < 4; i++) s_vtx[i] = (Vertex){xy[i][0], xy[i][1], uv[i][0], uv[i][1], 255, 255, 255, 255};
    s_nquads = 1;
    glBindTexture(GL_TEXTURE_2D, picture);
    glDisable(GL_BLEND);
    draw_queued();
    glEnable(GL_BLEND);
}

static void scissor(void)
{
    if (!s_clipped) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST); // GL counts rows from the bottom
    glScissor(s_clip.x * s_pw / s_w, (s_h - s_clip.y - s_clip.h) * s_ph / s_h, s_clip.w * s_pw / s_w,
              s_clip.h * s_ph / s_h);
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

static int s_open;   // there is a GL context

void gfxb_tex_destroy(GfxTex *t)
{
    flush(); // queued quads may still use it
    if (s_open) glDeleteTextures(1, &t->id);
    free(t);
}

void gfxb_quad(GfxTex *tex, const SDL_FPoint pos[4], const SDL_FPoint uv[4], const SDL_Color col[4], int blend)
{
    GLuint id = tex ? tex->id : s_white;
    if (s_nquads == MAX_QUADS || (s_nquads && (id != s_tex || blend != s_blend))) flush();
    s_tex = id;
    s_blend = blend;
    Vertex *v = &s_vtx[s_nquads++ * 4];
    for (int i = 0; i < 4; i++) {
        v[i] = (Vertex){pos[i].x, pos[i].y, uv[i].x, uv[i].y, col[i].r, col[i].g, col[i].b, col[i].a};
    }
}

void gfxb_clip(const SDL_Rect *r)
{
    flush();
    s_clipped = r != NULL;
    if (r) s_clip = *r;
    scissor();
}

void gfxb_effects(const float values[][4])
{
    if (!s_fx) return;
    flush();
    const int from = s_now;
    s_now = !s_now;
    glBindFramebuffer(GL_FRAMEBUFFER, s_target[s_now]);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(s_fx);
    glUniform4fv(s_fx_values, GFX_EFFECT_VALUES, &values[0][0]);
    whole_picture(s_picture[from]);
    glUseProgram(s_prog);
    scissor();
}

void gfxb_layer_begin(void)
{
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, s_target[2]);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    scissor();
}

// The layer lies over the picture; colours are premultiplied, so added light comes through too.
void gfxb_layer_end(const float values[][4])
{
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, s_target[s_now]);
    glDisable(GL_SCISSOR_TEST);
    if (values && s_fx) {
        glUseProgram(s_fx);
        glUniform4fv(s_fx_values, GFX_EFFECT_VALUES, &values[0][0]);
    }
    const float w = (float)s_w, h = (float)s_h;
    const float xy[4][2] = {{0, 0}, {w, 0}, {w, h}, {0, h}}, uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int i = 0; i < 4; i++) s_vtx[i] = (Vertex){xy[i][0], xy[i][1], uv[i][0], uv[i][1], 255, 255, 255, 255};
    s_nquads = 1;
    glBindTexture(GL_TEXTURE_2D, s_picture[2]);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw_queued();
    glUseProgram(s_prog);
    scissor();
}

void gfxb_begin_frame(Gfx *g)
{
    flush();
    fit_screen(g);
    s_now = 0;
    s_clipped = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, s_target[0]);
    glViewport(0, 0, s_pw, s_ph);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

// the finished picture onto the screen
static void to_screen(void)
{
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, s_sw, s_sh);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(s_vx, s_vy, s_pw, s_ph);
    whole_picture(s_picture[s_now]);
    glBindFramebuffer(GL_FRAMEBUFFER, s_target[s_now]);   // it can still be read
    glViewport(0, 0, s_pw, s_ph);
}

void gfxb_output_size(int *w, int *h)
{
    *w = s_pw;
    *h = s_ph;
}

int gfxb_read_pixels(unsigned char *rgba, int w, int h)
{
    flush();
    unsigned char *row = malloc((size_t)w * 4);
    if (!row) return -1;
    while (glGetError() != GL_NO_ERROR) {}
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

#ifdef __SWITCH__
// ---------------------------------------------------------------- Switch 2: EGL

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLSurface s_surf = EGL_NO_SURFACE;
static EGLContext s_ctx = EGL_NO_CONTEXT;

static int fail(int code, const char *what)
{
    SDL_SetError("%s (EGL error 0x%x)", what, (unsigned)eglGetError());
    fprintf(stderr, "gl: %s\n", SDL_GetError());
    return code;
}

static EGLSurface create_surface(EGLConfig cfg, int w, int h)
{
    nwindowSetDimensions(nwindowGetDefault(), (u32)w, (u32)h);
    return eglCreateWindowSurface(s_dpy, cfg, nwindowGetDefault(), NULL);
}

// out_w, out_h: size of the picture in pixels (the drawing coordinates stay s_w x s_h)
static int init_egl(int out_w, int out_h)
{
    s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, NULL, NULL)) return fail(-1, "eglInitialize failed");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return fail(-1, "eglBindAPI failed");
    static const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(s_dpy, cfg_attr, &cfg, 1, &n) || n == 0) return fail(-2, "no EGL config");
    s_surf = create_surface(cfg, out_w, out_h);
    if (s_surf == EGL_NO_SURFACE && out_h != s_h) {   // the chosen resolution does not work
        fprintf(stderr, "gl: no %dx%d picture (0x%x), using %dx%d\n", out_w, out_h, (unsigned)eglGetError(), s_w, s_h);
        s_surf = create_surface(cfg, s_w, s_h);
    }
    if (s_surf == EGL_NO_SURFACE) return fail(-2, "eglCreateWindowSurface failed");
    static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    s_ctx = eglCreateContext(s_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (s_ctx == EGL_NO_CONTEXT) return fail(-3, "eglCreateContext failed");
    if (!eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) return fail(-3, "eglMakeCurrent failed");
    return 0;
}

static void screen_size(Gfx *g, int *w, int *h)
{
    (void)g;
    EGLint sw = *w, sh = *h;
    eglQuerySurface(s_dpy, s_surf, EGL_WIDTH, &sw);
    eglQuerySurface(s_dpy, s_surf, EGL_HEIGHT, &sh);
    *w = sw;
    *h = sh;
}

int gfxb_init(Gfx *g, int w, int h, const GfxVideo *video)
{
    if (SDL_Init(SDL_INIT_EVENTS) != 0) return -1;
    s_w = w;
    s_h = h;
    const int out_h = video->height ? video->height : h;
    int rc = init_egl(out_h * w / h, out_h);
    if (rc == 0) rc = init_gl();
    if (rc == 0) {
        s_open = 1;
        rc = fit_screen(g);
    }
    if (rc == 0) gfxb_set_video(g, video);
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
    s_open = 0;
}

void gfxb_present(void)
{
    to_screen();
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

void gfxb_set_video(Gfx *g, const GfxVideo *video)
{
    (void)g;
    // 1: pictures queue up for the display; 0: the newest one replaces a waiting one (mailbox)
    if (!eglSwapInterval(s_dpy, video->vsync ? 1 : 0))
        fprintf(stderr, "gl: eglSwapInterval failed (0x%x)\n", (unsigned)eglGetError());
#ifdef __SWITCH2__
    static int fast;
    if (video->fps == 120) {
        const Result rc = nwindowSetFrameRates(nwindowGetDefault(), 120, 60, true);
        if (R_FAILED(rc)) fprintf(stderr, "gl: no 120 Hz (0x%x)\n", (unsigned)rc);
        fast = 1;
    } else if (fast) {
        nwindowSetSwapInterval(nwindowGetDefault(), 1);   // withdraws the request for 120 Hz
        fast = 0;
    }
#endif
    fprintf(stderr, "gl: vsync %d, %d fps wanted\n", video->vsync, video->fps);
}

int gfxb_max_height(const Gfx *g)
{
    (void)g;
    return 2160;
}

int gfxb_refresh_hz(void)
{
#ifdef __SWITCH2__
    u16 num = 0, den = 0;   // the interval between two pictures in seconds
    if (R_SUCCEEDED(nwindowGetPresentInterval(nwindowGetDefault(), &num, &den)) && num && den)
        return (den + num / 2) / num;
#endif
    return 0;
}

void gfxb_window_point(Gfx *g, int px, int py, float *x, float *y)
{
    (void)g;   // there is no window
    *x = (float)px;
    *y = (float)py;
}

#else
// ---------------------------------------------------------------- PC: an SDL window

static SDL_GLContext s_gl;
static SDL_Window *s_win;

static void screen_size(Gfx *g, int *w, int *h) { SDL_GL_GetDrawableSize(g->win, w, h); }

int gfxb_init(Gfx *g, int w, int h, const GfxVideo *video)
{
    // window sizes in real pixels, also on a desktop with display scaling
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | (video->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (SDL_getenv("GD_HIDDEN")) flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN; // test runs without a visible window
    const int win_h = video->height ? video->height : h;
    g->win = SDL_CreateWindow("Ounce Dash", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, win_h * w / h, win_h, flags);
    if (!g->win) return -2;
    s_win = g->win;
    s_gl = SDL_GL_CreateContext(g->win);
    if (!s_gl) return -3;
    int missing = 0;
#define FETCH(ret, name, args)                                                  \
    *(void **)(&p_gl##name) = SDL_GL_GetProcAddress("gl" #name);                \
    if (!p_gl##name) { fprintf(stderr, "gl: no gl" #name "\n"); missing = 1; }
    GL_FUNCTIONS(FETCH)
#undef FETCH
    if (missing) { SDL_SetError("the graphics driver lacks OpenGL 2.1 with framebuffer objects"); return -3; }
    s_w = w;
    s_h = h;
    int rc = init_gl();
    if (rc == 0) {
        s_open = 1;
        rc = fit_screen(g);
    }
    if (rc == 0) SDL_GL_SetSwapInterval(video->vsync ? 1 : 0);
    return rc;
}

void gfxb_quit(Gfx *g)
{
    if (s_gl) { SDL_GL_DeleteContext(s_gl); s_gl = NULL; }
    if (g->win) { SDL_DestroyWindow(g->win); g->win = NULL; }
    s_win = NULL;
    s_open = 0;
    s_pw = s_ph = 0;
    s_target[0] = s_target[1] = 0;
    s_nquads = 0;
}

void gfxb_present(void)
{
    to_screen();
    SDL_GL_SwapWindow(s_win);
}

void gfxb_set_video(Gfx *g, const GfxVideo *video)
{
    SDL_GL_SetSwapInterval(video->vsync ? 1 : 0);
    if (SDL_GetWindowFlags(g->win) & SDL_WINDOW_HIDDEN) return;
    if (video->fullscreen) {
        SDL_SetWindowFullscreen(g->win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    SDL_SetWindowFullscreen(g->win, 0);
    const int h = video->height ? video->height : s_h, w = h * s_w / s_h;
    int now_w, now_h;
    SDL_GetWindowSize(g->win, &now_w, &now_h);
    if (now_w != w || now_h != h) {
        SDL_SetWindowSize(g->win, w, h);
        const int on = SDL_GetWindowDisplayIndex(g->win);
        SDL_SetWindowPosition(g->win, SDL_WINDOWPOS_CENTERED_DISPLAY(on), SDL_WINDOWPOS_CENTERED_DISPLAY(on));
    }
}

int gfxb_max_height(const Gfx *g)
{
    SDL_DisplayMode m;
    return SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(g->win), &m) ? s_h : m.h;
}

int gfxb_refresh_hz(void) { return 0; }

// A position in the window (as mouse events give it) in drawing coordinates.
void gfxb_window_point(Gfx *g, int px, int py, float *x, float *y)
{
    int ww = s_w, wh = s_h;
    SDL_GetWindowSize(g->win, &ww, &wh);
    const float sx = (float)px * s_sw / (ww > 0 ? ww : 1), sy = (float)py * s_sh / (wh > 0 ? wh : 1);
    *x = (sx - s_vx) * s_w / (s_pw > 0 ? s_pw : 1);
    *y = (sy - s_vy) * s_h / (s_ph > 0 ? s_ph : 1);
}
#endif
