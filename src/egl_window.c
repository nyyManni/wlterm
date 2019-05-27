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

#define FONT_BUFFER_SIZE 1024
/* #define FONT_BUFFER_SIZE 512 */
#define MSDF_PRELOAD_N 254
#define CHECK_ERROR                                                  \
    do {                                                             \
        GLenum err = glGetError();                                   \
        if (err) {                                                   \
            fprintf(stderr, "line %d error: %x \n", __LINE__, err);  \
        }                                                            \
    } while (0);

/* double font_size = 8.5; */
/* double font_size = 8.5 / 18.0; */
double font_size = 8.5 / 8.0;

GLenum err;


/* GLuint font_texture; */
GLuint g_msdf_shader;

mat4 g_msdf_projection;
GLuint g_msdf_projection_uniform;
GLuint g_msdf_offset_uniform;
GLuint g_msdf_metadata_uniform;
GLuint g_msdf_point_data_uniform;
GLuint g_msdf_translate_uniform;
GLuint g_msdf_scale_uniform;
GLuint g_msdf_range_uniform;
GLuint g_msdf_glyph_height_uniform;
GLuint g_msdf_meta_offset_uniform;
GLuint g_msdf_point_offset_uniform;

GLuint g_msdf_atlas_texture;
GLuint g_msdf_index_buffer;
GLuint g_msdf_index_texture;
GLuint g_msdf_framebuffer;
/* GLuint g_msdf_glyph_uniform; */
/* GLuint g_msdf_glyph_texture; */


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

    g_msdf_shader =  create_program("src/msdf_vertex.glsl",
                                    "src/msdf_fragment.glsl",
                                    NULL);
    g_msdf_projection_uniform = glGetUniformLocation(g_msdf_shader, "projection");
    g_msdf_offset_uniform = glGetUniformLocation(g_msdf_shader, "offset");
    g_msdf_metadata_uniform = glGetUniformLocation(g_msdf_shader, "metadata");
    g_msdf_point_data_uniform = glGetUniformLocation(g_msdf_shader, "point_data");
    g_msdf_translate_uniform = glGetUniformLocation(g_msdf_shader, "translate");
    g_msdf_scale_uniform = glGetUniformLocation(g_msdf_shader, "scale");
    g_msdf_range_uniform = glGetUniformLocation(g_msdf_shader, "range");
    g_msdf_glyph_height_uniform = glGetUniformLocation(g_msdf_shader, "glyph_height");
    g_msdf_meta_offset_uniform = glGetUniformLocation(g_msdf_shader, "meta_offset");
    g_msdf_point_offset_uniform = glGetUniformLocation(g_msdf_shader, "point_offset");

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

void generate_msdf_atlas(const char *font_name, float scale, float range) {

    eglMakeCurrent(g_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_root_ctx);
    eglSwapInterval(g_gl_display, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint meta_buffer, point_buffer;
    glGenBuffers(1, &meta_buffer);
    glGenBuffers(1, &point_buffer);
    glGenBuffers(1, &g_msdf_index_buffer);

    GLuint meta_texture, point_texture;
    glGenTextures(1, &meta_texture);
    glGenTextures(1, &point_texture);

    glGenTextures(1, &g_msdf_atlas_texture);
    glGenFramebuffers(1, &g_msdf_framebuffer);


    struct font *f = malloc(sizeof (struct font));

    msdf_font_handle msdf_font = msdf_load_font(font_name);
    f->msdf_font = msdf_font;

    /* Calculate the amount of memory needed on the GPU.*/
    size_t meta_sizes[MSDF_PRELOAD_N];
    size_t point_sizes[MSDF_PRELOAD_N];
    size_t meta_size_sum = 0;
    size_t point_size_sum = 0;
    for (size_t i = 0; i < MSDF_PRELOAD_N; ++i) {
        msdf_glyph_buffer_size(msdf_font, i, &meta_sizes[i], &point_sizes[i]);
        meta_size_sum += meta_sizes[i];
        point_size_sum += point_sizes[i];
    }

    /* Allocate the calculated amount. */
    void *point_data = malloc(point_size_sum);
    void *metadata = malloc(meta_size_sum);


    /* Serialize the glyphs into RAM. */
    f->vertical_advance = msdf_font->height;
    struct gl_glyph_atlas_item atlas_index[MSDF_PRELOAD_N];
    char *meta_ptr = metadata;
    char *point_ptr = point_data;
    float offset_x = 1, offset_y = 1, y_increment = 0;
    for (size_t i = 0; i < MSDF_PRELOAD_N; ++i) {
        float glyph_width, glyph_height, buffer_width, buffer_height;
        float bearing_x, bearing_y, advance;
        msdf_serialize_glyph(msdf_font, i,
                             meta_ptr,
                             point_ptr,
                             &glyph_width, &glyph_height,
                             &bearing_x, &bearing_y,
                             &advance);

        buffer_width = (glyph_width + range) * scale;
        buffer_height = (glyph_height + range) * scale;

        meta_ptr += meta_sizes[i];
        point_ptr += point_sizes[i];

        if (offset_x + buffer_width > FONT_BUFFER_SIZE) {
            offset_y += (y_increment + 1);
            offset_x = 1;
            y_increment = 0;
        }
        y_increment = buffer_height > y_increment ? buffer_height : y_increment;

        atlas_index[i].offset_x = offset_x;
        atlas_index[i].offset_y = offset_y;
        atlas_index[i].size_x = buffer_width;
        atlas_index[i].size_y = buffer_height;
        atlas_index[i].bearing_x = bearing_x;
        atlas_index[i].bearing_y = bearing_y;
        atlas_index[i].glyph_width = glyph_width;
        atlas_index[i].glyph_height = glyph_height;
        f->horizontal_advances[i] = advance;

        offset_x += buffer_width + 1;
    }

    /* Allocate and fill the buffers on GPU. */
    glBindBuffer(GL_ARRAY_BUFFER, meta_buffer);
    glBufferData(GL_ARRAY_BUFFER, meta_size_sum, metadata, GL_STATIC_READ);

    glBindBuffer(GL_ARRAY_BUFFER, point_buffer);
    glBufferData(GL_ARRAY_BUFFER, point_size_sum, point_data, GL_STATIC_READ);

    glBindBuffer(GL_ARRAY_BUFFER, g_msdf_index_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(atlas_index), atlas_index, GL_STATIC_READ);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Link sampler textures to the buffers. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, meta_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, meta_buffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, point_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, point_buffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &g_msdf_index_texture);
    glBindTexture(GL_TEXTURE_BUFFER, g_msdf_index_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, g_msdf_index_buffer);

    glActiveTexture(GL_TEXTURE0);


    /* Generate the atlas texture and bind it as the framebuffer. */
    glBindTexture(GL_TEXTURE_2D, g_msdf_atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    int msdf_texture_size = FONT_BUFFER_SIZE;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, msdf_texture_size,
                 msdf_texture_size, 0, GL_RGBA, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, meta_texture);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, point_texture);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_msdf_framebuffer);

    glUseProgram(g_msdf_shader);
    glUniform1i(g_msdf_metadata_uniform, 0);
    glUniform1i(g_msdf_point_data_uniform, 1);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    mat4 msdf_projection;
    glm_ortho(-msdf_texture_size, msdf_texture_size,
              -msdf_texture_size, msdf_texture_size,
              -1.0, 1.0, g_msdf_projection);
    glm_ortho(0, msdf_texture_size,
              0, msdf_texture_size,
              -1.0, 1.0, msdf_projection);

    glUniformMatrix4fv(g_msdf_projection_uniform, 1, GL_FALSE, (GLfloat *) msdf_projection);
    glUniform2f(g_msdf_scale_uniform, scale, scale);
    glUniform1f(g_msdf_range_uniform, range);
    glUniform1i(g_msdf_meta_offset_uniform, 0);
    glUniform1i(g_msdf_point_offset_uniform, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_msdf_atlas_texture, 0);
    CHECK_ERROR

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("framebuffer incomplete: %x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glViewport(0, 0, msdf_texture_size, msdf_texture_size);

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint _vbo;
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), 0, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof (GLfloat), 0);
    glEnableVertexAttribArray(0);

    int meta_offset = 0;
    int point_offset = 0;
    for (int i = 0; i < MSDF_PRELOAD_N; ++i) {
        struct gl_glyph_atlas_item g = atlas_index[i];
        float w = g.size_x;
        float h = g.size_y;
        GLfloat bounding_box[] = {0, 0, w, 0, 0, h, 0, h, w, 0, w, h};
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bounding_box), bounding_box);

        glUniform2f(g_msdf_translate_uniform, -g.bearing_x + range / 2.0, 
                    g.glyph_height - g.bearing_y + range / 2.0);

        glUniform2f(g_msdf_offset_uniform, g.offset_x, g.offset_y);
        glUniform1i(g_msdf_meta_offset_uniform, meta_offset);
        glUniform1i(g_msdf_point_offset_uniform, point_offset / (2 * sizeof(GLfloat)));
        glUniform1f(g_msdf_glyph_height_uniform, g.size_y);

        /* Do not bother rendering control characters */
        if (i > 31 && !(i > 126 && i < 160))
            glDrawArrays(GL_TRIANGLES, 0, 6);

        meta_offset += meta_sizes[i];
        point_offset += point_sizes[i];
    }
    glDisableVertexAttribArray(0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    active_font = f;
}

struct font *load_font(const char *font_name, int height) {
    generate_msdf_atlas(font_name, 2.0, 4.0);
    return (struct font *)1;
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
    f->font_padding_uniform = glGetUniformLocation(f->text_shader, "padding");
    f->offset_uniform = glGetUniformLocation(f->text_shader, "offset");
    /* glUniform1i(f->font_texture_uniform, 0); */
    /* glUniform1i(f->font_vertex_uniform, 1); */

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
            glDrawArrays(GL_POINTS, 0, glyph_count);
            glyph_count = 0;
        }

        glyphs[glyph_count++] = (struct gl_glyph){x, y, color, text[i], font_size, 0.0, 0.0, 0.5};
        x += font->horizontal_advances[text[i]] * font_size;
    }

    if (flush && glyph_count) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(struct gl_glyph), glyphs);
        glDrawArrays(GL_POINTS, 0, glyph_count);
        glyph_count = 0;
    }

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

int max(int a, int b) { return a > b ? a : b; }

static inline void set_region(struct frame *f, int x, int y, int w, int h) {
    /* glScissor wants the botton-left corner of the area, the origin being in
     the bottom-left corner of the frame. */

    glScissor(max(0, x) * f->scale, max(0, f->height - y - h) * f->scale,
              max(0, w) * f->scale, max(0, h) * f->scale);
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
    glClear(GL_COLOR_BUFFER_BIT);

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
    glBindVertexArray(vao);

    glActiveTexture(GL_TEXTURE0);
    /* glBindTexture(GL_TEXTURE_2D, font->texture); */
    glBindTexture(GL_TEXTURE_2D, g_msdf_atlas_texture);

    glActiveTexture(GL_TEXTURE1);
    /* glBindTexture(GL_TEXTURE_BUFFER, font->vertex_texture); */
    glBindTexture(GL_TEXTURE_BUFFER, g_msdf_index_texture);

    glBindBuffer(GL_ARRAY_BUFFER, w->text_area_glyphs);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, x));

    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE,
                           sizeof(struct gl_glyph),
                           (void *)offsetof(struct gl_glyph, color));

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_INT,
                           sizeof(struct gl_glyph),
                           (void *)offsetof(struct gl_glyph, key));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                          sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, size));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, offset));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT,GL_FALSE,
                           sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, skew));

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT,GL_FALSE,
                           sizeof(struct gl_glyph),
                          (void *)offsetof(struct gl_glyph, strength));

    glUseProgram(w->frame->text_shader);
    glUniformMatrix4fv(w->frame->projection_uniform, 1, GL_FALSE, (GLfloat *) w->projection);
    /* glUniformMatrix4fv(w->frame->font_projection_uniform, 1, GL_FALSE, (GLfloat *) font->texture_projection); */
    glUniformMatrix4fv(w->frame->font_projection_uniform, 1, GL_FALSE, (GLfloat *) g_msdf_projection);
    glUniform1f(w->frame->font_padding_uniform, 2.0);
    glUniform2f(w->frame->offset_uniform, w->position[1] + w->linum_width, w->position[0]);
    glUniform1i(w->frame->font_texture_uniform, 0);
    glUniform1i(w->frame->font_vertex_uniform, 1);

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
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Overlays */
    glUseProgram(w->frame->overlay_shader);
    glUniformMatrix4fv(w->frame->overlay_projection_uniform, 1, GL_FALSE, (GLfloat *) w->projection);
    glUniform2f(w->frame->overlay_offset_uniform, w->position[1] + w->linum_width, w->position[0]);

    set_region(w->frame, w->x + w->linum_width, w->y,
               w->width - w->linum_width, w->height - modeline_h);

    GLuint vbo;
    GLuint _vao;
    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 12, 0);
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, 12, (void *)8);

    /* glLineWidth(2.0 * w->frame->scale); */
    glLineWidth(active_font->msdf_font->underline_thickness * font_size * 2.0);
    float underline_offset = active_font->msdf_font->underline_y * font_size;
    /* float underline_offset = 0.0; */
    struct gl_overlay_vertex overlays[] = {
        {13 * col_width, 8 * line_h - underline_offset, 0xc23127ff},
        {26 * col_width, 8 * line_h - underline_offset, 0xc23127ff},
        /* {3 * col_width, 30 * active_font->vertical_advance, 0xc23127ff}, */
        /* {15 * col_width, 30 * active_font->vertical_advance, 0xc23127ff}, */
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(overlays), (GLfloat *)overlays, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, 2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    /* glBindTexture(GL_TEXTURE_2D, active_font->msdf_atlas_texture); */
    /* glBindTexture(GL_TEXTURE_2D, g_msdf_atlas_texture); */
    /* glBindTexture(GL_TEXTURE_2D, active_font->texture); */


    glUseProgram(g_debug_shader);

    GLuint _vbo;
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    GLfloat rect[6][4] = {
        /* {0, 0, -2, -2}, */
        /* {0, 1, -2, 2}, */
        /* {1, 0, 2, 0}, */

        /* {0, 1, -2, 2}, */
        /* {1, 0, 2, -2}, */
        /* {1, 1, 2, 2} */
        {0, 0, 0, 1},
        {0, 1, 0, 0},
        {1, 0, 1, 1},

        {0, 1, 0, 0},
        {1, 0, 1, 1},
        {1, 1, 1, 0}
    };

    glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(GLfloat), (GLfloat *)rect, GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    /* GLfloat rect2[6][4] = { */
    /*     /\* {0, 0, -2, -2}, *\/ */
    /*     /\* {0, 1, -2, 2}, *\/ */
    /*     /\* {1, 0, 2, 0}, *\/ */

    /*     /\* {0, 1, -2, 2}, *\/ */
    /*     /\* {1, 0, 2, -2}, *\/ */
    /*     /\* {1, 1, 2, 2} *\/ */
    /*     {0, -1, 0, 1}, */
    /*     {0, 0, 0, 0}, */
    /*     {1, -1, 1, 1}, */

    /*     {0, 0, 0, 0}, */
    /*     {1, -1, 1, 1}, */
    /*     {1, 0, 1, 0} */
    /* }; */
    /* glBindTexture(GL_TEXTURE_2D, g_msdf_atlas_texture); */

    /* glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(GLfloat), (GLfloat *)rect2, GL_DYNAMIC_DRAW); */

    /* glDrawArrays(GL_TRIANGLES, 0, 6); */
}

void frame_render(struct frame *f) {

    eglMakeCurrent(g_gl_display, f->gl_surface, f->gl_surface, f->gl_ctx);
    eglSwapInterval(g_gl_display, 0);

    glViewport(0, 0, f->width * f->scale, f->height * f->scale);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    set_region(f, 0, f->height - f->minibuffer_height, f->width, f->minibuffer_height);
    vec3 _color;
    parse_color("0c1014", _color);
    glClearColor(_color[0], _color[1], _color[2], 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    FOR_EACH_WINDOW (f, w) {
        window_render(w);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(g_gl_display, f->gl_surface);
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
