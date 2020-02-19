#include "wlterm.h"


int main(int argc, char *argv[]) {

    struct wlterm_application *app = wlterm_application_create();

    wlterm_frame_create(app);

    wlterm_application_run(app);  /* Runs until all frames closed */

    wlterm_application_destroy(app);
}
