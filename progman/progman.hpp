#pragma once
#include <Mw/Milsko.h>

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ProgmanWindow {
  MwWidget mWindow = nullptr;

  MwWidget mViewport = nullptr;
  MwWidget mFolders = nullptr;
  int doubleClickTimer = 0;

  std::unordered_map<std::string, std::vector<GAppInfo *>> mItems;
  static void MWAPI tick(MwWidget handle, void *user, void *client);

public:
  struct Subwindow;

  struct FolderPair {
    MwWidget holder = nullptr;
    MwLLPixmap folder_pixmap = nullptr;
    MwWidget folder_icon = nullptr;
    MwWidget folder_name = nullptr;
    bool selected = false;
    ProgmanWindow *win = nullptr;
    Subwindow *subwin = nullptr;
    GAppInfo *info = nullptr;
    static void MWAPI icon_dbl_click(MwWidget handle, void *user, void *client);
    ~FolderPair() {
      MwDestroyWidget(folder_name);
      if (folder_icon)
        MwDestroyWidget(folder_icon);
      if (folder_pixmap)
        MwLLDestroyPixmap(folder_pixmap);
      MwDestroyWidget(holder);
    };
  };
  struct Subwindow {
    MwWidget subwindow = nullptr;
    ProgmanWindow *win = nullptr;
    MwWidget viewport = nullptr;
    MwWidget items = nullptr;
    MwWidget create_icon_table(std::vector<GAppInfo *> items);
    static void MWAPI remove(MwWidget handle, void *user, void *client);
    std::vector<FolderPair *> folderPairs;
    int doubleClickTimer = 0;
  };

  std::vector<FolderPair *> folderPairs;
  std::vector<Subwindow *> subwindows;

  /* collect all the categories/items */
  void update_list();
  ProgmanWindow();
  void setup();
  void run();

  void create_subwindow(std::string catName);
  void remove_subwindow(Subwindow *sub);

  MwWidget create_icon_table(
      std::unordered_map<std::string, std::vector<GAppInfo *>> items);
};
