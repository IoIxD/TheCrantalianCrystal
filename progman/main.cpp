#include "progman.hpp"
#include <Mw/Milsko.h>
#include <cstdio>
#include <gtk/gtk.h>

int main() {
  /* Until we fix subwindows in the wayland backend for milsko, just launch the
   * program under x11 */
  setenv("MW_BACKEND", "x11", 1);

  gtk_init();
  MwLibraryInit();

  auto win = ProgmanWindow();

  win.setup();

  win.run();

  return 0;
}
