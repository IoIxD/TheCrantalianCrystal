#include "progman.hpp"
#include "utils.hpp"
#include <gtk/gtk.h>
#include <string>

#define ICON_SIZE 72

static int gDoubleClickTimer = 0;

static void MWAPI tick(MwWidget handle, void *client, void *user) {
  if (gDoubleClickTimer > 0)
    --gDoubleClickTimer;
}

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

ProgmanWindow::ProgmanWindow() {
  MwSizeHints hints = {0};

  hints.min_width = hints.max_width = 640;
  hints.min_height = hints.max_height = 480;

  mWindow = MwVaCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT,
                             640, 400, MwNtitle, "Program Manager",
                             MwNsizeHints, &hints, NULL);
  mViewport =
      MwVaCreateWidget(MwViewportClass, NULL, mWindow, 5, 5, 630, 390, NULL);
  MwAddUserHandler(mWindow, MwNtickHandler, tick, NULL);

  update_list();

  mFolders = create_icon_table(mItems);
}

void ProgmanWindow::run() { MwLoop(mWindow); }

MwWidget ProgmanWindow::create_icon_table(
    std::unordered_map<std::string, std::vector<GAppInfo *>> items) {
  MwWidget table = nullptr;
  auto folder_height = (items.size() / 7) * ICON_SIZE;
  if (folder_height < 160) {
    folder_height = 160;
  } else {
    MwViewportSetSize(mViewport, 630, folder_height);
  }

  /* set them up */
  GtkIconTheme *theme =
      gtk_icon_theme_get_for_display(gdk_display_get_default());

  table = MwVaCreateWidget(MwTableClass, NULL, MwViewportGetViewport(mViewport),
                           0, 0, 640 - 25, folder_height, MwNcolumns, 7,
                           MwNmargin, 16, MwNrowSpan, 1, NULL);

  for (auto items : items) {
    GError *err = nullptr;
    FolderPair *f = new FolderPair();
    int width = ICON_SIZE;
    int height = ICON_SIZE;
    f->holder = MwVaCreateWidget(MwFrameClass, NULL, table, 0, 0, ICON_SIZE,
                                 ICON_SIZE, NULL);

    GtkIconPaintable *icon = gtk_icon_theme_lookup_icon(
        theme, gMainCategories.at(items.first).c_str(), NULL, 32, 1,
        GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
    if (icon) {
      auto pixbuf = gdk_pixbuf_new_from_file(
          g_file_get_parse_name(gtk_icon_paintable_get_file(icon)), &err);

      if (pixbuf) {
        width = gdk_pixbuf_get_width(pixbuf);
        height = gdk_pixbuf_get_height(pixbuf);
        auto px = gdk_pixbuf_get_pixels(pixbuf);
        auto size = gdk_pixbuf_get_byte_length(pixbuf);
        f->folder_pixmap = MwLoadRaw(mWindow, px, width, height);

        if (width > ICON_SIZE)
          width = ICON_SIZE;
        if (height > ICON_SIZE)
          height = ICON_SIZE;

        f->folder_icon = MwVaCreateWidget(
            MwImageClass, NULL, f->holder, (ICON_SIZE / 2) - width / 2,
            height / 2, width, height, MwNpixmap, f->folder_pixmap, NULL);

        g_object_unref(pixbuf);
      }
      g_object_unref(icon);
    };
    f->folder_name = MwVaCreateWidget(MwLabelClass, NULL, f->holder, 0,
                                      height + (height / 2), ICON_SIZE, 16,
                                      MwNtext, items.first.c_str(), NULL);

    MwAddUserHandler(f->holder, MwNmouseDownHandler, FolderPair::icon_dbl_click,
                     f);
  }

  return table;
}

void ProgmanWindow::FolderPair::icon_dbl_click(MwWidget handle, void *client,
                                               void *user) {
  ProgmanWindow::FolderPair *pair = (ProgmanWindow::FolderPair *)user;
  if (gDoubleClickTimer <= 0) {
    gDoubleClickTimer = 5;
  } else {
    /* we've double clicked */
    printf("%p\n", user);
  }
}
