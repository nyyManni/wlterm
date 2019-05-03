default:
	gcc -Isrc -I/usr/include/freetype2 \
        -lfreetype -lwayland-client -lrt -lxkbcommon -lwayland-egl -lGL -lEGL -lm -lmsdf \
        src/wlterm.c src/xdg-shell-protocol.c src/egl_util.c src/egl_window.c
