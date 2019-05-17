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

#include <GLES3/gl32.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h"
#include "egl_window.h"
#include "egl_util.h"

struct wl_display *g_display;
struct wl_compositor *g_compositor;
struct wl_seat *g_seat;
struct xkb_keymap *g_xkb_keymap;
struct xkb_state *g_xkb_state;
struct wl_keyboard *g_kbd;
struct wl_pointer *g_pointer;
struct xdg_wm_base *g_xdg_wm_base;
struct wl_shm *g_shm;

/* extern struct frame *selected_frame; */
extern struct frame *frames[];
extern int open_frames;


static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx,
                                 wl_fixed_t sy) {
}


static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface) {}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy) {

}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time, uint32_t button,
                                  uint32_t state) {
    struct window *w = selected_frame->root_window;

    for (uint32_t axis = 0; axis < 2;++axis)
        w->_scrolling_freely[axis] = false;
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value) {
    struct window *w = selected_frame->root_window;
    for (uint32_t axis = 0; axis < 2;++axis)
        w->_scrolling_freely[axis] = false;
    w->_kinetic_scroll[axis] = 0.0;
    for (int i = 1; i < SCROLL_WINDOW_SIZE; ++i) {
        w->_scroll_time_buffer[axis][i - 1] = w->_scroll_time_buffer[axis][i];
        w->_scroll_position_buffer[axis][i - 1] = w->_scroll_position_buffer[axis][i];
    }
    w->position[axis] += value / 250.0;
    if (w->position[axis] > 0) w->position[axis] = 0.0;
    w->_scroll_time_buffer[axis][SCROLL_WINDOW_SIZE - 1] = time;
    w->_scroll_position_buffer[axis][SCROLL_WINDOW_SIZE - 1] = w->position[axis];
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {
    struct window *w = selected_frame->root_window;

    wl_surface_commit(selected_frame->surface);
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
                                       uint32_t axis_source) {
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                     uint32_t time, uint32_t axis) {
    struct window *w = selected_frame->root_window;

    size_t window_len = SCROLL_WINDOW_SIZE;
    double *s_window = w->_scroll_position_buffer[axis];
    while (!(*s_window == *s_window)) {
        if (!--window_len) return;
        ++s_window;
    };
    uint32_t *t_window = w->_scroll_time_buffer[axis];
    t_window += (SCROLL_WINDOW_SIZE - window_len);

    uint32_t dt_window[SCROLL_WINDOW_SIZE];
    double ds_window[SCROLL_WINDOW_SIZE];
    double v_window[SCROLL_WINDOW_SIZE];

    for (size_t i = 0; i < window_len - 1; ++i) {
        dt_window[i] = t_window[i + 1] - t_window[i];
        ds_window[i] = s_window[i + 1] - s_window[i];
        v_window[i] = ds_window[i] / (double)dt_window[i];
    }

    size_t max_index = 0;
    double max_v = v_window[0];

    for (size_t i = 0; i < window_len - 1; ++i) {
        if (fabs(v_window[i]) > fabs(max_v)) {
            max_index = i;
            max_v = v_window[i];
        }
    }
    if (fabs(v_window[window_len - 2]) > 0.1) {
        w->_kinetic_scroll[axis] = max_v;
        w->_kinetic_scroll_t0[axis] = time;

        uint32_t delta_t = time - t_window[max_index];


        /* Return from suspend might do strange things to delta_t */
        if (delta_t < 10000) {
            w->position[axis] = s_window[max_index] + delta_t * max_v;
        }
        w->_scrolling_freely[axis] = true;
    }

    /* Clear window buffers */
    for (int i = 0; i < SCROLL_WINDOW_SIZE; ++i) {
        w->_scroll_time_buffer[axis][i] = 0;
        w->_scroll_position_buffer[axis][i] = NAN;
    }
}
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                         uint32_t axis, int32_t discrete) {
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

    selected_frame = wl_surface_get_user_data(surface);
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
        frame_close(selected_frame);
    } else if (sym == XKB_KEY_n) {
        frame_create();
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
    struct frame *w = data;
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


static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version) {

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

    struct frame *f = frame_create();

    if (argc > 1) {
        f->root_window->contents = read_buffer_contents(argv[1], &f->root_window->nlines);
    }

    while (wl_display_dispatch(g_display) != -1 && open_frames) {}
    kill_egl();

    wl_registry_destroy(registry);
    wl_display_disconnect(g_display);

    return 0;
}
