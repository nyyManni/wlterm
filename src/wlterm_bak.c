#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>

/* #include "xdg-shell-client-protocol.h" */
#include "xdg-shell-unstable-v6-client-protocol.h"
#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds =
        te.tv_sec * 1000LL + te.tv_usec / 1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}
/* void print_current_time_with_ms (void) */
/* { */
/*     long            ms; // Milliseconds */
/*     time_t          s;  // Seconds */
/*     struct timespec spec; */

/*     clock_gettime(CLOCK_REALTIME, &spec); */

/*     s  = spec.tv_sec; */
/*     ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds */
/*     if (ms > 999) { */
/*         s++; */
/*         ms = 0; */
/*     } */

/*     printf("Current time: %"PRIdMAX".%03ld seconds since the Epoch\n", */
/*            (intmax_t)s, ms); */
/* } */
struct wl_display *display;
struct wl_seat *seat;
struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_keyboard *kbd;
struct xkb_keymap *xkb_keymap;
struct xkb_context *xkb_context;
struct xkb_state *xkb_state;
struct zxdg_surface_v6 *xdg_surface;
/* struct xdg_wm_base *xdg_wm_base; */
struct zxdg_toplevel_v6 *xdg_toplevel;
struct zxdg_shell_v6 *xdg_shell;

cairo_t *cairo;
struct wl_buffer *buffer;
struct wl_surface *surface;
void *shm_data;

int window_width = 200;
int window_height = 200;
bool resized = true;
bool running;
long long begin;
void cairo_set_source_u32(cairo_t *cairo, uint32_t color) {
    cairo_set_source_rgba(
        cairo, (color >> (3 * 8) & 0xFF) / 255.0, (color >> (2 * 8) & 0xFF) / 255.0,
        (color >> (1 * 8) & 0xFF) / 255.0, (color >> (0 * 8) & 0xFF) / 255.0);
}

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int anonymous_shm_open(void) {
    char name[] = "/dmenu-XXXXXX";
    int retries = 100;

    do {
        randname(name + strlen(name) - 6);

        --retries;
        // shm_open guarantees that O_CLOEXEC is set
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

int create_shm_file(off_t size) {
    int fd = anonymous_shm_open();
    if (fd < 0) {
        return fd;
    }

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
                            int32_t fd, uint32_t size) {

    fprintf(stderr, "handling keymap\n");
    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        exit(1);
    }
    char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close(fd);
        exit(1);
    }
    xkb_keymap =
        xkb_keymap_new_from_string(xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_shm, size);
    close(fd);

    xkb_state = xkb_state_new(xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
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
    /* struct dmenu_panel *panel = data; */

    enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, key + 8);
    /* if (panel->on_keyevent) */
    /* 	panel->on_keyevent(panel, key_state, sym, panel->keyboard.control, */
    /* 					   panel->keyboard.shift); */
    fprintf(stderr, "handling keyinput\n");
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
    struct dmenu_panel *panel = data;
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        kbd = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(kbd, &keyboard_listener, panel);
    }
}
static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
    // Who cares
}

const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void handle_toplevel_configure(void *data, struct zxdg_toplevel_v6 *toplevel,
                                      int32_t width, int32_t height,
                                      struct wl_array *states) {
    resized = (width != window_width || height != window_height);

    /* if (width != 0 && height) { */
    /*     window_width = width; */
    /*     window_height = height; */
    /* } */
    fprintf(stderr, "width: %i, height: %i\n", window_width, window_height);
}

static void handle_toplevel_close(void *data, struct zxdg_toplevel_v6 *xdg_toplevel) {
    running = 0;
}

static const struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};

/* static void handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) { */
/*     xdg_wm_base_pong(xdg_wm_base, serial); */
/* } */

/* static const struct xdg_wm_base_listener xdg_base_listener = {.ping = handle_ping}; */

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {}

static const struct wl_buffer_listener buffer_listener = {.release = buffer_release};

static void handle_xdg_buffer_configure(void *data, struct zxdg_surface_v6 *xdg_surface,
                                        uint32_t serial) {
    fprintf(stderr, "configuring xdg surface\n");
    if (resized) {
        /* resize_surface(); */
        /* wl_surface_commit(surface); */
    }

    zxdg_surface_v6_ack_configure(xdg_surface, serial);
}

static struct zxdg_surface_v6_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};

static void create_xdg_surface()
{


    fprintf(stderr, "binding xdg surface\n");
	xdg_surface = zxdg_shell_v6_get_xdg_surface(xdg_shell, surface);
	zxdg_surface_v6_add_listener(xdg_surface, &xdg_surface_listener, NULL);

	xdg_toplevel = zxdg_surface_v6_get_toplevel(xdg_surface);
	zxdg_toplevel_v6_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	zxdg_toplevel_v6_set_title(xdg_toplevel, "simple-egl");
    wl_display_roundtrip(display);

	/* window->wait_for_configure = true; */
	wl_surface_commit(surface);
}

static void create_surface() {
    surface = wl_compositor_create_surface(compositor);
    create_xdg_surface();

    fprintf(stderr, "resizing surface\n");
    int32_t width = window_width;
    int32_t height = window_height;
    int stride = width * 4;
    int size = stride * height;

    int fd = create_shm_file(size);
    /* if (fd < 0) { */
    /*     fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size); */
    /*     return 1; */
    /* } */
    shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    /* if (shm_data == MAP_FAILED) { */
    /*     fprintf(stderr, "mmap failed: %m\n"); */
    /*     close(fd); */
    /*     return 1; */
    /* } */
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    /* cairo_surface_t *s = cairo_image_surface_create_for_data( */
    /*     shm_data, CAIRO_FORMAT_ARGB32, width, height, width * 4); */

    /* cairo = cairo_create(s); */

    /* fprintf(stderr, "clearing rectanglge: %ix%i\n", width, height); */
    /* cairo_set_source_u32(cairo, 0xffffffff); */
    /* cairo_rectangle(cairo, 0, 0, width, height); */
    /* cairo_fill(cairo); */

    wl_display_roundtrip(display);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    
    fprintf(stderr, "resized surface\n");
}


static void
xdg_shell_ping(void *data, struct zxdg_shell_v6 *shell, uint32_t serial)
{
	zxdg_shell_v6_pong(shell, serial);
}

static const struct zxdg_shell_v6_listener xdg_shell_listener = {
	xdg_shell_ping,
};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version) {

    fprintf(stderr, "Event %s\n", interface);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        /* seat = wl_registry_bind(registry, name, &wl_seat_interface, 1); */
        /* wl_seat_add_listener(seat, &seat_listener, NULL); */

    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);

    /* } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) { */
    /*     xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version); */

    } else if (strcmp(interface, "zxdg_shell_v6") == 0) {
        fprintf(stderr, "got shell\n");
        xdg_shell = wl_registry_bind(registry, name, &zxdg_shell_v6_interface, 1);
        zxdg_shell_v6_add_listener(xdg_shell, &xdg_shell_listener, NULL);

        /* xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface,
         * version); */

        /* } else if (strcmp(interface, xdg_toplevel_interface.name) == 0) { */
        /* 	/\* xdg_wm_base = wl_registry_bind(registry, name,
         * &xdg_wm_base_interface, 1); *\/ */
        /* 	xdg_toplevel = wl_registry_bind(registry, name, &xdg_toplevel_interface,
         * 1); */
        /* 	/\* xdg_wm_base_add_listener(xdg_wm_base, &xdg_base_listener, NULL); *\/
         */
        /* 	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL); */

        /* 	/\* xdg_surface_add_listener(xdg_surface, &, void *data) *\/ */
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

#define TOCK fprintf(stderr, "%d: %f\n", __LINE__, (double)(current_timestamp() - begin) / 1000);
int main(int argc, char *argv[]) {
    
    display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    

    wl_display_roundtrip(display);
    
    /* wl_display_roundtrip(display); */

    

    /* xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface); */
    
    /* xdg_toplevel = xdg_surface_get_toplevel(xdg_surface); */
    
    /* xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL); */
    

    /* xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL); */
    

    /* xdg_toplevel_set_title(xdg_toplevel, "lol"); */
    

    /* wl_display_roundtrip(display); */
    /* wl_surface_commit(surface); */
    
    /* wl_display_roundtrip(display); */
    

    /* here, do your time-consuming job */
    create_surface();
    wl_display_roundtrip(display);

    wl_surface_commit(surface);
    

    /* wl_display_roundtrip(display); */

    running = true;
    while (wl_display_dispatch(display) != -1 && running) {
        clock_t c2 = clock();
        
        fprintf(stderr, "cycle\n");
        
    }

    wl_display_disconnect(display);

    return 0;
}
