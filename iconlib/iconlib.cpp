#define USE_GTK
#include "iconlib.hpp"

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <format>

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>
#include <gtk/gtk.h>

IconManager::IconManager() {
  gtk_init();
  auto disp = gdk_display_get_default();
  mGTKIconTheme = gtk_icon_theme_get_for_display(disp);
}

void IconManager::get_icon(const char *app_id, int size, int *width,
                           int *height, unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  auto appinf =
      g_desktop_app_info_new(std::format("{}.desktop", app_id).c_str());

  if (appinf) {
    get_icon_from_gicon(g_app_info_get_icon((GAppInfo *)appinf), size, width,
                        height, pixels);
    g_object_unref(appinf);
  }
};

void IconManager::get_icon_by_name(const char *name, int size, int *width,
                                   int *height, unsigned char **pixels) {
  auto gicon = g_themed_icon_new(name);
  get_icon_from_gicon(gicon, size, width, height, pixels);
  g_object_unref(gicon);
}

void IconManager::get_icon_from_gicon(GIcon *gicon, int size, int *width,
                                      int *height, unsigned char **pixels) {
  *width = 0;
  *height = 0;
  *pixels = nullptr;

  if (!gicon)
    return;

  auto icon = gtk_icon_theme_lookup_by_gicon(
      mGTKIconTheme, gicon, size, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
  if (!icon)
    return;

  auto file = gtk_icon_paintable_get_file(icon);
  if (file) {
    GError *err = nullptr;
    auto path = g_file_get_parse_name(file);
    auto pixbuf = gdk_pixbuf_new_from_file(path, &err);

    if (pixbuf) {
      *width = gdk_pixbuf_get_width(pixbuf);
      *height = gdk_pixbuf_get_height(pixbuf);
      auto px = gdk_pixbuf_get_pixels(pixbuf);
      auto len = gdk_pixbuf_get_byte_length(pixbuf);

      *pixels = (unsigned char *)malloc(len);
      memcpy(*pixels, px, len);

      g_object_unref(pixbuf);
    } else if (err) {
      g_error_free(err);
    }

    g_free(path);
    g_object_unref(file);
  }
  g_object_unref(icon);
}
