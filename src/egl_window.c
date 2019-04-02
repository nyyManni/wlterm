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

#include "egl_util.h"
#include "egl_window.h"
#include "xdg-shell-client-protocol.h"

#define FONT_BUFFER_SIZE 4096
/* #define FONT_BUFFER_SIZE 512 */

#define CHECK_ERROR                               \
    do {                                          \
        GLenum err = glGetError();                \
        if (err) {                                \
            fprintf(stderr, "error: %i\n", err);  \
        }                                         \
    } while (0);

int line_spacing = 18 * 2.0;

FT_Library ft = NULL;
FT_Face face = NULL;

GLuint font_texture;
struct font *active_font;


int parse_color(const char *color, vec3 ret) {

    char buf[3] = {0};
    for (int channel = 0; channel < 3; ++channel) {
        memcpy(buf, &color[channel * 2], 2);
        ret[channel] = strtol(buf, NULL, 16) / 255.0;
    }


    return 1;
};

/* struct glyph glyph_map[254]; */
mat4 text_projection;

extern struct wl_display *g_display;
EGLDisplay g_gl_display;
EGLConfig g_gl_conf;
EGLContext g_root_ctx;

extern struct wl_compositor *g_compositor;
extern struct xdg_wm_base *g_xdg_wm_base;

struct frame *active_frame;
struct frame *frames[MAX_FRAMES];
int open_frames = 0;

static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

const struct wl_callback_listener frame_listener;
static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct frame *f = data;
    if (!f->open)
        return;
    wl_callback_destroy(callback);
    frame_render(f);

    callback = wl_surface_frame(f->surface);
    wl_callback_add_listener(callback, &frame_listener, f);
    wl_surface_commit(f->surface);
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


struct font *load_font(const char *font_name, int height) {
    struct font *f = malloc(sizeof (struct font));
    active_font = f;
    f->texture_size = FONT_BUFFER_SIZE;

    if (!ft && FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Could not init freetype library\n");
        return NULL;
    }
    if (FT_New_Face(ft, font_name, 0, &face)) {
        fprintf(stderr, "Could not init font\n");
        return NULL;
    }
    FT_Set_Pixel_Sizes(face, 0, height * 2.0);
    
    f->vertical_advance = face->size->metrics.height >> (6 + 1);

    eglMakeCurrent(g_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_root_ctx);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenTextures(1, &f->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f->texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, f->texture_size, f->texture_size,
                 0, GL_RED, GL_UNSIGNED_BYTE, 0);

    glm_ortho(-f->texture_size, f->texture_size,
              -f->texture_size, f->texture_size,
              -1.0, 1.0, f->texture_projection);

    glGenBuffers(1, &f->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, f->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, 254 * 6 * sizeof(GLfloat), 0, GL_STATIC_READ);

    int offset_x = 0, offset_y = 0, y_increment = 0;

    for (unsigned int i = 0; i < 254; ++i) {
        FT_Load_Char(face, i, FT_LOAD_RENDER);
        FT_GlyphSlot g = face->glyph;

        if (offset_x + g->bitmap.width > FONT_BUFFER_SIZE) {
            offset_y += (y_increment + 1);
            offset_x = 0;
        }

        glActiveTexture(GL_TEXTURE0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, offset_x, offset_y, g->bitmap.width,
                        g->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
        y_increment = y_increment > g->bitmap.rows ? y_increment : g->bitmap.rows;

        f->horizontal_advances[i] = g->advance.x >> (6 + 1);

        float _buf[] = {
            offset_x, offset_y, g->bitmap.width, g->bitmap.rows,
            g->bitmap_left, -g->bitmap_top,
        };
        glBufferSubData(GL_ARRAY_BUFFER, i * 6 * sizeof(GLuint), sizeof(_buf), _buf);
        offset_x += g->bitmap.width + 1;

    }
    font_texture = f->texture;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &f->vertex_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, f->vertex_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, f->vertex_buffer);

    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return f;
}

void frame_resize(struct frame *f, int width, int height) {

    f->width = width;
    f->height = height;
    wl_egl_window_resize(f->gl_window, width * f->scale, height * f->scale, 0, 0);
    glm_ortho(0.0, f->width, f->height, 0.0, -1.0, 1.0, f->projection);

    f->root_window->width = width - 100;
    f->root_window->height = height - f->minibuffer_height - 100;
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
    f->minibuffer_height = line_spacing / f->scale;
    f->root_window = malloc(sizeof(struct window));
    f->root_window->width = f->width;
    f->root_window->height = f->height - f->minibuffer_height;
    f->root_window->frame = f;
    f->root_window->x = 100;
    f->root_window->y = 100;
    f->root_window->position[0] = 0.0;
    f->root_window->position[1] = 0.0;
    f->root_window->__position_pending[0] = 0;
    f->root_window->__position_pending[1] = 0;
    f->root_window->linum_width = 50;
    f->root_window->contents = NULL;

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

    f->bg_shader = create_program("src/bg-vertex.glsl", "src/bg-fragment.glsl", NULL);
    f->text_shader = create_program("src/font-vertex.glsl", "src/font-fragment.glsl",
                                    "src/font-geometry.glsl");

    glGenBuffers(1, &f->root_window->linum_glyphs);
    glGenBuffers(1, &f->root_window->modeline_glyphs);
    glGenBuffers(1, &f->root_window->text_area_glyphs);

    glBindBuffer(GL_ARRAY_BUFFER, f->root_window->text_area_glyphs);
    glBufferData(GL_ARRAY_BUFFER, MAX_GLYPHS_PER_DRAW * sizeof(struct gl_glyph), 0, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    f->projection_uniform = glGetUniformLocation(f->text_shader, "projection");
    f->font_projection_uniform = glGetUniformLocation(f->text_shader, "font_projection");
    f->font_vertex_uniform = glGetUniformLocation(f->text_shader, "font_vertices");
    f->font_texture_uniform = glGetUniformLocation(f->text_shader, "font_texure");
    glUniform1i(f->font_texture_uniform, 0);
    glUniform1i(f->font_vertex_uniform, 1);

    f->bg_projection_uniform = glGetUniformLocation(f->bg_shader, "projection");
    f->bg_accent_color_uniform = glGetUniformLocation(f->bg_shader, "accentColor");

    f->color_uniform = glGetUniformLocation(f->text_shader, "font_color");
    f->offset_uniform = glGetUniformLocation(f->text_shader, "offset");

    wl_display_roundtrip(g_display);
    wl_surface_commit(f->surface);

    wl_surface_commit(f->surface);

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

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font->texture);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, font->vertex_texture);

    glBindBuffer(GL_ARRAY_BUFFER, w->text_area_glyphs);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0);

    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, 16, (void *)8);

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_INT, 16, (void *)12);

    glUseProgram(w->frame->text_shader);
    glUniformMatrix4fv(w->frame->projection_uniform, 1, GL_FALSE, (GLfloat *) w->projection);
    glUniformMatrix4fv(w->frame->font_projection_uniform, 1, GL_FALSE, (GLfloat *) font->texture_projection);
    glUniform2f(w->frame->offset_uniform, w->position[1] + w->linum_width, w->position[0]);
    glUniform1i(w->frame->font_texture_uniform, 0);
    glUniform1i(w->frame->font_vertex_uniform, 1);

    int base_x = x;
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\n') {
            x = base_x;
            y += font->vertical_advance;
            continue;
        }
        if (text[i] == '\r') {
            x = base_x;
        }
        if (glyph_count == MAX_GLYPHS_PER_DRAW) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(struct gl_glyph), glyphs);
            glDrawArrays(GL_POINTS, 0, glyph_count);
            glyph_count = 0;
        }

        glyphs[glyph_count++] = (struct gl_glyph){x, y, color, text[i]};
        x += font->horizontal_advances[text[i]];
    }

    if (flush) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(struct gl_glyph), glyphs);
        glDrawArrays(GL_POINTS, 0, glyph_count);
        glyph_count = 0;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


void draw_rect(int x, int y, int w, int h, char *color_, struct window *win) {
    vec3 color;
    parse_color(color_, color);
    glUseProgram(win->frame->bg_shader);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    GLfloat rect[6][2] = {
        {x, y},
        {x + w, y},
        {x, y + h},

        {x, y + h},
        {x + w, y},
        {x + w, y + h},
    };

    glUniform3fv(win->frame->bg_accent_color_uniform, 1, (GLfloat *) color);
    glUniformMatrix4fv(win->frame->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) win->projection);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), (GLfloat *)rect, GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
}

void draw_line(int x1, int y1, int x2, int y2, char *color_, struct window *win) {
    vec3 color;
    parse_color(color_, color);
    glUseProgram(win->frame->bg_shader);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    GLfloat line[2][2] = {{x1, y1}, {x2, y2}};
    glLineWidth(1.0 * win->frame->scale);

    glUniform3fv(win->frame->bg_accent_color_uniform, 1, (GLfloat *) color);
    glUniformMatrix4fv(win->frame->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) win->projection);
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat), (GLfloat *)line, GL_DYNAMIC_DRAW);

    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(0);
}

void draw_line_numbers(struct window *w, int n) {
    int ncols = ceil(log10(n + 1));
    int col_width = active_font->horizontal_advances['8'];

    w->linum_width = col_width * (ncols + 2);  /* Empty column on both sides. */

    /* Line number column */
    draw_rect(0, 0, w->linum_width, w->height, "11151c", w);
    
    char fmt[10] = {0};
    sprintf(fmt, "%%%dd", ncols);
    char buf[36];

    for (int i = 0; i < w->nlines; ++i) {
        int vscroll_lines = i * active_font->vertical_advance;
        if (vscroll_lines < -w->position[0] - active_font->vertical_advance) continue;
        if (vscroll_lines > -w->position[0] + (w->height + active_font->vertical_advance)) break;
        /* memset(buf, 0, 36); */
        int _n = sprintf(buf, fmt, i + 1);
        /* int _n = sprintf(buf, "%5d", i + 1); */
        draw_text(-w->linum_width - w->position[1], active_font->vertical_advance * (i + 1), buf, _n,
                  active_font, 0xffffffff /* color */, w, false /* flush */);
    }
    draw_text(0, 0, "", 0,
              active_font, 0xffffffff /* color */, w, true /* flush */);
    /* for  */
    /* glActiveTexture(GL_TEXTURE0); */
    /* glBindTexture(GL_TEXTURE_2D, font_texture); */

    /* glUseProgram(w->frame->text_shader); */

    /* vec3 text_color; */
    /* parse_color("0a3749", text_color); */
    /* glUniform2f(w->frame->offset_uniform, 30, w->position[0]); */
    /* glUniform3fv(w->frame->color_uniform, 1, text_color); */

    /* glBindBuffer(GL_ARRAY_BUFFER, w->linum_glyphs); */
    /* glEnableVertexAttribArray(0); */
    /* glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0); */

    /* int nglyphs = 0; */
    /* for (int i = 1; i <= n; ++i) { */
    /*     nglyphs += ceil(log10(i + 1)); */
    /* } */

    /* GLfloat *buf = malloc(nglyphs * 24 * sizeof(GLfloat)); */

    /* for (int l = 1; l <= n; ++l) { */
    /*     int offset_x = 0; */
    /*     for (int e = ncols; e; --e) { */
    /*         if (l >= pow(10, e)) { */
    /*             int d = (l % (int)pow(10, e + 1) / (int)pow(10, e)) + 48; */

    /*             struct glyph g = glyph_map[d]; */

    /*             GLfloat _w = (g.width * FONT_BUFFER_SIZE) / scale; */
    /*             GLfloat _h = (g.height * FONT_BUFFER_SIZE) / scale; */
    /*             GLfloat _x = (x + g.bearing_x / scale); */
    /*             GLfloat _y = (y - g.bearing_y / scale); */
    /*             GLfloat c[6][4] = { */
    /*                 {_x, _y,           g.offset_x, g.offset_y}, */
    /*                 {_x + _w, _y,      g.offset_x + g.width, g.offset_y}, */
    /*                 {_x, _y + _h,      g.offset_x, g.offset_y + g.height}, */

    /*                 {_x, _y + _h,      g.offset_x, g.offset_y + g.height}, */
    /*                 {_x + _w, _y,      g.offset_x + g.width, g.offset_y}, */
    /*                 {_x + _w, _y + _h, g.offset_x + g.width, g.offset_y + g.height}, */
    /*             }; */
    /*             memcpy(&text_data[counter * 6 * 4], &c, 6 * 4 * sizeof(GLfloat)); */






    /*         } */
    /*         offset_x += 9; */
    /*     } */
    /*     /\* int offset_x = 0; *\/ */

    /*     /\* for (int b = ceil(log10(l))) *\/ */


    /* } */


    /* glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(GLfloat) *nglyphs, buf, GL_DYNAMIC_DRAW); */
    /* glDrawArrays(GL_TRIANGLES, 0, 6 * nglyphs); */

    /* glDisableVertexAttribArray(0); */
    /* glActiveTexture(0); */
}

static inline void set_region(struct frame *f, int x, int y, int w, int h) {
    /* glScissor wants the botton-left corner of the area, the origin being in
     the bottom-left corner of the frame. */
    glScissor(x * f->scale, (f->height - y - h) * f->scale,
              w * f->scale, h * f->scale);
}

void window_render(struct window *w) {

    if (w->position[1] > 0) w->position[1] = 0;
    if (w->position[0] > 0) w->position[0] = 0;

    /* Prevent changing anything outside the window. */
    glEnable(GL_SCISSOR_TEST);

    set_region(w->frame, w->x, w->y, w->width, w->height);

    /* Set projection to offset content to window location. */
    glm_ortho(-w->x, w->width + (w->frame->width - w->width - w->x),
              w->height + (w->frame->height - w->height - w->x), -w->y,
              -1.0, 1.0, w->projection);

    vec3 _color;
    parse_color("0c1014", _color);
    glClearColor(_color[0], _color[1], _color[2], 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    draw_line_numbers(w, w->nlines);

    /* Modeline */
    draw_rect(0, w->height - 20, w->width, 20, "0a3749", w);

    set_region(w->frame, w->x + w->linum_width, w->y,
               w->width - w->linum_width, w->height - 20);

    /* Fill column indicator*/
    draw_line(400 + w->position[1], 0, 400 + w->position[1], w->height, "0a3749", w);

    if (w->contents) {
        for (int i = 0; i < w->nlines; ++i) {
            int vscroll_lines = i * active_font->vertical_advance;
            if (vscroll_lines < -w->position[0] - active_font->vertical_advance) continue;
            if (vscroll_lines > -w->position[0] + (w->height + active_font->vertical_advance)) break;
            draw_text(0.0, active_font->vertical_advance * (i + 1), w->contents[i], strlen(w->contents[i]),
                  active_font, 0xffffffff /* color */, w, false /* flush */);
        }
        draw_text(0.0, 0.0, "", 0,
                  active_font, 0xffffffff /* color */, w, true /* flush */);

    }

    glDisable(GL_SCISSOR_TEST);
}

void frame_render(struct frame *f) {

    eglMakeCurrent(g_gl_display, f->gl_surface, f->gl_surface, f->gl_ctx);

    glViewport(0, 0, f->width * f->scale, f->height * f->scale);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    window_render(f->root_window);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(g_gl_display, f->gl_surface);
    wl_surface_commit(f->surface);
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
