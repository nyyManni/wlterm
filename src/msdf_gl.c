
#ifdef __linux__
#define GL_GLEXT_PROTOTYPES
#else
/* Figure out something. */
#endif
#include "msdf_gl.h"


#include "_msdf_kernels.h"

typedef GLfloat vec4[4];
typedef vec4 mat4[4];

mat4 _MAT4_ZERO_INIT = {{0.0f, 0.0f, 0.0f, 0.0f}, 
                        {0.0f, 0.0f, 0.0f, 0.0f}, 
                        {0.0f, 0.0f, 0.0f, 0.0f}, 
                        {0.0f, 0.0f, 0.0f, 0.0f}};

static inline void
_ortho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
       GLfloat nearVal, GLfloat farVal, mat4  dest) {
  GLfloat rl, tb, fn;

  memcpy(dest, _MAT4_ZERO_INIT, 16 * sizeof(GLfloat));

  rl = 1.0f / (right  - left);
  tb = 1.0f / (top    - bottom);
  fn =-1.0f / (farVal - nearVal);

  dest[0][0] = 2.0f * rl;
  dest[1][1] = 2.0f * tb;
  dest[2][2] = 2.0f * fn;
  dest[3][0] =-(right  + left)    * rl;
  dest[3][1] =-(top    + bottom)  * tb;
  dest[3][2] = (farVal + nearVal) * fn;
  dest[3][3] = 1.0f;
}


struct _msdf_gl_context {
    GLuint gen_shader;
    GLuint render_shader;

    GLuint projection_uniform;

    GLuint metadata_uniform;
};

int __compile_shader(const char *source, GLenum type, GLuint *shader) {
    *shader = glCreateShader(type);

    glShaderSource(*shader, 1, (const char **)source, NULL);
    glCompileShader(*shader);
    
    GLint status;
	glGetShaderiv(*shader, GL_COMPILE_STATUS, &status);
	if (!status)
        return 0;
    
    return 1;
}

msdf_gl_context_t msdf_gl_create_context() {
    msdf_gl_context_t ctx = 
        (msdf_gl_context_t)malloc(sizeof(struct _msdf_gl_context));
    
    if (!ctx) return NULL;

    GLuint vertex_shader, fragment_shader;
    if (!__compile_shader(_msdf_vertex, GL_VERTEX_SHADER, &vertex_shader))
        return NULL;
    if (!__compile_shader(_msdf_fragment, GL_VERTEX_SHADER, &fragment_shader))
        return NULL;
    
    ctx->gen_shader = glCreateProgram();
    glAttachShader(ctx->gen_shader, vertex_shader);
    glAttachShader(ctx->gen_shader, fragment_shader);
    
    glLinkProgram(ctx->gen_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    GLint status;
	glGetShaderiv(ctx->gen_shader, GL_LINK_STATUS, &status);
	if (!status)
        return NULL;

    return ctx;
}

void msdf_destroy_context(msdf_gl_context_t ctx) {
    
    if (!ctx) return;
    
    free(ctx);
}
