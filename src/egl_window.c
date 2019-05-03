#include <stdio.h>
#include <stdlib.h>


#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include <cglm/mat4.h>
#include <cglm/cam.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "msdf.h"

#include "egl_util.h"
#include "egl_window.h"
#include "xdg-shell-client-protocol.h"

/* #define FONT_BUFFER_SIZE 4096 */
#define FONT_BUFFER_SIZE 512

#define CHECK_ERROR                               \
    do {                                          \
        GLenum err = glGetError();                \
        if (err) {                                \
            fprintf(stderr, "line %d error: %x\n", __LINE__, err);  \
        }                                         \
    } while (0);

/* double font_size = 8.5; */
double font_size = 8.5;

GLenum err;


GLuint font_texture;
GLuint g_msdf_shader;
GLuint g_msdf_projection_uniform;
GLuint g_debug_shader;
struct font *active_font;


int parse_color(const char *color, vec3 ret) {

    char buf[3] = {0};
    for (int channel = 0; channel < 3; ++channel) {
        memcpy(buf, &color[channel * 2], 2);
        ret[channel] = strtol(buf, NULL, 16) / 255.0;
    }


    return 1;
};

mat4 text_projection;

extern struct wl_display *g_display;
EGLDisplay g_gl_display;
EGLConfig g_gl_conf;
EGLContext g_root_ctx;


extern struct wl_compositor *g_compositor;
extern struct xdg_wm_base *g_xdg_wm_base;

struct frame *selected_frame;
struct frame *frames[MAX_FRAMES];
int open_frames = 0;

static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

const struct wl_callback_listener frame_listener;
static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct frame *f = data;

    wl_callback_destroy(callback);

    if (!f->open)
        return;
    bool dirty = false;

    FOR_EACH_WINDOW(f, w) {
        /* Perform kinetic scrolling on the windows of the frame. */
        for (uint32_t axis = 0; axis < 2; ++axis) {
            if (w->_scrolling_freely[axis]) {
                uint32_t delta_t = time - w->_kinetic_scroll_t0[axis];
                if (delta_t > 10000) {

                    /* For example resizing can lose the track of time. */
                    w->_kinetic_scroll_t0[axis] = time;
                    dirty = true;
                    break;
                }

                int sign = glm_sign(w->_kinetic_scroll[axis]);

                w->position[axis] += ((double)delta_t * w->_kinetic_scroll[axis]);

                w->_kinetic_scroll[axis] *= pow(0.996, delta_t);

                w->_kinetic_scroll_t0[axis] = time;
                if (w->position[axis] > 0) w->_scrolling_freely[axis] = false;

                if (fabs(w->_kinetic_scroll[axis]) < 0.005) {
                    w->_kinetic_scroll[axis] = 0.0;
                } else {
                    dirty = true;
                }

            }
        }
    }

    frame_render(f);
    callback = wl_surface_frame(f->surface);
    wl_callback_add_listener(callback, &frame_listener, f);
    if (dirty) wl_surface_commit(f->surface);
}

const struct wl_callback_listener frame_listener = {
    .done = frame_handle_done,
};
static void handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height,
                                      struct wl_array *states) {

    struct frame *f = data;

    frame_resize(f, width, height);
}

static void handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct frame *w = data;
    frame_close(w);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};
static void handle_xdg_buffer_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    struct frame *f = data;

    xdg_surface_ack_configure(f->xdg_surface, serial);
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};


void init_egl() {
    memset(frames, 0, MAX_FRAMES * sizeof(struct frame *));

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SAMPLE_BUFFERS, 1,
        /* EGL_SAMPLES, 4,  // This is for 4x MSAA. */
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
    eglSwapInterval(g_gl_display, 0);

    g_msdf_shader =  create_program("src/msdf-vertex.glsl",
                                    "src/msdf-fragment.glsl",
                                    NULL);
    g_msdf_projection_uniform = glGetUniformLocation(g_msdf_shader, "projection");

    g_debug_shader = create_program("src/texdebug-vertex.glsl",
                                    "src/texdebug-fragment.glsl",
                                    NULL);
    GLuint texture_uniform = glGetUniformLocation(g_debug_shader, "tex");
    /* glUniform1i(texture_uniform, 0); */

}

void kill_egl() {

    eglTerminate(g_gl_display);
    eglReleaseThread();
}

struct font *load_font(const char *font_name, int height) {
    struct font *f = malloc(sizeof (struct font));
    active_font = f;
    f->texture_size = FONT_BUFFER_SIZE;

    msdf_font_handle msdf_font = msdf_load_font(font_name);
    f->msdf_font = msdf_font;

    size_t serialized_size = msdf_glyph_serialized_size(msdf_font, '#');
    GLfloat *input = (GLfloat *)malloc(serialized_size);
    float _w, _h;
    msdf_serialize_glyph(msdf_font, '#', &input[0], &_w, &_h);

    f->vertical_advance = msdf_font->height;

    eglMakeCurrent(g_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_root_ctx);
    CHECK_ERROR
    eglSwapInterval(g_gl_display, 0);
    CHECK_ERROR
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    CHECK_ERROR

    glEnable(GL_CULL_FACE);
    CHECK_ERROR
    glEnable(GL_BLEND);
    CHECK_ERROR
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    CHECK_ERROR
    
        
        
        
        
        

    int msdf_texture_size = 256;

    glGenBuffers(1, &f->msdf_glyph_uniform);
    CHECK_ERROR
    glGenTextures(1, &f->msdf_glyph_texture);
    CHECK_ERROR
    glGenTextures(1, &f->msdf_atlas_texture);
    CHECK_ERROR
    glGenFramebuffers(1, &f->msdf_framebuffer);
    CHECK_ERROR

    glBindBuffer(GL_ARRAY_BUFFER, f->msdf_glyph_uniform);
    CHECK_ERROR
    glBufferData(GL_ARRAY_BUFFER, serialized_size, &input[0], GL_STATIC_READ);
    CHECK_ERROR

    glBindTexture(GL_TEXTURE_BUFFER, f->msdf_glyph_texture);
    CHECK_ERROR
    glBindTexture(GL_TEXTURE_2D, f->msdf_atlas_texture);
    CHECK_ERROR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    CHECK_ERROR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CHECK_ERROR

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
    CHECK_ERROR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK_ERROR

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, msdf_texture_size, msdf_texture_size, 0, GL_RGBA, GL_FLOAT, NULL);
    CHECK_ERROR

    glBindTexture(GL_TEXTURE_2D, 0);
    CHECK_ERROR

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f->msdf_framebuffer);
    CHECK_ERROR

    glUseProgram(g_msdf_shader);
    CHECK_ERROR

    GLuint vbo;
    glGenBuffers(1, &vbo);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    CHECK_ERROR
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    CHECK_ERROR
    glEnableVertexAttribArray(0);
    CHECK_ERROR

    /* int x = -msdf_texture_size; */
    /* int y = -msdf_texture_size; */

    /* float range = 4.0; */
    /* float scale = 1.0; */
    /* size_t w = ceil((_w + range) * scale); */
    /* size_t h = ceil((_h + range) * scale); */

    /* h = 300; */
    /* w = 300; */
    /* GLfloat rect[6][2] = { */
    /*     /\* {x, y}, *\/ */
    /*     /\* {x + w, y}, *\/ */
    /*     /\* {x, y + h}, *\/ */
    /*     {0, 0}, */
    /*     {200, 0}, */
    /*     {0, 200}, */
    /* }; */

    /* glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(GLfloat), (GLfloat *)rect, GL_DYNAMIC_DRAW); */
    CHECK_ERROR

    mat4 msdf_projection;
    /* glm_ortho(-msdf_texture_size, msdf_texture_size, */
    /*           -msdf_texture_size, msdf_texture_size, */
    /*           -1.0, 1.0, msdf_projection); */
    glm_ortho(0.0, 2 *msdf_texture_size, 2*msdf_texture_size, 0.0, -1.0, 1.0, msdf_projection);
    /* glm_ortho(0, msdf_texture_size / 2.0, */
    /*           0, msdf_texture_size / 2.0, */
    /*           -1.0, 1.0, msdf_projection); */

    /* glUniformMatrix4fv(g_msdf_projection_uniform, 1, GL_FALSE, (GLfloat *) msdf_projection); */
    CHECK_ERROR

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f->msdf_atlas_texture, 0);
    CHECK_ERROR

    /* unsigned int rbo; */
    /* glGenRenderbuffers(1, &rbo); */
    /* CHECK_ERROR */
    /* glBindRenderbuffer(GL_RENDERBUFFER, rbo);  */
    /* CHECK_ERROR */
    /* glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 800, 600);   */
    /* CHECK_ERROR */
    /* glBindRenderbuffer(GL_RENDERBUFFER, 0); */
    /* CHECK_ERROR */
    /* glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo); */
    /* CHECK_ERROR */

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("framebuffer incomplete: %x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    
    glViewport(0, 0, msdf_texture_size, msdf_texture_size);
    CHECK_ERROR

    vec3 _color;
    parse_color("224466", _color);
    glClearColor(_color[0], _color[1], _color[2], 1.0);
    CHECK_ERROR
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK_ERROR


    GLuint _vbo;
    glGenBuffers(1, &_vbo);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    CHECK_ERROR
    glEnableVertexAttribArray(0);
    CHECK_ERROR

    GLfloat _rect[6][4] = {
        {-0.8, -0.8},
        {-0.8, 0.8},
        {0.8, -0.8},

        {-0.8, 0.8},
        {0.8, -0.8},
        {0.8, 0.8}
    };
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), (GLfloat *)_rect, GL_DYNAMIC_DRAW);
    CHECK_ERROR
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    CHECK_ERROR

    glDrawArrays(GL_TRIANGLES, 0, 12);
    CHECK_ERROR

    glDisableVertexAttribArray(0);
    CHECK_ERROR

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    CHECK_ERROR






    glGenTextures(1, &f->texture);
    CHECK_ERROR
    glActiveTexture(GL_TEXTURE0);
    CHECK_ERROR
    glBindTexture(GL_TEXTURE_2D, f->texture);
    CHECK_ERROR

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, f->texture_size, f->texture_size,
                 0, GL_RGB, GL_FLOAT, 0);
    CHECK_ERROR

    glm_ortho(-f->texture_size, f->texture_size,
              -f->texture_size, f->texture_size,
              -1.0, 1.0, f->texture_projection);

    glGenBuffers(1, &f->vertex_buffer);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, f->vertex_buffer);
    CHECK_ERROR
    glBufferData(GL_ARRAY_BUFFER, 254 * 8 * sizeof(GLfloat), 0, GL_STATIC_READ);
    CHECK_ERROR

    int offset_x = 0, offset_y = 0, y_increment = 0;

    for (unsigned int i = 0; i < 254; ++i) {
        msdf_glyph_handle g = msdf_generate_glyph(msdf_font, i, 4.0, 1.0);

        if (offset_x + g->bitmap.width > FONT_BUFFER_SIZE) {
            offset_y += (y_increment + 1);
            offset_x = 0;
        }

        glActiveTexture(GL_TEXTURE0);
        CHECK_ERROR
        glTexSubImage2D(GL_TEXTURE_2D, 0, offset_x, offset_y, g->bitmap.width,
                        g->bitmap.height, GL_RGB, GL_FLOAT, g->bitmap.data);
        CHECK_ERROR
        y_increment = y_increment > g->bitmap.height ? y_increment : g->bitmap.height;

        f->horizontal_advances[i] = g->advance;

        float _buf[] = {
            offset_x, offset_y, g->bitmap.width, g->bitmap.height,
            g->bearing[0], -g->bearing[1],
            g->size[0], g->size[1]
        };
        glBufferSubData(GL_ARRAY_BUFFER, i * 8 * sizeof(GLuint), sizeof(_buf), _buf);
        CHECK_ERROR
        offset_x += g->bitmap.width + 1;
    }
    font_texture = f->texture;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    CHECK_ERROR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK_ERROR

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    CHECK_ERROR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CHECK_ERROR

    glBindTexture(GL_TEXTURE_2D, 0);
    CHECK_ERROR

    glGenTextures(1, &f->vertex_texture);
    CHECK_ERROR
    glActiveTexture(GL_TEXTURE1);
    CHECK_ERROR
    glBindTexture(GL_TEXTURE_BUFFER, f->vertex_texture);
    CHECK_ERROR
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, f->vertex_buffer);
    CHECK_ERROR

    glBindTexture(GL_TEXTURE_BUFFER, 0);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_ERROR

    return f;
}

void frame_resize(struct frame *f, int width, int height) {

    f->width = width;
    f->height = height;
    wl_egl_window_resize(f->gl_window, width * f->scale, height * f->scale, 0, 0);
    glm_ortho(0.0, f->width, f->height, 0.0, -1.0, 1.0, f->projection);

    f->root_window->width = width;
    f->root_window->height = height - f->minibuffer_height;

    wl_surface_commit(f->surface);
}


struct frame *frame_create() {
    struct frame **fp = frames - 1;

    if (open_frames == MAX_FRAMES)
        return NULL;

    while (*++fp);
    struct frame *f = *fp = malloc(sizeof(struct frame));

    f->width = 200;
    f->height = 200;
    f->open = true;
    f->scale = 2.0;

    f->has_minibuffer_p = true;
    f->minibuffer_height = active_font->vertical_advance * font_size;
    f->root_window = malloc(sizeof(struct window));
    f->root_window->width = f->width;
    f->root_window->height = f->height - f->minibuffer_height;
    f->root_window->frame = f;
    f->root_window->x = 0;
    f->root_window->y = 0;
    f->root_window->position[0] = 0.0;
    f->root_window->position[1] = 0.0;
    f->root_window->_kinetic_scroll[0] = 0.0;
    f->root_window->_kinetic_scroll[1] = 0.0;
    for (uint32_t axis = 0; axis < 2;++axis)
        f->root_window->_scrolling_freely[axis] = false;
    f->root_window->linum_width = 0;
    f->root_window->contents = NULL;
    for (uint32_t axis = 0; axis < 2; ++axis) {
        for (int i = 0; i < SCROLL_WINDOW_SIZE; ++i) {
            f->root_window->_scroll_time_buffer[axis][i] = 0;
            f->root_window->_scroll_position_buffer[axis][i] = NAN;
        }
    }
    f->root_window->next = NULL;

    /* Share the context between frames */
    f->gl_ctx = eglCreateContext(g_gl_display, g_gl_conf, g_root_ctx, context_attribs);

    f->surface = wl_compositor_create_surface(g_compositor);
    wl_surface_set_user_data(f->surface, f);
    wl_surface_set_buffer_scale(f->surface, f->scale);

    f->gl_window = wl_egl_window_create(f->surface, 200, 200);
    f->gl_surface = platform_create_egl_surface(g_gl_display, g_gl_conf, f->gl_window, NULL);

    f->xdg_surface = xdg_wm_base_get_xdg_surface(g_xdg_wm_base, f->surface);
    f->xdg_toplevel = xdg_surface_get_toplevel(f->xdg_surface);

    xdg_surface_add_listener(f->xdg_surface, &xdg_surface_listener, f);
    xdg_toplevel_add_listener(f->xdg_toplevel, &xdg_toplevel_listener, f);

    xdg_toplevel_set_title(f->xdg_toplevel, "Emacs");
    wl_surface_commit(f->surface);

    eglMakeCurrent(g_gl_display, f->gl_surface, f->gl_surface, f->gl_ctx);
    eglSwapInterval(g_gl_display, 0);

    f->bg_shader = create_program("src/bg-vertex.glsl",
                                  "src/bg-fragment.glsl",
                                  NULL);
    f->text_shader = create_program("src/font-vertex.glsl",
                                    "src/font-fragment.glsl",
                                    "src/font-geometry.glsl");
    f->overlay_shader = create_program("src/overlay-vertex.glsl",
                                       "src/overlay-fragment.glsl",
                                       "src/overlay-geometry.glsl");


    glEnable(GL_SCISSOR_TEST);
    CHECK_ERROR

    glGenBuffers(1, &f->root_window->linum_glyphs);
    CHECK_ERROR
    glGenBuffers(1, &f->root_window->modeline_glyphs);
    CHECK_ERROR
    glGenBuffers(1, &f->root_window->text_area_glyphs);
    CHECK_ERROR

    glBindBuffer(GL_ARRAY_BUFFER, f->root_window->text_area_glyphs);
    CHECK_ERROR
    glBufferData(GL_ARRAY_BUFFER, MAX_GLYPHS_PER_DRAW * sizeof(struct gl_glyph), 0, GL_STATIC_DRAW);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_ERROR

    f->projection_uniform = glGetUniformLocation(f->text_shader, "projection");
    f->font_projection_uniform = glGetUniformLocation(f->text_shader, "font_projection");
    f->font_vertex_uniform = glGetUniformLocation(f->text_shader, "font_vertices");
    f->font_texture_uniform = glGetUniformLocation(f->text_shader, "font_texure");
    f->font_padding_uniform = glGetUniformLocation(f->text_shader, "padding");
    f->offset_uniform = glGetUniformLocation(f->text_shader, "offset");
    /* glUniform1i(f->font_texture_uniform, 0); */
    CHECK_ERROR
    /* glUniform1i(f->font_vertex_uniform, 1); */
    CHECK_ERROR

    f->overlay_projection_uniform = glGetUniformLocation(f->overlay_shader, "projection");
    f->overlay_offset_uniform = glGetUniformLocation(f->overlay_shader, "offset");


    f->bg_projection_uniform = glGetUniformLocation(f->bg_shader, "projection");
    f->bg_accent_color_uniform = glGetUniformLocation(f->bg_shader, "accentColor");

    wl_display_roundtrip(g_display);

    eglSwapBuffers(g_gl_display, f->gl_surface);
    struct wl_callback *callback = wl_surface_frame(f->surface);
    wl_callback_add_listener(callback, &frame_listener, f);
    open_frames++;

    frame_render(f);
    return f;
}

void draw_text(int x, int y, char *text, size_t len, struct font *font,
               uint32_t color, struct window *w, bool flush) {

    static struct gl_glyph glyphs[MAX_GLYPHS_PER_DRAW];
    static int glyph_count = 0;


    int base_x = x;
    int col_width = active_font->horizontal_advances['8'] * font_size;
    int tab_width = 8;
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\n') {
            x = base_x;
            y += font->vertical_advance * font_size;
            continue;
        }
        if (text[i] == '\t') {
            int tab_stop = 0;
            int increment = 0;
            while (tab_stop <= x) tab_stop += (tab_width * col_width);
            x = tab_stop;
            continue;
        }
        if (text[i] == '\r') {
            x = base_x;
            continue;
        }
        if (glyph_count == MAX_GLYPHS_PER_DRAW) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(struct gl_glyph), glyphs);
    CHECK_ERROR
            glDrawArrays(GL_POINTS, 0, glyph_count);
    CHECK_ERROR
            glyph_count = 0;
        }

        glyphs[glyph_count++] = (struct gl_glyph){x, y, color, text[i], font_size, 0.0, 0.0, 0.5};
        x += font->horizontal_advances[text[i]] * font_size;
    }

    if (flush && glyph_count) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(struct gl_glyph), glyphs);
    CHECK_ERROR
        glDrawArrays(GL_POINTS, 0, glyph_count);
    CHECK_ERROR
        glyph_count = 0;
    }

}


void draw_rect(int x, int y, int w, int h, char *color_, struct window *win) {
    vec3 color;
    parse_color(color_, color);
    glUseProgram(win->frame->bg_shader);
    CHECK_ERROR
    GLuint vbo;
    glGenBuffers(1, &vbo);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    CHECK_ERROR
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    CHECK_ERROR
    glEnableVertexAttribArray(0);
    CHECK_ERROR

    GLfloat rect[6][2] = {
        {x, y},
        {x + w, y},
        {x, y + h},

        {x, y + h},
        {x + w, y},
        {x + w, y + h},
    };

    glUniform3fv(win->frame->bg_accent_color_uniform, 1, (GLfloat *) color);
    CHECK_ERROR
    glUniformMatrix4fv(win->frame->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) win->projection);
    CHECK_ERROR
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), (GLfloat *)rect, GL_DYNAMIC_DRAW);
    CHECK_ERROR

    glDrawArrays(GL_TRIANGLES, 0, 6);
    CHECK_ERROR
    glDisableVertexAttribArray(0);
    CHECK_ERROR
}

void draw_line(int x1, int y1, int x2, int y2, char *color_, struct window *win) {
    vec3 color;
    parse_color(color_, color);
    glUseProgram(win->frame->bg_shader);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    CHECK_ERROR
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    CHECK_ERROR
    glEnableVertexAttribArray(0);
    CHECK_ERROR

    GLfloat line[2][2] = {{x1, y1}, {x2, y2}};
    glLineWidth(1.0 * win->frame->scale);
    CHECK_ERROR

    glUniform3fv(win->frame->bg_accent_color_uniform, 1, (GLfloat *) color);
    CHECK_ERROR
    glUniformMatrix4fv(win->frame->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) win->projection);
    CHECK_ERROR
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat), (GLfloat *)line, GL_DYNAMIC_DRAW);
    CHECK_ERROR

    glDrawArrays(GL_LINES, 0, 2);
    CHECK_ERROR
    glDisableVertexAttribArray(0);
    CHECK_ERROR
}

int max(int a, int b) { return a > b ? a : b; }

static inline void set_region(struct frame *f, int x, int y, int w, int h) {
    /* glScissor wants the botton-left corner of the area, the origin being in
     the bottom-left corner of the frame. */

    glScissor(max(0, x) * f->scale, max(0, f->height - y - h) * f->scale,
              max(0, w) * f->scale, max(0, h) * f->scale);
    CHECK_ERROR
}

void window_render(struct window *w) {

    if (w->position[1] > 0) w->position[1] = 0;
    if (w->position[0] > 0) w->position[0] = 0;

    /* Prevent changing anything outside the window. */

    set_region(w->frame, w->x, w->y, w->width, w->height);

    /* Set projection to offset content to window location. */
    glm_ortho(-w->x, w->width + (w->frame->width - w->width - w->x),
              w->height + (w->frame->height - w->height - w->x), -w->y,
              -1.0, 1.0, w->projection);

    int modeline_h = active_font->vertical_advance * font_size;
    vec3 _color;
    parse_color("0c1014", _color);
    glClearColor(_color[0], _color[1], _color[2], 1.0);
    CHECK_ERROR
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK_ERROR

    /* Modeline */
    draw_rect(0, w->height - modeline_h, w->width, modeline_h, "0a3749", w);

    int ncols = ceil(log10(w->nlines + 1));
    int col_width = active_font->horizontal_advances['8'] * font_size;

    w->linum_width = col_width * (ncols + 2);  /* Empty column on both sides. */

    /* Line number column */
    draw_rect(0, 0, w->linum_width, w->height - modeline_h, "11151c", w);

    set_region(w->frame, w->x + w->linum_width, w->y,
               w->width - w->linum_width, w->height - modeline_h);

    /* Fill column indicator*/
    draw_line(79 * col_width + w->position[1], 0, 79 * col_width + w->position[1], w->height, "0a3749", w);

    struct font *font = active_font;
    GLuint vao;
    glGenVertexArrays(1, &vao);
    CHECK_ERROR
    glBindVertexArray(vao);
    CHECK_ERROR

    glActiveTexture(GL_TEXTURE0);
    CHECK_ERROR
    glBindTexture(GL_TEXTURE_2D, font->texture);
    CHECK_ERROR

    glActiveTexture(GL_TEXTURE1);
    CHECK_ERROR
    glBindTexture(GL_TEXTURE_BUFFER, font->vertex_texture);
    CHECK_ERROR

    glBindBuffer(GL_ARRAY_BUFFER, w->text_area_glyphs);
    CHECK_ERROR

    glEnableVertexAttribArray(0);
    CHECK_ERROR
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, x));
    CHECK_ERROR

    glEnableVertexAttribArray(1);
    CHECK_ERROR
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE,
                           sizeof(struct gl_glyph),
                           (void *)offsetof(struct gl_glyph, color));
    CHECK_ERROR

    glEnableVertexAttribArray(2);
    CHECK_ERROR
    glVertexAttribIPointer(2, 1, GL_INT,
                           sizeof(struct gl_glyph),
                           (void *)offsetof(struct gl_glyph, key));
    CHECK_ERROR

    glEnableVertexAttribArray(3);
    CHECK_ERROR
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                          sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, size));
    CHECK_ERROR

    glEnableVertexAttribArray(4);
    CHECK_ERROR
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, offset));
    CHECK_ERROR

    glEnableVertexAttribArray(5);
    CHECK_ERROR
    glVertexAttribPointer(5, 1, GL_FLOAT,GL_FALSE,
                           sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, skew));
    CHECK_ERROR

    glEnableVertexAttribArray(6);
    CHECK_ERROR
    glVertexAttribPointer(6, 1, GL_FLOAT,GL_FALSE,
                           sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, strength));
    CHECK_ERROR

    glUseProgram(w->frame->text_shader);
    CHECK_ERROR
    glUniformMatrix4fv(w->frame->projection_uniform, 1, GL_FALSE, (GLfloat *) w->projection);
    CHECK_ERROR
    glUniformMatrix4fv(w->frame->font_projection_uniform, 1, GL_FALSE, (GLfloat *) font->texture_projection);
    CHECK_ERROR
    glUniform1f(w->frame->font_padding_uniform, 2.0 / active_font->msdf_font->xheight);
    CHECK_ERROR
    glUniform2f(w->frame->offset_uniform, w->position[1] + w->linum_width, w->position[0]);
    CHECK_ERROR
    glUniform1i(w->frame->font_texture_uniform, 0);
    CHECK_ERROR
    glUniform1i(w->frame->font_vertex_uniform, 1);
    CHECK_ERROR

    /* Draw buffer text. */
    double line_h = active_font->vertical_advance * font_size;
    if (w->contents) {
        for (int i = 0; i < w->nlines; ++i) {
            int vscroll_lines = i * line_h;
            if (vscroll_lines < -w->position[0] - line_h) continue;
            if (vscroll_lines > -w->position[0] + (w->height + line_h)) break;
            draw_text(0.0, line_h * (i + 1), w->contents[i], strlen(w->contents[i]),
                  active_font, 0xffffffff /* color */, w, false /* flush */);
        }
        draw_text(0.0, 0.0, "", 0, active_font, 0, w, true /* flush */);
    }

    set_region(w->frame, w->x, w->y, w->linum_width, w->height - modeline_h);
    glUniform2f(w->frame->offset_uniform, 0.0, w->position[0]);
    CHECK_ERROR
    char buf[36];

    /* Draw line numbers */
    for (int i = 0; i < w->nlines; ++i) {
        int vscroll_lines = i * line_h;
        if (vscroll_lines < -w->position[0] - line_h) continue;
        if (vscroll_lines > -w->position[0] + (w->height + line_h)) break;
        int _n = sprintf(buf, "%*d", ncols, i + 1);
        draw_text(col_width, line_h * (i + 1), buf, _n,
                  active_font, 0x0a3749ff /* color */, w, false /* flush */);
    }
    draw_text(0, 0, "", 0, active_font, 0, w, true /* flush */);

    glDisableVertexAttribArray(0);
    CHECK_ERROR
    glDisableVertexAttribArray(1);
    CHECK_ERROR
    glDisableVertexAttribArray(2);
    CHECK_ERROR
    glDisableVertexAttribArray(3);
    CHECK_ERROR
    glDisableVertexAttribArray(4);
    CHECK_ERROR
    glDisableVertexAttribArray(5);
    CHECK_ERROR
    glBindVertexArray(0);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_ERROR

    /* Overlays */
    glUseProgram(w->frame->overlay_shader);
    CHECK_ERROR
    glUniformMatrix4fv(w->frame->overlay_projection_uniform, 1, GL_FALSE, (GLfloat *) w->projection);
    CHECK_ERROR
    glUniform2f(w->frame->overlay_offset_uniform, w->position[1] + w->linum_width, w->position[0]);
    CHECK_ERROR

    set_region(w->frame, w->x + w->linum_width, w->y,
               w->width - w->linum_width, w->height - modeline_h);

    GLuint vbo;
    GLuint _vao;
    glGenVertexArrays(1, &_vao);
    CHECK_ERROR
    glBindVertexArray(_vao);
    CHECK_ERROR
    glGenBuffers(1, &vbo);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    CHECK_ERROR
    glEnableVertexAttribArray(0);
    CHECK_ERROR
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 12, 0);
    CHECK_ERROR
    glEnableVertexAttribArray(1);
    CHECK_ERROR
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, 12, (void *)8);
    CHECK_ERROR

    /* glLineWidth(2.0 * w->frame->scale); */
    glLineWidth(active_font->msdf_font->underline_thickness * font_size * 2.0);
    CHECK_ERROR
    float underline_offset = active_font->msdf_font->underline_y * font_size;
    /* float underline_offset = 0.0; */
    struct gl_overlay_vertex overlays[] = {
        {13 * col_width, 8 * line_h - underline_offset, 0xc23127ff},
        {26 * col_width, 8 * line_h - underline_offset, 0xc23127ff},
        /* {3 * col_width, 30 * active_font->vertical_advance, 0xc23127ff}, */
        /* {15 * col_width, 30 * active_font->vertical_advance, 0xc23127ff}, */
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(overlays), (GLfloat *)overlays, GL_DYNAMIC_DRAW);
    CHECK_ERROR
    glDrawArrays(GL_LINES, 0, 2);
    CHECK_ERROR

    glBindVertexArray(0);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_ERROR

    glActiveTexture(GL_TEXTURE0);
    CHECK_ERROR
    glBindTexture(GL_TEXTURE_2D, active_font->msdf_atlas_texture);
    CHECK_ERROR


    /* glUseProgram(g_msdf_shader); */
    glUseProgram(g_debug_shader);
    CHECK_ERROR

    GLuint _vbo;
    glGenBuffers(1, &_vbo);
    CHECK_ERROR
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    CHECK_ERROR
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
    CHECK_ERROR
    glEnableVertexAttribArray(0);
    CHECK_ERROR

    GLfloat rect[6][4] = {
        /* {0, 0, -2, -2}, */
        /* {0, 1, -2, 2}, */
        /* {1, 0, 2, 0}, */

        /* {0, 1, -2, 2}, */
        /* {1, 0, 2, -2}, */
        /* {1, 1, 2, 2} */
        {0, 0, 0, 0},
        {0, 1, 0, 2},
        {1, 0, 2, 0},

        {0, 1, 0, 2},
        {1, 0, 2, 0},
        {1, 1, 2, 2}
    };

    glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(GLfloat), (GLfloat *)rect, GL_DYNAMIC_DRAW);
    CHECK_ERROR

    glDrawArrays(GL_TRIANGLES, 0, 6);
    CHECK_ERROR
}

void frame_render(struct frame *f) {

    eglMakeCurrent(g_gl_display, f->gl_surface, f->gl_surface, f->gl_ctx);
    CHECK_ERROR
    eglSwapInterval(g_gl_display, 0);
    CHECK_ERROR

    glViewport(0, 0, f->width * f->scale, f->height * f->scale);
    CHECK_ERROR
    glEnable(GL_BLEND);
    CHECK_ERROR
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    CHECK_ERROR

    set_region(f, 0, f->height - f->minibuffer_height, f->width, f->minibuffer_height);
    vec3 _color;
    parse_color("0c1014", _color);
    glClearColor(_color[0], _color[1], _color[2], 1.0);
    CHECK_ERROR
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK_ERROR

    FOR_EACH_WINDOW (f, w) {
        window_render(w);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    CHECK_ERROR
    glDisableVertexAttribArray(0);
    CHECK_ERROR

    eglSwapBuffers(g_gl_display, f->gl_surface);
    CHECK_ERROR
}

void frame_close(struct frame *f) {
    f->open = false;

    platform_destroy_egl_surface(g_gl_display, f->gl_surface);

    xdg_toplevel_destroy(f->xdg_toplevel);
    xdg_surface_destroy(f->xdg_surface);
    wl_surface_destroy(f->surface);

    struct frame **wp = frames - 1;
    while (*++wp != f);
    free(f);
    *wp = NULL;

    open_frames--;
}
