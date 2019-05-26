#ifndef EGL_WINDOW_H
#define EGL_WINDOW_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdbool.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <msdf.h>

#include <cglm/mat4.h>

#define MAX_FRAMES 64
#define SCALE 2
#define MAX_GLYPHS_PER_DRAW 8192
#define SCROLL_WINDOW_SIZE 5

struct window;
struct frame;

struct display_info {

    struct wl_dsplay *display;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;
    EGLDisplay gl_display;
    EGLConfig gl_conf;
    EGLContext root_context;

    struct frame *root_frame;
};



extern struct frame *selected_frame;

struct frame {

    struct frame *next;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    void *shm_data;

    bool open;
    int width;
    int height;

    double scale;

    /* OpenGL */
    struct wl_egl_window *gl_window;

    EGLContext gl_ctx;
    EGLConfig gl_conf;
    EGLDisplay gl_display;
    EGLSurface gl_surface;

    mat4 projection;

    GLuint text_shader;
    GLuint bg_shader;
    GLuint overlay_shader;

    struct window *root_window;

    GLuint projection_uniform;
    GLuint font_projection_uniform;
    GLuint font_texture_uniform;
    GLuint font_vertex_uniform;
    GLuint font_padding_uniform;
    
    GLuint msdf_projection_uniform;
    GLuint msdf_vertex_uniform;

    GLuint overlay_projection_uniform;
    GLuint overlay_offset_uniform;

    GLuint bg_projection_uniform;
    GLuint bg_accent_color_uniform;
    GLuint offset_uniform;

    bool has_minibuffer_p;
    int minibuffer_height;


};

struct window {
    struct frame *frame;
    struct window *next;

    /* Window position in frame's coordinates */
    int x;
    int y;

    int width;
    int height;

    int linum_width;

    /* Window contents. */
    GLuint linum_glyphs;
    GLuint left_fringe_glyphs;
    GLuint right_fringe_glyphs;
    GLuint left_margin_glyphs;
    GLuint right_margin_glyphs;
    GLuint text_area_glyphs;
    GLuint modeline_glyphs;

    GLuint rect_vbo;

    mat4 projection;

    /* Scrolling stuff */
    double position[2];
    bool _scrolling_freely[2];

    uint32_t _scroll_time_buffer[2][SCROLL_WINDOW_SIZE];
    double _scroll_position_buffer[2][SCROLL_WINDOW_SIZE];

    double _kinetic_scroll[2];
    uint32_t _kinetic_scroll_t0[2];

    char **contents;
    uint32_t nlines;
};

struct font {
    /* GLuint texture; */
    /* GLuint vertex_texture; */
    /* GLuint vertex_buffer; */

    GLuint msdf_glyph_uniform;
    GLuint msdf_glyph_texture;
    GLuint msdf_atlas_texture;
    GLuint msdf_framebuffer;

    /* mat4 texture_projection; */

    int texture_size;

    float vertical_advance;

    float horizontal_advances[254];
    
    msdf_font_handle msdf_font;

};

#define FOR_EACH_WINDOW(frame, w) \
    for (struct window *w = frame->root_window; w; w = w->next)


struct gl_glyph {
    GLfloat x;
    GLfloat y; 
    GLuint color;
    GLint key;
    GLfloat size;
    GLfloat offset;
    GLfloat skew;
    GLfloat strength;
};
struct gl_overlay_vertex {GLfloat x; GLfloat y; GLuint c;};
struct gl_glyph_atlas_item {
    GLfloat offset_x;
    GLfloat offset_y;
    GLfloat size_x;
    GLfloat size_y;
    GLfloat bearing_x;
    GLfloat bearing_y;
    GLfloat glyph_width;
    GLfloat glyph_height;
};

void init_egl();
void kill_egl();
struct font *load_font(const char *, int);
struct frame *frame_create();
void frame_close(struct frame *);
void frame_resize(struct frame *, int, int);
struct window *window_create(struct frame *);
void frame_render(struct frame *);

#endif /* EGL_WINDOW_H */
