#include "progman.hpp"
#include "gtk_loader.hpp"
#include "utils.hpp"
#include <set>
#include <string>

#define ICON_SIZE 72

/*
 * as per Freedesktop's Main Categories spec
 * (https://specifications.freedesktop.org/menu/latest/category-registry.html#main-category-registry)
 */
static std::unordered_map<std::string, std::string> gMainCategories = {
    {"AudioVideo", "applications-multimedia"},
    {"Develop", "applications-development"},
    {"Education", "applications-education"},
    {"Game", "applications-games"},
    {"Graphics", "applications-graphics"},
    {"Network", "applications-internet"},
    {"Office", "applications-office"},
    {"Science", "applications-science"},
    {"Settings", "preferences-desktop"},
    {"System", "applications-system"},
    {"Utility", "applications-utilities"},
};

void ProgmanWindow::update_list() {
  if (mItems.size() > 0)
    mItems.erase(mItems.begin());

  GList *apps = g_app_info_get_all();
  for (int i = 0; i < g_list_length(apps); i++) {
    GAppInfo **inf = (GAppInfo **)g_list_nth(apps, i);
    std::string name = g_app_info_get_name(*inf);
    const char *categories =
        g_desktop_app_info_get_categories((GDesktopAppInfo *)*inf);
    if (categories) {
      for (auto category : string_split(categories, ";")) {
        if (category == "Development")
          category = "Develop";
        if (gMainCategories.contains(category)) {
          if (!mItems.contains(category)) {
            mItems.insert_or_assign(category, std::vector<GAppInfo *>());
          }
          mItems.at(category).push_back(*inf);
        }
      }
    }
  }
}

ProgmanWindow::ProgmanWindow(/*void (*close_callback)(void *user), void *user*/)
/*: mCloseCallback(close_callback), mUserPtr(user) */ {
  MwSizeHints hints = {0};

  hints.min_width = hints.max_width = 640;
  hints.min_height = hints.max_height = 400;

  mWindow = MwVaCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT,
                             640, 400, MwNtitle, "Program Manager",
                             MwNsizeHints, &hints, NULL);
  mViewport =
      MwVaCreateWidget(MwViewportClass, NULL, mWindow, 5, 5, 630, 390, NULL);
  MwAddUserHandler(mWindow, MwNtickHandler, tick, this);

  /*MwAddUserHandler(
      mWindow, MwNcloseHandler,
      +[](MwWidget handle, void *user, void *client) {
        MwWidget msg = MwMessageBox(handle, "Break the Crantalian Crystal?",
                                    "Close?", MwMB_BUTTONYESNO);
        handle->close = 0;

        MwAddUserHandler(
            MwMessageBoxGetChild(msg, MwMB_BUTTONYES), MwNactivateHandler,
            +[](MwWidget handle, void *user, void *client) {
              ProgmanWindow *th = (ProgmanWindow *)user;

              handle->close = 1;
              th->mCloseCallback(th->mUserPtr);
            },
            user);
        MwAddUserHandler(
            MwMessageBoxGetChild(msg, MwMB_BUTTONNO), MwNactivateHandler,
            +[](MwWidget handle, void *user, void *client) {
              ProgmanWindow *th = (ProgmanWindow *)user;

              MwDestroyWidget(MwGetParent(handle));
            },
            user);
      },
      this);*/

  update_list();
}

void ProgmanWindow::setup() { mFolders = create_icon_table(mItems); }

void ProgmanWindow::run() {
  long tick = MwTimeGetTick();
  long over = 0;

  while (!MwWindowShouldClose(mWindow)) {
    int v = 0;
    long t, t2;
    long more;

    if (MwPending(mWindow)) {
      if ((v = MwStep(mWindow)) != 0)
        break;
    }

    for (auto sub : subwindows) {
      if (MwPending(sub->subwindow)) {
        if ((v = MwStep(sub->subwindow)) != 0)
          break;
      }
    }
  }
}

MwWidget ProgmanWindow::create_icon_table(
    std::unordered_map<std::string, std::vector<GAppInfo *>> items) {
  MwWidget table = nullptr;
  auto folder_height = (items.size() / 7) * ICON_SIZE;
  if (folder_height < 160) {
    folder_height = 160;
  } else {
    MwViewportSetSize(mViewport, 630, folder_height);
  }

  table = MwVaCreateWidget(MwTableClass, NULL, MwViewportGetViewport(mViewport),
                           0, 0, 640 - 25, folder_height, MwNcolumns, 7,
                           MwNmargin, 16, MwNrowSpan, 1, NULL);

  for (auto items : items) {
    FolderPair *f = new FolderPair();
    int width = ICON_SIZE;
    int height = ICON_SIZE;
    f->holder = MwVaCreateWidget(MwFrameClass, NULL, table, 0, 0, ICON_SIZE,
                                 ICON_SIZE, NULL);

    unsigned char *px = nullptr;
    mIcons.get_icon_by_name(gMainCategories.at(items.first).c_str(), 32, &width,
                            &height, &px);
    if (px) {
      f->folder_pixmap = MwLoadRaw(mWindow, px, width, height);
      free(px);

      if (width > ICON_SIZE)
        width = ICON_SIZE;
      if (height > ICON_SIZE)
        height = ICON_SIZE;

      f->folder_icon = MwVaCreateWidget(
          MwImageClass, NULL, f->holder, (ICON_SIZE / 2) - width / 2,
          height / 2, width, height, MwNpixmap, f->folder_pixmap, NULL);
    } else {
      width = ICON_SIZE;
      height = ICON_SIZE;
    }
    f->folder_name = MwVaCreateWidget(MwLabelClass, NULL, f->holder, 0,
                                      height + (height / 2), ICON_SIZE, 16,
                                      MwNtext, items.first.c_str(), NULL);

    f->win = this;

    MwAddUserHandler(f->holder, MwNmouseDownHandler, FolderPair::icon_dbl_click,
                     f);
    MwAddUserHandler(f->folder_name, MwNmouseDownHandler,
                     FolderPair::icon_dbl_click, f);
    if (f->folder_icon)
      MwAddUserHandler(f->folder_icon, MwNmouseDownHandler,
                       FolderPair::icon_dbl_click, f);

    // if (f->folder_pixmap)
    //   MwAddUserHandler(f->folder_pixmap, MwNmouseDownHandler,
    //                    FolderPair::icon_dbl_click, f);

    folderPairs.push_back(f);
  }

  return table;
}

void MWAPI ProgmanWindow::tick(MwWidget handle, void *user, void *client) {
  ProgmanWindow *prog = (ProgmanWindow *)user;
  if (prog->doubleClickTimer > 0) {
    --prog->doubleClickTimer;
  }
  for (auto sub : prog->subwindows) {
    if (sub->doubleClickTimer > 0) {
      --sub->doubleClickTimer;
    }
  }
}

void MWAPI ProgmanWindow::FolderPair::icon_dbl_click(MwWidget handle,
                                                     void *user, void *client) {
  ProgmanWindow::FolderPair *pair = (ProgmanWindow::FolderPair *)user;
  auto bg = MwGetInteger(pair->folder_name, MwNdarkTheme)
                ? MwDefaultDarkBackground
                : MwDefaultBackground;
  auto fg = MwGetInteger(pair->folder_name, MwNdarkTheme)
                ? MwDefaultDarkForeground
                : MwDefaultForeground;
  if (pair->win) {
    for (auto p : pair->win->folderPairs) {
      p->selected = false;
    }
    pair->selected = true;
    for (auto p : pair->win->folderPairs) {
      MwVaApply(p->folder_name, MwNbackground, p->selected ? "#03c" : bg, NULL);
      MwVaApply(p->folder_name, MwNforeground, p->selected ? "#fff" : fg, NULL);
    }

    if (pair->win->doubleClickTimer <= 0) {
      pair->win->doubleClickTimer = 10;
    } else {
      /* we've double clicked */
      pair->win->create_subwindow(MwGetString(pair->folder_name, MwNtext));
    }
  } else if (pair->subwin) {
    for (auto p : pair->subwin->folderPairs) {
      p->selected = false;
    }
    pair->selected = true;
    for (auto p : pair->subwin->folderPairs) {
      MwVaApply(p->folder_name, MwNbackground, p->selected ? "#03c" : bg, NULL);
      MwVaApply(p->folder_name, MwNforeground, p->selected ? "#fff" : fg, NULL);
    }

    if (pair->subwin->doubleClickTimer <= 0) {
      pair->subwin->doubleClickTimer = 10;
    } else {
      /* we've double clicked */
      GError *error;
      GAppLaunchContext *context =
          (GAppLaunchContext *)gdk_display_get_app_launch_context(
              gdk_display_get_default());
      g_app_info_launch(pair->info, NULL, context, &error);
    }
  }
}
void ProgmanWindow::create_subwindow(std::string catName) {
  Subwindow *sub = new Subwindow();
  sub->subwindow = MwVaCreateWidget(MwSubWindowClass, NULL, mWindow, 25, 25,
                                    320, 200, MwNtitle, catName.c_str(), NULL);
  sub->viewport = MwVaCreateWidget(MwViewportClass, NULL,
                                   MwSubWindowGetFrame(sub->subwindow), 5, 5,
                                   320 - 15, 200 - 35, NULL);
  MwAddUserHandler(sub->subwindow, MwNcloseHandler, Subwindow::remove, sub);
  MwAddUserHandler(sub->subwindow, MwNresizeHandler, Subwindow::resize, sub);

  sub->win = this;

  sub->items = sub->create_icon_table(mItems.at(catName));

  subwindows.push_back(sub);
};
void ProgmanWindow::remove_subwindow(Subwindow *sub) {
  std::erase(subwindows, sub);
}

MwWidget
ProgmanWindow::Subwindow::create_icon_table(std::vector<GAppInfo *> items) {
  this->appinfos = items;

  MwWidget table = nullptr;
  auto folder_height = (items.size() / 5) * ICON_SIZE;
  if (folder_height < 160) {
    folder_height = 160;
  } else {
    MwViewportSetSize(viewport, 630, folder_height);
  }

  table = MwVaCreateWidget(MwTableClass, NULL, MwViewportGetViewport(viewport),
                           0, 0, 640 - 25, folder_height, MwNcolumns, 7,
                           MwNmargin, 16, MwNrowSpan, 1, NULL);

  std::set<std::string> added;

  for (auto item : items) {
    FolderPair *f = new FolderPair();
    int width = ICON_SIZE;
    int height = ICON_SIZE;

    auto name = g_app_info_get_name(item);

    if (added.contains(name)) {
      continue;
    }
    added.insert(added.end(), name);

    f->holder = MwVaCreateWidget(MwFrameClass, NULL, table, 0, 0, ICON_SIZE,
                                 ICON_SIZE, NULL);

    f->info = item;

    unsigned char *px = nullptr;
    win->mIcons.get_icon_from_gicon(g_app_info_get_icon(item), 32, &width,
                                    &height, &px);
    if (px) {
      f->folder_pixmap = MwLoadRaw(subwindow, px, width, height);
      free(px);

      if (width > 32)
        width = 32;
      if (height > 32)
        height = 32;

      f->folder_icon = MwVaCreateWidget(
          MwImageClass, NULL, f->holder, (ICON_SIZE / 2) - width / 2,
          height / 2, width, height, MwNpixmap, f->folder_pixmap, NULL);
    } else {
      width = ICON_SIZE;
      height = ICON_SIZE;
    }
    f->folder_name = MwVaCreateWidget(MwLabelClass, NULL, f->holder, 0,
                                      height + (height / 2), ICON_SIZE, 16,
                                      MwNtext, name, NULL);

    f->subwin = this;

    MwAddUserHandler(f->holder, MwNmouseDownHandler, FolderPair::icon_dbl_click,
                     f);
    MwAddUserHandler(f->folder_name, MwNmouseDownHandler,
                     FolderPair::icon_dbl_click, f);
    if (f->folder_icon)
      MwAddUserHandler(f->folder_icon, MwNmouseDownHandler,
                       FolderPair::icon_dbl_click, f);

    this->folderPairs.push_back(f);
  }

  return table;
}
void MWAPI ProgmanWindow::Subwindow::remove(MwWidget handle, void *user,
                                            void *client) {
  Subwindow *sub = (Subwindow *)user;
  //   for (auto f : sub->folderPairs) {
  //     delete f;
  //   }
  sub->folderPairs.clear();
  sub->win->remove_subwindow(sub);
  MwShow(sub->subwindow, MwFALSE);

  MwDestroyWidget(sub->viewport);
  MwDestroyWidget(sub->items);
  MwDestroyWidget(sub->subwindow);
};
void MWAPI ProgmanWindow::Subwindow::resize(MwWidget handle, void *user,
                                            void *client) {
  Subwindow *sub = (Subwindow *)user;

  auto folder_height = (sub->appinfos.size() / 5) * ICON_SIZE;
  if (folder_height < 160) {
    folder_height = 160;
  }
  auto width = MwGetInteger(handle, MwNwidth) - 15;
  auto height = MwGetInteger(handle, MwNheight) - 35;
  MwVaApply(sub->viewport, MwNwidth, width, MwNheight, height, NULL);
  MwViewportSetSize(sub->viewport, width * 2, folder_height + (height * 2));
  MwViewportSetSize(sub->items, width * 2, folder_height + (height * 2));
};
