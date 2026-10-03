#include "clipboard.hpp"

#include "ext-data-control-v1-protocol.hpp"
#include "wayland-client-protocol.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {

void put_u32(std::vector<unsigned char> &out, uint32_t v) {
  out.push_back(v >> 24);
  out.push_back(v >> 16);
  out.push_back(v >> 8);
  out.push_back(v);
}

uint32_t crc32(const unsigned char *data, size_t length) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) {
        c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
      }
      table[i] = c;
    }
  }
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFF;
}

void put_chunk(std::vector<unsigned char> &out, const char *type,
               const std::vector<unsigned char> &data) {
  put_u32(out, data.size());
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put_u32(out, crc32(&out[start], out.size() - start));
}

} // namespace

std::vector<unsigned char> encode_png(const unsigned char *rgba, int width,
                                      int height) {
  // The rows, each starting with its filter (none), as RGB.
  std::vector<unsigned char> raw;
  raw.reserve((size_t)(width * 3 + 1) * height);
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    for (int x = 0; x < width; x++) {
      const unsigned char *p = rgba + ((size_t)y * width + x) * 4;
      raw.insert(raw.end(), p, p + 3);
    }
  }

  // Wrapped in zlib as stored (uncompressed) deflate blocks.
  std::vector<unsigned char> zlib = {0x78, 0x01};
  size_t at = 0;
  do {
    size_t length = std::min<size_t>(raw.size() - at, 65535);
    zlib.push_back(at + length == raw.size());
    zlib.push_back(length);
    zlib.push_back(length >> 8);
    zlib.push_back(~length);
    zlib.push_back(~length >> 8);
    zlib.insert(zlib.end(), raw.begin() + at, raw.begin() + at + length);
    at += length;
  } while (at < raw.size());
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put_u32(zlib, b << 16 | a);

  std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<unsigned char> header;
  put_u32(header, width);
  put_u32(header, height);
  // (8 bits per channel, RGB, and the only compression, filtering and
  // interlacing there are)
  header.insert(header.end(), {8, 2, 0, 0, 0});
  put_chunk(png, "IHDR", header);
  put_chunk(png, "IDAT", zlib);
  put_chunk(png, "IEND", {});
  return png;
}

namespace {

const char *PNG_TYPE = "image/png";

struct Clipboard {
  wl_seat *seat = nullptr;
  ext_data_control_manager_v1 *manager = nullptr;
  std::vector<unsigned char> png;
  bool cancelled = false;
};

void registry_global(void *data, wl_registry *registry, uint32_t name,
                     const char *interface, uint32_t version) {
  Clipboard *clip = (Clipboard *)data;
  if (strcmp(interface, wl_seat_interface.name) == 0 && !clip->seat) {
    clip->seat =
        (wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface, 1);
  } else if (strcmp(interface, ext_data_control_manager_v1_interface.name) ==
             0) {
    clip->manager = (ext_data_control_manager_v1 *)wl_registry_bind(
        registry, name, &ext_data_control_manager_v1_interface, 1);
  }
}

void registry_global_remove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void source_send(void *data, ext_data_control_source_v1 *,
                 const char *mime_type, int32_t fd) {
  Clipboard *clip = (Clipboard *)data;
  if (strcmp(mime_type, PNG_TYPE) == 0) {
    // (whoever's pasting reads it all at their own pace)
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    size_t at = 0;
    while (at < clip->png.size()) {
      ssize_t n = write(fd, clip->png.data() + at, clip->png.size() - at);
      if (n <= 0) {
        break;
      }
      at += n;
    }
  }
  close(fd);
}

void source_cancelled(void *data, ext_data_control_source_v1 *) {
  ((Clipboard *)data)->cancelled = true;
}

const ext_data_control_source_v1_listener source_listener = {
    .send = source_send,
    .cancelled = source_cancelled,
};

// Waits for events and handles them.
bool dispatch(wl_display *display) {
  while (wl_display_prepare_read(display) != 0) {
    if (wl_display_dispatch_pending(display) < 0) {
      return false;
    }
  }
  wl_display_flush(display);

  pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
  if (poll(&pfd, 1, -1) <= 0) {
    wl_display_cancel_read(display);
    return false;
  }
  if (wl_display_read_events(display) < 0) {
    return false;
  }
  return wl_display_dispatch_pending(display) >= 0;
}

// Holds the clipboard until something else takes it.
int serve(std::vector<unsigned char> png) {
  wl_display *display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "tcc_snip: couldn't connect to the compositor\n");
    return 1;
  }

  Clipboard clip;
  clip.png = std::move(png);
  wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &clip);
  wl_display_roundtrip(display);
  if (!clip.seat || !clip.manager) {
    fprintf(stderr, "tcc_snip: the compositor can't set the clipboard\n");
    return 1;
  }

  ext_data_control_device_v1 *device =
      ext_data_control_manager_v1_get_data_device(clip.manager, clip.seat);
  ext_data_control_source_v1 *source =
      ext_data_control_manager_v1_create_data_source(clip.manager);
  ext_data_control_source_v1_add_listener(source, &source_listener, &clip);
  ext_data_control_source_v1_offer(source, PNG_TYPE);
  ext_data_control_device_v1_set_selection(device, source);

  while (!clip.cancelled && dispatch(display)) {
  }

  ext_data_control_source_v1_destroy(source);
  ext_data_control_device_v1_destroy(device);
  wl_display_disconnect(display);
  return 0;
}

} // namespace

bool copy_png_to_clipboard(std::vector<unsigned char> png) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("tcc_snip: fork");
    return false;
  }
  if (pid > 0) {
    return true;
  }

  // Nothing the snip had open stays open here; its connection to the
  // compositor especially, or its window would hang around as long as this.
  close_range(3, ~0U, 0);
  setsid();
  // (in case whoever's pasting goes away part way through)
  signal(SIGPIPE, SIG_IGN);
  _exit(serve(std::move(png)));
}
