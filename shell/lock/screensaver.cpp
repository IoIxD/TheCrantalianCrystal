#include "lock.hpp"

#include "../client/ssd.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/socket.h>
#include <unistd.h>

/*
 * The shell's end of the screensaver loader (see scr_ipc.hpp).
 *
 * The loader runs third party code, so nothing it sends is trusted: anything
 * off gets it stopped, and the lock screen goes black. In particular a buffer
 * the compositor would object to has to be caught here, since a protocol
 * error would take the whole shell's connection down with it.
 */

void TCCLock::start_screensaver() {
  if (!mDmabuf || mModifiers.empty()) {
    fprintf(stderr, "lock: no usable zwp_linux_dmabuf_v1, so no "
                    "screensaver\n");
    return;
  }

  // It's installed next to us.
  char exe[PATH_MAX] = {};
  if (readlink("/proc/self/exe", exe, sizeof(exe) - 1) < 0) {
    perror("readlink");
    return;
  }
  std::string path =
      (std::filesystem::path(exe).parent_path() / "tcc_scr_loader").string();

  int fds[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0,
                 fds) < 0) {
    perror("socketpair");
    return;
  }
  // (worked out before forking, only async-signal-safe calls are allowed
  // after it)
  std::string fd_arg = std::to_string(fds[1]);

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  if (pid == 0) {
    // The loader's end is the only thing it inherits, and blocking.
    fcntl(fds[1], F_SETFD, 0);
    fcntl(fds[1], F_SETFL, 0);
    const char *argv[] = {path.c_str(), fd_arg.c_str(), NULL};
    execv(argv[0], (char **)argv);
    _exit(127);
  }
  close(fds[1]);

  mScrPid = pid;
  mScrFd = fds[0];
  // Reaped along with everything else the shell starts.
  mClient->mChildren.push_back(pid);

  ScrMsg msg;
  msg.type = SCR_MSG_MODIFIERS;
  msg.n_modifiers = mModifiers.size();
  std::copy(mModifiers.begin(), mModifiers.end(), msg.modifiers);
  scr_send(msg);
}

void TCCLock::stop_screensaver() {
  if (mScrFd >= 0) {
    // Closing our end is its cue to exit, but it may be hung.
    close(mScrFd);
    mScrFd = -1;
    kill(mScrPid, SIGTERM);
    mScrPid = -1;
  }
  for (Surface *surface : mSurfaces) {
    scr_destroy_buffers(surface);
  }
}

void TCCLock::screensaver_failed(const char *why) {
  if (mScrFd < 0) {
    return;
  }
  fprintf(stderr, "lock: stopping the screensaver: %s\n", why);
  stop_screensaver();
  for (Surface *surface : mSurfaces) {
    if (surface->configured) {
      show_black(surface);
    }
  }
}

void TCCLock::scr_send(const ScrMsg &msg) {
  if (mScrFd < 0) {
    return;
  }
  if (send(mScrFd, &msg, sizeof(msg), MSG_DONTWAIT | MSG_NOSIGNAL) < 0 &&
      errno != EAGAIN) {
    // (EAGAIN means it's stopped reading, i.e. hung, in which case the
    // message can go: the screen just stops changing.)
    screensaver_failed(strerror(errno));
  }
}

TCCLock::Surface *TCCLock::surface_from_id(uint32_t id) {
  for (Surface *surface : mSurfaces) {
    if (surface->id == id) {
      return surface;
    }
  }
  return nullptr;
}

void TCCLock::scr_configure(Surface *surface) {
  ScrMsg msg;
  msg.type = SCR_MSG_CONFIGURE;
  msg.output = surface->id;
  msg.width = surface->width;
  msg.height = surface->height;
  scr_send(msg);
}

// Has the loader blur what's under the dialog, if it's up, so its glass has
// something frosted behind it.
void TCCLock::scr_blur(Surface *surface) {
  ScrMsg msg;
  msg.type = SCR_MSG_BLUR;
  msg.output = surface->id;
  if (surface->egl_window) {
    dialog_frame(surface, &msg.x, &msg.y, &msg.width, &msg.height);
    // Matching the decoration's rounded corners.
    msg.radius = SSD_CORNER_RADIUS;
  }
  scr_send(msg);
}

void TCCLock::scr_destroy_buffers(Surface *surface) {
  if (surface->frame_callback) {
    wl_callback_destroy(surface->frame_callback);
    surface->frame_callback = nullptr;
  }
  for (ScrBuffer *&buffer : surface->scr_buffers) {
    if (!buffer) {
      continue;
    }
    if (buffer->params) {
      zwp_linux_buffer_params_v1_destroy(buffer->params);
    }
    if (buffer->buffer) {
      wl_buffer_destroy(buffer->buffer);
    }
    delete buffer;
    buffer = nullptr;
  }
}

void TCCLock::scr_process() {
  while (mScrFd >= 0) {
    ScrMsg msg;
    iovec iov = {&msg, sizeof(msg)};
    char control[CMSG_SPACE(sizeof(int))];
    msghdr hdr = {};
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = control;
    hdr.msg_controllen = sizeof(control);

    ssize_t n = recvmsg(mScrFd, &hdr, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      return;
    }
    if (n <= 0) {
      screensaver_failed(n == 0 ? "it exited" : strerror(errno));
      return;
    }

    int fd = -1;
    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&hdr); cmsg;
         cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
          cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
        memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
      }
    }

    bool ok = n == sizeof(msg) && !(hdr.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
              scr_handle(msg, fd);
    if (!ok) {
      if (fd >= 0) {
        close(fd);
      }
      screensaver_failed("it sent something bogus");
      return;
    }
  }
}

// Takes ownership of fd. Returns false if the loader's misbehaving.
bool TCCLock::scr_handle(const ScrMsg &msg, int fd) {
  if (msg.type != SCR_MSG_BUFFER && fd >= 0) {
    return false;
  }
  if (msg.buffer >= SCR_MAX_BUFFERS) {
    return false;
  }

  Surface *surface = surface_from_id(msg.output);

  switch (msg.type) {
  case SCR_MSG_BUFFER:
    if (fd < 0) {
      return false;
    }
    if (!surface) {
      // Removed since, fine.
      close(fd);
      return true;
    }
    return scr_handle_buffer(surface, msg, fd);

  case SCR_MSG_READY: {
    if (!surface) {
      return true;
    }
    ScrBuffer *buffer = surface->scr_buffers[msg.buffer];
    // (it could be for a size we've since moved on from)
    if (!buffer || buffer->width != surface->width ||
        buffer->height != surface->height) {
      return true;
    }
    if (buffer->buffer) {
      scr_present(buffer);
    } else {
      buffer->ready_pending = true;
    }
    return true;
  }

  default:
    return false;
  }
}

bool TCCLock::scr_handle_buffer(Surface *surface, const ScrMsg &msg, int fd) {
  if (msg.width != surface->width || msg.height != surface->height) {
    // For a size we've since moved on from.
    close(fd);
    return true;
  }

  // Everything the compositor would treat as a fatal error, checked the way
  // it would: the layout has to be one it advertised, and the buffer has to
  // actually be big enough.
  if (std::find(mModifiers.begin(), mModifiers.end(), msg.modifier) ==
      mModifiers.end()) {
    close(fd);
    return false;
  }
  off_t size = lseek(fd, 0, SEEK_END);
  uint64_t needed = (uint64_t)msg.offset + (uint64_t)msg.stride * msg.height;
  if (size <= 0 || msg.stride < (uint64_t)msg.width * 4 ||
      needed > (uint64_t)size) {
    close(fd);
    return false;
  }

  ScrBuffer *&slot = surface->scr_buffers[msg.buffer];
  if (slot) {
    if (slot->params) {
      zwp_linux_buffer_params_v1_destroy(slot->params);
    }
    if (slot->buffer) {
      wl_buffer_destroy(slot->buffer);
    }
    delete slot;
  }
  ScrBuffer *buffer = new ScrBuffer();
  buffer->surface = surface;
  buffer->id = msg.buffer;
  buffer->width = msg.width;
  buffer->height = msg.height;
  slot = buffer;

  // Imported with create rather than create_immed, so a failure is an event
  // rather than a protocol error.
  buffer->params = zwp_linux_dmabuf_v1_create_params(mDmabuf);
  zwp_linux_buffer_params_v1_add_listener(buffer->params, &mParamsListener,
                                          buffer);
  zwp_linux_buffer_params_v1_add(buffer->params, fd, 0, msg.offset,
                                 msg.stride, msg.modifier >> 32,
                                 msg.modifier & 0xffffffff);
  // (the request has its own copy of it)
  close(fd);
  zwp_linux_buffer_params_v1_create(buffer->params, msg.width, msg.height,
                                    SCR_FORMAT, 0);
  return true;
}

void TCCLock::params_created(void *data, zwp_linux_buffer_params_v1 *params,
                             wl_buffer *wl_buffer) {
  ScrBuffer *buffer = (ScrBuffer *)data;
  TCCLock *self = buffer->surface->lock;

  zwp_linux_buffer_params_v1_destroy(params);
  buffer->params = nullptr;
  buffer->buffer = wl_buffer;
  wl_buffer_add_listener(wl_buffer, &self->mBufferListener, buffer);

  if (buffer->ready_pending) {
    buffer->ready_pending = false;
    self->scr_present(buffer);
  }
}

void TCCLock::params_failed(void *data, zwp_linux_buffer_params_v1 *params) {
  ScrBuffer *buffer = (ScrBuffer *)data;
  // (takes the params with it)
  buffer->surface->lock->screensaver_failed(
      "the compositor couldn't import its buffers");
}

void TCCLock::scr_present(ScrBuffer *buffer) {
  Surface *surface = buffer->surface;
  // Anything else would be a protocol error for a lock surface.
  if (buffer->width != surface->width || buffer->height != surface->height) {
    return;
  }

  wl_surface_attach(surface->surface, buffer->buffer, 0, 0);
  wl_surface_damage(surface->surface, 0, 0, INT32_MAX, INT32_MAX);
  if (!surface->frame_callback) {
    surface->frame_callback = wl_surface_frame(surface->surface);
    wl_callback_add_listener(surface->frame_callback, &mFrameListener,
                             surface);
  }
  wl_surface_commit(surface->surface);
}

// The frame's been shown, so it's time for the next one.
void TCCLock::frame_done(void *data, wl_callback *callback, uint32_t time) {
  Surface *surface = (Surface *)data;
  wl_callback_destroy(callback);
  surface->frame_callback = nullptr;

  ScrMsg msg;
  msg.type = SCR_MSG_FRAME;
  msg.output = surface->id;
  surface->lock->scr_send(msg);
}

void TCCLock::buffer_release(void *data, wl_buffer *wl_buffer) {
  ScrBuffer *buffer = (ScrBuffer *)data;

  ScrMsg msg;
  msg.type = SCR_MSG_RELEASE;
  msg.output = buffer->surface->id;
  msg.buffer = buffer->id;
  buffer->surface->lock->scr_send(msg);
}
