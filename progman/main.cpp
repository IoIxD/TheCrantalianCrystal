#include "progman.hpp"
#include <Mw/Milsko.h>
#include <cstdio>
#include <gtk/gtk.h>

int main() {
  gtk_init();
  MwLibraryInit();

  auto win = ProgmanWindow();

  win.setup();

  win.run();

  return 0;
}
