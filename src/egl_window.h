#ifndef EGL_WINDOW_H
#define EGL_WINDOW_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdbool.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>

#include <cglm/mat4.h>

#define MAX_FRAMES 64
#define SCALE 2
#define MAX_GLYPHS_PER_DRAW 4096

struct window;

struct frame {

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

    GLuint shader_program;
    GLuint text_shader;
    GLuint bg_shader;

    
    struct window *root_window;
    


    GLuint projection_uniform;
    GLuint font_projection_uniform;
    GLuint font_texture_uniform;
    GLuint font_vertex_uniform;
    GLuint font_scale_uniform;

    GLuint bg_projection_uniform;
    GLuint bg_accent_color_uniform;
    GLuint offset_uniform;
    
    bool has_minibuffer_p;
    int minibuffer_height;

};

struct window {
    struct frame *frame;
    
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
    int32_t __position_pending[2];
    double inertia[2]; /* Pixels per second */
    uint32_t axis_time[2];
    double velocity[2];
    
    
    char **contents;
    uint32_t nlines;
};

struct glyph {
    FT_ULong code;
    GLuint texture;

    GLfloat offset_x;
    GLfloat offset_y;
    GLfloat width;
    GLfloat height;

    int advance;
    int bearing_x;
    int bearing_y;
};

struct font {
    GLuint texture;
    GLuint vertex_texture;
    GLuint vertex_buffer;
    mat4 texture_projection;
    
    int texture_size;
    
    int vertical_advance;
    
    int horizontal_advances[254];
};


struct gl_glyph {GLfloat x; GLfloat y; GLuint c; GLint k;};

void init_egl();
void kill_egl();
struct font *load_font(const char *, int);
struct frame *frame_create();
void frame_close(struct frame *);
void frame_resize(struct frame *, int, int);
struct window *window_create(struct frame *);
void frame_render(struct frame *);

#endif /* EGL_WINDOW_H */
