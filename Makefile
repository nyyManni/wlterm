default:
	gcc -g -Isrc -I/usr/include/freetype2    -lwayland-client -lrt     -lxkbcommon -lwayland-egl -lGL -lEGL src/wlterm.c -lm -L/usr/local/lib  src/xdg-shell-protocol.c src/egl_util.c src/egl_window.c
