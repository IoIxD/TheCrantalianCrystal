#include "lock.hpp"

#include "../client/ssd.hpp"
#include "../utils/texture.hpp"
#include "lock_ssd_shader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>

#define DIALOG_WIDTH 380
#define DIALOG_HEIGHT 112
#define FIELD_HEIGHT 22
#define DIALOG_ALPHA 0.6f

// The password box, in the dialog's coordinates.
#define FIELD_X (SSD_BORDER_SIZE + 16)
#define FIELD_Y (SSD_BORDER_SIZE_TOP + 54)
#define FIELD_WIDTH (DIALOG_WIDTH - 32)

bool TCCLock::setup_egl() {
  if (mEGLContext != EGL_NO_CONTEXT) {
    return true;
  }

  EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                             EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE,
                             8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                             // For the decoration's rounded corners.
                             EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 1,
                              EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLint major, minor, n;

  mEGLDisplay =
      eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, mClient->mDisplay, NULL);
  if (mEGLDisplay == EGL_NO_DISPLAY ||
      eglInitialize(mEGLDisplay, &major, &minor) != EGL_TRUE) {
    fprintf(stderr, "lock: eglInitialize error: %0X\n", eglGetError());
    return false;
  }
  if (eglChooseConfig(mEGLDisplay, config_attribs, &mEGLConfig, 1, &n) !=
          EGL_TRUE ||
      n < 1) {
    fprintf(stderr, "lock: eglChooseConfig error: %0X\n", eglGetError());
    return false;
  }
  if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
    fprintf(stderr, "lock: eglBindAPI error: %0X\n", eglGetError());
    return false;
  }
  mEGLContext = eglCreateContext(mEGLDisplay, mEGLConfig, EGL_NO_CONTEXT,
                                 context_attribs);
  if (mEGLContext == EGL_NO_CONTEXT) {
    fprintf(stderr, "lock: eglCreateContext error: %0X\n", eglGetError());
    return false;
  }
  return true;
}

// Fills a rect given in pixels, from the top left, replacing what's there.
static void fill_rect(int x, int y, int w, int h, int width, int height,
                      float r, float g, float b, float a = 1.f) {
  float x0 = ((float)x / width) * 2.0f - 1.0f;
  float y0 = 1.0f - ((float)y / height) * 2.0f;
  float x1 = ((float)(x + w) / width) * 2.0f - 1.0f;
  float y1 = 1.0f - ((float)(y + h) / height) * 2.0f;

  // Premultiplied, like Wayland wants.
  glColor4f(r * a, g * a, b * a, a);
  glBegin(GL_QUADS);
  glVertex2f(x0, y0);
  glVertex2f(x1, y0);
  glVertex2f(x1, y1);
  glVertex2f(x0, y1);
  glEnd();
}

// Things that need a current context, so can't be done in setup_egl.
void TCCLock::setup_gl() {
  if (!mSSDProgram) {
    mSSDProgram = ssd_create_shader_program(LOCK_SSD_FRAG_SOURCE);
  }
  if (!mIconLoaded) {
    mIconLoaded = true;
    int width = 0, height = 0;
    unsigned char *pixels = nullptr;
    mClient->mIconManager.get_icon_by_name("system-lock-screen", 16, &width,
                                           &height, &pixels);
    if (pixels) {
      mIconTexture = TextureManager::NewGLTextureID(width, height, pixels);
      free(pixels);
    }
  }
}

// Where the dialog goes on a lock surface, from the top left. It's the size
// of its decoration.
void TCCLock::dialog_frame(Surface *surface, int *x, int *y, int *width,
                           int *height) {
  *width = DIALOG_WIDTH + SSD_BORDER_SIZE * 2;
  *height = DIALOG_HEIGHT + (SSD_BORDER_SIZE_TOTAL);
  *x = (surface->width - *width) / 2;
  *y = (surface->height - *height) / 2;
}

// x and y are in lock surface coordinates.
bool TCCLock::close_button_contains(Surface *surface, double x, double y) {
  if (!surface) {
    return false;
  }
  int frame_x, frame_y, frame_w, frame_h;
  dialog_frame(surface, &frame_x, &frame_y, &frame_w, &frame_h);
  double button_x = frame_x + frame_w - SSD_NAV_BUTTON_CLOSE_X_FROM_RIGHT;
  double button_y = frame_y + SSD_NAV_BUTTON_Y;
  return x >= button_x && x < button_x + SSD_NAV_BUTTON_WIDTH &&
         y >= button_y && y < button_y + SSD_NAV_BUTTON_HEIGHT;
}

// x and y are in lock surface coordinates.
bool TCCLock::field_contains(Surface *surface, double x, double y) {
  if (!surface) {
    return false;
  }
  x -= surface->dialog_x;
  y -= surface->dialog_y;
  return x >= FIELD_X && x < FIELD_X + FIELD_WIDTH && y >= FIELD_Y &&
         y < FIELD_Y + FIELD_HEIGHT;
}

void TCCLock::dialog_map(Surface *surface) {
  if (surface->egl_window || !setup_egl()) {
    return;
  }
  int x, y, width, height;
  dialog_frame(surface, &x, &y, &width, &height);

  surface->egl_window =
      wl_egl_window_create(surface->dialog_surface, width, height);
  surface->egl_surface = eglCreatePlatformWindowSurface(
      mEGLDisplay, mEGLConfig, surface->egl_window, NULL);
  if (surface->egl_surface == EGL_NO_SURFACE) {
    fprintf(stderr, "lock: eglCreatePlatformWindowSurface error: %0X\n",
            eglGetError());
    wl_egl_window_destroy(surface->egl_window);
    surface->egl_window = nullptr;
    return;
  }
  // Swapping mustn't block the window manager waiting on the compositor.
  eglMakeCurrent(mEGLDisplay, surface->egl_surface, surface->egl_surface,
                 mEGLContext);
  eglSwapInterval(mEGLDisplay, 0);

  draw(surface);
  scr_blur(surface);
}

void TCCLock::dialog_unmap(Surface *surface) {
  if (!surface->egl_window) {
    return;
  }
  eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(mEGLDisplay, surface->egl_surface);
  wl_egl_window_destroy(surface->egl_window);
  surface->egl_surface = EGL_NO_SURFACE;
  surface->egl_window = nullptr;

  // EGL's let go of it, so no buffer unmaps it.
  wl_surface_attach(surface->dialog_surface, NULL, 0, 0);
  wl_surface_commit(surface->dialog_surface);
  scr_blur(surface);
}

void TCCLock::draw(Surface *surface) {
  if (surface->egl_surface == EGL_NO_SURFACE) {
    return;
  }
  if (eglMakeCurrent(mEGLDisplay, surface->egl_surface, surface->egl_surface,
                     mEGLContext) != EGL_TRUE) {
    fprintf(stderr, "lock: eglMakeCurrent error: %08X\n", eglGetError());
    return;
  }
  setup_gl();

  int frame_x, frame_y, w, h;
  dialog_frame(surface, &frame_x, &frame_y, &w, &h);

  glViewport(0, 0, w, h);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT);

  SSDNavButtons buttons;
  buttons.close_hover = mCloseHover;
  buttons.close_held = mCloseHeld;
  ssd_draw_backing(mSSDProgram, w, h, buttons);

  if (mIconTexture) {
    ssd_draw_icon(mIconTexture, 0, 0, h);
  }
  glViewport(0, 0, w, h);

  glColor4f(1.f, 1.f, 1.f, 1.f);
  mGlyphManager.draw_text("Workstation Locked", SSD_TITLE_X, SSD_TITLE_Y, w, h,
                          true, false);

  // The dialog's contents, on see-through white so the screensaver shows
  // through (rather than the decoration's gradient underneath).
  int x = SSD_BORDER_SIZE;
  int y = SSD_BORDER_SIZE_TOP;
  fill_rect(x, y, DIALOG_WIDTH, DIALOG_HEIGHT, w, h, 1.f, 1.f, 1.f,
            DIALOG_ALPHA);

  fill_rect(FIELD_X - 1, FIELD_Y - 1, FIELD_WIDTH + 2, FIELD_HEIGHT + 2, w, h,
            0.f, 0.f, 0.f);
  fill_rect(FIELD_X, FIELD_Y, FIELD_WIDTH, FIELD_HEIGHT, w, h, 1.f, 1.f, 1.f);

  glColor4f(1.f, 1.f, 1.f, 1.f);
  mGlyphManager.draw_text("This session is locked.", x + 16, y + 22, w, h,
                          false, true);
  mGlyphManager.draw_text(
      std::format("Enter the password for {} to unlock it.", mUser), x + 16,
      y + 42, w, h, false, true);

  // One asterisk per character typed, as many as fit.
  size_t chars = std::count_if(mPassword, mPassword + mPasswordLen,
                               [](char c) { return (c & 0xC0) != 0x80; });
  std::string stars(std::min<size_t>(chars, 48), '*');
  mGlyphManager.draw_text(stars, FIELD_X + 4, FIELD_Y + 16, w, h, false, true);

  // The cursor, just after them.
  if (mFieldFocused && mCaretVisible) {
    int caret_x = FIELD_X + 4 + mGlyphManager.text_width(stars, false, true);
    fill_rect(caret_x, FIELD_Y + 4, 1, FIELD_HEIGHT - 8, w, h, 0.f, 0.f, 0.f);
  }

  std::string status = mPam.busy() ? "Checking password..." : mStatus;
  if (!status.empty()) {
    mGlyphManager.draw_text(status, x + 16, y + 100, w, h, false, true);
  }

  if (eglSwapBuffers(mEGLDisplay, surface->egl_surface) != EGL_TRUE) {
    fprintf(stderr, "lock: eglSwapBuffers error: %08X\n", eglGetError());
  }
}

void TCCLock::draw_all() {
  for (Surface *surface : mSurfaces) {
    draw(surface);
  }
}
