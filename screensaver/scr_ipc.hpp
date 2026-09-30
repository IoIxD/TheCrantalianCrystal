#pragma once

#include <cstdint>

/*
 * How the shell's lock screen and the screensaver loader (tcc_scr_loader)
 * talk.
 *
 * The loader runs the screensaver module in its own process, so it crashing
 * can't take the shell (and with it the lock screen) down. It draws into
 * dmabufs and passes them over, and the shell shows them on its lock
 * surfaces. It has no Wayland connection of its own.
 *
 * Every message is one ScrMsg over a SOCK_SEQPACKET socket. BUFFER messages
 * carry the dmabuf's fd with SCM_RIGHTS.
 *
 * Shell -> loader:
 *   MODIFIERS  the modifiers the compositor takes for SCR_FORMAT, sent first
 *   CONFIGURE  (output, width, height) an output to draw for, or its new size
 *   REMOVE     (output) stop drawing for an output
 *   FRAME      (output) the last frame has been shown, draw the next one
 *   RELEASE    (output, buffer) the compositor is done reading a buffer
 *   BLUR       (output, x, y, width, height, radius) blur this part of the
 *              frames, e.g. under a see-through window, a rounded rect in
 *              surface coordinates from the top left. width 0 turns it off.
 *
 * Loader -> shell:
 *   BUFFER     (output, buffer, width, height, stride, offset, modifier) + fd
 *              a new buffer, replacing any old one with the same id
 *   READY      (output, buffer) a frame has been drawn into a buffer
 *
 * After CONFIGURE the loader sends its buffers and draws a first frame
 * unprompted, then one per FRAME. Buffers are only drawn into again once
 * they've been released.
 *
 * The shell treats everything the loader sends as untrusted.
 */

// DRM_FORMAT_XRGB8888
#define SCR_FORMAT 0x34325258
// DRM_FORMAT_MOD_INVALID, i.e. the driver picks the layout implicitly.
#define SCR_MOD_INVALID 0x00ffffffffffffffULL
#define SCR_MAX_BUFFERS 3
#define SCR_MAX_MODIFIERS 128

enum ScrMsgType : uint32_t {
  SCR_MSG_MODIFIERS = 1,
  SCR_MSG_CONFIGURE,
  SCR_MSG_REMOVE,
  SCR_MSG_FRAME,
  SCR_MSG_RELEASE,
  SCR_MSG_BUFFER,
  SCR_MSG_READY,
  SCR_MSG_BLUR,
};

struct ScrMsg {
  uint32_t type = 0;
  uint32_t output = 0;
  uint32_t buffer = 0;
  int32_t x = 0, y = 0;
  int32_t width = 0, height = 0;
  uint32_t radius = 0;
  uint32_t stride = 0, offset = 0;
  uint64_t modifier = 0;

  uint32_t n_modifiers = 0;
  uint64_t modifiers[SCR_MAX_MODIFIERS] = {};
};
