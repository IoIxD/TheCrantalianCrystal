#pragma once

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>
#include <gtk/gtk.h>

#define TCC_GTK_FUNCS(X)                                                       \
  X(g_list_length)                                                             \
  X(g_list_nth)                                                                \
  X(g_app_info_get_all)                                                        \
  X(g_app_info_get_name)                                                       \
  X(g_app_info_get_id)                                                         \
  X(g_app_info_get_icon)                                                       \
  X(g_app_info_launch)                                                         \
  X(g_desktop_app_info_get_categories)                                         \
  X(gtk_init)                                                                  \
  X(gdk_display_get_default)                                                   \
  X(gdk_display_get_app_launch_context)

struct GtkLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_GTK_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if gtk4 could not be loaded.
  static GtkLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
