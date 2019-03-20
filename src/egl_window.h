#include <stdbool.h>


#include <GLES2/gl2.h>
#include <EGL/egl.h>

#define MAX_WINDOWS 64
#define SCALE 2

struct window {

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    void *shm_data;

    bool open;
    int width;
    int height;

    /* bool resized; */
    bool configured;
    struct wl_egl_window *gl_window;

    /* OpenGL */
    EGLContext gl_ctx;
    EGLConfig gl_conf;
    EGLDisplay gl_display;
    EGLSurface gl_surface;

    GLuint shader_program;

    /* Window contents. */
    uint32_t rotation_offset;
    GLuint rotation_uniform;
    GLuint pos;
    GLuint col;
};


struct window *window_create();
void window_close(struct window *);
void window_render(struct window *);
