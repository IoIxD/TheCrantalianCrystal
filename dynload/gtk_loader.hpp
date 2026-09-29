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
  X(gdk_pixbuf_read_pixels)                                                    \
  X(g_list_length)                                                             \
  X(g_list_nth)                                                                \
  X(g_app_info_get_all)                                                        \
  X(g_app_info_get_name)                                                       \
  X(g_app_info_get_id)                                                         \
  X(g_app_info_launch)                                                         \
  X(g_desktop_app_info_get_categories)                                         \
  X(gtk_init)                                                                  \
  X(gdk_display_get_app_launch_context)

struct GtkLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_GTK_FUNCS(X)
#undef X

  void *handle = nullptr;
};

extern GtkLib *GTK_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#undef g_free
#define g_free GTK_LIB->g_free
#define g_error_free GTK_LIB->g_error_free
#define g_path_is_absolute GTK_LIB->g_path_is_absolute
#define g_object_unref GTK_LIB->g_object_unref
#define g_desktop_app_info_new GTK_LIB->g_desktop_app_info_new
#define g_app_info_get_icon GTK_LIB->g_app_info_get_icon
#define g_themed_icon_new GTK_LIB->g_themed_icon_new
#define g_file_get_path GTK_LIB->g_file_get_path
#define gtk_init_check GTK_LIB->gtk_init_check
#define gdk_display_get_default GTK_LIB->gdk_display_get_default
#define gtk_icon_theme_get_for_display GTK_LIB->gtk_icon_theme_get_for_display
#define gtk_icon_theme_new GTK_LIB->gtk_icon_theme_new
#define gtk_icon_theme_set_search_path GTK_LIB->gtk_icon_theme_set_search_path
#define gtk_icon_theme_get_theme_name GTK_LIB->gtk_icon_theme_get_theme_name
#define gtk_icon_theme_set_theme_name GTK_LIB->gtk_icon_theme_set_theme_name
#define gtk_icon_theme_has_icon GTK_LIB->gtk_icon_theme_has_icon
#define gtk_icon_theme_lookup_icon GTK_LIB->gtk_icon_theme_lookup_icon
#define gtk_icon_theme_lookup_by_gicon GTK_LIB->gtk_icon_theme_lookup_by_gicon
#define gtk_icon_paintable_get_file GTK_LIB->gtk_icon_paintable_get_file
#define gdk_pixbuf_new_from_data GTK_LIB->gdk_pixbuf_new_from_data
#define gdk_pixbuf_scale_simple GTK_LIB->gdk_pixbuf_scale_simple
#define gdk_pixbuf_get_file_info GTK_LIB->gdk_pixbuf_get_file_info
#define gdk_pixbuf_format_get_name GTK_LIB->gdk_pixbuf_format_get_name
#define gdk_pixbuf_format_is_scalable GTK_LIB->gdk_pixbuf_format_is_scalable
#define gdk_pixbuf_new_from_file GTK_LIB->gdk_pixbuf_new_from_file
#define gdk_pixbuf_new_from_file_at_size                                       \
  GTK_LIB->gdk_pixbuf_new_from_file_at_size
#define gdk_pixbuf_get_has_alpha GTK_LIB->gdk_pixbuf_get_has_alpha
#define gdk_pixbuf_add_alpha GTK_LIB->gdk_pixbuf_add_alpha
#define gdk_pixbuf_get_width GTK_LIB->gdk_pixbuf_get_width
#define gdk_pixbuf_get_height GTK_LIB->gdk_pixbuf_get_height
#define gdk_pixbuf_get_rowstride GTK_LIB->gdk_pixbuf_get_rowstride
#define gdk_pixbuf_read_pixels GTK_LIB->gdk_pixbuf_read_pixels
#define g_list_length GTK_LIB->g_list_length
#define g_list_nth GTK_LIB->g_list_nth
#define g_app_info_get_all GTK_LIB->g_app_info_get_all
#define g_app_info_get_name GTK_LIB->g_app_info_get_name
#define g_app_info_get_id GTK_LIB->g_app_info_get_id
#define g_app_info_launch GTK_LIB->g_app_info_launch
#define g_desktop_app_info_get_categories                                      \
  GTK_LIB->g_desktop_app_info_get_categories
#define gtk_init GTK_LIB->gtk_init
#define gdk_display_get_app_launch_context                                     \
  GTK_LIB->gdk_display_get_app_launch_context
#endif
