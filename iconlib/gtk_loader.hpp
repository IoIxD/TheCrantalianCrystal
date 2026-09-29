#pragma once

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>
#include <gtk/gtk.h>

#define TCC_GTK_FUNCS(X)                                                       \
  X(g_free)                                                                    \
  X(g_error_free)                                                              \
  X(g_path_is_absolute)                                                        \
  X(g_object_unref)                                                            \
  X(g_desktop_app_info_new)                                                    \
  X(g_app_info_get_icon)                                                       \
  X(g_themed_icon_new)                                                         \
  X(g_file_get_path)                                                           \
  X(gtk_init_check)                                                            \
  X(gdk_display_get_default)                                                   \
  X(gtk_icon_theme_get_for_display)                                            \
  X(gtk_icon_theme_new)                                                        \
  X(gtk_icon_theme_set_search_path)                                            \
  X(gtk_icon_theme_get_theme_name)                                             \
  X(gtk_icon_theme_set_theme_name)                                             \
  X(gtk_icon_theme_has_icon)                                                   \
  X(gtk_icon_theme_lookup_icon)                                                \
  X(gtk_icon_theme_lookup_by_gicon)                                            \
  X(gtk_icon_paintable_get_file)                                               \
  X(gdk_pixbuf_new_from_data)                                                  \
  X(gdk_pixbuf_scale_simple)                                                   \
  X(gdk_pixbuf_get_file_info)                                                  \
  X(gdk_pixbuf_format_get_name)                                                \
  X(gdk_pixbuf_format_is_scalable)                                             \
  X(gdk_pixbuf_new_from_file)                                                  \
  X(gdk_pixbuf_new_from_file_at_size)                                          \
  X(gdk_pixbuf_get_has_alpha)                                                  \
  X(gdk_pixbuf_add_alpha)                                                      \
  X(gdk_pixbuf_get_width)                                                      \
  X(gdk_pixbuf_get_height)                                                     \
  X(gdk_pixbuf_get_rowstride)                                                  \
  X(gdk_pixbuf_read_pixels)

// Function table for a dynamically loaded gtk4 (and the glib, gio and
// gdk-pixbuf libraries it depends on). Each member has the same name and
// signature as the function it points to.
//
// Hidden so that a program linking iconlib can have its own GtkLib without
// the two interposing each other.
struct __attribute__((visibility("hidden"))) GtkLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_GTK_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if gtk4 could not be loaded.
  static GtkLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
