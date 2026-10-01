#pragma once
#define _MILSKO
#include <Mw/Milsko.h>

#include "iconlib.hpp"

#include "desktop_varlink.hpp"

#include <gio-unix-2.0/gio/gdesktopappinfo.h>
#include <glib-2.0/glib.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "registry.hpp"

class ProgmanWindow {
  IconManager mIcons;
  MwWidget mWindow = nullptr;

  MwWidget mViewport = nullptr;
  MwWidget mFolders = nullptr;
  int doubleClickTimer = 0;

  TCCVarlinkConnection *mVarlink;

  std::unordered_map<std::string, std::vector<GAppInfo *>> mItems;
  static void MWAPI tick(MwWidget handle, void *user, void *client);

  void (*mCloseCallback)(void *user);
  void *mUserPtr;

  TCCRegistryConnection *mRegistry = nullptr;

public:
  ProgmanWindow(void (*close_callback)(void *user), void *user);
  struct Subwindow;

  struct FolderPair {
    MwWidget holder = nullptr;
    MwPixmap folder_pixmap = nullptr;
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
        MwDestroyPixmap(folder_pixmap);
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
    static void MWAPI resize(MwWidget handle, void *user, void *client);
    std::vector<FolderPair *> folderPairs;
    std::vector<GAppInfo *> appinfos;
    int doubleClickTimer = 0;
  };
  struct ControlPanelWindow {
    MwWidget subwindow = nullptr;
    ProgmanWindow *win = nullptr;
    MwWidget items = nullptr;
    MwWidget popup = nullptr;
    MwWidget popup_box = nullptr;
    MwWidget popup_text = nullptr;
    MwWidget popup_sep1 = nullptr;

    MwWidget popup_entry = nullptr;
    MwWidget popup_checkbox_checkbox = nullptr;
    MwWidget popup_checkbox_text = nullptr;

    MwWidget popup_sep2 = nullptr;
    MwWidget popup_submit = nullptr;
    static void MWAPI remove(MwWidget handle, void *user, void *client);
    static void MWAPI resize(MwWidget handle, void *user, void *client);
    static void MWAPI activate(MwWidget handle, void *user, void *call);
    static void MWAPI submit(MwWidget handle, void *user, void *call);
    int doubleClickTimer = 0;

    std::string ty_name;
    TCCRegistryType ty;

    void refresh_items();
  };
  std::vector<FolderPair *> folderPairs;
  std::vector<Subwindow *> subwindows;
  std::vector<ControlPanelWindow *> ctrl_panel_windows;

  /* collect all the categories/items */
  void update_list();
  ProgmanWindow();
  void setup();
  void run();

  void create_subwindow(std::string catName);
  void remove_subwindow(Subwindow *sub);

  void create_control_panel();

  MwWidget create_icon_table(
      std::unordered_map<std::string, std::vector<GAppInfo *>> items);
};
