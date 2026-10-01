#pragma once

#include <cstdint>

#include <drm/drm.h>
#include <drm/drm_fourcc.h>

/*
 * Communication between the shell's lock screen and the screensaver loader.
 *
 * The loader runs the screensaver and draws the output onto a dmabuf that the
 * shell shows on the lockscreen.
 *
 * Messages consist of a ScrMsg sent over a SOCK_SEQPACKET socket. BUFFER
 * messages carry the dmabuf's fd with SCM_RIGHTS. This could (and should)
 * probably be replaced with a varlink interface
 */

#define SCR_FORMAT DRM_FORMAT_XRGB8888
#define SCR_MOD_INVALID DRM_FORMAT_MOD_INVALID
#define SCR_MAX_BUFFERS 3
#define SCR_MAX_MODIFIERS 128

enum ScrMsgType : uint32_t {
  // the modifiers the compositor takes for SCR_FORMAT, sent first
  SCR_MSG_MODIFIERS = 1,
  // (output, width, height) an output to draw for, or its new size
  SCR_MSG_CONFIGURE,
  // (output) stop drawing for an output
  SCR_MSG_REMOVE,
  // (output) the last frame has been shown, draw the next one
  SCR_MSG_FRAME,
  // (output, buffer) the compositor is done reading a buffer
  SCR_MSG_RELEASE,
  // (output, buffer, width, height, stride, offset, modifier) + fd
  // a new buffer, replacing any old one with the same id.
  // Carries the dmabuf's fd with SCM_RIGHTS.
  SCR_MSG_BUFFER,
  // (output, buffer) a frame has been drawn into a buffer
  SCR_MSG_READY,
  // (output, x, y, width, height, radius) blur this part of the
  // frames, e.g. under a see-through window, a rounded rect in
  // surface coordinates from the top left. width 0 turns it off.
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
