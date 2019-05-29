
#ifdef __linux__
#define GL_GLEXT_PROTOTYPES
#else
/* Figure out something. */
#endif

#include "msdf_gl.h"

#include "_msdf_shaders.h"

typedef struct msdf_gl_index_entry {
    GLfloat offset_x;
    GLfloat offset_y;
    GLfloat size_x;
    GLfloat size_y;
    GLfloat bearing_x;
    GLfloat bearing_y;
    GLfloat glyph_width;
    GLfloat glyph_height;
} msdf_gl_index_entry;

GLfloat _MAT4_ZERO_INIT[4][4] = {{0.0f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 0.0f}};

static inline void _ortho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
                          GLfloat nearVal, GLfloat farVal, GLfloat dest[][4]) {
    GLfloat rl, tb, fn;

    memcpy(dest, _MAT4_ZERO_INIT, 16 * sizeof(GLfloat));

    rl = 1.0f / (right - left);
    tb = 1.0f / (top - bottom);
    fn = -1.0f / (farVal - nearVal);

    dest[0][0] = 2.0f * rl;
    dest[1][1] = 2.0f * tb;
    dest[2][2] = 2.0f * fn;
    dest[3][0] = -(right + left) * rl;
    dest[3][1] = -(top + bottom) * tb;
    dest[3][2] = (farVal + nearVal) * fn;
    dest[3][3] = 1.0f;
}


int compile_shader(const char *source, GLenum type, GLuint *shader) {
    *shader = glCreateShader(type);
    if (!*shader) {
        fprintf(stderr, "failed to create shader\n");
    }

    glShaderSource(*shader, 1, (const char *const *)&source, NULL);
    glCompileShader(*shader);

    GLint status;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &status);
    if (!status)
        return 0;

    return 1;
}

msdf_gl_context_t msdf_gl_create_context() {
    msdf_gl_context_t ctx = (msdf_gl_context_t)malloc(sizeof(struct _msdf_gl_context));

    if (!ctx)
        return NULL;

    GLuint vertex_shader, geometry_shader, fragment_shader;
    if (!compile_shader(_msdf_vertex, GL_VERTEX_SHADER, &vertex_shader))
        return NULL;
    if (!compile_shader(_msdf_fragment, GL_FRAGMENT_SHADER, &fragment_shader))
        return NULL;

    ctx->gen_shader = glCreateProgram();
    if (!(ctx->gen_shader = glCreateProgram()))
        return NULL;

    glAttachShader(ctx->gen_shader, vertex_shader);
    glAttachShader(ctx->gen_shader, fragment_shader);

    glLinkProgram(ctx->gen_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint status;
    glGetProgramiv(ctx->gen_shader, GL_LINK_STATUS, &status);
    if (!status)
        return NULL;

    ctx->_projection_uniform = glGetUniformLocation(ctx->gen_shader, "projection");
    ctx->_texture_offset_uniform = glGetUniformLocation(ctx->gen_shader, "offset");
    ctx->_translate_uniform = glGetUniformLocation(ctx->gen_shader, "translate");
    ctx->_scale_uniform = glGetUniformLocation(ctx->gen_shader, "scale");
    ctx->_range_uniform = glGetUniformLocation(ctx->gen_shader, "range");
    ctx->_glyph_height_uniform = glGetUniformLocation(ctx->gen_shader, "glyph_height");

    ctx->_meta_offset_uniform = glGetUniformLocation(ctx->gen_shader, "meta_offset");
    ctx->_point_offset_uniform = glGetUniformLocation(ctx->gen_shader, "point_offset");

    ctx->metadata_uniform = glGetUniformLocation(ctx->gen_shader, "metadata");
    ctx->point_data_uniform = glGetUniformLocation(ctx->gen_shader, "point_data");

    GLenum err = glGetError();
    if (err) {
        fprintf(stderr, "error: %x \n", err);
        glDeleteProgram(ctx->gen_shader);
        return NULL;
    }

    if (!(ctx->render_shader = glCreateProgram()))
        return NULL;
    glAttachShader(ctx->render_shader, vertex_shader);
    glAttachShader(ctx->render_shader, geometry_shader);
    glAttachShader(ctx->render_shader, fragment_shader);

    glLinkProgram(ctx->render_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(geometry_shader);
    glDeleteShader(fragment_shader);

    glGetProgramiv(ctx->render_shader, GL_LINK_STATUS, &status);

    if (!status) {
        glDeleteProgram(ctx->gen_shader);
        return NULL;
    }

    if (!compile_shader(_font_vertex, GL_VERTEX_SHADER, &vertex_shader))
        return NULL;
    if (!compile_shader(_font_geometry, GL_GEOMETRY_SHADER, &geometry_shader))
        return NULL;
    if (!compile_shader(_font_fragment, GL_FRAGMENT_SHADER, &fragment_shader))
        return NULL;

    ctx->window_projection_uniform = glGetUniformLocation(ctx->render_shader, "projection");
    ctx->_font_projection_uniform = glGetUniformLocation(ctx->render_shader, "font_projection");
    ctx->_font_vertex_uniform = glGetUniformLocation(ctx->render_shader, "font_vertices");
    ctx->_font_texture_uniform = glGetUniformLocation(ctx->render_shader, "font_texture");
    ctx->_padding_uniform = glGetUniformLocation(ctx->render_shader, "padding");
    ctx->_offset_uniform = glGetUniformLocation(ctx->render_shader, "offset");


    return ctx;
}

msdf_gl_font_t msdf_gl_load_font(msdf_gl_context_t ctx, const char *font_name,
                                 double range, double scale, size_t texture_size) {

    msdf_gl_font_t f = (msdf_gl_font_t)malloc(sizeof(struct _msdf_gl_font) * 2);

    f->_msdf_font = msdf_load_font(font_name);

    f->scale = scale;
    f->range = range;
    f->texture_size = texture_size;
    f->context = ctx;

    glGenBuffers(1, &f->_meta_input_buffer);
    glGenBuffers(1, &f->_point_input_buffer);
    glGenTextures(1, &f->_meta_input_texture);
    glGenTextures(1, &f->_point_input_texture);

    glGenBuffers(1, &f->_index_buffer);
    glGenTextures(1, &f->index_texture);

    glGenTextures(1, &f->atlas_texture);
    glGenFramebuffers(1, &f->_atlas_framebuffer);

    return f;
}

int msdf_gl_render_glyphs(msdf_gl_font_t font, int32_t start, int32_t end) {

    msdf_gl_context_t ctx = font->context;

    if (end - start <= 0)
        return -1;

    /* Calculate the amount of memory needed on the GPU.*/
    size_t *meta_sizes = (size_t *)malloc((end - start + 1) * sizeof(size_t));
    size_t *point_sizes = (size_t *)malloc((end - start + 1) * sizeof(size_t));
    msdf_gl_index_entry *atlas_index =
        (msdf_gl_index_entry *)malloc((end - start + 1) * sizeof(msdf_gl_index_entry));

    size_t meta_size_sum = 0, point_size_sum = 0;
    for (size_t i = 0; i <= end - start; ++i) {
        msdf_glyph_buffer_size(font->_msdf_font, start + i, &meta_sizes[i],
                               &point_sizes[i]);
        meta_size_sum += meta_sizes[i];
        point_size_sum += point_sizes[i];
    }

    /* Allocate the calculated amount. */
    void *point_data = malloc(point_size_sum);
    void *metadata = malloc(meta_size_sum);

    /* Serialize the glyphs into RAM. */
    font->vertical_advance = font->_msdf_font->height;
    char *meta_ptr = metadata;
    char *point_ptr = point_data;
    float offset_x = 1, offset_y = 1, y_increment = 0;
    for (size_t i = 0; i <= end - start; ++i) {
        float glyph_width, glyph_height, buffer_width, buffer_height;
        float bearing_x, bearing_y, advance;
        msdf_serialize_glyph(font->_msdf_font, start + i, meta_ptr, point_ptr,
                             &glyph_width, &glyph_height, &bearing_x, &bearing_y,
                             &advance);

        buffer_width = (glyph_width + font->range) * font->scale;
        buffer_height = (glyph_height + font->range) * font->scale;

        meta_ptr += meta_sizes[i];
        point_ptr += point_sizes[i];

        if (offset_x + buffer_width > font->texture_size) {
            offset_y += (y_increment + 1);
            offset_x = 1;
            y_increment = 0;
        }
        y_increment = buffer_height > y_increment ? buffer_height : y_increment;

        atlas_index[i].offset_x = offset_x;
        atlas_index[i].offset_y = offset_y;
        atlas_index[i].size_x = buffer_width;
        atlas_index[i].size_y = buffer_height;
        atlas_index[i].bearing_x = bearing_x;
        atlas_index[i].bearing_y = bearing_y;
        atlas_index[i].glyph_width = glyph_width;
        atlas_index[i].glyph_height = glyph_height;
        font->horizontal_advances[i + start] = advance;

        offset_x += buffer_width + 1;
    }

    /* Allocate and fill the buffers on GPU. */
    glBindBuffer(GL_ARRAY_BUFFER, font->_meta_input_buffer);
    glBufferData(GL_ARRAY_BUFFER, meta_size_sum, metadata, GL_STATIC_READ);

    glBindBuffer(GL_ARRAY_BUFFER, font->_point_input_buffer);
    glBufferData(GL_ARRAY_BUFFER, point_size_sum, point_data, GL_STATIC_READ);

    glBindBuffer(GL_ARRAY_BUFFER, font->_index_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(atlas_index), &atlas_index[0], GL_STATIC_READ);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Link sampler textures to the buffers. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, font->_meta_input_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, font->_meta_input_buffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, font->_point_input_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, font->_point_input_buffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, font->index_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, font->_index_buffer);

    glActiveTexture(GL_TEXTURE0);

    /* Generate the atlas texture and bind it as the framebuffer. */
    glBindTexture(GL_TEXTURE_2D, font->atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Generate the atlas texture and bind it as the framebuffer. */
    glBindTexture(GL_TEXTURE_2D, font->atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, font->texture_size, font->texture_size, 0,
                 GL_RGBA, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, font->_meta_input_texture);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, font->_point_input_texture);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, font->_atlas_framebuffer);

    glUseProgram(ctx->gen_shader);
    glUniform1i(ctx->metadata_uniform, 0);
    glUniform1i(ctx->point_data_uniform, 1);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    GLfloat msdf_projection[4][4];
    _ortho(-font->texture_size, font->texture_size, -font->texture_size,
           font->texture_size, -1.0, 1.0, font->projection);
    _ortho(0, font->texture_size, 0, font->texture_size, -1.0, 1.0, msdf_projection);

    glUniformMatrix4fv(ctx->_projection_uniform, 1, GL_FALSE, (GLfloat *)msdf_projection);
    glUniform2f(ctx->_scale_uniform, font->scale, font->scale);
    glUniform1f(ctx->_range_uniform, font->range);
    glUniform1i(ctx->_meta_offset_uniform, 0);
    glUniform1i(ctx->_point_offset_uniform, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           font->atlas_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("framebuffer incomplete: %x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glViewport(0, 0, font->texture_size, font->texture_size);

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint _vbo;
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), 0, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);

    int meta_offset = 0;
    int point_offset = 0;
    for (int i = 0; i <= end - start; ++i) {
        msdf_gl_index_entry g = atlas_index[i];
        float w = g.size_x;
        float h = g.size_y;
        GLfloat bounding_box[] = {0, 0, w, 0, 0, h, 0, h, w, 0, w, h};
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bounding_box), bounding_box);

        glUniform2f(ctx->_translate_uniform, -g.bearing_x + font->range / 2.0,
                    g.glyph_height - g.bearing_y + font->range / 2.0);

        glUniform2f(ctx->_texture_offset_uniform, g.offset_x, g.offset_y);
        glUniform1i(ctx->_meta_offset_uniform, meta_offset);
        glUniform1i(ctx->_point_offset_uniform, point_offset / (2 * sizeof(GLfloat)));
        glUniform1f(ctx->_glyph_height_uniform, g.size_y);

        /* Do not bother rendering control characters */
        /* if (i > 31 && !(i > 126 && i < 160)) */
        glDrawArrays(GL_TRIANGLES, 0, 6);

        meta_offset += meta_sizes[i];
        point_offset += point_sizes[i];
    }
    glDisableVertexAttribArray(0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    if (meta_sizes)
        free(meta_sizes);
    if (point_sizes)
        free(point_sizes);
    if (atlas_index)
        free(atlas_index);

    return end - start;
}

void msdf_gl_destroy_context(msdf_gl_context_t ctx) {

    if (!ctx)
        return;

    glDeleteProgram(ctx->gen_shader);

    free(ctx);
}
