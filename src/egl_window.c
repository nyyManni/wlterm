#include <stdio.h>
#include <stdlib.h>


#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <cglm/mat4.h>
#include <cglm/cam.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "egl_util.h"
#include "egl_window.h"
#include "xdg-shell-client-protocol.h"

#define CHECK_ERROR                               \
    do {                                          \
        GLenum err = glGetError();                \
        if (err) {                                \
            fprintf(stderr, "error: %i\n", err);  \
        }                                         \
    } while (0);

bool first_window = true;

FT_Library ft = NULL;
FT_Face face = NULL;

GLuint font_texture;

struct glyph active_glyph;
mat4 text_projection;

extern struct wl_display *g_display;
EGLDisplay g_gl_display;
EGLConfig g_gl_conf;
EGLContext g_root_ctx;

extern struct wl_compositor *g_compositor;
extern struct xdg_wm_base *g_xdg_wm_base;

struct window *active_window;
struct window *windows[MAX_WINDOWS];
int open_windows = 0;

static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

const struct wl_callback_listener frame_listener;
static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct window *w = data;
    if (!w->open)
        return;
    wl_callback_destroy(callback);
    window_render(w);

    callback = wl_surface_frame(w->surface);
    wl_callback_add_listener(callback, &frame_listener, w);
    wl_surface_commit(w->surface);
}

const struct wl_callback_listener frame_listener = {
    .done = frame_handle_done,
};
static void handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height,
                                      struct wl_array *states) {

    struct window *w = data;

    if (width && height) {
        w->width = width;
        w->height = height;
    }
    wl_egl_window_resize(w->gl_window, width * SCALE, height * SCALE, 0, 0);
}

static void handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct window *w = data;
    window_close(w);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};
static void handle_xdg_buffer_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    struct window *w = data;

    xdg_surface_ack_configure(w->xdg_surface, serial);
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};


void init_egl() {
    memset(windows, 0, MAX_WINDOWS * sizeof(struct window *));

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SAMPLE_BUFFERS, 1,
        EGL_SAMPLES, 4,  // This is for 4x MSAA.
        EGL_NONE
    };
    g_gl_display = platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR, g_display, NULL);
    EGLint major, minor, count, n, size, i;
    EGLConfig *configs;
    eglInitialize(g_gl_display, &major, &minor);
    eglBindAPI(EGL_OPENGL_ES_API);
    eglGetConfigs(g_gl_display, NULL, 0, &count);
    configs = calloc(count, sizeof *configs);
    eglChooseConfig(g_gl_display, config_attribs, configs, count, &n);
    eglSwapInterval(g_gl_display, 0);

    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(g_gl_display, configs[i], EGL_BUFFER_SIZE, &size);
        if (size == 32) {
            g_gl_conf = configs[i];
            break;
        }
    }

    free(configs);

    g_root_ctx = eglCreateContext(g_gl_display, g_gl_conf, EGL_NO_CONTEXT, context_attribs);
    eglMakeCurrent(g_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_root_ctx);
}

void kill_egl() {

    eglTerminate(g_gl_display);
    eglReleaseThread();
}


bool load_font(const char *font_name, int height) {
    if (!ft && FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Could not init freetype library\n");
        return false;
    }
    if (FT_New_Face(ft, font_name, 0, &face)) {
        fprintf(stderr, "Could not init font\n");
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, height);

    eglMakeCurrent(g_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_root_ctx);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    active_glyph.code = 0x00e4;
    FT_Load_Char(face, active_glyph.code, FT_LOAD_RENDER);

    glGenTextures(1, &font_texture);
    /* GLuint fbo; */
    /* glGenFramebuffers(1, &fbo); */
    /* glBindFramebuffer(GL_FRAMEBUFFER, fbo); */
    glBindTexture(GL_TEXTURE_2D, font_texture);


    FT_GlyphSlot g = face->glyph;
    fprintf(stderr, "width: %u, height: %u\n", g->bitmap.width, g->bitmap.rows);

    glm_ortho(0.0, 1024, 1024, 0.0, 0.0, 1.0, text_projection);

    active_glyph.texture = font_texture;
    glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RED,
      /* g->bitmap.width, */
      /* 256, */
      /* g->bitmap.rows, */
      1024,
      1024,
      /* 320, */
      /* 500, */
      0,
      GL_RED,
      GL_UNSIGNED_BYTE,
      0
      /* g->bitmap.buffer */
    );
    active_glyph.offset_x = 100.0 / 1024.0;
    active_glyph.offset_y = 100.0 / 1024.0;
    active_glyph.width = g->bitmap.width / 1024.0;
    active_glyph.height = g->bitmap.rows / 1024.0;

    glTexSubImage2D(GL_TEXTURE_2D, 0, 100, 100, g->bitmap.width, g->bitmap.rows,
                    GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    /* glBindFramebuffer(GL_FRAMEBUFFER, 0); */

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return true;
}


struct window *window_create() {
    struct window **wp = windows - 1;

    if (open_windows == MAX_WINDOWS)
        return NULL;

    while (*++wp);
    struct window *w = *wp = malloc(sizeof(struct window));

    w->width = 200;
    w->height = 200;
    w->open = true;

    /* Share the context between windows */
    w->gl_ctx = eglCreateContext(g_gl_display, g_gl_conf, g_root_ctx, context_attribs);

    w->surface = wl_compositor_create_surface(g_compositor);
    wl_surface_set_user_data(w->surface, w);
    wl_surface_set_buffer_scale(w->surface, SCALE);

    w->gl_window = wl_egl_window_create(w->surface, 200, 200);
    w->gl_surface = platform_create_egl_surface(g_gl_display, g_gl_conf, w->gl_window, NULL);

    w->xdg_surface = xdg_wm_base_get_xdg_surface(g_xdg_wm_base, w->surface);
    w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);

    xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, w);
    xdg_toplevel_add_listener(w->xdg_toplevel, &xdg_toplevel_listener, w);

    xdg_toplevel_set_title(w->xdg_toplevel, "Emacs");
    wl_surface_commit(w->surface);

    eglMakeCurrent(g_gl_display, w->gl_surface, w->gl_surface, w->gl_ctx);
    eglSwapInterval(g_gl_display, 0);

    GLint status;

    GLuint font_vert = make_shader("src/font-vertex.glsl", GL_VERTEX_SHADER);
    GLuint font_frag = make_shader("src/font-fragment.glsl", GL_FRAGMENT_SHADER);
    w->text_shader = glCreateProgram();
    glAttachShader(w->text_shader, font_vert);
    glAttachShader(w->text_shader, font_frag);
    glLinkProgram(w->text_shader);
    glDeleteShader(font_vert);
    glDeleteShader(font_frag);

    glGetProgramiv(w->shader_program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1000];
        GLsizei len;
        glGetProgramInfoLog(w->shader_program, 1000, &len, log);
        fprintf(stderr, "Error: linking:\n%*s\n", len, log);
        exit(1);
    }

    w->projection_uniform = glGetUniformLocation(w->text_shader, "projection");
    w->color_uniform = glGetUniformLocation(w->text_shader, "textColor");
    w->text_projection_uniform = glGetUniformLocation(w->text_shader, "textProjection");

    wl_display_roundtrip(g_display);
    wl_surface_commit(w->surface);

    wl_surface_commit(w->surface);

    eglSwapBuffers(g_gl_display, w->gl_surface);
    struct wl_callback *callback = wl_surface_frame(w->surface);
    wl_callback_add_listener(callback, &frame_listener, w);
    open_windows++;

    window_render(w);
    return w;
}

void window_render(struct window *w) {

    eglMakeCurrent(g_gl_display, w->gl_surface, w->gl_surface, w->gl_ctx);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, w->width * SCALE, w->height * SCALE);

    mat4 projection;
    glm_ortho(0.0, w->width * SCALE, w->height * SCALE, 0.0, -1.0, 1.0, projection);


    glActiveTexture(GL_TEXTURE0);

    glUseProgram(w->text_shader);

    glBindTexture(GL_TEXTURE_2D, font_texture);

    glUniformMatrix4fv(w->projection_uniform, 1, GL_FALSE, (GLfloat *) projection);
    GLfloat color[3] = {1.0, 0.3, 0.3};
    glUniform3fv(w->color_uniform, 1, (GLfloat *) color);
    glUniformMatrix4fv(w->text_projection_uniform, 1, GL_FALSE, (GLfloat *) text_projection);
 
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

    int x = 100;
    int y = 100;

    GLfloat _w = active_glyph.width * 1024;
    GLfloat _h = active_glyph.height * 1024;

    struct glyph g = active_glyph;
    GLfloat box[1][6][4] = {{
        /* {x     , y     , g.offset_x + g.width, 1 - g.offset_y}, */
        /* {x + _w, y     , g.offset_x          , 1 - g.offset_y}, */
        /* {x     , y + _h, g.offset_x + g.width, 1 - g.offset_y + g.height}, */

        /* {x     , y + _h, g.offset_x + g.width, 1 - g.offset_y + g.height}, */
        /* {x + _w, y     , g.offset_x          , 1 - g.offset_y}, */
        /* {x + _w, y + _h, g.offset_x          , 1 - g.offset_y + g.height}, */

        {x     , y     , g.offset_x          , g.offset_y},
        {x + _w, y     , g.offset_x + g.width, g.offset_y},
        {x     , y + _h, g.offset_x          , g.offset_y + g.height},

        {x     , y + _h, g.offset_x          , g.offset_y + g.height},
        {x + _w, y     , g.offset_x + g.width, g.offset_y},
        {x + _w, y + _h, g.offset_x + g.width, g.offset_y + g.height},

        /* {x     , y     , 0, 0}, */
        /* {x + _w, y     , 1, 0}, */
        /* {x     , y + _h, 0, 1}, */

        /* {x     , y + _h, 0, 1}, */
        /* {x + _w, y     , 1, 0}, */
        /* {x + _w, y + _h, 1, 1}, */
    }};

 
    glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 12);


    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(g_gl_display, w->gl_surface);
    wl_surface_commit(w->surface);
}

void window_close(struct window *w) {
    w->open = false;

    platform_destroy_egl_surface(g_gl_display, w->gl_surface);

    xdg_toplevel_destroy(w->xdg_toplevel);
    xdg_surface_destroy(w->xdg_surface);
    wl_surface_destroy(w->surface);

    struct window **wp = windows - 1;
    while (*++wp != w);
    free(w);
    *wp = NULL;

    open_windows--;
}
