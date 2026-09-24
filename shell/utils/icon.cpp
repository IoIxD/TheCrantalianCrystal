#include "icon.hpp"

#include <dlfcn.h>
#include <format>

IconManager::IconManager() {
  gtk_init();
  auto disp = gdk_display_get_default();
  mGTKIconTheme = gtk_icon_theme_get_for_display(disp);
}

void IconManager::get_icon(const char *app_id, int size, int *width,
                           int *height, unsigned char **pixels) {
  auto appinf =
      g_desktop_app_info_new(std::format("{}.desktop", app_id).c_str());

  if (appinf) {
    GError *err = nullptr;

    auto gicon = g_app_info_get_icon((GAppInfo *)appinf);
    if (gicon) {
      auto icon = gtk_icon_theme_lookup_by_gicon(mGTKIconTheme, gicon, size, 1,
                                                 GTK_TEXT_DIR_NONE,
                                                 GTK_ICON_LOOKUP_NONE);

      if (icon) {
        auto pixbuf = gdk_pixbuf_new_from_file(
            g_file_get_parse_name(gtk_icon_paintable_get_file(icon)), &err);

        if (pixbuf) {
          *width = gdk_pixbuf_get_width(pixbuf);
          *height = gdk_pixbuf_get_height(pixbuf);
          auto px = gdk_pixbuf_get_pixels(pixbuf);
          auto size = gdk_pixbuf_get_byte_length(pixbuf);

          *pixels = (unsigned char *)malloc(size);
          memcpy(*pixels, px, size);

          g_object_unref(pixbuf);
        } else {
          *width = 0;
          *height = 0;
          *pixels = nullptr;
        }
        g_object_unref(icon);
      }
    }
    g_object_unref(appinf);
  }
};
