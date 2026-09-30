/*
 * I am too dogshit at math and especially trig to do this myself.
 * Do note then that this is done by AI.
 */

#define GL_GLEXT_PROTOTYPES
#include "tunnel_shader.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

#define EXPORT extern "C" __attribute__((visibility("default")))

// A named namespace, so these pointers hide the GL prototypes of the same
// name for everything below, exports included.
namespace tunnel {

#define GL_FUNCS(X)                                                            \
  X(glAttachShader)                                                            \
  X(glBindAttribLocation)                                                      \
  X(glBindBuffer)                                                              \
  X(glBufferData)                                                              \
  X(glBufferSubData)                                                           \
  X(glClear)                                                                   \
  X(glClearColor)                                                              \
  X(glCompileShader) X(glCreateProgram) X(glCreateShader) X(glDeleteBuffers)   \
      X(glDeleteProgram) X(glDeleteShader) X(glDepthFunc) X(glDisable)         \
          X(glDrawElements) X(glEnable) X(glEnableVertexAttribArray)           \
              X(glGenBuffers) X(glGetProgramInfoLog) X(glGetProgramiv)         \
                  X(glGetShaderInfoLog) X(glGetShaderiv)                       \
                      X(glGetUniformLocation) X(glLinkProgram)                 \
                          X(glShaderSource) X(glUniform1f)                     \
                              X(glUniformMatrix4fv) X(glUseProgram)            \
                                  X(glVertexAttribPointer) X(glViewport)
#define X(name) decltype(&::name) name;
GL_FUNCS(X)
#undef X

constexpr double SPEED = 7.0;    // units per second along the tunnel
constexpr double SEGMENT = 1.0;  // length of one ring-to-ring slice
constexpr double FOG_END = 40.0; // distance at which the walls are black
// Everything past the fog is black anyway, so only draw a little beyond it.
constexpr int SEGMENTS = (int)(FOG_END / SEGMENT) + 10;
constexpr int SIDES = 4;              // walls around each ring
constexpr double HALF_WIDTH = 4.0;    // centerline to the left/right walls
constexpr double HALF_HEIGHT = 2.0;   // centerline to the floor/ceiling
constexpr double LOOK_AHEAD = 5.0;    // camera aims this far down the path
constexpr double COLOR_PERIOD = 90.0; // tunnel length of one full colour cycle
constexpr int RINGS = SEGMENTS + 1;   // ring count, one more than segments
constexpr int INDICES = SEGMENTS * SIDES * 6; // two triangles per wall quad

// Vertex attribute slots, bound by name when the program is linked.
constexpr GLuint ATTRIB_POSITION = 0;
constexpr GLuint ATTRIB_COLOR = 1;

struct Vec3 {
  double x, y, z;

  Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }

  Vec3 cross(const Vec3 &o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }

  Vec3 normalized() const {
    double len = std::sqrt(x * x + y * y + z * z);
    return len > 0.0 ? *this * (1.0 / len) : *this;
  }
};

const Vec3 WORLD_UP = {0.0, 1.0, 0.0};

// Rectangular cross-section, walking counterclockwise from the top right, so
// wall i spans corners i and i + 1: ceiling, left, floor, right.
const double CORNERS[SIDES + 1][2] = {
    {1.0, 1.0}, {-1.0, 1.0}, {-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}};

// Hashes a knot index into a repeatable pseudo-random value in [-1, 1].
double knot_value(uint64_t seed, int64_t knot, uint64_t salt) {
  uint64_t z = seed ^ (salt * 0xD1B54A32D192ED03ull) ^
               ((uint64_t)knot * 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return (double)(z >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

struct NoiseLayer {
  double spacing;   // distance between random knots
  double amplitude; // how far a knot can pull the path off-axis
};

// Big slow bends with smaller wiggles on top.
const NoiseLayer LAYERS[] = {{40.0, 16.0}, {13.0, 3.5}};

// Uniform cubic B-spline through random knots: smooth (continuous curvature)
// yet unpredictable. Writes the value and its derivative with respect to s.
void noise(uint64_t seed, uint64_t axis, double s, double &value,
           double &slope) {
  value = 0.0;
  slope = 0.0;

  uint64_t salt = axis * 16;
  for (const NoiseLayer &layer : LAYERS) {
    double u = s / layer.spacing;
    double base = std::floor(u);
    double f = u - base;
    int64_t i = (int64_t)base;

    double p0 = knot_value(seed, i - 1, salt);
    double p1 = knot_value(seed, i, salt);
    double p2 = knot_value(seed, i + 1, salt);
    double p3 = knot_value(seed, i + 2, salt);

    double f2 = f * f;
    double f3 = f2 * f;
    double g = 1.0 - f;

    value += layer.amplitude *
             (p0 * g * g * g + p1 * (3.0 * f3 - 6.0 * f2 + 4.0) +
              p2 * (-3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0) + p3 * f3) /
             6.0;
    slope += layer.amplitude / layer.spacing *
             (-p0 * g * g + p1 * (3.0 * f2 - 4.0 * f) +
              p2 * (-3.0 * f2 + 2.0 * f + 1.0) + p3 * f2) /
             2.0;

    salt++;
  }
}

// The centerline heads down -z forever while x and y wander randomly, so the
// tunnel's heading keeps changing both horizontally and vertically.
void path(uint64_t seed, double s, Vec3 &pos, Vec3 &dir) {
  double x, y, dx, dy;
  noise(seed, 0, s, x, dx);
  noise(seed, 1, s, y, dy);
  pos = {x, y, -s};
  dir = Vec3{dx, dy, -1.0}.normalized();
}

Vec3 center(uint64_t seed, double s) {
  Vec3 pos, dir;
  path(seed, s, pos, dir);
  return pos;
}

struct Color {
  float r, g, b;
};

// Colours the tunnel cycles through, in order, wrapping back to the first.
const char *PALETTE[] = {"#B0DB61", "#1FB29C", "#157668", "#3D7F55",
                         "#09342E", "#BA3649", "#D8F8F3", "#BCB176"};
constexpr int PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);

Color hex_color(const char *hex) {
  unsigned long v = std::strtoul(hex + 1, nullptr, 16);
  return {((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f,
          (v & 0xFF) / 255.0f};
}

// The colour is a function of position along the tunnel, not of time, so it
// is painted onto the walls and slides past with them. One COLOR_PERIOD walks
// through the whole palette, blending each colour into the next.
Color wall_color(double s) {
  double u = s / COLOR_PERIOD;
  u = (u - std::floor(u)) * PALETTE_SIZE;
  int i = (int)u;
  float f = (float)(u - i);
  Color a = hex_color(PALETTE[i % PALETTE_SIZE]);
  Color b = hex_color(PALETTE[(i + 1) % PALETTE_SIZE]);
  return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
}

// A ring depends only on its index along the tunnel, so each one is worked
// out once when it comes into view and reused until it falls behind.
struct Ring {
  long index = LONG_MIN;
  Vec3 center;         // absolute position of the ring's centerline point
  Vec3 corners[SIDES]; // offsets from center to each corner
  Color color;
};

struct Vertex {
  GLfloat x, y, z; // relative to the camera
  GLfloat r, g, b;
};

struct Tunnel {
  uint64_t seed = 0;
  double distance = 0.0;
  std::chrono::steady_clock::time_point last;

  Ring rings[RINGS];

  // Rebuilt every frame, one entry per ring corner, nearest ring first.
  Vertex vertices[RINGS * SIDES];

  // GL objects are made on the first draw, once a context is guaranteed to be
  // current.
  bool gl_ready = false;
  bool gl_failed = false;
  GLuint program = 0;
  GLuint vertex_buffer = 0;
  GLuint index_buffer = 0;
  GLint view_uniform = -1;
  GLint projection_uniform = -1;

  const Ring &ring(long k) {
    Ring &r = rings[((k % RINGS) + RINGS) % RINGS];
    if (r.index == k)
      return r;

    double s = k * SEGMENT;
    Vec3 fwd;
    path(seed, s, r.center, fwd);
    Vec3 right = fwd.cross(WORLD_UP).normalized();
    Vec3 up = right.cross(fwd);

    for (int i = 0; i < SIDES; i++) {
      r.corners[i] = right * (HALF_WIDTH * CORNERS[i][0]) +
                     up * (HALF_HEIGHT * CORNERS[i][1]);
    }
    r.color = wall_color(s);
    r.index = k;
    return r;
  }

  bool setup_gl();
  void free_gl();
};

GLuint compile_shader(GLenum type, const char *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "tunnel: shader compilation failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool Tunnel::setup_gl() {
  GLuint vert = compile_shader(GL_VERTEX_SHADER, TUNNEL_VERT_SOURCE);
  GLuint frag = compile_shader(GL_FRAGMENT_SHADER, TUNNEL_FRAG_SOURCE);
  if (!vert || !frag) {
    if (vert)
      glDeleteShader(vert);
    if (frag)
      glDeleteShader(frag);
    return false;
  }

  program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glBindAttribLocation(program, ATTRIB_POSITION, "position");
  glBindAttribLocation(program, ATTRIB_COLOR, "color");
  glLinkProgram(program);
  // The program keeps them alive for as long as it needs them.
  glDeleteShader(vert);
  glDeleteShader(frag);

  GLint status;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    fprintf(stderr, "tunnel: shader program linking failed: %s\n", log);
    return false;
  }

  view_uniform = glGetUniformLocation(program, "view");
  projection_uniform = glGetUniformLocation(program, "projection");
  glUseProgram(program);
  glUniform1f(glGetUniformLocation(program, "fog_end"), (float)FOG_END);

  // Back to front, so the tunnel still looks right even without a depth
  // buffer. Segment j joins ring j (near) to ring j + 1 (far), and each wall
  // is split into two triangles.
  GLushort indices[INDICES];
  GLushort *idx = indices;
  for (int j = SEGMENTS - 1; j >= 0; j--) {
    for (int i = 0; i < SIDES; i++) {
      int next = (i + 1) % SIDES;
      GLushort near_a = (GLushort)(j * SIDES + i);
      GLushort far_a = (GLushort)((j + 1) * SIDES + i);
      GLushort far_b = (GLushort)((j + 1) * SIDES + next);
      GLushort near_b = (GLushort)(j * SIDES + next);
      *idx++ = near_a;
      *idx++ = far_a;
      *idx++ = far_b;
      *idx++ = near_a;
      *idx++ = far_b;
      *idx++ = near_b;
    }
  }

  glGenBuffers(1, &index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glGenBuffers(1, &vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), NULL, GL_STREAM_DRAW);

  return true;
}

void Tunnel::free_gl() {
  if (vertex_buffer)
    glDeleteBuffers(1, &vertex_buffer);
  if (index_buffer)
    glDeleteBuffers(1, &index_buffer);
  if (program)
    glDeleteProgram(program);
  vertex_buffer = index_buffer = program = 0;
}

// Column-major perspective projection, equivalent to glFrustum with a 70
// degree vertical field of view.
void projection_matrix(int width, int height, GLfloat m[16]) {
  double aspect = (double)width / (double)height;

  constexpr double near = 0.1;
  constexpr double far = SEGMENTS * SEGMENT + 20.0;
  double f = 1.0 / std::tan(70.0 * M_PI / 360.0);

  for (int i = 0; i < 16; i++)
    m[i] = 0.0f;
  m[0] = (float)(f / aspect);
  m[5] = (float)f;
  m[10] = (float)((far + near) / (near - far));
  m[11] = -1.0f;
  m[14] = (float)(2.0 * far * near / (near - far));
}

// Column-major look-at rotation; translation is already folded into the
// vertices by rendering relative to the eye.
void view_matrix(const Vec3 &eye, const Vec3 &target, GLfloat m[16]) {
  Vec3 f = (target - eye).normalized();
  Vec3 s = f.cross(WORLD_UP).normalized();
  Vec3 u = s.cross(f);

  GLfloat v[16] = {
      (float)s.x, (float)u.x, (float)-f.x, 0.0f, //
      (float)s.y, (float)u.y, (float)-f.y, 0.0f, //
      (float)s.z, (float)u.z, (float)-f.z, 0.0f, //
      0.0f,       0.0f,       0.0f,        1.0f,
  };
  for (int i = 0; i < 16; i++)
    m[i] = v[i];
}

EXPORT void *tcc_scr_init() {
  void *gl = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
#define X(name)                                                                \
  if (!gl || !(name = (decltype(name))dlsym(gl, #name)))                       \
    return fprintf(stderr, "tunnel: can't load %s\n", #name), nullptr;
  GL_FUNCS(X)
#undef X

  Tunnel *t = new Tunnel();
  t->last = std::chrono::steady_clock::now();
  // A different path every time the screensaver starts.
  t->seed = (uint64_t)t->last.time_since_epoch().count();

  return t;
};

EXPORT void tcc_scr_draw(void *ctx, int width, int height) {
  Tunnel *t = (Tunnel *)ctx;
  if (!t || t->gl_failed)
    return;

  if (!t->gl_ready) {
    if (!t->setup_gl()) {
      t->free_gl();
      t->gl_failed = true;
      return;
    }
    t->gl_ready = true;
  }

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  auto now = std::chrono::steady_clock::now();
  double dt = std::chrono::duration<double>(now - t->last).count();
  t->last = now;
  if (dt > 0.1)
    dt = 0.1; // don't lurch forward after a stall
  t->distance += SPEED * dt;

  glViewport(0, 0, width, height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  Vec3 eye = center(t->seed, t->distance);

  GLfloat projection[16];
  GLfloat view[16];
  projection_matrix(width, height, projection);
  view_matrix(eye, center(t->seed, t->distance + LOOK_AHEAD), view);

  // Rings are pinned to whole multiples of SEGMENT so the geometry stays put
  // in the world and slides past the camera instead of travelling with it.
  long first = (long)std::floor(t->distance / SEGMENT) - 1;

  // Everything is drawn relative to the camera so float precision holds up no
  // matter how far down the tunnel we've travelled.
  Vertex *v = t->vertices;
  for (int j = 0; j < RINGS; j++) {
    const Ring &r = t->ring(first + j);
    Vec3 rel = r.center - eye;
    for (int i = 0; i < SIDES; i++) {
      Vec3 p = rel + r.corners[i];
      *v++ = {(float)p.x, (float)p.y, (float)p.z,
              r.color.r,  r.color.g,  r.color.b};
    }
  }

  glUseProgram(t->program);
  glUniformMatrix4fv(t->projection_uniform, 1, GL_FALSE, projection);
  glUniformMatrix4fv(t->view_uniform, 1, GL_FALSE, view);

  glBindBuffer(GL_ARRAY_BUFFER, t->vertex_buffer);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(t->vertices), t->vertices);
  glEnableVertexAttribArray(ATTRIB_POSITION);
  glEnableVertexAttribArray(ATTRIB_COLOR);
  glVertexAttribPointer(ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (const void *)offsetof(Vertex, x));
  glVertexAttribPointer(ATTRIB_COLOR, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (const void *)offsetof(Vertex, r));

  // Colours are interpolated across each wall, blending each ring's colour
  // into the next for a continuous gradient down the tunnel.
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->index_buffer);
  glDrawElements(GL_TRIANGLES, INDICES, GL_UNSIGNED_SHORT, (const void *)0);

  glDisable(GL_DEPTH_TEST);
};

// The loader calls this with the context still current, so GL objects can be
// released here.
EXPORT void tcc_scr_free(void *ctx) {
  Tunnel *t = (Tunnel *)ctx;
  if (!t)
    return;

  t->free_gl();
  delete t;
};

} // namespace tunnel
