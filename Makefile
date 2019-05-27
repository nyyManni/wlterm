



default:
	gcc -g -Isrc -I/usr/include/freetype2 \
         -lfreetype -lwayland-client -lrt -lxkbcommon -lwayland-egl -lGL -lEGL -lm -lmsdf \
         src/wlterm.c src/xdg-shell-protocol.c src/egl_util.c src/egl_window.c

src/libmsdfgl.so: src/msdf_gl.o
	gcc -shared $^ -o $@

src/msdf_gl.o: src/msdf_gl.c src/_msdf_kernels.h
	gcc -Isrc -I/usr/include/freetype2 -O3 -Wall -fPIC -DPIC -c $< -o $@ -Isrc

# Generate a C-header containing char arrays of the shader files.
src/_msdf_kernels.h: src/msdf_vertex.glsl src/msdf_fragment.glsl
	echo '#ifndef _MSDF_KERNELS_H\n#define _MSDF_KERNELS_H' >$@
	for f in $^; do fname=$${f##*/}; sed -e '1 i\\nconst char * _'$${fname%%.glsl}' =' -e 's/^\(.*\)$$/"\1"/' -e 's/\\/\\\\/' -e '$$ a;' $$f >>$@; done
	echo '#endif /* _MSDF_KERNELS_H */' >>$@
