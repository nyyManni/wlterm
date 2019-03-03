default:
	# gcc -Isrc -I/usr/include/pango-2.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I/usr/include/cairo -lwayland-client -lrt -lcairo -lpango-1.0 -lglib-2.0 -lgobject-2.0 -lpangocairo-1.0 -lxkbcommon src/wlterm.c src/xdg-shell-unstable-v6-protocol.c
	gcc -Isrc -I/usr/include/pango-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I/usr/include/cairo -lwayland-client -lrt -lcairo -lpango-1.0 -lglib-2.0 -lgobject-2.0 -lpangocairo-1.0 -lxkbcommon src/wlterm.c src/xdg-shell-protocol.c
