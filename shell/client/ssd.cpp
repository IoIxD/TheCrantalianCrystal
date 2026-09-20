#include "client.hpp"
#include <assert.h>
#include <csignal>

static const char *vertex_shader_src = R"(#version 330
layout (location = 0) in vec2 position;
out vec4 pos;
void main() {
  gl_Position = vec4(position, 0.0, 1.0);
  pos = gl_Position;
}
)";

static const char *fragment_shader_src = R"(#version 330
in vec4 pos;
uniform vec2 resolution;

float rounded_box_sdf(vec2 p, vec2 half_size, float radius) {
  vec2 q = abs(p) - half_size + radius;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    vec3 baseColor = vec3(.416, .196, .576);
    vec3 lowColor = vec3(1.0, .612, .404);
    float mixBy = (1.0 - pos.y);
    if(mixBy > 0.75) mixBy = 0.75;

    vec3 mixedColor = mix(baseColor,lowColor,mixBy);

    vec2 frag_coord = (pos.xy * 0.5 + 0.5) * resolution;
    float dist = rounded_box_sdf(frag_coord - resolution * 0.5, resolution * 0.5, 7.0);
    float alpha = 1.0 - smoothstep(-0.75, 0.75, dist);
    if (alpha <= 0.0) {
        discard;
    }

    float border_dist = (dist + 2.0);
    float black_dist = (dist + 3.0);
    float darken = 0.15 * (1.0 - smoothstep(0.0, 1.0, border_dist));
    mixedColor -= darken;
    if(resolution.y - frag_coord.y >= 22 ) {
        float black = 0.15 * (1.0 - smoothstep(0.0, 1.0, black_dist));
        mixedColor -= black;
    }


    gl_FragColor = vec4(mixedColor.rgb, alpha);
}
)";

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    printf("ERROR: shader compilation failed: %s\n", log);
    raise(SIGTRAP);
  }

  return shader;
}

static GLuint create_shader_program() {
  GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
  GLuint fragment_shader =
      compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glBindAttribLocation(program, 0, "position");
  glLinkProgram(program);

  GLint status;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    printf("ERROR: shader program linking failed: %s\n", log);
    raise(SIGTRAP);
  }

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  return program;
}

void TCCClient::Window::setup_egl() {
  const char *extensions;

  EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_DEPTH_SIZE,
                             24,
                             EGL_NONE};
  EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION,
                             1,
                             EGL_CONTEXT_MAJOR_VERSION,
                             1,
                             EGL_CONTEXT_MINOR_VERSION,
                             1,
                             EGL_NONE};
  EGLint major, minor, n, count, i, size;
  EGLConfig *configs;
  EGLBoolean ret;

  egl_display =
      eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, client->mDisplay, NULL);

  ret = eglInitialize(egl_display, &major, &minor);
  assert(ret == EGL_TRUE);

  if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1)
    assert(0);

  configs = (EGLConfig *)calloc(count, sizeof *configs);
  assert(configs);

  ret = eglChooseConfig(egl_display, config_attribs, configs, count, &n);
  assert(ret && n >= 1);

  egl_config = configs[0];

  free(configs);

  egl_window = wl_egl_window_create(decor_surface, decor_width, decor_height);
  if (!egl_window) {
    printf("ERROR: eglCreateWindowSurface, %0X\n", eglGetError());
    raise(SIGTRAP);
  }

  ret = eglBindAPI(EGL_OPENGL_API);
  assert(ret == EGL_TRUE);
  egl_context =
      eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, contextAttribs);
  assert(egl_context);

  egl_surface =
      eglCreatePlatformWindowSurface(egl_display, egl_config, egl_window, NULL);
  if (egl_surface == EGL_NO_SURFACE) {
    printf("eglCreatePlatformWindowSurface error: %0X\n", eglGetError());
    raise(SIGTRAP);
  }

  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    printf("eglMakeCurrent error (init) %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  eglSwapInterval(egl_display, 0);

  egl_shader_program = create_shader_program();
};
void TCCClient::Window::egl_draw() {
  wl_egl_window_resize(egl_window, decor_width, decor_height, 0, 0);
  glViewport(0, 0, decor_width, decor_height);

  glClearColor(0.f, 0.0f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(egl_shader_program);
  glUniform2f(glGetUniformLocation(egl_shader_program, "resolution"),
              (float)decor_width, (float)decor_height);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 1.0f);
  glVertex3f(-1, -1, 1); // bottom-left
  glTexCoord2f(1.0f, 1.0f);
  glVertex3f(1, -1, 1);
  glTexCoord2f(1.0f, 0.0f);
  glVertex3f(1, 1, 1);
  glTexCoord2f(0.0f, 0.0f);
  glVertex3f(-1, 1, 1); // top-right
  glEnd();

  glUseProgram(0);
  glDisable(GL_BLEND);

  eglSwapBuffers(egl_display, egl_surface);
};
