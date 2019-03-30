#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

#include <sys/time.h>

#include <GLES2/gl2.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h"
#include "egl_window.h"

struct wl_display *g_display;
struct wl_compositor *g_compositor;
struct wl_seat *g_seat;
struct xkb_keymap *g_xkb_keymap;
struct xkb_state *g_xkb_state;
struct wl_keyboard *g_kbd;
struct wl_pointer *g_pointer;
struct xdg_wm_base *g_xdg_wm_base;
struct wl_shm *g_shm;

extern struct window *active_window;
extern struct window *windows[];
extern int open_windows;


static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx,
                                 wl_fixed_t sy) {
    /* struct display *display = data; */
    /* struct wl_buffer *buffer; */
    /* struct wl_cursor *cursor = display->default_cursor; */
    /* struct wl_cursor_image *image; */

    /* if (display->window->fullscreen) */
    /*      wl_pointer_set_cursor(pointer, serial, NULL, 0, 0); */
    /* else if (cursor) { */
    /*      image = display->default_cursor->images[0]; */
    /*      buffer = wl_cursor_image_get_buffer(image); */
    /*      if (!buffer) */
    /*          return; */
    /*      wl_pointer_set_cursor(pointer, serial, */
    /*                    display->cursor_surface, */
    /*                    image->hotspot_x, */
    /*                    image->hotspot_y); */
    /*      wl_surface_attach(display->cursor_surface, buffer, 0, 0); */
    /*      wl_surface_damage(display->cursor_surface, 0, 0, */
    /*                image->width, image->height); */
    /*      wl_surface_commit(display->cursor_surface); */
    /* } */
}


static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface) {}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy) {

    /* fprintf(stderr, "axis motion\n"); */
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time, uint32_t button,
                                  uint32_t state) {
    /* struct display *display = data; */

    /* active_window->inertia[0] = active_window->inertia[1] = 0.0; */
    /* if (!display->window->xdg_toplevel) */
    /*     return; */

    /* if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) */
    /*     zxdg_toplevel_v6_move(display->window->xdg_toplevel, display->seat, serial); */
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value) {

    uint32_t other_axis = !axis;

    /* static uint32_t a = 999; */
    /* if (a == axis) { */
    /*     scroll[0] = 0; */
    /*     scroll[1] = 0; */
    /* } */

    active_window->__position_pending[axis] = value;
    /* fprintf(stderr, "scroll time: %d\n", time); */

    active_window->velocity[axis] = (double)active_window->__position_pending[axis] /
                                    (time - active_window->axis_time[axis]);
    active_window->axis_time[axis] = time;
    /* fprintf(stderr, "scroll scroll scroll\n"); */
    /* fprintf(stderr, "x: %f, y: %f\n", active_window->position[0], active_window->position[1]); */

    /* Trigger a throttled redraw */
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {

    if (!active_window->__position_pending[0] && !active_window->__position_pending[1])
        return;

    for (uint32_t axis = 0; axis < 2; ++axis) {

        /* active_window->axis_time[axis] = 0; */
        /* if (active_window->__position_pending[axis]) { */
        /* } else { */
        /*     active_window->velocity[axis] = 0.0; */
        /* } */

        active_window->position[axis] += active_window->__position_pending[axis] / 150.0;

        /* while (active_window->position[axis] < 0) */
        /*     active_window->position[axis] += 800; */
        /* while (active_window->position[axis] > 800) */
        /*     active_window->position[axis] -= 800; */

        /* a = axis; */
        /* scroll[axis] = value; */

        active_window->__position_pending[axis] = 0;
        /* active_window->__position_pending[1] = 0; */
    }

    /* fprintf(stderr, "scroll frame\n"); */
    /* wl_surface_commit(active_window->surface); */
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
                                       uint32_t axis_source) {
    /* fprintf(stderr, "axis source: %d\n", axis_source); */
}
static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                     uint32_t time, uint32_t axis) {
    /* active_window->inertia = true; */
    active_window->inertia[axis] = 1.0;
    /* active_window->inertia[0] = active_window->inertia[0] = 0.0; */

    uint32_t tdiff = time - active_window->axis_time[axis];
    /* fprintf(stderr, "%d\n", active_window->axis_time[axis]); */
    /* fprintf(stderr, "scroll velocity: %f\n", active_window->velocity[axis]); */
    /* fprintf(stderr, "scroll velocity: %f\n", active_window->velocity[1]); */

    active_window->inertia[axis] = active_window->velocity[axis];
    /* active_window->inertia[1] = active_window->velocity[1]; */
    active_window->velocity[axis] = 0;
    /* active_window->velocity[1] = 0; */

    wl_surface_commit(active_window->surface);
    /* fprintf(stderr, "axis stop\n"); */
    /* active_window->axis_time[0] = active_window->axis_time[1] = 0; */
    if (time - active_window->axis_time[axis] > 30) {
        active_window->inertia[axis] = 0;
    }

    active_window->axis_time[axis] = 0;
}
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                         uint32_t axis, int32_t discrete) {
    /* fprintf(stderr, "axis discrete\n"); */
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void touch_handle_down(void *data, struct wl_touch *wl_touch, uint32_t serial,
                              uint32_t time, struct wl_surface *surface, int32_t id,
                              wl_fixed_t x_w, wl_fixed_t y_w) {
    /* fprintf(stderr, "touch down\n"); */
    /* struct display *d = (struct display *)data; */

    /* if (!d->shell) */
    /*     return; */

    /* zxdg_toplevel_v6_move(d->window->xdg_toplevel, d->seat, serial); */
}

static void touch_handle_up(void *data, struct wl_touch *wl_touch, uint32_t serial,
                            uint32_t time, int32_t id) {}

static void touch_handle_motion(void *data, struct wl_touch *wl_touch, uint32_t time,
                                int32_t id, wl_fixed_t x_w, wl_fixed_t y_w) {}

static void touch_handle_frame(void *data, struct wl_touch *wl_touch) {}

static void touch_handle_cancel(void *data, struct wl_touch *wl_touch) {}

static const struct wl_touch_listener touch_listener = {
    touch_handle_down,  touch_handle_up,     touch_handle_motion,
    touch_handle_frame, touch_handle_cancel,
};

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
                            int32_t fd, uint32_t size) {

    struct xkb_context *xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        exit(1);
    }
    char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close(fd);
        exit(1);
    }
    struct xkb_keymap *xkb_keymap =
        xkb_keymap_new_from_string(xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_shm, size);
    close(fd);

    g_xkb_state = xkb_state_new(xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {

    active_window = wl_surface_get_user_data(surface);
    // Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                           struct wl_surface *surface) {
    // Who cares
}
static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                                 int32_t rate, int32_t delay) {}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group) {}


static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t _key_state) {
    enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(g_xkb_state, key + 8);

    if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;
    if (sym == XKB_KEY_c) {
        window_close(active_window);
    } else if (sym == XKB_KEY_n) {
        window_create();
    }
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                                     enum wl_seat_capability caps) {
    struct window *w = data;
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        g_kbd = wl_seat_get_keyboard(g_seat);
        wl_keyboard_add_listener(g_kbd, &keyboard_listener, NULL);
    }
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        g_pointer = wl_seat_get_pointer(g_seat);
        wl_pointer_add_listener(g_pointer, &pointer_listener, NULL);
    }
}
static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
    // Who cares
}

const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};


static void handle_xdg_buffer_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    struct window *w = data;

    /* fprintf(stderr, "configured xdg surface\n"); */
    xdg_surface_ack_configure(w->xdg_surface, serial);
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version) {
    /* fprintf(stderr, "register: %s\n", interface); */

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        g_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        g_seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(g_seat, &seat_listener, NULL);

    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g_shm = wl_registry_bind(registry, name, &wl_shm_interface, version);

    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        g_xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};


int main(int argc, char *argv[]) {

    g_display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(g_display);
    init_egl();
    if (!load_font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15)) {
        wl_registry_destroy(registry);
        wl_display_disconnect(g_display);
        return 1;
    }
    
    struct window *w = window_create();

    while (wl_display_dispatch(g_display) != -1 && open_windows) {}
    kill_egl();

    wl_registry_destroy(registry);
    wl_display_disconnect(g_display);

    return 0;
}
