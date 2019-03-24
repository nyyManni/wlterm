#include <stdio.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H

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
    /* w->resized = (width != w->width || height != w->height); */

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

    /* fprintf(stderr, "configured xdg surface\n"); */
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

    /* glEnable(GL_CULL_FACE); */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    FT_Load_Char(face, 0x00e4, FT_LOAD_RENDER);
    FT_GlyphSlot g = face->glyph;

    glGenTextures(1, &font_texture);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RED,
      g->bitmap.width,
      g->bitmap.rows,
      0,
      GL_RED,
      GL_UNSIGNED_BYTE,
      g->bitmap.buffer
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* int x = 100; */
    /* int y = 100; */
    /* float x2 = x + g->bitmap_left; */
    /* float y2 = -y - g->bitmap_top; */
    /* float wi = g->bitmap.width; */
    /* float h = g->bitmap.rows; */
    /* fprintf(stderr, "%f, %f, %f, %f\n", x2, y2, wi, h); */

    glBindTexture(GL_TEXTURE_2D, 0);
    /* glEnable(GL_BLEND); */
    /* glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); */

    FT_Done_Face(face);
    FT_Done_FreeType(ft);



    /* glBindTexture(GL_TEXTURE_2D, 0); */


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
    w->rotation_offset = timestamp();

    /* Share the context between windows */
    w->gl_ctx = eglCreateContext(g_gl_display, g_gl_conf, g_root_ctx, context_attribs);
    /* if (active_window) { */
    /*         w->gl_ctx = eglCreateContext(g_gl_display, g_gl_conf, active_window->gl_ctx, context_attribs); */
    /* } else { */
    /*     w->gl_ctx = eglCreateContext(g_gl_display, g_gl_conf, EGL_NO_CONTEXT, context_attribs); */
    /* } */

    w->surface = wl_compositor_create_surface(g_compositor);
    wl_surface_set_user_data(w->surface, w);
    wl_surface_set_buffer_scale(w->surface, SCALE);

    w->gl_window = wl_egl_window_create(w->surface, 200, 200);
    w->gl_surface =
        platform_create_egl_surface(g_gl_display, g_gl_conf, w->gl_window, NULL);

    w->xdg_surface = xdg_wm_base_get_xdg_surface(g_xdg_wm_base, w->surface);
    w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);

    xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, w);
    xdg_toplevel_add_listener(w->xdg_toplevel, &xdg_toplevel_listener, w);

    xdg_toplevel_set_title(w->xdg_toplevel, "Emacs");
    wl_surface_commit(w->surface);

    eglMakeCurrent(g_gl_display, w->gl_surface, w->gl_surface, w->gl_ctx);
    eglSwapInterval(g_gl_display, 0);

    GLint status;
    GLuint vert = make_shader("src/vertex-shader.glsl", GL_VERTEX_SHADER);
    GLuint frag = make_shader("src/fragment-shader.glsl", GL_FRAGMENT_SHADER);

    w->shader_program = glCreateProgram();
    glAttachShader(w->shader_program, vert);
    glAttachShader(w->shader_program, frag);
    glLinkProgram(w->shader_program);
    glDeleteShader(vert);
    glDeleteShader(frag);

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


    /* w->pos = 0; */
    /* w->col = 1; */

    /* glBindAttribLocation(w->shader_program, w->pos, "pos"); */
    /* glBindAttribLocation(w->shader_program, w->col, "color"); */

    w->rotation_uniform = glGetUniformLocation(w->shader_program, "rotation");
    w->offset_uniform = glGetUniformLocation(w->shader_program, "offset");
    w->projection_uniform = glGetUniformLocation(w->shader_program, "projection");

    w->projection_font_uniform = glGetUniformLocation(w->text_shader, "projection");
    w->color_uniform = glGetUniformLocation(w->text_shader, "textColor");

    wl_display_roundtrip(g_display);
    wl_surface_commit(w->surface);

    wl_surface_commit(w->surface);

    eglSwapBuffers(g_gl_display, w->gl_surface);
    struct wl_callback *callback = wl_surface_frame(w->surface);
    wl_callback_add_listener(callback, &frame_listener, w);
    open_windows++;

    
    /* if (first_window) { */
    /*     load_font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 512); */
    /*     first_window = false; */
    /* } */


    window_render(w);
    return w;
}

void window_render(struct window *w) {

    if (!w->open)
        return;

    eglMakeCurrent(g_gl_display, w->gl_surface, w->gl_surface, w->gl_ctx);
    /* glDisable(GL_BLEND); */
    /* /\* fprintf(stderr, "drawing...\n"); *\/ */

    /* GLfloat verts[3][2] = { */
    /*     {w->width, 0}, */
    /*     {0, w->height * SCALE}, */
    /*     {w->width * SCALE, w->height * SCALE} */
    /* }; */
    /* /\* verts[0][0] = w->width; *\/ */
    /* /\* verts[1][0] = w->width * SCALE; *\/ */
    /* /\* verts[1][1] = w->height * SCALE; *\/ */
    /* /\* verts[0][0] = w->height * SCALE; *\/ */

    /* static const GLfloat colors[3][3] = { */
    /*     { 1, 0, 0 }, */
    /*     { 0, 1, 0 }, */
    /*     { 0, 0, 1 } */
    /* }; */
    /* GLfloat angle; */
    /* GLfloat rotation[4][4] = { */
    /*     { 1, 0, 0, 0 }, */
    /*     { 0, 1, 0, 0 }, */
    /*     { 0, 0, 1, 0 }, */
    /*     { 0, 0, 0, 1 } */
    /* }; */
    /* static const uint32_t speed_div = 5, benchmark_interval = 5; */
    /* struct wl_region *region; */
    /* EGLint rect[4]; */
    /* EGLint buffer_age = 0; */

    /* glEnable(GL_BLEND); */
    /* glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); */
    /* angle = fmod(((timestamp() + w->rotation_offset) / (double)speed_div), 360) * M_PI / 180.0; */
    /* rotation[0][0] =  cos(angle); */
    /* rotation[0][2] =  sin(angle); */
    /* rotation[2][0] = -sin(angle); */
    /* rotation[2][2] =  cos(angle); */

    /* GLfloat offset[4] = {0.0, 0.0, 0, 0}; */

    glViewport(0, 0, w->width * SCALE, w->height * SCALE);

    mat4 projection;
    glm_ortho(0.0, w->width * SCALE, w->height * SCALE, 0.0, -1.0, 1.0, projection);

    /* glUniformMatrix4fv(w->rotation_uniform, 1, GL_FALSE, (GLfloat *) rotation); */
    /* glUniformMatrix4fv(w->projection_uniform, 1, GL_FALSE, (GLfloat *) projection); */
    /* glUniform4fv(w->offset_uniform, 1, (GLfloat *) offset); */

    /* glClearColor(0.0, 0.0, 0.0, 0.5); */
    /* glClear(GL_COLOR_BUFFER_BIT); */

    /* /\* glUseProgram(w->shader_program); *\/ */
    /* /\* glVertexAttribPointer(w->pos, 2, GL_FLOAT, GL_FALSE, 0, verts); *\/ */
    /* /\* glVertexAttribPointer(w->col, 3, GL_FLOAT, GL_FALSE, 0, colors); *\/ */
    /* /\* glEnableVertexAttribArray(w->pos); *\/ */
    /* /\* glEnableVertexAttribArray(w->col); *\/ */
    /* glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts); */
    /* glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, colors); */
    /* glEnableVertexAttribArray(0); */
    /* glEnableVertexAttribArray(1); */

    /* glDrawArrays(GL_TRIANGLES, 0, 3); */


    /* glDisableVertexAttribArray(0); */
    /* glDisableVertexAttribArray(1); */



    glUseProgram(w->text_shader);

    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, font_texture);

    glUniformMatrix4fv(w->projection_font_uniform, 1, GL_FALSE, (GLfloat *) projection);
    GLfloat color[3] = {1.0, 0.0, 0.0};
    glUniform3fv(w->color_uniform, 1, (GLfloat *) color);

    /* Draw some text */
    /* glPixelStorei(GL_UNPACK_ALIGNMENT, 1); */

    /* FT_Load_Char(face, 0x00e4, FT_LOAD_RENDER); */
    /* FT_GlyphSlot g = face->glyph; */

    /* glGenTextures(1, &font_texture); */
    /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); */
    /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); */

    /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); */
    /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); */

    /* glTexImage2D( */
    /*   GL_TEXTURE_2D, */
    /*   0, */
    /*   GL_RED, */
    /*   g->bitmap.width, */
    /*   g->bitmap.rows, */
    /*   0, */
    /*   GL_RED, */
    /*   GL_UNSIGNED_BYTE, */
    /*   g->bitmap.buffer */
    /* ); */
    float x2 = 133.000000, y2 = -488.000000, wi = 232.000000, h = 395.000000;
 
    int x = 100;
    int y = 100;
    /* float x2 = x + g->bitmap_left; */
    /* float y2 = -y - g->bitmap_top; */
    /* float wi = g->bitmap.width; */
    /* float h = g->bitmap.rows; */
 
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

    GLfloat box[8][4] = {
        {x2,     y    , 0, 0},
        {x2 + wi, y    , 1, 0},
        {x2,     y + h, 0, 1},
        {x2 + wi, y + h, 1, 1},

        {x2+500,     y    +800, 0, 0},
        {x2 + wi+500, y    +800, 1, 0},
        {x2+500,     y + h+800, 0, 1},
        {x2 + wi+500, y + h+800, 1, 1},

    };
 
    glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_DYNAMIC_DRAW);


    glDrawArrays(GL_TRIANGLE_STRIP, 0, 8);

    glBindTexture(GL_TEXTURE_2D, 0);


    /* glDisableVertexAttribArray(0); */

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
