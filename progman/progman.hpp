#pragma once
#include <Mw/Milsko.h>

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>

#include <string>
#include <unordered_map>
#include <vector>

class ProgmanWindow {
  struct FolderPair {
    MwWidget holder = nullptr;
    MwLLPixmap folder_pixmap = nullptr;
    MwWidget folder_icon = nullptr;
    MwWidget folder_name = nullptr;
    static void MWAPI icon_dbl_click(MwWidget handle, void *client, void *user);
  };

  MwWidget mWindow = nullptr;

  MwWidget mViewport = nullptr;
  MwWidget mFolders = nullptr;

  std::unordered_map<std::string, std::vector<GAppInfo *>> mItems;

public:
  /* collect all the categories/items */
  void update_list();
  ProgmanWindow();
  void run();

  MwWidget create_icon_table(
      std::unordered_map<std::string, std::vector<GAppInfo *>> items);
};
