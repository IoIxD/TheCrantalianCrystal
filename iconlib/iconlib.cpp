#include "iconlib.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <format>

#include "gtk_loader.hpp"

/*
 * glib defines g_free as a function-like macro, which would also expand
 * mLib->g_free(...). every call here goes through mLib anyway.
 */
#undef g_free

IconManager::IconManager() {
  auto lib = GtkLib::get();
  if (!lib)
    return;

  if (!lib->gtk_init_check()) {
    fprintf(stderr, "tcc_iconlib: could not initialize gtk\n");
    return;
  }

  mLib = lib;
  mGTKIconTheme =
      mLib->gtk_icon_theme_get_for_display(mLib->gdk_display_get_default());
}

IconManager::~IconManager() {
  for (auto &[path, theme] : mExtraThemes)
    mLib->g_object_unref(theme);
}

void IconManager::get_icon(const char *app_id, int size, int *width,
                           int *height, unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  if (!mLib)
    return;

  auto appinf =
      mLib->g_desktop_app_info_new(std::format("{}.desktop", app_id).c_str());

  if (appinf) {
    get_icon_from_gicon(mLib->g_app_info_get_icon((GAppInfo *)appinf), size,
                        width, height, pixels);
    mLib->g_object_unref(appinf);
  }
};

void IconManager::get_icon_by_name(const char *name, int size, int *width,
                                   int *height, unsigned char **pixels) {
  if (!mLib) {
    *width = 0;
    *height = 0;
    *pixels = nullptr;
    return;
  }

  auto gicon = mLib->g_themed_icon_new(name);
  get_icon_from_gicon(gicon, size, width, height, pixels);
  mLib->g_object_unref(gicon);
}

void IconManager::get_icon_by_name(const char *name, const char *extra_path,
                                   int size, int *width, int *height,
                                   unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  if (!mLib || !name || !*name)
    return;

  if (mLib->g_path_is_absolute(name)) {
    get_icon_from_file(name, size, width, height, pixels);
    return;
  }

  if (extra_path && *extra_path) {
    auto theme = extra_theme(extra_path);
    if (mLib->gtk_icon_theme_has_icon(theme, name)) {
      auto icon = mLib->gtk_icon_theme_lookup_icon(theme, name, nullptr, size,
                                                   1, GTK_TEXT_DIR_NONE,
                                                   GTK_ICON_LOOKUP_NONE);
      load_paintable(icon, size, width, height, pixels);
      mLib->g_object_unref(icon);
      if (*pixels)
        return;
    }
  }

  get_icon_by_name(name, size, width, height, pixels);
}

GtkIconTheme *IconManager::extra_theme(const char *path) {
  auto it = mExtraThemes.find(path);
  if (it != mExtraThemes.end())
    return it->second;

  /*
   * a theme whose only search path is the extra directory. it finds both
   * loose icons in the directory and icons laid out like a theme inside it
   * (e.g. hicolor/22x22/apps/foo.png).
   */
  auto theme = mLib->gtk_icon_theme_new();
  const char *search_path[] = {path, nullptr};
  mLib->gtk_icon_theme_set_search_path(theme, search_path);

  auto theme_name = mLib->gtk_icon_theme_get_theme_name(mGTKIconTheme);
  if (theme_name) {
    mLib->gtk_icon_theme_set_theme_name(theme, theme_name);
    mLib->g_free(theme_name);
  }

  mExtraThemes.emplace(path, theme);
  return theme;
}

void IconManager::get_icon_from_gicon(GIcon *gicon, int size, int *width,
                                      int *height, unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  if (!mLib || !gicon)
    return;

  auto icon = mLib->gtk_icon_theme_lookup_by_gicon(
      mGTKIconTheme, gicon, size, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
  if (!icon)
    return;

  load_paintable(icon, size, width, height, pixels);
  mLib->g_object_unref(icon);
}

void IconManager::load_paintable(GtkIconPaintable *icon, int size, int *width,
                                 int *height, unsigned char **pixels) {
  auto file = mLib->gtk_icon_paintable_get_file(icon);
  if (!file)
    return;

  auto path = mLib->g_file_get_path(file);
  if (path)
    get_icon_from_file(path, size, width, height, pixels);

  mLib->g_free(path);
  mLib->g_object_unref(file);
}

void IconManager::get_icon_from_pixels(const unsigned char *src, int src_width,
                                       int src_height, int size, int *width,
                                       int *height, unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  if (!mLib || !src || src_width <= 0 || src_height <= 0)
    return;

  int w = src_width, h = src_height;
  if (w > size || h > size) {
    if (w >= h) {
      h = std::max(1, h * size / w);
      w = size;
    } else {
      w = std::max(1, w * size / h);
      h = size;
    }
  }

  auto out = (unsigned char *)malloc((size_t)w * h * 4);
  if (w == src_width && h == src_height) {
    memcpy(out, src, (size_t)w * h * 4);
  } else {
    auto pixbuf = mLib->gdk_pixbuf_new_from_data(
        src, GDK_COLORSPACE_RGB, true, 8, src_width, src_height, src_width * 4,
        nullptr, nullptr);
    auto scaled =
        mLib->gdk_pixbuf_scale_simple(pixbuf, w, h, GDK_INTERP_BILINEAR);
    int stride = mLib->gdk_pixbuf_get_rowstride(scaled);
    auto scaled_px = mLib->gdk_pixbuf_read_pixels(scaled);
    for (int y = 0; y < h; y++)
      memcpy(out + (size_t)y * w * 4, scaled_px + (size_t)y * stride,
             (size_t)w * 4);
    mLib->g_object_unref(scaled);
    mLib->g_object_unref(pixbuf);
  }

  *width = w;
  *height = h;
  *pixels = out;
}

void IconManager::get_icon_from_file(const char *path, int size, int *width,
                                     int *height, unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  if (!mLib)
    return;

  int file_width = 0, file_height = 0;
  auto format = mLib->gdk_pixbuf_get_file_info(path, &file_width, &file_height);
  if (!format)
    return;

  /*
   * scalable images are rendered at the requested size (their nominal size is
   * often far off), other images are only scaled down.
   */
  auto format_name = mLib->gdk_pixbuf_format_get_name(format);
  bool scalable = mLib->gdk_pixbuf_format_is_scalable(format) ||
                  (format_name && strcmp(format_name, "svg") == 0);
  mLib->g_free(format_name);

  GError *err = nullptr;
  GdkPixbuf *pixbuf;
  if (scalable || file_width > size || file_height > size)
    pixbuf = mLib->gdk_pixbuf_new_from_file_at_size(path, size, size, &err);
  else
    pixbuf = mLib->gdk_pixbuf_new_from_file(path, &err);

  if (!pixbuf) {
    if (err)
      mLib->g_error_free(err);
    return;
  }

  /* make sure we hand out 4 channels, even for images without alpha */
  if (!mLib->gdk_pixbuf_get_has_alpha(pixbuf)) {
    auto with_alpha = mLib->gdk_pixbuf_add_alpha(pixbuf, false, 0, 0, 0);
    mLib->g_object_unref(pixbuf);
    pixbuf = with_alpha;
  }

  int w = mLib->gdk_pixbuf_get_width(pixbuf);
  int h = mLib->gdk_pixbuf_get_height(pixbuf);
  int stride = mLib->gdk_pixbuf_get_rowstride(pixbuf);
  auto src = mLib->gdk_pixbuf_read_pixels(pixbuf);

  /* rows in the pixbuf may be padded, ours are not */
  auto out = (unsigned char *)malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    memcpy(out + (size_t)y * w * 4, src + (size_t)y * stride, (size_t)w * 4);

  *width = w;
  *height = h;
  *pixels = out;

  mLib->g_object_unref(pixbuf);
}
