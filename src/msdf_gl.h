#ifndef MSDF_GL_H
#define MSDF_GL_H

/**
 * OpenGL implementation for multi-channel signed distance field generator
 * ---------------------------------------------------------------
 *
 * MSDF-GL             Henrik Nyman,    (c) 2019 -
 * Original msdfgen by Viktor Chlumsky, (c) 2014 - 2019
 *
 * The technique used to generate multi-channel distance fields in this code has
 * been developed by Viktor Chlumsky in 2014 for his master's thesis, "Shape
 * Decomposition for Multi-Channel Distance Fields". It provides improved
 * quality of sharp corners in glyphs and other 2D shapes in comparison to
 * monochrome distance fields. To reconstruct an image of the shape, apply the
 * median of three operation on the triplet of sampled distance field values.
 */

#include <ft2build.h>
#include FT_FREETYPE_H

#include <GL/gl.h>

#include <msdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _msdf_gl_context *msdf_gl_context_t;

typedef struct _map_elem {
    int key;
    int index;
    double horizontal_advance;
} map_elem_t;

typedef struct _msdfgl_elem_list {
    struct _msdfgl_elem_list *next;
    map_elem_t data[];
} msdfgl_elem_list_t;

typedef struct _msdfgl_map {
    void *root;
    size_t chunk_size;

    size_t i;
    msdfgl_elem_list_t *cur_list;
    msdfgl_elem_list_t *elems;
} msdfgl_map_t;

/**
 * Compile shaders and configure uniforms.
 *
 * Returns a new MSDF GL context, or NULL if creating the context failed.
 */
msdf_gl_context_t msdf_gl_create_context();

/**
 * Release resources allocated by `msdf_gl_crate_context`.
 */
void msdf_gl_destroy_context(msdf_gl_context_t ctx);

struct _msdf_gl_font {
    char *font_name;

    double scale;
    double range;
    int texture_width;

    double vertical_advance;
    float horizontal_advances[256];
    
    msdfgl_map_t character_index;

    GLfloat atlas_projection[4][4];

    /**
     * 2D RGBA atlas texture containing all MSDF-glyph bitmaps.
     */
    GLuint atlas_texture;
    GLuint _atlas_framebuffer;

    /**
     * 1D buffer containing glyph position information per character in the
     * atlas texture.
     */
    GLuint index_texture;
    GLuint _index_buffer;
    
    /**
     * Amount of glyphs currently rendered on the textures.
     */
    size_t _nglyphs;
    
    /**
     * The current size of the buffer index texture.
     */
    size_t _nallocated;
    
    /**
     * The amount of allocated texture height.
     */
    int _texture_height;

    /**
     * MSDF_GL context handle.
     */
    msdf_gl_context_t context;

    /**
     * FreeType Face handle.
     */
    FT_Face face;

    /**
     * The location in the atlas where the next bitmap would be rendered.
     */
    size_t _offset_y;
    size_t _y_increment;
    size_t _offset_x;

    /**
     * Texture buffer objects for serialized FreeType data input.
     */
    GLuint _meta_input_buffer;
    GLuint _point_input_buffer;
    GLuint _meta_input_texture;
    GLuint _point_input_texture;

    msdf_font_handle _msdf_font;
};
typedef struct _msdf_gl_font *msdf_gl_font_t;

typedef struct _msdf_gl_glyph {
    GLfloat x;
    GLfloat y;
    GLuint color;
    GLint key;
    GLfloat size;
    GLfloat offset;
    GLfloat skew;
    GLfloat strength;
} msdf_gl_glyph_t;

/**
 * Load font from a font file and generate textures and buffers for it.
 */
msdf_gl_font_t msdf_gl_load_font(msdf_gl_context_t ctx, const char *font_name,
                                 double range, double scale, int texture_size);

/**
 * Release resources allocated by `msdf_gl_load_font`.
 */
void msdf_gl_destroy_font(msdf_gl_font_t font);

/**
 * Render a single glyph onto the MSFD atlas. Intented use case is to generate
 * the bitmaps on-demand as the characters are appearing.
 */
int msdf_gl_generate_glyph(msdf_gl_font_t font, int32_t char_code);

/**
 * Render a range of glyphs onto the MSFD atlas. The range is inclusive. Intended
 * use case is to initialize the atlas in the beginning with e.g. ASCII characters.
 */
int msdf_gl_generate_glyphs(msdf_gl_font_t font, int32_t start, int32_t end);

/**
 * Render arbitrary character codes in bulk.
 */
int msdf_gl_generate_glyph_list(msdf_gl_font_t font, int32_t *list, size_t n);

/**
 * Shortcuts for common generators.
 */
#define msdf_gl_generate_ascii(font) msdf_gl_generate_glyphs(font, 0, 128)
#define msdf_gl_generate_ascii_ext(font) msdf_gl_generate_glyphs(font, 0, 256)

/**
 * Render a list of glyphs.
 */
void msdf_gl_render(msdf_gl_font_t font, msdf_gl_glyph_t *glyphs, int n,
                    GLfloat *projection);

#ifdef __cplusplus
}
#endif

#endif /* MSDF_GL_H */
