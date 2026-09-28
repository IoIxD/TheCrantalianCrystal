#pragma once

#ifdef USE_GTK
#include <gtk/gtk.h>
#endif

class IconManager {
  struct _GtkIconTheme *mGTKIconTheme;

public:
  IconManager();
  /* look up the icon for a .desktop app id */
  void get_icon(const char *app_id, int size, int *width, int *height,
                unsigned char **pixels);
  /* look up a named icon from the current theme */
  void get_icon_by_name(const char *name, int size, int *width, int *height,
                        unsigned char **pixels);
  /*
   * look up a GIcon in the current theme and load it. on failure width/height
   * are 0 and pixels is null, otherwise pixels must be free()'d by the caller.
   */
  void get_icon_from_gicon(struct _GIcon *gicon, int size, int *width,
                           int *height, unsigned char **pixels);
};
