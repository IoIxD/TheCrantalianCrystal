#pragma once

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>
#include <gtk/gtk.h>
#include <string>

class IconManager {
  GtkIconTheme *mGTKIconTheme;

public:
  IconManager();
  void get_icon(const char *app_id, int size, int *width, int *height,
                unsigned char **pixels);
};
