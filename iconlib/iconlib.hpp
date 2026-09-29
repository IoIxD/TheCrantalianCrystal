#pragma once

#include <map>
#include <string>

class IconManager {
  struct _GtkIconTheme *mGTKIconTheme = nullptr;
  /* themes that only search one extra directory, keyed by that directory */
  std::map<std::string, struct _GtkIconTheme *> mExtraThemes;

  struct _GtkIconTheme *extra_theme(const char *path);
  void load_paintable(struct _GtkIconPaintable *icon, int size, int *width,
                      int *height, unsigned char **pixels);

public:
  IconManager();
  ~IconManager();
  IconManager(const IconManager &) = delete;
  IconManager &operator=(const IconManager &) = delete;
  /* look up the icon for a .desktop app id */
  void get_icon(const char *app_id, int size, int *width, int *height,
                unsigned char **pixels);
  /* look up a named icon from the current theme */
  void get_icon_by_name(const char *name, int size, int *width, int *height,
                        unsigned char **pixels);
  /*
   * look up a named icon, checking extra_path (a directory of loose icons or
   * an icon theme, may be null) before the current theme. name may also be an
   * absolute path to an image file.
   */
  void get_icon_by_name(const char *name, const char *extra_path, int size,
                        int *width, int *height, unsigned char **pixels);
  /* look up a GIcon in the current theme and load it */
  void get_icon_from_gicon(struct _GIcon *gicon, int size, int *width,
                           int *height, unsigned char **pixels);
  /* copy RGBA pixels, scaled down to fit in size x size if they are larger */
  void get_icon_from_pixels(const unsigned char *src, int src_width,
                            int src_height, int size, int *width, int *height,
                            unsigned char **pixels);
  /* load an image file, scaled down to fit in size x size if it is larger */
  void get_icon_from_file(const char *path, int size, int *width, int *height,
                          unsigned char **pixels);
};
