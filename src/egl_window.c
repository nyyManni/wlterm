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
    wl_egl_window_resize(w->gl_window, width * w->scale, height * w->scale, 0, 0);
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


struct window *window_create() {
    struct window **wp = windows - 1;

    if (open_windows == MAX_WINDOWS)
        return NULL;

    while (*++wp);
    struct window *w = *wp = malloc(sizeof(struct window));

    w->width = 200;
    w->height = 200;
    w->open = true;
    w->scale = 2.0;
    w->position[0] = 0.0;
    w->position[1] = 0.0;
    w->__position_pending[0] = 0;
    w->__position_pending[1] = 0;

    /* Share the context between windows */
    w->gl_ctx = eglCreateContext(g_gl_display, g_gl_conf, g_root_ctx, context_attribs);

    w->surface = wl_compositor_create_surface(g_compositor);
    wl_surface_set_user_data(w->surface, w);
    wl_surface_set_buffer_scale(w->surface, w->scale);

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


    w->bg_shader = create_program("src/bg-vertex.glsl", "src/bg-fragment.glsl");
    w->text_shader = create_program("src/font-vertex.glsl", "src/font-fragment.glsl");

    glGenBuffers(1, &w->linum_glyphs);
    glGenBuffers(1, &w->text_area_glyphs);
    glGenBuffers(1, &w->modeline_glyphs);

    w->projection_uniform = glGetUniformLocation(w->text_shader, "projection");
    w->bg_projection_uniform = glGetUniformLocation(w->bg_shader, "projection");
    w->bg_accent_color_uniform = glGetUniformLocation(w->bg_shader, "accentColor");
    w->color_uniform = glGetUniformLocation(w->text_shader, "textColor");
    w->offset_uniform = glGetUniformLocation(w->text_shader, "offset");

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


void render_text(char *texts[], int nrows, int x, int y, double scale) {
    static GLfloat text_data[2048 * 6 * 4] = {0};
    static int done = false;

    static int total = 0;
    static int counter = 0;
    y += line_spacing / scale;
    int orig_x = x;
    static unsigned int bufsize = 0;
    if (done) {
        goto out;
    }
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
    done = true;
 out:
    glBufferData(GL_ARRAY_BUFFER, 24 * counter * sizeof(GLfloat), text_data, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6 * counter);
}


void window_render(struct window *w) {

    eglMakeCurrent(g_gl_display, w->gl_surface, w->gl_surface, w->gl_ctx);

    mat4 projection;
    glm_ortho(0.0, w->width, w->height, 0.0, -1.0, 1.0, projection);
    glViewport(0, 0, w->width * w->scale, w->height * w->scale);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* fprintf(stderr, "%f\n", w->height * 1.0); */
    vec3 _color;
    parse_color("0c1014", _color);
    glClearColor(_color[0], _color[1], _color[2], 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(w->bg_shader);
    glEnableVertexAttribArray(0);
    
    GLuint vbo2;
    glGenBuffers(1, &vbo2);
    glBindBuffer(GL_ARRAY_BUFFER, vbo2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    
    GLfloat linum_column[6][2] = {
        {0, 0},
        {100, 0},
        {0, w->height},
        
        {0, w->height},
        {100, 0},
        {100, w->height},

        /* {10 + 100, 10}, */
        /* {20 + 100, 10}, */
        /* {10 + 100, 28}, */
        
        /* {10 + 100, 28}, */
        /* {20 + 100, 10}, */
        /* {20 + 100, 28}, */
    };
    /* GLfloat bg_color[3] = {1.0, 0.3, 0.3}; */
    vec3 bg_accent_color;
    parse_color("11151c", bg_accent_color);
    glUniform3fv(w->bg_accent_color_uniform, 1, (GLfloat *) bg_accent_color);
    glUniformMatrix4fv(w->bg_projection_uniform, 1, GL_FALSE, (GLfloat *) projection);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), (GLfloat *)linum_column, GL_DYNAMIC_DRAW);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(0);



    glActiveTexture(GL_TEXTURE0);

    glUseProgram(w->text_shader);

    glBindTexture(GL_TEXTURE_2D, font_texture);

    glUniformMatrix4fv(w->projection_uniform, 1, GL_FALSE, (GLfloat *) projection);
    /* uint32_t t = timestamp(); */
    /* glUniform2f(w->offset_uniform, 100 + sin(t / 200.0) * 100.0, 100 + cos(t / 200.0) * 100.0); */
    if (w->position[1] < 100) w->position[1] = 100;
    if (w->position[0] < 0) w->position[0] = 0;
    glUniform2f(w->offset_uniform, w->position[1], w->position[0]);
    GLfloat color[3] = {1.0, 0.3, 0.3};
    glUniform3fv(w->color_uniform, 1, (GLfloat *) color);
 
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

    /* #define LINES 1 */
    /* char *texts[LINES] = {0}; */
    /* for (int i = 0; i < LINES; ++i) { */
    /*     texts[i] = "The quick brown fox jumps over the lazy dog."; */
    /* } */
    
    render_text(texts, LINES, 0, 0, w->scale);//line_spacing);
    
    /* char *texts2[] = { */
    /*     "1", */
    /*     "2", */
    /*     "3", */
    /*     "4", */
    /*     "5" */
    /* }; */
    
    /* glUniform2f(w->offset_uniform, w->position[1], 0); */
    /* render_text(texts2, LINES, 0, line_spacing); */


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
