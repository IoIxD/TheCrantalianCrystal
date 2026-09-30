#include "lock.hpp"

#include "../utils/support.hpp"
#include "xkbcommon_loader.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <pwd.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

TCCLock::TCCLock(TCCClient *client) : mClient(client) {
  mlock(mPassword, sizeof(mPassword));

  mWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (mWakeFd < 0) {
    perror("eventfd");
  }
  mBlinkFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (mBlinkFd < 0) {
    perror("timerfd_create");
  }

  if (passwd *pw = getpwuid(getuid())) {
    mUser = pw->pw_name;
  } else if (const char *user = getenv("USER")) {
    mUser = user;
  }

  if (XKB_LIB && XKB_LIB->handle) {
    mXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  }
}

TCCLock::~TCCLock() {
  clear_password();
  munlock(mPassword, sizeof(mPassword));
  if (mWakeFd >= 0) {
    close(mWakeFd);
  }
  if (mBlinkFd >= 0) {
    close(mBlinkFd);
  }
}

void TCCLock::bind_manager(uint32_t name) {
  mManager = (ext_session_lock_manager_v1 *)wl_registry_bind(
      mClient->mRegistry, name, &ext_session_lock_manager_v1_interface, 1);
}

void TCCLock::bind_dmabuf(uint32_t name, uint32_t version) {
  // Version 3 still tells us the modifiers directly, without the feedback
  // object's format table.
  if (version < 3) {
    return;
  }
  mDmabuf = (zwp_linux_dmabuf_v1 *)wl_registry_bind(
      mClient->mRegistry, name, &zwp_linux_dmabuf_v1_interface, 3);
  zwp_linux_dmabuf_v1_add_listener(mDmabuf, &mDmabufListener, this);
}

void TCCLock::bind_idle_notifier(uint32_t name) {
  mIdleNotifier = (ext_idle_notifier_v1 *)wl_registry_bind(
      mClient->mRegistry, name, &ext_idle_notifier_v1_interface, 1);
}

// Idle as far as the compositor's concerned: it won't be while something's
// inhibiting idling, like a video playing.
void TCCLock::idle_idled(void *data, ext_idle_notification_v1 *notification) {
  TCCLock *self = (TCCLock *)data;
  self->do_lock();
}

void TCCLock::idle_resumed(void *data,
                           ext_idle_notification_v1 *notification) {}

void TCCLock::dmabuf_format(void *data, zwp_linux_dmabuf_v1 *dmabuf,
                            uint32_t format) {}

void TCCLock::dmabuf_modifier(void *data, zwp_linux_dmabuf_v1 *dmabuf,
                              uint32_t format, uint32_t modifier_hi,
                              uint32_t modifier_lo) {
  TCCLock *self = (TCCLock *)data;
  uint64_t modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;
  if (format == SCR_FORMAT && self->mModifiers.size() < SCR_MAX_MODIFIERS &&
      std::find(self->mModifiers.begin(), self->mModifiers.end(), modifier) ==
          self->mModifiers.end()) {
    self->mModifiers.push_back(modifier);
  }
}

void TCCLock::start() {
  mLogind.on_lock = [this]() { do_lock(); };
  mLogind.on_unlock = [this]() { unlock(); };
  mLogind.on_prepare_for_sleep = [this](bool start) {
    if (!start) {
      // Woke back up, get ready for the next time.
      mLogind.take_sleep_inhibitor();
      return;
    }
    mSleepPending = true;
    if (mState == STATE_LOCKED) {
      sleep_ready();
    } else {
      do_lock();
    }
  };

  if (mLogind.connect()) {
    mLogind.take_sleep_inhibitor();
  }
}

void TCCLock::request_lock() {
  mLockRequested = true;
  uint64_t one = 1;
  if (mWakeFd >= 0 && write(mWakeFd, &one, sizeof(one)) < 0) {
    perror("write");
  }
}

void TCCLock::add_poll_fds(std::vector<pollfd> &fds) {
  if (mWakeFd >= 0) {
    fds.push_back({mWakeFd, POLLIN, 0});
  }
  if (mLogind.fd() >= 0) {
    fds.push_back({mLogind.fd(), POLLIN, 0});
  }
  if (mScrFd >= 0) {
    fds.push_back({mScrFd, POLLIN, 0});
  }
  if (mBlinkFd >= 0) {
    fds.push_back({mBlinkFd, POLLIN, 0});
  }
}

void TCCLock::process() {
  uint64_t count;
  while (mWakeFd >= 0 && read(mWakeFd, &count, sizeof(count)) > 0) {
  }

  if (mLockRequested.exchange(false)) {
    do_lock();
  }

  bool ok = false;
  std::string message;
  if (mPam.poll_result(&ok, &message)) {
    if (ok) {
      unlock();
    } else {
      mStatus = message;
      draw_all();
    }
  }

  uint64_t blinks = 0;
  if (mBlinkFd >= 0 && read(mBlinkFd, &blinks, sizeof(blinks)) > 0 &&
      blinks % 2) {
    mCaretVisible = !mCaretVisible;
    draw_all();
  }

  mLogind.process();
  scr_process();
}

// How long the cursor spends on, and then off.
#define BLINK_MS 530

void TCCLock::set_field_focused(bool focused) {
  mFieldFocused = focused;
  if (focused) {
    restart_blink();
    return;
  }
  itimerspec off = {};
  if (mBlinkFd >= 0) {
    timerfd_settime(mBlinkFd, 0, &off, NULL);
  }
  mCaretVisible = false;
}

// Shows the cursor from the start of a blink, e.g. so it doesn't disappear
// while typing.
void TCCLock::restart_blink() {
  if (!mFieldFocused) {
    return;
  }
  mCaretVisible = true;
  itimerspec blink = {};
  blink.it_value.tv_nsec = BLINK_MS * 1000000L;
  blink.it_interval = blink.it_value;
  if (mBlinkFd >= 0) {
    timerfd_settime(mBlinkFd, 0, &blink, NULL);
  }
}

void TCCLock::do_lock() {
  if (mState != STATE_UNLOCKED) {
    return;
  }

  // Don't lock unless we'll be able to unlock again.
  const char *why = nullptr;
  if (!mManager) {
    why = "ext_session_lock_manager_v1 isn't supported by the compositor";
  } else if (!tcc_support::systemd()) {
    why = "the system isn't running systemd";
  } else if (!tcc_support::pam(TCC_PAM_SERVICE)) {
    why = "PAM isn't available (is libpam installed, and "
          "shell/lock/" TCC_PAM_SERVICE ".pam installed as "
          "/etc/pam.d/" TCC_PAM_SERVICE "?)";
  } else if (!mXkbContext) {
    why = "libxkbcommon couldn't be loaded";
  }
  if (why) {
    fprintf(stderr, "not locking the session: %s\n", why);
    sleep_ready();
    return;
  }

  clear_password();
  mStatus.clear();
  mUnlockWhenLocked = false;
  mDialogShown = false;
  mCloseHover = false;
  mCloseHeld = false;
  set_field_focused(false);

  mLock = ext_session_lock_manager_v1_lock(mManager);
  ext_session_lock_v1_add_listener(mLock, &mLockListener, this);
  mState = STATE_PENDING;

  start_screensaver();

  for (TCCClient::Output *output : mClient->outputs()) {
    create_surface(output);
  }
  wl_display_flush(mClient->mDisplay);
}

void TCCLock::unlock() {
  if (mState == STATE_PENDING) {
    // Can't take it back until the compositor answers.
    mUnlockWhenLocked = true;
    return;
  }
  if (mState != STATE_LOCKED) {
    return;
  }

  ext_session_lock_v1_unlock_and_destroy(mLock);
  mLock = nullptr;
  mState = STATE_UNLOCKED;
  stop_screensaver();
  destroy_surfaces();
  clear_password();
  mStatus.clear();
  set_field_focused(false);
  wl_display_flush(mClient->mDisplay);

  mLogind.set_locked_hint(false);
}

// Let logind carry on with going to sleep, if it was waiting on us.
void TCCLock::sleep_ready() {
  if (mSleepPending) {
    mSleepPending = false;
    mLogind.release_sleep_inhibitor();
  }
}

void TCCLock::lock_locked(void *data, ext_session_lock_v1 *lock) {
  TCCLock *self = (TCCLock *)data;
  self->mState = STATE_LOCKED;
  self->mLogind.set_locked_hint(true);
  self->sleep_ready();

  if (self->mUnlockWhenLocked) {
    self->mUnlockWhenLocked = false;
    self->unlock();
  }
}

void TCCLock::lock_finished(void *data, ext_session_lock_v1 *lock) {
  TCCLock *self = (TCCLock *)data;

  if (self->mState == STATE_LOCKED) {
    // The compositor unlocked us some other way.
    ext_session_lock_v1_unlock_and_destroy(lock);
    self->mLogind.set_locked_hint(false);
  } else {
    fprintf(stderr, "the compositor refused to lock the session (is another "
                    "lock screen running?)\n");
    ext_session_lock_v1_destroy(lock);
  }

  self->mLock = nullptr;
  self->mState = STATE_UNLOCKED;
  self->mUnlockWhenLocked = false;
  self->stop_screensaver();
  self->destroy_surfaces();
  self->clear_password();
  self->set_field_focused(false);
  self->sleep_ready();
}

void TCCLock::create_surface(TCCClient::Output *output) {
  if (!mLock || output->wl_output_name == 0 || output->removed) {
    return;
  }
  for (Surface *surface : mSurfaces) {
    if (surface->output == output) {
      return;
    }
  }

  Surface *surface = new Surface();
  surface->lock = this;
  surface->output = output;
  surface->wl_output_id = (wl_output *)wl_registry_bind(
      mClient->mRegistry, output->wl_output_name, &wl_output_interface, 1);
  surface->surface = wl_compositor_create_surface(mClient->mCompositor);
  surface->lock_surface = ext_session_lock_v1_get_lock_surface(
      mLock, surface->surface, surface->wl_output_id);
  ext_session_lock_surface_v1_add_listener(surface->lock_surface,
                                           &mSurfaceListener, surface);
  surface->id = mNextSurfaceId++;
  // The screensaver draws with GL into its buffers, which leaves them upside
  // down.
  wl_surface_set_buffer_transform(surface->surface,
                                  WL_OUTPUT_TRANSFORM_FLIPPED_180);

  // Desynchronized, so the dialog can update without waiting on a
  // screensaver frame.
  surface->dialog_surface = wl_compositor_create_surface(mClient->mCompositor);
  surface->dialog_subsurface = wl_subcompositor_get_subsurface(
      mClient->mSubcompositor, surface->dialog_surface, surface->surface);
  wl_subsurface_set_desync(surface->dialog_subsurface);

  mSurfaces.push_back(surface);
}

void TCCLock::destroy_surface(Surface *surface) {
  for (Seat *seat : mSeats) {
    if (seat->pointer_surface == surface) {
      seat->pointer_surface = nullptr;
    }
  }

  ScrMsg msg;
  msg.type = SCR_MSG_REMOVE;
  msg.output = surface->id;
  scr_send(msg);
  scr_destroy_buffers(surface);
  if (surface->black_buffer) {
    wl_buffer_destroy(surface->black_buffer);
  }

  dialog_unmap(surface);
  wl_subsurface_destroy(surface->dialog_subsurface);
  wl_surface_destroy(surface->dialog_surface);

  ext_session_lock_surface_v1_destroy(surface->lock_surface);
  wl_surface_destroy(surface->surface);
  wl_output_destroy(surface->wl_output_id);

  std::erase(mSurfaces, surface);
  delete surface;
}

void TCCLock::destroy_surfaces() {
  for (Surface *surface : std::vector<Surface *>(mSurfaces)) {
    destroy_surface(surface);
  }
}

// A fully black buffer: zeroes are black in XRGB8888, and a fresh memfd is
// all zeroes without touching any memory.
static wl_buffer *create_black_buffer(wl_shm *shm, int width, int height) {
  int fd = memfd_create("tcc-lock-black", MFD_CLOEXEC);
  if (fd < 0) {
    perror("memfd_create");
    return nullptr;
  }
  int size = width * height * 4;
  if (ftruncate(fd, size) < 0) {
    perror("ftruncate");
    close(fd);
    return nullptr;
  }
  wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
  wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                                width * 4, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);
  return buffer;
}

void TCCLock::show_black(Surface *surface) {
  if (!surface->black_buffer) {
    return;
  }
  wl_surface_attach(surface->surface, surface->black_buffer, 0, 0);
  wl_surface_damage(surface->surface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surface->surface);
}

void TCCLock::surface_configure(void *data,
                                ext_session_lock_surface_v1 *lock_surface,
                                uint32_t serial, uint32_t width,
                                uint32_t height) {
  Surface *surface = (Surface *)data;
  TCCLock *self = surface->lock;

  ext_session_lock_surface_v1_ack_configure(lock_surface, serial);

  if (surface->configured && surface->width == (int)width &&
      surface->height == (int)height) {
    wl_surface_commit(surface->surface);
    return;
  }
  surface->width = width;
  surface->height = height;
  surface->configured = true;

  // Everything sized for the old size goes. The next commit has to be the
  // new size exactly, so it's black until the screensaver catches up.
  self->scr_destroy_buffers(surface);
  if (surface->black_buffer) {
    wl_buffer_destroy(surface->black_buffer);
  }
  surface->black_buffer =
      create_black_buffer(self->mClient->mShm, width, height);

  int dialog_w, dialog_h;
  self->dialog_frame(surface, &surface->dialog_x, &surface->dialog_y,
                     &dialog_w, &dialog_h);
  wl_subsurface_set_position(surface->dialog_subsurface, surface->dialog_x,
                             surface->dialog_y);

  self->show_black(surface);
  self->scr_configure(surface);

  if (self->mDialogShown && !surface->egl_window) {
    self->dialog_map(surface);
  } else {
    // (it's moved, if it's there)
    self->scr_blur(surface);
  }
}

void TCCLock::output_added(TCCClient::Output *output) {
  create_surface(output);
}

void TCCLock::output_removed(TCCClient::Output *output) {
  for (Surface *surface : mSurfaces) {
    if (surface->output == output) {
      destroy_surface(surface);
      return;
    }
  }
}

void TCCLock::clear_password() {
  explicit_bzero(mPassword, sizeof(mPassword));
  mPasswordLen = 0;
}
