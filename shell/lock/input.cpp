#include "lock.hpp"

#include "xkbcommon_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

// How far the pointer has to travel before it counts as the user waking the
// screen up, so a jittery mouse or a synthetic motion event doesn't.
constexpr double WAKE_DISTANCE = 16.0;

/*
 * We get our own wl_keyboard and wl_pointer for each of the shell's seats,
 * separate from the ones the window manager uses. Events for surfaces that
 * aren't ours (like decorations) come to them too, and get ignored.
 */

void TCCLock::seat_capabilities(wl_seat *wl_seat, uint32_t capabilities) {
  bool has_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
  bool has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

  auto it = std::find_if(mSeats.begin(), mSeats.end(),
                         [wl_seat](Seat *s) { return s->seat == wl_seat; });
  Seat *seat = nullptr;
  if (it != mSeats.end()) {
    seat = *it;
  } else {
    seat = new Seat();
    seat->lock = this;
    seat->seat = wl_seat;
    mSeats.push_back(seat);

    if (mIdleNotifier) {
      seat->idle_notification = ext_idle_notifier_v1_get_idle_notification(
          mIdleNotifier, TCC_LOCK_IDLE_MS, wl_seat);
      ext_idle_notification_v1_add_listener(seat->idle_notification,
                                            &mIdleListener, this);
    }
  }

  if (has_keyboard && !seat->keyboard && mXkbContext) {
    seat->keyboard = wl_seat_get_keyboard(wl_seat);
    wl_keyboard_add_listener(seat->keyboard, &mKeyboardListener, seat);
  } else if (!has_keyboard && seat->keyboard) {
    seat_destroy_keyboard(seat);
  }

  if (has_pointer && !seat->pointer) {
    seat->pointer = wl_seat_get_pointer(wl_seat);
    wl_pointer_add_listener(seat->pointer, &mPointerListener, seat);
    if (mClient->mCursorShapeManager) {
      seat->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(
          mClient->mCursorShapeManager, seat->pointer);
    }
  } else if (!has_pointer && seat->pointer) {
    seat_destroy_pointer(seat);
  }
}

void TCCLock::seat_removed(wl_seat *wl_seat) {
  for (Seat *seat : std::vector<Seat *>(mSeats)) {
    if (seat->seat == wl_seat) {
      seat_destroy_keyboard(seat);
      seat_destroy_pointer(seat);
      if (seat->idle_notification) {
        ext_idle_notification_v1_destroy(seat->idle_notification);
      }
      std::erase(mSeats, seat);
      delete seat;
    }
  }
}

void TCCLock::seat_destroy_keyboard(Seat *seat) {
  if (seat->state) {
    xkb_state_unref(seat->state);
    seat->state = nullptr;
  }
  if (seat->keymap) {
    xkb_keymap_unref(seat->keymap);
    seat->keymap = nullptr;
  }
  if (seat->keyboard) {
    if (wl_keyboard_get_version(seat->keyboard) >=
        WL_KEYBOARD_RELEASE_SINCE_VERSION)
      wl_keyboard_release(seat->keyboard);
    else
      wl_keyboard_destroy(seat->keyboard);
    seat->keyboard = nullptr;
  }
}

void TCCLock::seat_destroy_pointer(Seat *seat) {
  if (seat->cursor_shape_device) {
    wp_cursor_shape_device_v1_destroy(seat->cursor_shape_device);
    seat->cursor_shape_device = nullptr;
  }
  if (seat->pointer) {
    if (wl_pointer_get_version(seat->pointer) >=
        WL_POINTER_RELEASE_SINCE_VERSION)
      wl_pointer_release(seat->pointer);
    else
      wl_pointer_destroy(seat->pointer);
    seat->pointer = nullptr;
  }
  seat->pointer_surface = nullptr;
}

// Also finds the surface a dialog belongs to, setting dialog if it's that.
TCCLock::Surface *TCCLock::surface_from_wl_surface(wl_surface *wl_surface,
                                                   bool *dialog) {
  for (Surface *surface : mSurfaces) {
    if (surface->surface == wl_surface ||
        surface->dialog_surface == wl_surface) {
      *dialog = surface->dialog_surface == wl_surface;
      return surface;
    }
  }
  return nullptr;
}

// No cursor until the dialog is up.
void TCCLock::seat_update_cursor(Seat *seat) {
  if (!seat->pointer || !seat->pointer_surface) {
    return;
  }
  if (!mDialogShown) {
    wl_pointer_set_cursor(seat->pointer, seat->pointer_serial, NULL, 0, 0);
  } else if (seat->cursor_shape_device) {
    wp_cursor_shape_device_v1_set_shape(
        seat->cursor_shape_device, seat->pointer_serial,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
  }
}

void TCCLock::show_dialog() {
  if (mDialogShown) {
    return;
  }
  mDialogShown = true;
  for (Seat *seat : mSeats) {
    seat_update_cursor(seat);
  }
  for (Surface *surface : mSurfaces) {
    if (surface->configured) {
      dialog_map(surface);
    }
  }
}

void TCCLock::hide_dialog() {
  mDialogShown = false;
  mCloseHover = false;
  mCloseHeld = false;
  set_field_focused(false);
  clear_password();
  mStatus.clear();
  for (Seat *seat : mSeats) {
    seat->wake_x = seat->pointer_x;
    seat->wake_y = seat->pointer_y;
    seat_update_cursor(seat);
  }
  for (Surface *surface : mSurfaces) {
    dialog_unmap(surface);
  }
}

void TCCLock::keyboard_keymap(void *data, wl_keyboard *wl_keyboard,
                              uint32_t format, int32_t fd, uint32_t size) {
  Seat *seat = (Seat *)data;
  TCCLock *self = seat->lock;

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  char *map = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map == MAP_FAILED) {
    return;
  }
  xkb_keymap *keymap = xkb_keymap_new_from_string(
      self->mXkbContext, map, XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map, size);
  if (!keymap) {
    return;
  }

  if (seat->state) {
    xkb_state_unref(seat->state);
  }
  if (seat->keymap) {
    xkb_keymap_unref(seat->keymap);
  }
  seat->keymap = keymap;
  seat->state = xkb_state_new(keymap);
}

void TCCLock::keyboard_key(void *data, wl_keyboard *wl_keyboard,
                           uint32_t serial, uint32_t time, uint32_t key,
                           uint32_t state) {
  Seat *seat = (Seat *)data;
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    // evdev keycodes are offset by 8 in xkb.
    seat->lock->handle_key(seat, key + 8);
  }
}

void TCCLock::keyboard_modifiers(void *data, wl_keyboard *wl_keyboard,
                                 uint32_t serial, uint32_t mods_depressed,
                                 uint32_t mods_latched, uint32_t mods_locked,
                                 uint32_t group) {
  Seat *seat = (Seat *)data;
  if (seat->state) {
    xkb_state_update_mask(seat->state, mods_depressed, mods_latched,
                          mods_locked, 0, 0, group);
  }
}

void TCCLock::pointer_enter(void *data, wl_pointer *pointer, uint32_t serial,
                            wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
  Seat *seat = (Seat *)data;
  bool dialog = false;
  seat->pointer_surface = seat->lock->surface_from_wl_surface(surface, &dialog);
  if (!seat->pointer_surface) {
    return;
  }
  seat->pointer_serial = serial;
  seat->pointer_on_dialog = dialog;
  seat->lock->pointer_moved(seat, x, y);
  seat->wake_x = seat->pointer_x;
  seat->wake_y = seat->pointer_y;
  seat->lock->seat_update_cursor(seat);
}

// Keeps the pointer's position in lock surface coordinates, whichever of the
// lock surface or its dialog it's over.
void TCCLock::pointer_moved(Seat *seat, wl_fixed_t x, wl_fixed_t y) {
  seat->pointer_x = wl_fixed_to_double(x);
  seat->pointer_y = wl_fixed_to_double(y);
  if (seat->pointer_on_dialog) {
    seat->pointer_x += seat->pointer_surface->dialog_x;
    seat->pointer_y += seat->pointer_surface->dialog_y;
  }
}

void TCCLock::pointer_leave(void *data, wl_pointer *pointer, uint32_t serial,
                            wl_surface *surface) {
  Seat *seat = (Seat *)data;
  seat->pointer_surface = nullptr;
}

void TCCLock::pointer_motion(void *data, wl_pointer *pointer, uint32_t time,
                             wl_fixed_t x, wl_fixed_t y) {
  Seat *seat = (Seat *)data;
  if (!seat->pointer_surface) {
    return;
  }
  seat->lock->pointer_moved(seat, x, y);
  seat->lock->handle_pointer_motion(seat);
}

void TCCLock::pointer_button(void *data, wl_pointer *pointer, uint32_t serial,
                             uint32_t time, uint32_t button, uint32_t state) {
  Seat *seat = (Seat *)data;
  if (!seat->pointer_surface || button != BTN_LEFT) {
    return;
  }
  seat->lock->handle_pointer_button(seat,
                                    state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void TCCLock::pointer_axis(void *data, wl_pointer *pointer, uint32_t time,
                           uint32_t axis, wl_fixed_t value) {
  Seat *seat = (Seat *)data;
  if (seat->pointer_surface && seat->lock->mState == STATE_LOCKED) {
    seat->lock->show_dialog();
  }
}

// Ignored events
void TCCLock::keyboard_enter(void *data, wl_keyboard *wl_keyboard,
                             uint32_t serial, wl_surface *surface,
                             wl_array *keys) {}
void TCCLock::keyboard_leave(void *data, wl_keyboard *wl_keyboard,
                             uint32_t serial, wl_surface *surface) {}
void TCCLock::keyboard_repeat_info(void *data, wl_keyboard *wl_keyboard,
                                   int32_t rate, int32_t delay) {}

void TCCLock::handle_pointer_motion(Seat *seat) {
  if (mState != STATE_LOCKED) {
    return;
  }
  if (!mDialogShown) {
    if (std::hypot(seat->pointer_x - seat->wake_x,
                   seat->pointer_y - seat->wake_y) > WAKE_DISTANCE) {
      show_dialog();
    }
    return;
  }

  bool hover = close_button_contains(seat->pointer_surface, seat->pointer_x,
                                     seat->pointer_y);
  if (hover != mCloseHover) {
    mCloseHover = hover;
    draw_all();
  }
}

void TCCLock::handle_pointer_button(Seat *seat, bool pressed) {
  if (mState != STATE_LOCKED) {
    return;
  }
  if (!mDialogShown) {
    if (pressed) {
      show_dialog();
    }
    return;
  }

  bool inside = close_button_contains(seat->pointer_surface, seat->pointer_x,
                                      seat->pointer_y);
  if (pressed) {
    mCloseHeld = inside;
    // Clicking the password box focuses it, clicking anywhere else doesn't.
    set_field_focused(field_contains(seat->pointer_surface, seat->pointer_x,
                                     seat->pointer_y));
    draw_all();
    return;
  }

  // Only if it's released over the button it was pressed on, like the
  // window decorations.
  bool clicked = mCloseHeld && inside;
  mCloseHeld = false;
  if (clicked) {
    hide_dialog();
  } else {
    draw_all();
  }
}

void TCCLock::handle_key(Seat *seat, uint32_t keycode) {
  // Nothing but lock surfaces can have keyboard focus while locked, so any
  // key we get then is meant for us.
  if (mState != STATE_LOCKED || !seat->state) {
    return;
  }
  // The key that brings the dialog up gets typed too, so the password can
  // just be typed straight away.
  show_dialog();
  if (mPam.busy()) {
    return;
  }

  switch (xkb_state_key_get_one_sym(seat->state, keycode)) {
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter:
    submit();
    return;
  case XKB_KEY_BackSpace:
    // Back up over a whole UTF-8 sequence.
    while (mPasswordLen > 0) {
      char c = mPassword[--mPasswordLen];
      mPassword[mPasswordLen] = '\0';
      if ((c & 0xC0) != 0x80) {
        break;
      }
    }
    break;
  case XKB_KEY_Escape:
    clear_password();
    break;
  default: {
    char buf[16];
    int n = xkb_state_key_get_utf8(seat->state, keycode, buf, sizeof(buf));
    // Skip control characters.
    if (n > 0 && (unsigned char)buf[0] >= 0x20 && buf[0] != 0x7f &&
        mPasswordLen + n < sizeof(mPassword)) {
      memcpy(mPassword + mPasswordLen, buf, n);
      mPasswordLen += n;
    }
    explicit_bzero(buf, sizeof(buf));
    break;
  }
  }

  mStatus.clear();
  restart_blink();
  draw_all();
}

void TCCLock::submit() {
  if (mPasswordLen == 0 || mPam.busy()) {
    return;
  }
  mPam.start(mUser, mPassword, mPasswordLen, mWakeFd);
  clear_password();
  mStatus.clear();
  draw_all();
}
