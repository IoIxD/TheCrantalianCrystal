/*
 * Runs a screensaver module for the shell's lock screen, drawing into dmabufs
 * the shell puts on its lock surfaces. It's a separate process so a module
 * crashing or hanging can't take the lock screen down with it. See
 * scr_ipc.hpp for how it talks to the shell.
 *
 * There's no Wayland connection here at all: we draw with EGL straight on a
 * GPU render node through GBM.
 *
 * usage: tcc_scr_loader <socket fd>
 */

#include "dynload.hpp"
#include "egl_loader.hpp"
#include "gbm_loader.hpp"

#include "scr_ipc.hpp"
#include "screensaver.hpp"

#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

typedef void (*PFNEGLIMAGETARGETTEXTURE2DOES)(GLenum target, void *image);

// Everything GL we need ourselves, from eglGetProcAddress. (The module finds
// its own.)
#define GL_FUNCS(X)                                                            \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                               \
  X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                         \
  X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                               \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                     \
  X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                             \
  X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                       \
  X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                             \
  X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                       \
  X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)               \
  X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                 \
  X(PFNEGLIMAGETARGETTEXTURE2DOES, glEGLImageTargetTexture2DOES)               \
  X(decltype(&::glGenTextures), glGenTextures)                                 \
  X(decltype(&::glDeleteTextures), glDeleteTextures)                           \
  X(decltype(&::glBindTexture), glBindTexture)                                 \
  X(decltype(&::glTexParameteri), glTexParameteri)                             \
  X(decltype(&::glTexImage2D), glTexImage2D)                                   \
  X(decltype(&::glFinish), glFinish)                                           \
  X(decltype(&::glViewport), glViewport)                                       \
  X(decltype(&::glEnable), glEnable)                                           \
  X(decltype(&::glDisable), glDisable)                                         \
  X(decltype(&::glBlendFunc), glBlendFunc)                                     \
  X(decltype(&::glDrawArrays), glDrawArrays)                                   \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                   \
  X(PFNGLBINDBUFFERPROC, glBindBuffer)                                         \
  X(PFNGLCREATESHADERPROC, glCreateShader)                                     \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                                     \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                                   \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                       \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                             \
  X(PFNGLDELETESHADERPROC, glDeleteShader)                                     \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                   \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                                     \
  X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                         \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                       \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                     \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                         \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                         \
  X(PFNGLUNIFORM1IPROC, glUniform1i)                                           \
  X(PFNGLUNIFORM1FPROC, glUniform1f)                                           \
  X(PFNGLUNIFORM2FPROC, glUniform2f)                                           \
  X(PFNGLUNIFORM4FPROC, glUniform4f)                                           \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                       \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)               \
  X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)

namespace gl {
#define X(type, name) type name = nullptr;
GL_FUNCS(X)
#undef X
} // namespace gl

static PFNEGLCREATEIMAGEKHRPROC egl_create_image = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC egl_destroy_image = nullptr;

struct Buffer {
  gbm_bo *bo = nullptr;
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GLuint texture = 0, depth = 0, fbo = 0;
  // Shown (or about to be) by the shell, so not to be drawn into.
  bool busy = false;
};

// An intermediate for blurring: a texture and a framebuffer drawing into it.
struct Target {
  GLuint texture = 0, fbo = 0;
  int width = 0, height = 0;
};

struct Output {
  uint32_t id = 0;
  int width = 0, height = 0;
  Buffer buffers[SCR_MAX_BUFFERS];
  bool frame_wanted = false;

  // What to blur, in GL coordinates (from the bottom left). Width 0 for
  // nothing.
  int blur_x = 0, blur_y = 0, blur_width = 0, blur_height = 0;
  int blur_radius = 0;
  // Half resolution, blurred horizontally and then vertically.
  Target blur_targets[2];
};

static int sock = -1;
static gbm_device *gbm = nullptr;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static Screensaver *scr = nullptr;
static std::vector<Output *> outputs;
// What the compositor takes, from the shell.
static std::vector<uint64_t> modifiers;

static GLuint blur_program = 0;
static GLuint composite_program = 0;

// How far past the blurred area to sample from, so its edges blur with
// what's around them rather than fading out.
constexpr int BLUR_MARGIN = 32;

static const char *QUAD_VERT = R"(#version 120
attribute vec2 position;
varying vec2 uv;
void main() {
    uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// One direction of a gaussian blur, over the part of tex given by src.
static const char *BLUR_FRAG = R"(#version 120
uniform sampler2D tex;
uniform vec4 src;  // x, y, width, height, as texture coordinates
uniform vec2 dir;  // one step along the blur, as texture coordinates
varying vec2 uv;
void main() {
    vec2 p = src.xy + uv * src.zw;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = -12; i <= 12; i++) {
        float w = exp(-float(i * i) / 32.0);  // sigma of 4 steps
        sum += texture2D(tex, p + dir * float(i)).rgb * w;
        total += w;
    }
    gl_FragColor = vec4(sum / total, 1.0);
}
)";

// The blurred result back onto the frame, with rounded corners (using the
// same distance function as the decorations).
static const char *COMPOSITE_FRAG = R"(#version 120
uniform sampler2D tex;
uniform vec4 src;
uniform vec2 size;
uniform float radius;
varying vec2 uv;
void main() {
    vec2 q = abs(uv * size - size * 0.5) - size * 0.5 + radius;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float alpha = 1.0 - smoothstep(-0.75, 0.75, dist);
    gl_FragColor = vec4(texture2D(tex, src.xy + uv * src.zw).rgb, alpha);
}
)";

[[noreturn]] static void die(const char *what) {
  fprintf(stderr, "tcc_scr_loader: %s\n", what);
  exit(1);
}

static void send_msg(const ScrMsg &msg, int fd = -1) {
  iovec iov = {(void *)&msg, sizeof(msg)};
  msghdr hdr = {};
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;

  char control[CMSG_SPACE(sizeof(int))] = {};
  if (fd >= 0) {
    hdr.msg_control = control;
    hdr.msg_controllen = sizeof(control);
    cmsghdr *cmsg = CMSG_FIRSTHDR(&hdr);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
  }

  if (sendmsg(sock, &hdr, MSG_NOSIGNAL) < 0) {
    // The shell's gone, or unlocked.
    exit(0);
  }
}

static int open_render_node() {
  if (const char *path = getenv("TCC_SCR_RENDER_NODE")) {
    return open(path, O_RDWR | O_CLOEXEC);
  }
  for (int i = 128; i < 192; i++) {
    int fd = open(std::format("/dev/dri/renderD{}", i).c_str(),
                  O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
      return fd;
    }
  }
  return -1;
}

static GLuint compile_shader(GLenum type, const char *source) {
  GLuint shader = gl::glCreateShader(type);
  gl::glShaderSource(shader, 1, &source, NULL);
  gl::glCompileShader(shader);
  GLint ok;
  gl::glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    gl::glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "tcc_scr_loader: %s\n", log);
    die("shader compilation failed");
  }
  return shader;
}

static GLuint create_program(const char *fragment_source) {
  GLuint vert = compile_shader(GL_VERTEX_SHADER, QUAD_VERT);
  GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  GLuint program = gl::glCreateProgram();
  gl::glAttachShader(program, vert);
  gl::glAttachShader(program, frag);
  gl::glBindAttribLocation(program, 0, "position");
  gl::glLinkProgram(program);
  gl::glDeleteShader(vert);
  gl::glDeleteShader(frag);
  GLint ok;
  gl::glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    die("shader program linking failed");
  }
  return program;
}

static void setup_egl() {
  int fd = open_render_node();
  if (fd < 0) {
    die("couldn't open a render node");
  }
  gbm = gbm_create_device(fd);
  if (!gbm) {
    die("gbm_create_device failed");
  }

  egl_display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
  EGLint major, minor;
  if (egl_display == EGL_NO_DISPLAY ||
      !eglInitialize(egl_display, &major, &minor)) {
    die("eglInitialize failed");
  }
  if (!eglBindAPI(EGL_OPENGL_API)) {
    die("eglBindAPI failed");
  }

  // Everything's drawn into our own framebuffers, so there's no need for a
  // config (EGL_KHR_no_config_context) or a surface
  // (EGL_KHR_surfaceless_context).
  EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 2,
                              EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
  egl_context = eglCreateContext(egl_display, EGL_NO_CONFIG_KHR,
                                 EGL_NO_CONTEXT, context_attribs);
  if (egl_context == EGL_NO_CONTEXT) {
    die("eglCreateContext failed");
  }
  if (!eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                      egl_context)) {
    die("eglMakeCurrent failed");
  }

  egl_create_image =
      (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  egl_destroy_image =
      (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  if (!egl_create_image || !egl_destroy_image) {
    die("EGL_KHR_image_base isn't supported");
  }
#define X(type, name)                                                          \
  if (!(gl::name = (type)eglGetProcAddress(#name)))                            \
    die("missing " #name);
  GL_FUNCS(X)
#undef X

  blur_program = create_program(BLUR_FRAG);
  composite_program = create_program(COMPOSITE_FRAG);
}

static void buffer_free(Buffer *buffer) {
  if (buffer->fbo)
    gl::glDeleteFramebuffers(1, &buffer->fbo);
  if (buffer->depth)
    gl::glDeleteRenderbuffers(1, &buffer->depth);
  if (buffer->texture)
    gl::glDeleteTextures(1, &buffer->texture);
  if (buffer->image != EGL_NO_IMAGE_KHR)
    egl_destroy_image(egl_display, buffer->image);
  if (buffer->bo)
    gbm_bo_destroy(buffer->bo);
  *buffer = Buffer();
}

static gbm_bo *create_bo(int width, int height, uint64_t *modifier) {
  // Only single plane layouts, that's all the shell takes.
  std::vector<uint64_t> explicit_modifiers;
  bool implicit = false;
  for (uint64_t mod : modifiers) {
    if (mod == SCR_MOD_INVALID) {
      implicit = true;
    } else if (gbm_device_get_format_modifier_plane_count(gbm, SCR_FORMAT,
                                                          mod) == 1) {
      explicit_modifiers.push_back(mod);
    }
  }

  if (!explicit_modifiers.empty()) {
    gbm_bo *bo = gbm_bo_create_with_modifiers(
        gbm, width, height, SCR_FORMAT, explicit_modifiers.data(),
        explicit_modifiers.size());
    if (bo) {
      *modifier = gbm_bo_get_modifier(bo);
      return bo;
    }
  }
  if (implicit) {
    *modifier = SCR_MOD_INVALID;
    return gbm_bo_create(gbm, width, height, SCR_FORMAT,
                         GBM_BO_USE_RENDERING);
  }
  return nullptr;
}

// Makes a buffer, hooks it up to a framebuffer to draw into, and sends it.
static void buffer_create(Output *output, uint32_t id) {
  Buffer *buffer = &output->buffers[id];
  uint64_t modifier = SCR_MOD_INVALID;

  buffer->bo = create_bo(output->width, output->height, &modifier);
  if (!buffer->bo || gbm_bo_get_plane_count(buffer->bo) != 1) {
    die("couldn't make a buffer the compositor would take");
  }
  int fd = gbm_bo_get_fd(buffer->bo);
  if (fd < 0) {
    die("gbm_bo_get_fd failed");
  }
  uint32_t stride = gbm_bo_get_stride(buffer->bo);
  uint32_t offset = gbm_bo_get_offset(buffer->bo, 0);

  std::vector<EGLint> attribs = {
      EGL_WIDTH,
      output->width,
      EGL_HEIGHT,
      output->height,
      EGL_LINUX_DRM_FOURCC_EXT,
      SCR_FORMAT,
      EGL_DMA_BUF_PLANE0_FD_EXT,
      fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      (EGLint)offset,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,
      (EGLint)stride,
  };
  if (modifier != SCR_MOD_INVALID) {
    attribs.insert(attribs.end(),
                   {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                    (EGLint)(modifier & 0xffffffff),
                    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
                    (EGLint)(modifier >> 32)});
  }
  attribs.push_back(EGL_NONE);
  buffer->image = egl_create_image(egl_display, EGL_NO_CONTEXT,
                                   EGL_LINUX_DMA_BUF_EXT, NULL, attribs.data());
  if (buffer->image == EGL_NO_IMAGE_KHR) {
    die("couldn't import our own buffer into EGL");
  }

  gl::glGenTextures(1, &buffer->texture);
  gl::glBindTexture(GL_TEXTURE_2D, buffer->texture);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // (the blur samples past the edge of the frame)
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl::glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, buffer->image);
  gl::glBindTexture(GL_TEXTURE_2D, 0);

  gl::glGenRenderbuffers(1, &buffer->depth);
  gl::glBindRenderbuffer(GL_RENDERBUFFER, buffer->depth);
  gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                            output->width, output->height);
  gl::glBindRenderbuffer(GL_RENDERBUFFER, 0);

  gl::glGenFramebuffers(1, &buffer->fbo);
  gl::glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
  gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, buffer->texture, 0);
  gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, buffer->depth);
  if (gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
      GL_FRAMEBUFFER_COMPLETE) {
    die("framebuffer incomplete");
  }
  gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);

  ScrMsg msg;
  msg.type = SCR_MSG_BUFFER;
  msg.output = output->id;
  msg.buffer = id;
  msg.width = output->width;
  msg.height = output->height;
  msg.stride = stride;
  msg.offset = offset;
  msg.modifier = modifier;
  send_msg(msg, fd);
  close(fd);
}

static void target_free(Target *target) {
  if (target->fbo)
    gl::glDeleteFramebuffers(1, &target->fbo);
  if (target->texture)
    gl::glDeleteTextures(1, &target->texture);
  *target = Target();
}

static void target_resize(Target *target, int width, int height) {
  if (target->texture && target->width == width &&
      target->height == height) {
    return;
  }
  target_free(target);
  target->width = width;
  target->height = height;

  gl::glGenTextures(1, &target->texture);
  gl::glBindTexture(GL_TEXTURE_2D, target->texture);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, NULL);
  gl::glBindTexture(GL_TEXTURE_2D, 0);

  gl::glGenFramebuffers(1, &target->fbo);
  gl::glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
  gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, target->texture, 0);
  gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void draw_quad() {
  static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  gl::glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  gl::glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// Blurs the output's blur area of what's been drawn into buffer, in place.
static void blur(Output *output, Buffer *buffer) {
  int w = output->width, h = output->height;

  // What's sampled from: the area plus a margin, within the frame.
  int sx = std::max(output->blur_x - BLUR_MARGIN, 0);
  int sy = std::max(output->blur_y - BLUR_MARGIN, 0);
  int sw = std::min(output->blur_x + output->blur_width + BLUR_MARGIN, w) - sx;
  int sh =
      std::min(output->blur_y + output->blur_height + BLUR_MARGIN, h) - sy;
  if (sw <= 0 || sh <= 0) {
    return;
  }
  Target *horizontal = &output->blur_targets[0];
  Target *vertical = &output->blur_targets[1];
  target_resize(horizontal, (sw + 1) / 2, (sh + 1) / 2);
  target_resize(vertical, (sw + 1) / 2, (sh + 1) / 2);

  // The module may have left all sorts of state behind.
  gl::glDisable(GL_DEPTH_TEST);
  gl::glDisable(GL_CULL_FACE);
  gl::glDisable(GL_SCISSOR_TEST);
  gl::glDisable(GL_BLEND);
  gl::glActiveTexture(GL_TEXTURE0);
  gl::glBindBuffer(GL_ARRAY_BUFFER, 0);
  for (GLuint i = 1; i < 8; i++) {
    gl::glDisableVertexAttribArray(i);
  }
  gl::glEnableVertexAttribArray(0);

  gl::glUseProgram(blur_program);
  gl::glUniform1i(gl::glGetUniformLocation(blur_program, "tex"), 0);

  // Horizontally, from the frame into half resolution.
  gl::glBindFramebuffer(GL_FRAMEBUFFER, horizontal->fbo);
  gl::glViewport(0, 0, horizontal->width, horizontal->height);
  gl::glBindTexture(GL_TEXTURE_2D, buffer->texture);
  gl::glUniform4f(gl::glGetUniformLocation(blur_program, "src"),
                  (float)sx / w, (float)sy / h, (float)sw / w, (float)sh / h);
  gl::glUniform2f(gl::glGetUniformLocation(blur_program, "dir"), 2.f / w, 0.f);
  draw_quad();

  // Then vertically.
  gl::glBindFramebuffer(GL_FRAMEBUFFER, vertical->fbo);
  gl::glBindTexture(GL_TEXTURE_2D, horizontal->texture);
  gl::glUniform4f(gl::glGetUniformLocation(blur_program, "src"), 0.f, 0.f,
                  1.f, 1.f);
  gl::glUniform2f(gl::glGetUniformLocation(blur_program, "dir"), 0.f,
                  1.f / vertical->height);
  draw_quad();

  // And back into the frame, over just the area.
  gl::glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
  gl::glViewport(output->blur_x, output->blur_y, output->blur_width,
                 output->blur_height);
  gl::glEnable(GL_BLEND);
  gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl::glUseProgram(composite_program);
  gl::glUniform1i(gl::glGetUniformLocation(composite_program, "tex"), 0);
  gl::glBindTexture(GL_TEXTURE_2D, vertical->texture);
  gl::glUniform4f(gl::glGetUniformLocation(composite_program, "src"),
                  (float)(output->blur_x - sx) / sw,
                  (float)(output->blur_y - sy) / sh,
                  (float)output->blur_width / sw,
                  (float)output->blur_height / sh);
  gl::glUniform2f(gl::glGetUniformLocation(composite_program, "size"),
                  (float)output->blur_width, (float)output->blur_height);
  gl::glUniform1f(gl::glGetUniformLocation(composite_program, "radius"),
                  (float)output->blur_radius);
  draw_quad();

  gl::glDisable(GL_BLEND);
  gl::glBindTexture(GL_TEXTURE_2D, 0);
  gl::glUseProgram(0);
}

// x and y are from the top left, and from the shell.
static void output_set_blur(Output *output, int x, int y, int width,
                            int height, int radius) {
  if (width <= 0 || height <= 0 || radius < 0) {
    output->blur_width = output->blur_height = 0;
    return;
  }
  // Within the frame.
  int x0 = std::clamp(x, 0, output->width);
  int y0 = std::clamp(y, 0, output->height);
  int x1 = std::clamp(x + width, 0, output->width);
  int y1 = std::clamp(y + height, 0, output->height);

  output->blur_x = x0;
  // (GL's origin is the bottom left)
  output->blur_y = output->height - y1;
  output->blur_width = x1 - x0;
  output->blur_height = y1 - y0;
  output->blur_radius = std::min(radius, 64);
}

static Output *find_output(uint32_t id) {
  for (Output *output : outputs) {
    if (output->id == id) {
      return output;
    }
  }
  return nullptr;
}

static void output_configure(uint32_t id, int width, int height) {
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    return;
  }
  Output *output = find_output(id);
  if (!output) {
    output = new Output();
    output->id = id;
    outputs.push_back(output);
  }
  if (output->width == width && output->height == height) {
    return;
  }

  for (Buffer &buffer : output->buffers) {
    buffer_free(&buffer);
  }
  output->width = width;
  output->height = height;
  for (uint32_t i = 0; i < SCR_MAX_BUFFERS; i++) {
    buffer_create(output, i);
  }
  // The first frame comes unprompted.
  output->frame_wanted = true;
}

static void output_remove(uint32_t id) {
  Output *output = find_output(id);
  if (!output) {
    return;
  }
  for (Buffer &buffer : output->buffers) {
    buffer_free(&buffer);
  }
  for (Target &target : output->blur_targets) {
    target_free(&target);
  }
  std::erase(outputs, output);
  delete output;
}

static void output_draw(Output *output) {
  Buffer *buffer = nullptr;
  uint32_t id = 0;
  for (; id < SCR_MAX_BUFFERS; id++) {
    if (!output->buffers[id].busy) {
      buffer = &output->buffers[id];
      break;
    }
  }
  if (!buffer) {
    // Waiting for the compositor to let go of one.
    return;
  }

  gl::glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
  if (scr && scr->draw) {
    scr->draw(scr->ctx, output->width, output->height);
  }
  if (output->blur_width > 0 && output->blur_height > 0) {
    blur(output, buffer);
  }
  // No explicit sync, so the frame has to be done before the shell gets it.
  gl::glFinish();
  gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);

  buffer->busy = true;
  output->frame_wanted = false;

  ScrMsg msg;
  msg.type = SCR_MSG_READY;
  msg.output = output->id;
  msg.buffer = id;
  send_msg(msg);
}

static void handle_msg(const ScrMsg &msg) {
  switch (msg.type) {
  case SCR_MSG_MODIFIERS:
    modifiers.assign(msg.modifiers,
                     msg.modifiers +
                         std::min<uint32_t>(msg.n_modifiers, SCR_MAX_MODIFIERS));
    break;
  case SCR_MSG_CONFIGURE:
    output_configure(msg.output, msg.width, msg.height);
    break;
  case SCR_MSG_REMOVE:
    output_remove(msg.output);
    break;
  case SCR_MSG_FRAME:
    if (Output *output = find_output(msg.output)) {
      output->frame_wanted = true;
    }
    break;
  case SCR_MSG_BLUR:
    if (Output *output = find_output(msg.output)) {
      output_set_blur(output, msg.x, msg.y, msg.width, msg.height,
                      msg.radius);
    }
    break;
  case SCR_MSG_RELEASE:
    if (Output *output = find_output(msg.output)) {
      if (msg.buffer < SCR_MAX_BUFFERS) {
        output->buffers[msg.buffer].busy = false;
      }
    }
    break;
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <socket fd>\n", argv[0]);
    return 1;
  }
  sock = atoi(argv[1]);
  fcntl(sock, F_SETFD, FD_CLOEXEC);

  // Don't outlive the shell.
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  if (getppid() == 1) {
    return 0;
  }

  if (!dynload_setup::egl() || !dynload_setup::gbm()) {
    return 1;
  }
  setup_egl();

  /* todo: loading custom ones */
  scr = new Screensaver("tcc_scr_tunnel");

  for (;;) {
    ScrMsg msg;
    ssize_t n = recv(sock, &msg, sizeof(msg), 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      // The shell closed its end: unlocked.
      break;
    }
    if (n == sizeof(msg)) {
      handle_msg(msg);
    }

    // Catch up on everything else that's come in before drawing.
    pollfd pfd = {sock, POLLIN, 0};
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
      n = recv(sock, &msg, sizeof(msg), 0);
      if (n <= 0) {
        goto done;
      }
      if (n == sizeof(msg)) {
        handle_msg(msg);
      }
    }

    for (Output *output : outputs) {
      if (output->frame_wanted) {
        output_draw(output);
      }
    }
  }
done:

  // (the screensaver may free GL objects, so it goes while the context is
  // still current.)
  delete scr;
  for (Output *output : std::vector<Output *>(outputs)) {
    output_remove(output->id);
  }
  eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(egl_display, egl_context);
  eglTerminate(egl_display);
  gbm_device_destroy(gbm);
  return 0;
}
