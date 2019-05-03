
#ifndef MSDF_UTIL_H
#define MSDF_UTIL_H

#include <stdlib.h>
#include <msdf.h>


#include <GL/gl.h>

enum color {
    BLACK = 0,
    RED = 1,
    GREEN = 2,
    BLUE = 4,

    YELLOW = RED | GREEN,
    MAGENTA = BLUE | RED,
    CYAN = BLUE | GREEN,
    WHITE = RED | GREEN | BLUE
};

typedef struct segment {
    GLuint color;
    GLint npoints;
    GLfloat points[][2];
} segment;

typedef struct contour {
    GLint winding;
    GLint nsegments;
    struct segment segments[];
} contour;

typedef struct glyph {
    GLint  ncontours;
    struct contour contours[];
} glyph;


static inline size_t glyph_serialized_size(unsigned int code) {
    return 0;
}

static inline void serialize_glyph(msdf_font_handle font, unsigned int code, void *buf) {
    
}

#endif
