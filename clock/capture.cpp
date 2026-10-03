#include "capture.hpp"

#include "ext-image-capture-source-v1-protocol.hpp"
#include "ext-image-copy-capture-v1-protocol.hpp"
#include "wayland-client-protocol.hpp"
#include "xdg-output-unstable-v1-protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

struct Capture {
  uint32_t output_name = 0;

  wl_shm *shm = nullptr;
  wl_output *output = nullptr;
  zxdg_output_manager_v1 *xdg_output_manager = nullptr;
  ext_output_image_capture_source_manager_v1 *source_manager = nullptr;
  ext_image_copy_capture_manager_v1 *copy_manager = nullptr;

  // The output's size in its own pixels, and in logical ones.
  int mode_width = 0, mode_height = 0, scale = 1;
  int logical_width = 0, logical_height = 0;

  // What the session wants the buffer to be.
  int buffer_width = 0, buffer_height = 0;
  std::vector<uint32_t> shm_formats;
  bool session_done = false, stopped = false;

  uint32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
  bool ready = false, failed = false;
};

void registry_global(void *data, wl_registry *registry, uint32_t name,
                     const char *interface, uint32_t version) {
  Capture *cap = (Capture *)data;
  if (strcmp(interface, wl_shm_interface.name) == 0) {
    cap->shm = (wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    if (!cap->output && (cap->output_name == 0 || cap->output_name == name)) {
      cap->output = (wl_output *)wl_registry_bind(
          registry, name, &wl_output_interface, std::min(version, 4u));
    }
  } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
    cap->xdg_output_manager = (zxdg_output_manager_v1 *)wl_registry_bind(
        registry, name, &zxdg_output_manager_v1_interface,
        std::min(version, 3u));
  } else if (strcmp(interface,
                    ext_output_image_capture_source_manager_v1_interface
                        .name) == 0) {
    cap->source_manager =
        (ext_output_image_capture_source_manager_v1 *)wl_registry_bind(
            registry, name,
            &ext_output_image_capture_source_manager_v1_interface, 1);
  } else if (strcmp(interface,
                    ext_image_copy_capture_manager_v1_interface.name) == 0) {
    cap->copy_manager = (ext_image_copy_capture_manager_v1 *)wl_registry_bind(
        registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
  }
}

void registry_global_remove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void output_geometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t,
                     int32_t, const char *, const char *, int32_t) {}

void output_mode(void *data, wl_output *, uint32_t flags, int32_t width,
                 int32_t height, int32_t) {
  Capture *cap = (Capture *)data;
  if (flags & WL_OUTPUT_MODE_CURRENT) {
    cap->mode_width = width;
    cap->mode_height = height;
  }
}

void output_done(void *, wl_output *) {}

void output_scale(void *data, wl_output *, int32_t factor) {
  ((Capture *)data)->scale = std::max(factor, 1);
}

void output_name(void *, wl_output *, const char *) {}

void output_description(void *, wl_output *, const char *) {}

const wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

void xdg_output_logical_position(void *, zxdg_output_v1 *, int32_t, int32_t) {}

void xdg_output_logical_size(void *data, zxdg_output_v1 *, int32_t width,
                             int32_t height) {
  Capture *cap = (Capture *)data;
  cap->logical_width = width;
  cap->logical_height = height;
}

void xdg_output_done(void *, zxdg_output_v1 *) {}

void xdg_output_name(void *, zxdg_output_v1 *, const char *) {}

void xdg_output_description(void *, zxdg_output_v1 *, const char *) {}

const zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_logical_position,
    .logical_size = xdg_output_logical_size,
    .done = xdg_output_done,
    .name = xdg_output_name,
    .description = xdg_output_description,
};

void session_buffer_size(void *data, ext_image_copy_capture_session_v1 *,
                         uint32_t width, uint32_t height) {
  Capture *cap = (Capture *)data;
  cap->buffer_width = width;
  cap->buffer_height = height;
}

void session_shm_format(void *data, ext_image_copy_capture_session_v1 *,
                        uint32_t format) {
  ((Capture *)data)->shm_formats.push_back(format);
}

void session_dmabuf_device(void *, ext_image_copy_capture_session_v1 *,
                           wl_array *) {}

void session_dmabuf_format(void *, ext_image_copy_capture_session_v1 *,
                           uint32_t, wl_array *) {}

void session_done(void *data, ext_image_copy_capture_session_v1 *) {
  ((Capture *)data)->session_done = true;
}

void session_stopped(void *data, ext_image_copy_capture_session_v1 *) {
  ((Capture *)data)->stopped = true;
}

const ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = session_buffer_size,
    .shm_format = session_shm_format,
    .dmabuf_device = session_dmabuf_device,
    .dmabuf_format = session_dmabuf_format,
    .done = session_done,
    .stopped = session_stopped,
};

void frame_transform(void *data, ext_image_copy_capture_frame_v1 *,
                     uint32_t transform) {
  ((Capture *)data)->transform = transform;
}

void frame_damage(void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t,
                  int32_t, int32_t) {}

void frame_presentation_time(void *, ext_image_copy_capture_frame_v1 *,
                             uint32_t, uint32_t, uint32_t) {}

void frame_ready(void *data, ext_image_copy_capture_frame_v1 *) {
  ((Capture *)data)->ready = true;
}

void frame_failed(void *data, ext_image_copy_capture_frame_v1 *, uint32_t) {
  ((Capture *)data)->failed = true;
}

const ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = frame_transform,
    .damage = frame_damage,
    .presentation_time = frame_presentation_time,
    .ready = frame_ready,
    .failed = frame_failed,
};

// Waits (a second at most) for events and handles them.
bool dispatch(wl_display *display) {
  while (wl_display_prepare_read(display) != 0) {
    if (wl_display_dispatch_pending(display) < 0) {
      return false;
    }
  }
  wl_display_flush(display);

  pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
  if (poll(&pfd, 1, 1000) <= 0) {
    wl_display_cancel_read(display);
    return false;
  }
  if (wl_display_read_events(display) < 0) {
    return false;
  }
  return wl_display_dispatch_pending(display) >= 0;
}

// Where red, green and blue are in a pixel of the format, in bytes (little
// endian, as wl_shm formats are).
bool format_layout(uint32_t format, int *r, int *g, int *b) {
  switch (format) {
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XRGB8888:
    *r = 2, *g = 1, *b = 0;
    return true;
  case WL_SHM_FORMAT_ABGR8888:
  case WL_SHM_FORMAT_XBGR8888:
    *r = 0, *g = 1, *b = 2;
    return true;
  default:
    return false;
  }
}

bool capture(wl_display *display, Capture *cap, int x, int y, int width,
             int height, Backdrop *out) {
  if (!cap->shm || !cap->output || !cap->source_manager ||
      !cap->copy_manager) {
    fprintf(stderr, "tcc_clock: the compositor can't capture outputs\n");
    return false;
  }

  wl_output_add_listener(cap->output, &output_listener, cap);
  zxdg_output_v1 *xdg_output = nullptr;
  if (cap->xdg_output_manager) {
    xdg_output = zxdg_output_manager_v1_get_xdg_output(cap->xdg_output_manager,
                                                       cap->output);
    zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener, cap);
  }
  // (for the output's size)
  wl_display_roundtrip(display);

  ext_image_capture_source_v1 *source =
      ext_output_image_capture_source_manager_v1_create_source(
          cap->source_manager, cap->output);
  ext_image_copy_capture_session_v1 *session =
      ext_image_copy_capture_manager_v1_create_session(cap->copy_manager,
                                                       source, 0);
  ext_image_copy_capture_session_v1_add_listener(session, &session_listener,
                                                 cap);

  while (!cap->session_done && !cap->stopped) {
    if (!dispatch(display)) {
      break;
    }
  }

  bool ok = false;
  int r = 0, g = 0, b = 0;
  uint32_t format = 0;
  for (uint32_t f : cap->shm_formats) {
    if (format_layout(f, &r, &g, &b)) {
      format = f;
      ok = true;
      break;
    }
  }

  int bw = cap->buffer_width, bh = cap->buffer_height;
  int stride = bw * 4;
  size_t size = (size_t)stride * bh;
  int fd = -1;
  void *data = MAP_FAILED;
  wl_shm_pool *pool = nullptr;
  wl_buffer *buffer = nullptr;
  ext_image_copy_capture_frame_v1 *frame = nullptr;

  if (!cap->session_done || cap->stopped || bw <= 0 || bh <= 0) {
    fprintf(stderr, "tcc_clock: the capture session didn't start\n");
    ok = false;
    goto out;
  }
  if (!ok) {
    fprintf(stderr, "tcc_clock: no usable format to capture into\n");
    goto out;
  }
  ok = false;

  fd = memfd_create("tcc_clock-capture", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, size) < 0) {
    perror("tcc_clock: shm");
    goto out;
  }
  data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    perror("tcc_clock: mmap");
    goto out;
  }
  pool = wl_shm_create_pool(cap->shm, fd, size);
  buffer = wl_shm_pool_create_buffer(pool, 0, bw, bh, stride, format);

  frame = ext_image_copy_capture_session_v1_create_frame(session);
  ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, cap);
  ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
  ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, bw, bh);
  ext_image_copy_capture_frame_v1_capture(frame);

  while (!cap->ready && !cap->failed) {
    if (!dispatch(display)) {
      break;
    }
  }
  if (!cap->ready) {
    fprintf(stderr, "tcc_clock: capturing the output failed\n");
    goto out;
  }
  if (cap->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
    // todo: rotated/flipped outputs
    fprintf(stderr, "tcc_clock: can't capture transformed outputs yet\n");
    goto out;
  }

  {
    // Output pixels per logical pixel.
    int lw = cap->logical_width;
    if (lw <= 0) {
      lw = cap->mode_width > 0 ? cap->mode_width / cap->scale : bw;
    }
    double scale = (double)bw / lw;
    out->output_width = lw;
    out->output_height = cap->logical_height > 0 ? cap->logical_height
                                                 : (int)std::lround(bh / scale);
    if (width <= 0 || height <= 0) {
      x = 0, y = 0;
      width = out->output_width, height = out->output_height;
    }

    const unsigned char *src = (const unsigned char *)data;
    int ox = (int)std::lround(x * scale), oy = (int)std::lround(y * scale);
    out->width = std::max((int)std::lround(width * scale), 1);
    out->height = std::max((int)std::lround(height * scale), 1);
    out->pixels.resize((size_t)out->width * out->height * 4);
    for (int row = 0; row < out->height; row++) {
      int sy = std::clamp(oy + row, 0, bh - 1);
      for (int col = 0; col < out->width; col++) {
        int sx = std::clamp(ox + col, 0, bw - 1);
        const unsigned char *p = src + (size_t)sy * stride + sx * 4;
        unsigned char *d = &out->pixels[((size_t)row * out->width + col) * 4];
        d[0] = p[r];
        d[1] = p[g];
        d[2] = p[b];
        d[3] = 255;
      }
    }
    ok = true;
  }

out:
  if (frame)
    ext_image_copy_capture_frame_v1_destroy(frame);
  if (buffer)
    wl_buffer_destroy(buffer);
  if (pool)
    wl_shm_pool_destroy(pool);
  if (data != MAP_FAILED)
    munmap(data, size);
  if (fd >= 0)
    close(fd);
  ext_image_copy_capture_session_v1_destroy(session);
  ext_image_capture_source_v1_destroy(source);
  if (xdg_output)
    zxdg_output_v1_destroy(xdg_output);
  return ok;
}

} // namespace

bool capture_backdrop(uint32_t output_name, int x, int y, int width,
                      int height, Backdrop *out) {
  wl_display *display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "tcc_clock: couldn't connect to the compositor\n");
    return false;
  }

  Capture cap;
  cap.output_name = output_name;
  wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &cap);
  wl_display_roundtrip(display);

  bool ok = capture(display, &cap, x, y, width, height, out);

  if (cap.copy_manager)
    ext_image_copy_capture_manager_v1_destroy(cap.copy_manager);
  if (cap.source_manager)
    ext_output_image_capture_source_manager_v1_destroy(cap.source_manager);
  if (cap.xdg_output_manager)
    zxdg_output_manager_v1_destroy(cap.xdg_output_manager);
  if (cap.output)
    wl_proxy_destroy((wl_proxy *)cap.output);
  if (cap.shm)
    wl_proxy_destroy((wl_proxy *)cap.shm);
  wl_proxy_destroy((wl_proxy *)registry);
  wl_display_disconnect(display);
  return ok;
}
