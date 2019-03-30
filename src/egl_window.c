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

#define FONT_BUFFER_SIZE 4096

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


int parse_color(const char *color, vec3 ret) {
    
    char buf[3] = {0};
    for (int channel = 0; channel < 3; ++channel) {
        memcpy(buf, &color[channel * 2], 2);
        ret[channel] = strtol(buf, NULL, 16) / 255.0;
    }


    return 1;
};

struct glyph glyph_map[254];
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


bool load_font(const char *font_name, int height) {
    if (!ft && FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Could not init freetype library\n");
        return false;
    }
    if (FT_New_Face(ft, font_name, 0, &face)) {
        fprintf(stderr, "Could not init font\n");
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, height * 2.0);

    eglMakeCurrent(g_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_root_ctx);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenTextures(1, &font_texture);
    glBindTexture(GL_TEXTURE_2D, font_texture);


    FT_GlyphSlot g = face->glyph;

    glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RED,
      FONT_BUFFER_SIZE,
      FONT_BUFFER_SIZE,
      0,
      GL_RED,
      GL_UNSIGNED_BYTE,
      0
    );

    int offset_x = 0, offset_y = 0, y_increment = 0;

    for (unsigned int i = 0; i < 254; ++i) {
        FT_Load_Char(face, i, FT_LOAD_RENDER);
        FT_GlyphSlot g = face->glyph;

        if (offset_x + g->bitmap.width > FONT_BUFFER_SIZE) {
            offset_y += (y_increment + 1);
            offset_x = 0;
        }
        
        glTexSubImage2D(GL_TEXTURE_2D, 0, offset_x, offset_y, g->bitmap.width,
                        g->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
        y_increment = y_increment > g->bitmap.rows ? y_increment : g->bitmap.rows;

        glyph_map[i].code = i;
        glyph_map[i].texture = font_texture;
        glyph_map[i].offset_x = (double)offset_x / FONT_BUFFER_SIZE;
        glyph_map[i].offset_y = (double)offset_y / FONT_BUFFER_SIZE;
        glyph_map[i].width = g->bitmap.width / (double)FONT_BUFFER_SIZE;
        glyph_map[i].height = g->bitmap.rows / (double)FONT_BUFFER_SIZE;

        glyph_map[i].bearing_x = g->bitmap_left;
        glyph_map[i].bearing_y = g->bitmap_top;
        glyph_map[i].advance = g->advance.x >> 6;

        offset_x += g->bitmap.width + 1;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return true;
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


    f->bg_shader = create_program("src/bg-vertex.glsl", "src/bg-fragment.glsl");
    f->text_shader = create_program("src/font-vertex.glsl", "src/font-fragment.glsl");

    glGenBuffers(1, &f->root_window->linum_glyphs);
    glGenBuffers(1, &f->root_window->text_area_glyphs);
    glGenBuffers(1, &f->root_window->modeline_glyphs);


    f->projection_uniform = glGetUniformLocation(f->text_shader, "projection");
    f->bg_projection_uniform = glGetUniformLocation(f->bg_shader, "projection");
    f->bg_accent_color_uniform = glGetUniformLocation(f->bg_shader, "accentColor");
    f->color_uniform = glGetUniformLocation(f->text_shader, "textColor");
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


void draw_text(char *texts[], int nrows, int x, int y, double scale) {
    static GLfloat text_data[2048 * 6 * 4] = {0};
    /* static int done = false; */

    int total = 0;
    int counter = 0;
    y += line_spacing / scale;
    int orig_x = x;
    unsigned int bufsize = 0;
    /* if (done) { */
    /*     goto out; */
    /* } */
    for (size_t j = 0; j < nrows; ++j) {
        const char *text = texts[j];
        size_t n = strlen(text);
        total += n;

        struct glyph *_g = NULL;

        for (size_t i = 0; i < n; ++i) {
            struct glyph g = glyph_map[text[i]];
            _g = &g;
            GLfloat _w = (g.width * FONT_BUFFER_SIZE) / scale;
            GLfloat _h = (g.height * FONT_BUFFER_SIZE) / scale;
            GLfloat _x = (x + g.bearing_x / scale);
            GLfloat _y = (y - g.bearing_y / scale);
            GLfloat c[6][4] = {
                {_x, _y,           g.offset_x, g.offset_y},
                {_x + _w, _y,      g.offset_x + g.width, g.offset_y},
                {_x, _y + _h,      g.offset_x, g.offset_y + g.height},

                {_x, _y + _h,      g.offset_x, g.offset_y + g.height},
                {_x + _w, _y,      g.offset_x + g.width, g.offset_y},
                {_x + _w, _y + _h, g.offset_x + g.width, g.offset_y + g.height},
            };
            memcpy(&text_data[counter * 6 * 4], &c, 6 * 4 * sizeof(GLfloat));
            counter++;

            bufsize += sizeof(c);

            x += g.advance / scale;
        }
        y += line_spacing / scale;
        x = orig_x;
    }
 /*    done = true; */
 /* out: */
    glBufferData(GL_ARRAY_BUFFER, 24 * counter * sizeof(GLfloat), text_data, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6 * counter);
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
    
    GLfloat linum_column[6][2] = {
        {x, y},
        {x + w, y},
        {x, y + h},
        
        {x, y + h},
        {x + w, y},
        {x + w, y + h},
    };

    glUniform3fv(win->frame->bg_accent_color_uniform, 1, (GLfloat *) color);
    glUniformMatrix4fv(win->frame->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) win->projection);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), (GLfloat *)linum_column, GL_DYNAMIC_DRAW);
    
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
    
    GLfloat linum_column[2][2] = {{x1, y1}, {x2, y2}};
    glLineWidth(1.0 * win->frame->scale);

    glUniform3fv(win->frame->bg_accent_color_uniform, 1, (GLfloat *) color);
    glUniformMatrix4fv(win->frame->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) win->projection);
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat), (GLfloat *)linum_column, GL_DYNAMIC_DRAW);
    
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(0);
}

static inline void set_region(struct frame *f, int x, int y, int w, int h) {
    /* glScissor wants the botton-left corner of the area, the origin being in
     the bottom-left corner of the frame. */
    glScissor(x * f->scale, (f->height - y - h) * f->scale,
              w * f->scale, h * f->scale);
}

void window_render(struct window *w) {
    
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
    
    /* Line number column */
    draw_rect(0, 0, w->linum_width, w->height, "11151c", w);
    
    /* Fill column indicator*/
    draw_line(200, 0, 200, w->height, "0a3749", w);
    
    glActiveTexture(GL_TEXTURE0);

    glUseProgram(w->frame->text_shader);

    glBindTexture(GL_TEXTURE_2D, font_texture);

    /* glUniformMatrix4fv(w->frame->projection_uniform, 1, GL_FALSE, (GLfloat *) w->frame->projection); */
    glUniformMatrix4fv(w->frame->projection_uniform, 1, GL_FALSE, (GLfloat *) w->projection);
    if (w->position[1] < w->linum_width) w->position[1] = w->linum_width;
    if (w->position[0] < 0) w->position[0] = 0;
    glUniform2f(w->frame->offset_uniform, w->position[1], w->position[0]);
    GLfloat color[3] = {1.0, 0.3, 0.3};
    glUniform3fv(w->frame->color_uniform, 1, (GLfloat *) color);
 

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
    
    char *texts[] = {
        "def add(a: int, b: int) -> int:",
        "    \"\"\" The main entry point. \"\"\"",
        "    return a + b",
        " ",
        "main(3, 5)"
    };
    int LINES = 5;

    
    draw_text(texts, LINES, 0, 0, w->frame->scale);

    char *texts2[] = {
        "1",
        "2",
        "3",
        "4",
        "5"
    };
    
    glUniform2f(w->frame->offset_uniform, 30, w->position[0]);
    draw_text(texts2, LINES, 0, 0, w->frame->scale);

    /* Modeline */
    draw_rect(0, w->height - 20, w->width, 20, "0a3749", w);
    
    glDisable(GL_SCISSOR_TEST);
}

void frame_render(struct frame *f) {

    eglMakeCurrent(g_gl_display, f->gl_surface, f->gl_surface, f->gl_ctx);

    glViewport(0, 0, f->width * f->scale, f->height * f->scale);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    
    window_render(f->root_window);

    /* fprintf(stderr, "%f\n", w->height * 1.0); */
    /* vec3 _color; */
    /* parse_color("0c1014", _color); */
    /* glClearColor(_color[0], _color[1], _color[2], 1.0); */
    /* glClear(GL_COLOR_BUFFER_BIT); */
    
    /* glUseProgram(f->bg_shader); */
    /* glEnableVertexAttribArray(0); */
    
    /* GLuint vbo2; */
    /* glGenBuffers(1, &vbo2); */
    /* glBindBuffer(GL_ARRAY_BUFFER, vbo2); */
    /* glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0); */
    
    /* GLfloat linum_column[6][2] = { */
    /*     {0, 0}, */
    /*     {100, 0}, */
    /*     {0, f->height}, */
        
    /*     {0, f->height}, */
    /*     {100, 0}, */
    /*     {100, f->height}, */

    /* }; */
    /* /\* GLfloat bg_color[3] = {1.0, 0.3, 0.3}; *\/ */
    /* vec3 bg_accent_color; */
    /* parse_color("11151c", bg_accent_color); */
    /* glUniform3fv(f->bg_accent_color_uniform, 1, (GLfloat *) bg_accent_color); */
    /* glUniformMatrix4fv(f->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) projection); */
    /* glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), (GLfloat *)linum_column, GL_DYNAMIC_DRAW); */
    
    /* glDrawArrays(GL_TRIANGLES, 0, 6); */

    /* glDisableVertexAttribArray(0); */



    /* glActiveTexture(GL_TEXTURE0); */

    /* glUseProgram(f->text_shader); */

    /* glBindTexture(GL_TEXTURE_2D, font_texture); */

    /* glUniformMatrix4fv(f->projection_uniform, 1, GL_FALSE, (GLfloat *) projection); */
    /* /\* uint32_t t = timestamp(); *\/ */
    /* /\* glUniform2f(w->offset_uniform, 100 + sin(t / 200.0) * 100.0, 100 + cos(t / 200.0) * 100.0); *\/ */
    /* if (f->position[1] < 100) f->position[1] = 100; */
    /* if (f->position[0] < 0) f->position[0] = 0; */
    /* glUniform2f(f->offset_uniform, f->position[1], f->position[0]); */
    /* GLfloat color[3] = {1.0, 0.3, 0.3}; */
    /* glUniform3fv(f->color_uniform, 1, (GLfloat *) color); */
 
    /* GLuint vbo; */
    /* glGenBuffers(1, &vbo); */
    /* glEnableVertexAttribArray(0); */
    /* glBindBuffer(GL_ARRAY_BUFFER, vbo); */
    /* glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0); */
    
    /* char *texts[] = { */
    /*     "def add(a: int, b: int) -> int:", */
    /*     "    \"\"\" The main entry point. \"\"\"", */
    /*     "    return a + b", */
    /*     " ", */
    /*     "main(3, 5)" */
    /* }; */
    /* int LINES = 5; */

    /* /\* #define LINES 1 *\/ */
    /* /\* char *texts[LINES] = {0}; *\/ */
    /* /\* for (int i = 0; i < LINES; ++i) { *\/ */
    /* /\*     texts[i] = "The quick brown fox jumps over the lazy dog."; *\/ */
    /* /\* } *\/ */
    
    /* render_text(texts, LINES, 0, 0, f->scale);//line_spacing); */
    
    /* /\* char *texts2[] = { *\/ */
    /* /\*     "1", *\/ */
    /* /\*     "2", *\/ */
    /* /\*     "3", *\/ */
    /* /\*     "4", *\/ */
    /* /\*     "5" *\/ */
    /* /\* }; *\/ */
    
    /* /\* glUniform2f(w->offset_uniform, w->position[1], 0); *\/ */
    /* /\* render_text(texts2, LINES, 0, line_spacing); *\/ */


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
