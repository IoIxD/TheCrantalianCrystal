#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <poll.h>

#include "../client/client.hpp"
#include "../protocol/ext-idle-notify-v1-protocol.hpp"
#include "../protocol/ext-session-lock-v1.hpp"
#include "../protocol/linux-dmabuf-v1-protocol.hpp"
#include "../utils/glyph.hpp"

#include "egl_loader.hpp"
#include "scr_ipc.hpp"

#include "registry.hpp"

#include "logind.hpp"
#include "pam.hpp"

/*
 * Locks the session with ext_session_lock_v1, and unlocks it again once the
 * user's password checks out with PAM.
 *
 * Each lock surface shows frames from the screensaver loader, a separate
 * process (see scr_ipc.hpp), or black if it isn't running. The dialog is a
 * subsurface on top, drawn with EGL, and unmapped while hidden.
 *
 * Locking can be asked for from any thread (request_lock), everything else
 * happens on the shell's main thread.
 */
class TCCLock {
  enum State {
    STATE_UNLOCKED,
    // Asked the compositor to lock, waiting on locked or finished.
    STATE_PENDING,
    STATE_LOCKED,
  };

  struct Surface;

  // A frame buffer from the screensaver loader.
  struct ScrBuffer {
    Surface *surface = nullptr;
    uint32_t id = 0;
    int width = 0, height = 0;
    // While the compositor's importing it.
    zwp_linux_buffer_params_v1 *params = nullptr;
    wl_buffer *buffer = nullptr;
    // The loader said it was ready before the import finished.
    bool ready_pending = false;
  };

  struct Surface {
    TCCLock *lock = nullptr;
    TCCClient::Output *output = nullptr;
    // What the screensaver loader knows this surface by.
    uint32_t id = 0;
    wl_output *wl_output_id = nullptr;
    wl_surface *surface = nullptr;
    ext_session_lock_surface_v1 *lock_surface = nullptr;
    int width = 0, height = 0;
    bool configured = false;

    // Shown when there's no screensaver frame, at width x height.
    wl_buffer *black_buffer = nullptr;
    ScrBuffer *scr_buffers[SCR_MAX_BUFFERS] = {};
    // Pending while waiting to ask the loader for the next frame.
    wl_callback *frame_callback = nullptr;

    wl_surface *dialog_surface = nullptr;
    wl_subsurface *dialog_subsurface = nullptr;
    // Where the dialog is on the lock surface.
    int dialog_x = 0, dialog_y = 0;
    // Only while the dialog is shown.
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
  };

  // Our own input objects for one of the shell's seats.
  struct Seat {
    TCCLock *lock = nullptr;
    wl_seat *seat = nullptr;

    wl_keyboard *keyboard = nullptr;
    struct xkb_keymap *keymap = nullptr;
    struct xkb_state *state = nullptr;

    wl_pointer *pointer = nullptr;
    // Only set if the compositor supports wp_cursor_shape_manager_v1.
    wp_cursor_shape_device_v1 *cursor_shape_device = nullptr;
    // The lock surface the pointer is over (or its dialog), and where on the
    // lock surface.
    Surface *pointer_surface = nullptr;
    bool pointer_on_dialog = false;
    uint32_t pointer_serial = 0;
    double pointer_x = 0, pointer_y = 0;
    // Where the pointer has to move away from to bring the dialog up.
    double wake_x = 0, wake_y = 0;

    // Tells us once the seat's been idle for TCC_LOCK_IDLE_MS.
    ext_idle_notification_v1 *idle_notification = nullptr;
  };

  TCCClient *mClient;

  ext_session_lock_manager_v1 *mManager = nullptr;
  zwp_linux_dmabuf_v1 *mDmabuf = nullptr;
  ext_idle_notifier_v1 *mIdleNotifier = nullptr;
  // Modifiers the compositor takes for SCR_FORMAT.
  std::vector<uint64_t> mModifiers;
  ext_session_lock_v1 *mLock = nullptr;
  State mState = STATE_UNLOCKED;
  // Something asked to unlock before the compositor said we were locked.
  bool mUnlockWhenLocked = false;
  // logind is waiting on us to lock before the system goes to sleep.
  bool mSleepPending = false;

  std::vector<Surface *> mSurfaces;
  uint32_t mNextSurfaceId = 1;
  std::vector<Seat *> mSeats;
  struct xkb_context *mXkbContext = nullptr;

  TCCRegistryConnection *mRegistry;

  // Wakes the main loop up for request_lock and finished PAM conversations.
  int mWakeFd = -1;
  std::atomic<bool> mLockRequested = false;

  EGLDisplay mEGLDisplay = EGL_NO_DISPLAY;
  EGLConfig mEGLConfig = nullptr;
  EGLContext mEGLContext = EGL_NO_CONTEXT;
  GlyphManager mGlyphManager;
  // For drawing the dialog with the same decorations as windows.
  GLuint mSSDProgram = 0;
  GLuint mIconTexture = 0;
  bool mIconLoaded = false;

  std::string mUser;
  // What's been typed so far. Locked into memory so it never gets swapped.
  char mPassword[TCC_PASSWORD_MAX];
  size_t mPasswordLen = 0;
  // Shown under the password box, e.g. why the last attempt failed.
  std::string mStatus;
  // The dialog only shows up once there's been some input.
  bool mDialogShown = false;
  bool mCloseHover = false;
  bool mCloseHeld = false;
  // The password box has been clicked on, so it shows a blinking cursor.
  bool mFieldFocused = false;
  bool mCaretVisible = false;
  // Blinks it.
  int mBlinkFd = -1;

  // The screensaver loader, while it's running.
  pid_t mScrPid = -1;
  int mScrFd = -1;

  TCCPamAuth mPam;
  TCCLogind mLogind;

  void do_lock();
  void unlock();
  void sleep_ready();

  void create_surface(TCCClient::Output *output);
  void destroy_surface(Surface *surface);
  void destroy_surfaces();
  void show_black(Surface *surface);

  // In screensaver.cpp
  void start_screensaver();
  void stop_screensaver();
  void screensaver_failed(const char *why);
  void scr_send(const ScrMsg &msg);
  void scr_process();
  bool scr_handle(const ScrMsg &msg, int fd);
  bool scr_handle_buffer(Surface *surface, const ScrMsg &msg, int fd);
  void scr_present(ScrBuffer *buffer);
  void scr_configure(Surface *surface);
  void scr_blur(Surface *surface);
  void scr_destroy_buffers(Surface *surface);
  Surface *surface_from_id(uint32_t id);

  bool setup_egl();
  void setup_gl();
  void dialog_frame(Surface *surface, int *x, int *y, int *width, int *height);
  bool close_button_contains(Surface *surface, double x, double y);
  bool field_contains(Surface *surface, double x, double y);
  void set_field_focused(bool focused);
  void restart_blink();
  void draw(Surface *surface);
  void draw_all();
  void dialog_map(Surface *surface);
  void dialog_unmap(Surface *surface);

  Surface *surface_from_wl_surface(wl_surface *surface, bool *dialog);
  void seat_destroy_keyboard(Seat *seat);
  void seat_destroy_pointer(Seat *seat);
  void seat_update_cursor(Seat *seat);
  void show_dialog();
  void hide_dialog();
  void handle_key(Seat *seat, uint32_t keycode);
  void pointer_moved(Seat *seat, wl_fixed_t x, wl_fixed_t y);
  void handle_pointer_motion(Seat *seat);
  void handle_pointer_button(Seat *seat, bool pressed);
  void submit();
  void clear_password();

  static void lock_locked(void *data, ext_session_lock_v1 *lock);
  static void lock_finished(void *data, ext_session_lock_v1 *lock);
  const ext_session_lock_v1_listener mLockListener = {
      .locked = lock_locked,
      .finished = lock_finished,
  };

  static void surface_configure(void *data,
                                ext_session_lock_surface_v1 *lock_surface,
                                uint32_t serial, uint32_t width,
                                uint32_t height);
  const ext_session_lock_surface_v1_listener mSurfaceListener = {
      .configure = surface_configure,
  };

  static void frame_done(void *data, wl_callback *callback, uint32_t time);
  const wl_callback_listener mFrameListener = {
      .done = frame_done,
  };

  static void dmabuf_format(void *data, zwp_linux_dmabuf_v1 *dmabuf,
                            uint32_t format);
  static void dmabuf_modifier(void *data, zwp_linux_dmabuf_v1 *dmabuf,
                              uint32_t format, uint32_t modifier_hi,
                              uint32_t modifier_lo);
  const zwp_linux_dmabuf_v1_listener mDmabufListener = {
      .format = dmabuf_format,
      .modifier = dmabuf_modifier,
  };

  static void params_created(void *data, zwp_linux_buffer_params_v1 *params,
                             wl_buffer *buffer);
  static void params_failed(void *data, zwp_linux_buffer_params_v1 *params);
  const zwp_linux_buffer_params_v1_listener mParamsListener = {
      .created = params_created,
      .failed = params_failed,
  };

  static void idle_idled(void *data, ext_idle_notification_v1 *notification);
  static void idle_resumed(void *data, ext_idle_notification_v1 *notification);
  const ext_idle_notification_v1_listener mIdleListener = {
      .idled = idle_idled,
      .resumed = idle_resumed,
  };

  static void buffer_release(void *data, wl_buffer *buffer);
  const wl_buffer_listener mBufferListener = {
      .release = buffer_release,
  };

  static void keyboard_keymap(void *data, wl_keyboard *keyboard,
                              uint32_t format, int32_t fd, uint32_t size);
  static void keyboard_enter(void *data, wl_keyboard *keyboard, uint32_t serial,
                             wl_surface *surface, wl_array *keys);
  static void keyboard_leave(void *data, wl_keyboard *keyboard, uint32_t serial,
                             wl_surface *surface);
  static void keyboard_key(void *data, wl_keyboard *keyboard, uint32_t serial,
                           uint32_t time, uint32_t key, uint32_t state);
  static void keyboard_modifiers(void *data, wl_keyboard *keyboard,
                                 uint32_t serial, uint32_t mods_depressed,
                                 uint32_t mods_latched, uint32_t mods_locked,
                                 uint32_t group);
  static void keyboard_repeat_info(void *data, wl_keyboard *keyboard,
                                   int32_t rate, int32_t delay);
  const wl_keyboard_listener mKeyboardListener = {
      .keymap = keyboard_keymap,
      .enter = keyboard_enter,
      .leave = keyboard_leave,
      .key = keyboard_key,
      .modifiers = keyboard_modifiers,
      .repeat_info = keyboard_repeat_info,
  };

  static void pointer_enter(void *data, wl_pointer *pointer, uint32_t serial,
                            wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
  static void pointer_leave(void *data, wl_pointer *pointer, uint32_t serial,
                            wl_surface *surface);
  static void pointer_motion(void *data, wl_pointer *pointer, uint32_t time,
                             wl_fixed_t x, wl_fixed_t y);
  static void pointer_button(void *data, wl_pointer *pointer, uint32_t serial,
                             uint32_t time, uint32_t button, uint32_t state);
  static void pointer_axis(void *data, wl_pointer *pointer, uint32_t time,
                           uint32_t axis, wl_fixed_t value);
  const wl_pointer_listener mPointerListener = {
      .enter = pointer_enter,
      .leave = pointer_leave,
      .motion = pointer_motion,
      .button = pointer_button,
      .axis = pointer_axis,
  };

public:
  TCCLock(TCCClient *client);
  ~TCCLock();

  // Called for the ext_session_lock_manager_v1 global.
  void bind_manager(uint32_t name);
  // Called for the zwp_linux_dmabuf_v1 global.
  void bind_dmabuf(uint32_t name, uint32_t version);
  // Called for the ext_idle_notifier_v1 global.
  void bind_idle_notifier(uint32_t name);
  // Called once the client is connected, hooks up logind.
  void start();

  // Safe to call from any thread.
  void request_lock();

  // Main loop integration: add_poll_fds before poll(), process() after.
  void add_poll_fds(std::vector<pollfd> &fds);
  void process();

  void output_added(TCCClient::Output *output);
  void output_removed(TCCClient::Output *output);
  void seat_capabilities(wl_seat *seat, uint32_t capabilities);
  void seat_removed(wl_seat *seat);
};
